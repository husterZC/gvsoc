"use strict";

const canvas = document.getElementById("flowCanvas");
const ctx = canvas.getContext("2d");
const tooltip = document.getElementById("tooltip");

const ui = {
  play: document.getElementById("playBtn"),
  stepBack: document.getElementById("stepBackBtn"),
  stepFwd: document.getElementById("stepFwdBtn"),
  speed: document.getElementById("speedSelect"),
  fit: document.getElementById("fitBtn"),
  zoomIn: document.getElementById("zoomInBtn"),
  zoomOut: document.getElementById("zoomOutBtn"),
  slider: document.getElementById("timeSlider"),
  cycle: document.getElementById("cycleText"),
  startCycle: document.getElementById("startCycle"),
  endCycle: document.getElementById("endCycle"),
  chipCount: document.getElementById("chipCount"),
  clusterCount: document.getElementById("clusterCount"),
  packetCount: document.getElementById("packetCount"),
  segmentCount: document.getElementById("segmentCount"),
  archPath: document.getElementById("archPath"),
  tracePath: document.getElementById("tracePath"),
  browseArch: document.getElementById("browseArchBtn"),
  browseTrace: document.getElementById("browseTraceBtn"),
  loadFlow: document.getElementById("loadFlowBtn"),
  loadStatus: document.getElementById("loadStatus"),
  packetSearch: document.getElementById("packetSearch"),
  actionFilter: document.getElementById("actionFilter"),
  showLabels: document.getElementById("showLabels"),
  showInactive: document.getElementById("showInactive"),
  emptyState: document.getElementById("emptyState"),
  fileModal: document.getElementById("fileModal"),
  fileModalTitle: document.getElementById("fileModalTitle"),
  closeFileModal: document.getElementById("closeFileModalBtn"),
  browserPath: document.getElementById("browserPath"),
  upDir: document.getElementById("upDirBtn"),
  goDir: document.getElementById("goDirBtn"),
  fileList: document.getElementById("fileList"),
};

let data = null;
let graph = null;
let config = null;
let fileBrowser = {
  kind: "arch",
  currentPath: "",
  parentPath: "",
};
let state = {
  cycle: 0,
  playing: false,
  speed: 1,
  scale: 1,
  offsetX: 0,
  offsetY: 0,
  dragging: false,
  dragStart: null,
  lastFrame: performance.now(),
  mouse: { x: 0, y: 0 },
  hovered: null,
};

function colorForPacket(packet) {
  const action = (packet?.action || "").toLowerCase();
  const phase = (packet?.phase || "").toLowerCase();
  const op = (packet?.op || "").toLowerCase();
  if (packet?.kind === "collective" || op !== "none") return "#c084fc";
  if (action.includes("write") || phase.includes("write")) return "#4fb6ff";
  if (action.includes("read") || phase.includes("read")) return "#60d394";
  if (action.includes("two")) return "#f2c14e";
  return "#ff8f5f";
}

function hashText(text) {
  let hash = 0;
  for (let i = 0; i < text.length; i++) {
    hash = ((hash << 5) - hash + text.charCodeAt(i)) | 0;
  }
  return Math.abs(hash);
}

function resizeCanvas() {
  const rect = canvas.parentElement.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.floor(rect.width * dpr));
  canvas.height = Math.max(1, Math.floor(rect.height * dpr));
  canvas.style.width = `${rect.width}px`;
  canvas.style.height = `${rect.height}px`;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  draw();
}

function setStatus(message, kind = "") {
  ui.loadStatus.textContent = message;
  ui.loadStatus.classList.toggle("error", kind === "error");
  ui.loadStatus.classList.toggle("ok", kind === "ok");
}

function setStatsEmpty() {
  ui.chipCount.textContent = "-";
  ui.clusterCount.textContent = "-";
  ui.packetCount.textContent = "-";
  ui.segmentCount.textContent = "-";
  ui.slider.min = "0";
  ui.slider.max = "1";
  ui.slider.value = "0";
  ui.startCycle.textContent = "0";
  ui.endCycle.textContent = "1";
  ui.cycle.textContent = "0";
}

function clearActionFilter() {
  while (ui.actionFilter.options.length > 1) {
    ui.actionFilter.remove(1);
  }
  ui.actionFilter.value = "";
}

