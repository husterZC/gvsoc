from __future__ import annotations

import re
from pathlib import Path

from .model import FlowEvent


ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
FLOW_RE = re.compile(
    r"^\s*(?P<time>\d+):\s*(?P<cycle>\d+):\s*\[(?P<path>[^\]]+)\]\s*FLOW\s+(?P<fields>.*)$"
)
SYSTEM_RE = re.compile(
    r"\[SystemInfo\]:\s*chip_id\s*=\s*(?P<chip>\d+),\s*num_chip\s*=\s*(?P<num_chip>\d+),\s*num_cluster\s*=\s*(?P<num_cluster>\d+)"
)


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def parse_key_values(text: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for item in text.strip().split():
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        fields[key] = value
    return fields


def classify_path(path: str) -> tuple[str, int | None, str]:
    clean = path.strip().strip("/")
    if clean.endswith("/flow"):
        clean = clean[:-len("/flow")]

    parts = clean.split("/") if clean else []
    if not parts:
        return "unknown", None, ""

    if parts[0].startswith("chip_"):
        try:
            chip = int(parts[0].split("_", 1)[1])
        except ValueError:
            chip = None
        if len(parts) >= 3 and parts[1].startswith("cluster_"):
            return "chip", chip, parts[2]
        if len(parts) >= 3 and parts[1] == "onchip_interco":
            return "onchip", chip, parts[2]
        return "chip", chip, parts[-1]

    if parts[0] == "offchip_interco" and len(parts) >= 2:
        return "offchip", None, parts[1]

    return parts[0], None, parts[-1]


def parse_trace(path: str | Path) -> tuple[list[FlowEvent], dict[str, int]]:
    trace_path = Path(path)
    events: list[FlowEvent] = []
    system: dict[str, int] = {}

    with trace_path.open(errors="replace") as file:
        for line in file:
            clean = strip_ansi(line.rstrip())
            system_match = SYSTEM_RE.search(clean)
            if system_match:
                system["num_chip"] = int(system_match.group("num_chip"))
                system["num_cluster"] = int(system_match.group("num_cluster"))

            match = FLOW_RE.match(clean)
            if match is None:
                continue

            raw_path = match.group("path").strip()
            component_path = raw_path.strip().strip("/")
            if component_path.endswith("/flow"):
                component_path = component_path[:-len("/flow")]

            scope, chip, component_name = classify_path(raw_path)
            events.append(FlowEvent(
                index=len(events),
                time_ps=int(match.group("time")),
                cycle=int(match.group("cycle")),
                path=raw_path,
                component_path=component_path,
                scope=scope,
                chip=chip,
                component_name=component_name,
                fields=parse_key_values(match.group("fields")),
            ))

    return events, system