function worldToScreen(point) {
  return {
    x: point.x * state.scale + state.offsetX,
    y: point.y * state.scale + state.offsetY,
  };
}

function screenToWorld(point) {
  return {
    x: (point.x - state.offsetX) / state.scale,
    y: (point.y - state.offsetY) / state.scale,
  };
}

function nodeById(id) {
  return graph.nodesById.get(id);
}

function sortRouters(a, b) {
  return String(a.router).localeCompare(String(b.router), undefined, { numeric: true });
}

function parseLevelRouter(router) {
  const match = String(router || "").match(/^level_(\d+)_(.+)$/);
  if (!match) return null;
  return { level: Number(match[1]), coord: match[2] };
}

function parseCoordRouter(router) {
  const text = String(router || "");
  if (!/^\d+(?:_\d+)*$/.test(text)) return null;
  return text.split("_").map((value) => Number(value));
}

function groupOnchipRouterRows(routers) {
  if (routers.some((node) => node.router === "root")) {
    const rows = new Map([[0, routers.filter((node) => node.router === "root")]]);
    for (const node of routers) {
      const parsed = parseLevelRouter(node.router);
      if (!parsed) continue;
      const row = parsed.level + 1;
      if (!rows.has(row)) rows.set(row, []);
      rows.get(row).push(node);
    }
    return [...rows.entries()].sort((a, b) => a[0] - b[0]).map(([, row]) => row.sort(sortRouters));
  }

  const levelRouters = routers.filter((node) => parseLevelRouter(node.router));
  if (levelRouters.length) {
    const maxLevel = Math.max(...levelRouters.map((node) => parseLevelRouter(node.router).level));
    const rows = new Map();
    for (const node of levelRouters) {
      const parsed = parseLevelRouter(node.router);
      const row = maxLevel - parsed.level;
      if (!rows.has(row)) rows.set(row, []);
      rows.get(row).push(node);
    }
    return [...rows.entries()].sort((a, b) => a[0] - b[0]).map(([, row]) => row.sort(sortRouters));
  }

  return [];
}

function estimateOnchipLayout(routers) {
  const rows = groupOnchipRouterRows(routers);
  if (rows.length) {
    return {
      rows: rows.length,
      widest: Math.max(...rows.map((row) => row.length), 1),
    };
  }
  if (routers.length) {
    return {
      rows: Math.max(1, Math.ceil(routers.length / 6)),
      widest: Math.min(routers.length, 6),
    };
  }
  return { rows: 1, widest: 1 };
}

function placeLine(row, x1, x2, y, width = 58, height = 28) {
  row.forEach((node, index) => {
    const span = Math.max(1, row.length - 1);
    const t = row.length === 1 ? 0.5 : index / span;
    node.x = x1 + (x2 - x1) * t;
    node.y = y;
    node.w = width;
    node.h = height;
  });
}

function placeGrid(routers, box, topY, bottomY) {
  const cols = Math.max(1, Math.min(6, Math.ceil(Math.sqrt(routers.length))));
  const rows = Math.max(1, Math.ceil(routers.length / cols));
  routers.sort(sortRouters).forEach((node, index) => {
    const col = index % cols;
    const row = Math.floor(index / cols);
    node.x = box.x + box.w * (col + 1) / (cols + 1);
    node.y = topY + (bottomY - topY) * (row + 1) / (rows + 1);
    node.w = 62;
    node.h = 30;
  });
}

function placeOnchipRouters(routers, box) {
  if (!routers.length) return;
  const topY = box.y + 62;
  const bottomY = box.y + box.h - 128;
  const leftX = box.x + 56;
  const rightX = box.x + box.w - 56;
  const levelRows = groupOnchipRouterRows(routers);
  if (levelRows.length) {
    levelRows.forEach((row, index) => {
      const y = topY + (bottomY - topY) * (index + 0.5) / levelRows.length;
      placeLine(row, leftX, rightX, y, row.length > 10 ? 46 : 58, 28);
    });
    return;
  }

  const coords = routers.map((node) => parseCoordRouter(node.router));
  if (coords.every(Boolean) && coords.length > 1) {
    const maxX = Math.max(...coords.map((coord) => coord[0]));
    const maxY = Math.max(...coords.map((coord) => coord[1] || 0));
    routers.forEach((node, index) => {
      const coord = coords[index];
      node.x = leftX + (rightX - leftX) * (coord[0] + 0.5) / (maxX + 1);
      node.y = topY + (bottomY - topY) * ((coord[1] || 0) + 0.5) / (maxY + 1);
      if (coord.length > 2) node.y += (coord[2] % 3) * 12;
      node.w = 58;
      node.h = 28;
    });
    return;
  }

  placeGrid(routers, box, topY, bottomY);
}

function buildGraph(model) {
  const nodes = model.nodes.map((node) => ({ ...node, x: 0, y: 0, w: 80, h: 40 }));
  const nodesById = new Map(nodes.map((node) => [node.id, node]));
  const links = model.links.map((link) => ({ ...link }));
  const packetsById = new Map(model.packets.map((packet) => [packet.id, packet]));
  const segments = model.segments.map((segment) => ({ ...segment }));
  const arch = model.arch;

  const chipCount = Math.max(1, arch.num_chip || 1);
  const clustersPerChip = Math.max(1, arch.num_cluster || 1);
  const onchipRouters = nodes.filter((node) => node.kind === "router" && node.scope === "onchip");
  const layoutHints = [];
  for (let chip = 0; chip < chipCount; chip++) {
    layoutHints.push(estimateOnchipLayout(onchipRouters.filter((node) => node.chip === chip)));
  }
  const widestOnchipRow = Math.max(...layoutHints.map((hint) => hint.widest), 1);
  const tallestOnchipRows = Math.max(...layoutHints.map((hint) => hint.rows), 1);
  const chipWidth = Math.max(460, 92 * clustersPerChip + 100, widestOnchipRow * 54 + 120);
  const chipHeight = Math.max(330, tallestOnchipRows * 58 + 170);
  const chipGapX = 190;
  const chipGapY = 240;
  const cols = chipCount <= 3 ? chipCount : Math.ceil(Math.sqrt(chipCount));

  const chipBoxes = [];
  for (let chip = 0; chip < chipCount; chip++) {
    const col = chip % cols;
    const row = Math.floor(chip / cols);
    const x = 80 + col * (chipWidth + chipGapX);
    const y = 80 + row * (chipHeight + chipGapY);
    chipBoxes.push({ chip, x, y, w: chipWidth, h: chipHeight });
    const chipNode = nodesById.get(`chip:${chip}`);
    if (chipNode) Object.assign(chipNode, { x: x + chipWidth / 2, y: y + chipHeight / 2, w: chipWidth, h: chipHeight });

    for (let cluster = 0; cluster < clustersPerChip; cluster++) {
      const node = nodesById.get(`cluster:${chip}:${cluster}`);
      if (!node) continue;
      const spacing = chipWidth / (clustersPerChip + 1);
      node.x = x + spacing * (cluster + 1);
      node.y = y + chipHeight - 62;
      node.w = cluster === 0 ? 92 : 76;
      node.h = 40;
    }
  }

  for (const box of chipBoxes) {
    const routers = onchipRouters.filter((node) => node.chip === box.chip);
    routers.sort(sortRouters);
    placeOnchipRouters(routers, box);
  }

  const offchipRouters = nodes.filter((node) => node.kind === "router" && node.scope === "offchip");
  offchipRouters.sort((a, b) => String(a.router).localeCompare(String(b.router), undefined, { numeric: true }));
  const baseY = Math.max(...chipBoxes.map((box) => box.y + box.h)) + 150;
  const offchipCount = Math.max(1, offchipRouters.length);
  offchipRouters.forEach((node, index) => {
    const numeric = Number(node.router);
    const relatedBox = Number.isFinite(numeric) ? chipBoxes[numeric] : null;
    node.x = relatedBox ? relatedBox.x + relatedBox.w / 2 : 120 + index * 150;
    node.y = baseY + (index % 2) * 46;
    node.w = 82;
    node.h = 38;
  });

  const xs = nodes.filter((n) => n.kind !== "chip").map((n) => n.x);
  const ys = nodes.filter((n) => n.kind !== "chip").map((n) => n.y);
  const bounds = {
    minX: Math.min(...xs, ...chipBoxes.map((b) => b.x)) - 90,
    minY: Math.min(...ys, ...chipBoxes.map((b) => b.y)) - 80,
    maxX: Math.max(...xs, ...chipBoxes.map((b) => b.x + b.w)) + 90,
    maxY: Math.max(...ys, ...chipBoxes.map((b) => b.y + b.h), baseY) + 120,
  };

  return { nodes, nodesById, links, packetsById, segments, chipBoxes, bounds };
}

function fitView() {
  if (!graph) return;
  const wrap = canvas.parentElement.getBoundingClientRect();
  const bw = graph.bounds.maxX - graph.bounds.minX;
  const bh = graph.bounds.maxY - graph.bounds.minY;
  const scale = Math.min(wrap.width / bw, wrap.height / bh) * 0.9;
  state.scale = Math.max(0.1, Math.min(3.5, scale));
  state.offsetX = wrap.width / 2 - ((graph.bounds.minX + graph.bounds.maxX) / 2) * state.scale;
  state.offsetY = wrap.height / 2 - ((graph.bounds.minY + graph.bounds.maxY) / 2) * state.scale;
  draw();
}

function drawRoundRect(ctx, x, y, w, h, r) {
  const radius = Math.min(r, w / 2, h / 2);
  ctx.beginPath();
  ctx.moveTo(x + radius, y);
  ctx.arcTo(x + w, y, x + w, y + h, radius);
  ctx.arcTo(x + w, y + h, x, y + h, radius);
  ctx.arcTo(x, y + h, x, y, radius);
  ctx.arcTo(x, y, x + w, y, radius);
  ctx.closePath();
}

function drawChipBoxes() {
  for (const box of graph.chipBoxes) {
    ctx.fillStyle = "#111820";
    ctx.strokeStyle = "#405061";
    ctx.lineWidth = 1.4 / state.scale;
    drawRoundRect(ctx, box.x, box.y, box.w, box.h, 8);
    ctx.fill();
    ctx.stroke();

    ctx.strokeStyle = "#263442";
    ctx.lineWidth = 1 / state.scale;
    ctx.beginPath();
    ctx.moveTo(box.x + 18, box.y + box.h - 108);
    ctx.lineTo(box.x + box.w - 18, box.y + box.h - 108);
    ctx.stroke();

    ctx.fillStyle = "#cbd5e1";
    ctx.font = `${15 / state.scale}px sans-serif`;
    ctx.fillText(`Chip ${box.chip}`, box.x + 18, box.y + 28);
  }
}

function linkKey(link) {
  return `${link.source}->${link.target}`;
}

function drawLinks(activeLinkIds) {
  const linkGroups = new Map();
  for (const link of graph.links) {
    const pair = [link.source, link.target].sort().join("|");
    if (!linkGroups.has(pair)) linkGroups.set(pair, []);
    linkGroups.get(pair).push(link);
  }

  for (const link of graph.links) {
    if (!ui.showInactive.checked && !activeLinkIds.has(link.id)) continue;
    const source = nodeById(link.source);
    const target = nodeById(link.target);
    if (!source || !target) continue;

    const group = linkGroups.get([link.source, link.target].sort().join("|")) || [];
    const index = group.findIndex((candidate) => candidate.id === link.id);
    const offset = (index - (group.length - 1) / 2) * 10;
    const active = activeLinkIds.has(link.id);

    const dx = target.x - source.x;
    const dy = target.y - source.y;
    const len = Math.max(1, Math.hypot(dx, dy));
    const nx = -dy / len;
    const ny = dx / len;

    ctx.beginPath();
    ctx.moveTo(source.x + nx * offset, source.y + ny * offset);
    ctx.lineTo(target.x + nx * offset, target.y + ny * offset);
    ctx.strokeStyle = active
      ? (link.scope === "offchip" ? "#f2c14e" : "#6bbcff")
      : (link.scope === "offchip" ? "#4a3a1f" : "#263647");
    ctx.globalAlpha = active ? 0.95 : (link.scope === "offchip" ? 0.55 : 0.36);
    ctx.lineWidth = (active ? 3.0 : (link.scope === "offchip" ? 1.8 : 1.15)) / state.scale;
    ctx.lineCap = "round";
    ctx.stroke();
    ctx.globalAlpha = 1;
  }
}

function drawNodes() {
  for (const node of graph.nodes) {
    if (node.kind === "chip") continue;
    if (node.kind === "cluster") {
      ctx.fillStyle = node.cluster === 0 ? "#18334a" : "#16232e";
      ctx.strokeStyle = node.cluster === 0 ? "#4fb6ff" : "#425263";
      ctx.lineWidth = 1.3 / state.scale;
      drawRoundRect(ctx, node.x - node.w / 2, node.y - node.h / 2, node.w, node.h, 7);
      ctx.fill();
      ctx.stroke();
    } else if (node.kind === "router") {
      ctx.fillStyle = node.scope === "offchip" ? "#2f2717" : "#1b2a35";
      ctx.strokeStyle = node.scope === "offchip" ? "#f2c14e" : "#7dd3fc";
      ctx.lineWidth = 1.3 / state.scale;
      drawRoundRect(ctx, node.x - node.w / 2, node.y - node.h / 2, node.w, node.h, 6);
      ctx.fill();
      ctx.stroke();
    }

    if (ui.showLabels.checked) {
      ctx.fillStyle = "#dbe5f0";
      ctx.font = `${12 / state.scale}px sans-serif`;
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(node.label, node.x, node.y);
    }
  }
  ctx.textAlign = "left";
  ctx.textBaseline = "alphabetic";
}

function activeSegments() {
  const packetQuery = ui.packetSearch.value.trim();
  const actionFilter = ui.actionFilter.value;
  return graph.segments.filter((segment) => {
    if (state.cycle < segment.start_cycle || state.cycle > segment.end_cycle) return false;
    const packet = graph.packetsById.get(segment.packet_id);
    if (packetQuery && !segment.packet_id.includes(packetQuery)) return false;
    if (actionFilter && packet?.action !== actionFilter) return false;
    return true;
  });
}

function segmentPosition(segment) {
  if (segment.kind === "link") {
    const source = nodeById(segment.source);
    const target = nodeById(segment.target);
    if (!source || !target) return null;
    const span = Math.max(1, segment.end_cycle - segment.start_cycle);
    const t = Math.max(0, Math.min(1, (state.cycle - segment.start_cycle) / span));
    const lane = (hashText(segment.packet_id) % 7) - 3;
    const dx = target.x - source.x;
    const dy = target.y - source.y;
    const len = Math.max(1, Math.hypot(dx, dy));
    const nx = -dy / len;
    const ny = dx / len;
    return {
      x: source.x + dx * t + nx * lane * 4,
      y: source.y + dy * t + ny * lane * 4,
      angle: Math.atan2(dy, dx),
    };
  }

  const node = nodeById(segment.node);
  if (!node) return null;
  const jitter = (hashText(segment.packet_id) % 9) - 4;
  return { x: node.x + jitter * 3, y: node.y - node.h / 2 - 15, angle: 0 };
}

function packetTooltip(segment, packet) {
  return [
    `id: ${segment.packet_id}`,
    `kind: ${packet?.kind || "unknown"}`,
    `action: ${packet?.action || "unknown"}`,
    `phase: ${packet?.phase || "unknown"}`,
    `op: ${packet?.op || "none"}`,
    `src: ${packet?.src || "na"}`,
    `dst: ${packet?.dst || "na"}`,
    `root: ${packet?.root || "na"}`,
    `component: ${segment.component_path}`,
    `dir: ${segment.direction}`,
    `cycle: ${Math.round(state.cycle)}`,
    `window: ${segment.start_cycle}..${segment.end_cycle}`,
    `status: ${segment.status || "unknown"}`,
  ].join("\n");
}

function drawPackets(segments) {
  const hits = [];
  for (const segment of segments) {
    const pos = segmentPosition(segment);
    if (!pos) continue;
    const packet = graph.packetsById.get(segment.packet_id);
    const color = colorForPacket(packet);
    const radius = segment.kind === "link" ? 8 : 6;

    ctx.save();
    ctx.translate(pos.x, pos.y);
    ctx.rotate(pos.angle);
    ctx.fillStyle = color;
    ctx.strokeStyle = "#0b1118";
    ctx.lineWidth = 1.5 / state.scale;
    drawRoundRect(ctx, -12, -7, 24, 14, 7);
    ctx.fill();
    ctx.stroke();
    ctx.restore();

    const screen = worldToScreen(pos);
    hits.push({ x: screen.x, y: screen.y, r: radius * state.scale + 7, segment, packet });
  }
  return hits;
}

function updateTooltip(hits) {
  let closest = null;
  let closestDist = Infinity;
  for (const hit of hits) {
    const dist = Math.hypot(hit.x - state.mouse.x, hit.y - state.mouse.y);
    if (dist <= hit.r && dist < closestDist) {
      closest = hit;
      closestDist = dist;
    }
  }

  state.hovered = closest;
  if (!closest) {
    tooltip.classList.add("hidden");
    return;
  }

  tooltip.textContent = packetTooltip(closest.segment, closest.packet);
  tooltip.style.left = `${Math.min(state.mouse.x + 16, canvas.parentElement.clientWidth - 380)}px`;
  tooltip.style.top = `${Math.min(state.mouse.y + 16, canvas.parentElement.clientHeight - 220)}px`;
  tooltip.classList.remove("hidden");
}

function draw() {
  const rect = canvas.parentElement.getBoundingClientRect();
  ctx.save();
  ctx.setTransform(window.devicePixelRatio || 1, 0, 0, window.devicePixelRatio || 1, 0, 0);
  ctx.clearRect(0, 0, rect.width, rect.height);
  ctx.fillStyle = "#0d1117";
  ctx.fillRect(0, 0, rect.width, rect.height);
  if (!graph) {
    ctx.restore();
    tooltip.classList.add("hidden");
    ui.emptyState.classList.remove("hidden");
    return;
  }
  ui.emptyState.classList.add("hidden");
  ctx.translate(state.offsetX, state.offsetY);
  ctx.scale(state.scale, state.scale);

  const segments = activeSegments();
  const activeLinkIds = new Set(segments.filter((segment) => segment.kind === "link").map((segment) => segment.component_path));

  drawChipBoxes();
  drawLinks(activeLinkIds);
  drawNodes();
  const hits = drawPackets(segments);
  ctx.restore();

  updateTooltip(hits);
}

function updateCycle(cycle) {
  if (!data) return;
  const start = data.time.start;
  const end = data.time.end;
  state.cycle = Math.max(start, Math.min(end, cycle));
  ui.slider.value = String(Math.round(state.cycle));
  ui.cycle.textContent = String(Math.round(state.cycle));
  draw();
}

function tick(now) {
  const dt = now - state.lastFrame;
  state.lastFrame = now;
  if (state.playing && data) {
    const span = Math.max(1, data.time.end - data.time.start);
    const cyclesPerSecond = Math.max(20, span / 8) * state.speed;
    let next = state.cycle + (dt / 1000) * cyclesPerSecond;
    if (next >= data.time.end) {
      next = data.time.end;
      state.playing = false;
      ui.play.textContent = "Play";
    }
    updateCycle(next);
  }
  requestAnimationFrame(tick);
}

function zoomAt(screenPoint, factor) {
  const before = screenToWorld(screenPoint);
  state.scale = Math.max(0.08, Math.min(6, state.scale * factor));
  state.offsetX = screenPoint.x - before.x * state.scale;
  state.offsetY = screenPoint.y - before.y * state.scale;
  draw();
}

function populateUi(model) {
  ui.chipCount.textContent = String(model.arch.num_chip);
  ui.clusterCount.textContent = String(model.arch.num_cluster);
  ui.packetCount.textContent = String(model.packets.length);
  ui.segmentCount.textContent = String(model.segments.length);
  ui.slider.min = String(model.time.start);
  ui.slider.max = String(model.time.end);
  ui.slider.value = String(model.time.start);
  ui.startCycle.textContent = String(model.time.start);
  ui.endCycle.textContent = String(model.time.end);
  ui.cycle.textContent = String(model.time.start);

  clearActionFilter();
  const actions = [...new Set(model.packets.map((packet) => packet.action).filter(Boolean))].sort();
  for (const action of actions) {
    const option = document.createElement("option");
    option.value = action;
    option.textContent = action;
    ui.actionFilter.appendChild(option);
  }
}

async function loadFlowModel() {
  const arch = ui.archPath.value.trim();
  const trace = ui.tracePath.value.trim();
  if (!arch || !trace) {
    setStatus("Select both arch and trace paths.", "error");
    return;
  }

  state.playing = false;
  ui.play.textContent = "Play";
  ui.loadFlow.disabled = true;
  setStatus("Loading...");
  try {
    const params = new URLSearchParams({ arch, trace });
    const response = await fetch(`/api/data?${params.toString()}`);
    const model = await response.json();
    if (!response.ok) throw new Error(model.error || `HTTP ${response.status}`);
    data = model;
    graph = buildGraph(data);
    state.cycle = data.time.start;
    state.scale = 1;
    state.offsetX = 0;
    state.offsetY = 0;
    populateUi(data);
    resizeCanvas();
    fitView();
    updateCycle(data.time.start);
    setStatus("Loaded.", "ok");
  } catch (error) {
    data = null;
    graph = null;
    setStatsEmpty();
    clearActionFilter();
    draw();
    setStatus(error.message || String(error), "error");
  } finally {
    ui.loadFlow.disabled = false;
  }
}

async function loadInitialModel() {
  const response = await fetch("/api/data");
  const model = await response.json();
  if (!response.ok) throw new Error(model.error || `HTTP ${response.status}`);
  data = model;
  graph = buildGraph(data);
  populateUi(data);
  resizeCanvas();
  fitView();
  updateCycle(data.time.start);
  setStatus("Loaded.", "ok");
}

function showEmpty() {
  data = null;
  graph = null;
  setStatsEmpty();
  clearActionFilter();
  draw();
}

function pathParent(path) {
  const cleaned = String(path || "").replace(/\/+$/, "");
  const index = cleaned.lastIndexOf("/");
  if (index <= 0) return config?.browse_root || "/";
  return cleaned.slice(0, index);
}

async function openFileBrowser(kind) {
  fileBrowser.kind = kind;
  const selectedPath = kind === "arch" ? ui.archPath.value.trim() : ui.tracePath.value.trim();
  const startPath = selectedPath ? pathParent(selectedPath) : config.browse_root;
  ui.fileModalTitle.textContent = kind === "arch" ? "Select Architecture" : "Select Trace";
  ui.fileModal.classList.remove("hidden");
  await loadFileList(startPath);
}

function closeFileBrowser() {
  ui.fileModal.classList.add("hidden");
}

async function loadFileList(path) {
  ui.browserPath.value = path;
  ui.fileList.textContent = "";
  const loading = document.createElement("div");
  loading.className = "fileItem empty";
  loading.textContent = "Loading...";
  ui.fileList.appendChild(loading);

  try {
    const params = new URLSearchParams({ path, kind: fileBrowser.kind });
    const response = await fetch(`/api/list?${params.toString()}`);
    const listing = await response.json();
    if (!response.ok) throw new Error(listing.error || `HTTP ${response.status}`);
    fileBrowser.currentPath = listing.path;
    fileBrowser.parentPath = listing.parent;
    ui.browserPath.value = listing.path;
    renderFileList(listing.entries);
  } catch (error) {
    ui.fileList.textContent = "";
    const item = document.createElement("div");
    item.className = "fileItem empty";
    item.textContent = error.message || String(error);
    ui.fileList.appendChild(item);
  }
}

function renderFileList(entries) {
  ui.fileList.textContent = "";
  if (!entries.length) {
    const item = document.createElement("div");
    item.className = "fileItem empty";
    item.textContent = "No matching files";
    ui.fileList.appendChild(item);
    return;
  }

  for (const entry of entries) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = `fileItem ${entry.is_dir ? "dir" : "file"}`;
    const icon = document.createElement("span");
    icon.textContent = entry.is_dir ? "D" : "F";
    const label = document.createElement("span");
    label.textContent = entry.name;
    button.append(icon, label);
    button.addEventListener("click", () => {
      if (entry.is_dir) {
        loadFileList(entry.path);
      } else if (entry.selectable) {
        if (fileBrowser.kind === "arch") ui.archPath.value = entry.path;
        else ui.tracePath.value = entry.path;
        closeFileBrowser();
      }
    });
    ui.fileList.appendChild(button);
  }
}

function bindControls() {
  ui.play.addEventListener("click", () => {
    if (!data) return;
    if (state.cycle >= data.time.end) updateCycle(data.time.start);
    state.playing = !state.playing;
    ui.play.textContent = state.playing ? "Pause" : "Play";
  });
  ui.stepBack.addEventListener("click", () => updateCycle(state.cycle - 1));
  ui.stepFwd.addEventListener("click", () => updateCycle(state.cycle + 1));
  ui.speed.addEventListener("change", () => { state.speed = Number(ui.speed.value); });
  ui.slider.addEventListener("input", () => updateCycle(Number(ui.slider.value)));
  ui.fit.addEventListener("click", fitView);
  ui.zoomIn.addEventListener("click", () => zoomAt({ x: canvas.parentElement.clientWidth / 2, y: canvas.parentElement.clientHeight / 2 }, 1.2));
  ui.zoomOut.addEventListener("click", () => zoomAt({ x: canvas.parentElement.clientWidth / 2, y: canvas.parentElement.clientHeight / 2 }, 1 / 1.2));
  ui.packetSearch.addEventListener("input", draw);
  ui.actionFilter.addEventListener("change", draw);
  ui.showLabels.addEventListener("change", draw);
  ui.showInactive.addEventListener("change", draw);
  ui.loadFlow.addEventListener("click", loadFlowModel);
  ui.browseArch.addEventListener("click", () => { openFileBrowser("arch"); });
  ui.browseTrace.addEventListener("click", () => { openFileBrowser("trace"); });
  ui.closeFileModal.addEventListener("click", closeFileBrowser);
  ui.upDir.addEventListener("click", () => loadFileList(fileBrowser.parentPath || config.browse_root));
  ui.goDir.addEventListener("click", () => loadFileList(ui.browserPath.value.trim() || config.browse_root));
  ui.browserPath.addEventListener("keydown", (event) => {
    if (event.key === "Enter") loadFileList(ui.browserPath.value.trim() || config.browse_root);
  });
  ui.fileModal.addEventListener("click", (event) => {
    if (event.target === ui.fileModal) closeFileBrowser();
  });
  ui.archPath.addEventListener("keydown", (event) => {
    if (event.key === "Enter") loadFlowModel();
  });
  ui.tracePath.addEventListener("keydown", (event) => {
    if (event.key === "Enter") loadFlowModel();
  });

  canvas.addEventListener("wheel", (event) => {
    event.preventDefault();
    const rect = canvas.getBoundingClientRect();
    const point = { x: event.clientX - rect.left, y: event.clientY - rect.top };
    zoomAt(point, event.deltaY < 0 ? 1.12 : 1 / 1.12);
  }, { passive: false });

  canvas.addEventListener("mousedown", (event) => {
    state.dragging = true;
    canvas.classList.add("dragging");
    state.dragStart = { x: event.clientX, y: event.clientY, ox: state.offsetX, oy: state.offsetY };
  });

  window.addEventListener("mousemove", (event) => {
    const rect = canvas.getBoundingClientRect();
    state.mouse = { x: event.clientX - rect.left, y: event.clientY - rect.top };
    if (state.dragging && state.dragStart) {
      state.offsetX = state.dragStart.ox + event.clientX - state.dragStart.x;
      state.offsetY = state.dragStart.oy + event.clientY - state.dragStart.y;
    }
    draw();
  });

  window.addEventListener("mouseup", () => {
    state.dragging = false;
    canvas.classList.remove("dragging");
    state.dragStart = null;
  });

  window.addEventListener("resize", resizeCanvas);
}

async function init() {
  const response = await fetch("/api/config");
  config = await response.json();
  ui.archPath.value = config.initial_arch || "";
  ui.tracePath.value = config.initial_trace || "";
  bindControls();
  resizeCanvas();
  if (config.has_initial_data) {
    await loadInitialModel();
  } else {
    showEmpty();
    setStatus(`Browse root: ${config.browse_root}`);
  }
  requestAnimationFrame(tick);
}

init().catch((error) => {
  console.error(error);
  document.body.innerHTML = `<pre class="fatal">${error.stack || error}</pre>`;
});
