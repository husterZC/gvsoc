from __future__ import annotations

import functools
import json
import socket
import threading
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse

from .topology import build_flow_model


def default_browse_root() -> Path:
    return Path(__file__).resolve().parents[2]


class FlowVizHandler(SimpleHTTPRequestHandler):
    def __init__(
        self,
        *args,
        data: dict[str, Any] | None,
        initial_arch: str | None,
        initial_trace: str | None,
        browse_root: str,
        directory: str,
        **kwargs,
    ):
        self.flowviz_data = data
        self.initial_arch = initial_arch
        self.initial_trace = initial_trace
        self.browse_root = browse_root
        super().__init__(*args, directory=directory, **kwargs)

    def do_GET(self):
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        if parsed.path == "/api/config":
            self._send_json({
                "browse_root": self.browse_root,
                "initial_arch": self.initial_arch,
                "initial_trace": self.initial_trace,
                "has_initial_data": self.flowviz_data is not None,
            })
            return
        if parsed.path == "/api/data":
            arch = self._query_one(query, "arch")
            trace = self._query_one(query, "trace")
            if arch or trace:
                if not arch or not trace:
                    self._send_json({"error": "Both arch and trace paths are required."}, status=400)
                    return
                try:
                    self._send_json(build_flow_model(arch, trace))
                except Exception as exc:
                    self._send_json({"error": str(exc)}, status=400)
                return
            if self.flowviz_data is None:
                self._send_json({"error": "No flow model loaded."}, status=404)
                return
            self._send_json(self.flowviz_data)
            return
        if parsed.path == "/api/list":
            self._send_list(query)
            return
        if parsed.path == "/":
            self.path = "/index.html"
        super().do_GET()

    def _query_one(self, query: dict[str, list[str]], name: str) -> str | None:
        values = query.get(name)
        if not values:
            return None
        value = values[0].strip()
        return value or None

    def _send_json(self, data: Any, status: int = 200):
        payload = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _send_list(self, query: dict[str, list[str]]):
        path = Path(self._query_one(query, "path") or self.browse_root).expanduser()
        kind = self._query_one(query, "kind") or "any"
        try:
            resolved = path.resolve()
            if not resolved.exists():
                raise FileNotFoundError(f"{resolved} does not exist")
            if not resolved.is_dir():
                resolved = resolved.parent
            entries = []
            for child in sorted(resolved.iterdir(), key=lambda item: (not item.is_dir(), item.name.lower())):
                if child.name.startswith("."):
                    continue
                is_dir = child.is_dir()
                selectable = child.is_file() and self._selectable_file(child, kind)
                if is_dir or selectable:
                    entries.append({
                        "name": child.name,
                        "path": str(child),
                        "is_dir": is_dir,
                        "selectable": selectable,
                    })
            self._send_json({
                "path": str(resolved),
                "parent": str(resolved.parent) if resolved.parent != resolved else str(resolved),
                "entries": entries[:1000],
            })
        except Exception as exc:
            self._send_json({"error": str(exc)}, status=400)

    def _selectable_file(self, path: Path, kind: str) -> bool:
        if kind == "arch":
            return path.suffix == ".py"
        if kind == "trace":
            return path.suffix in (".txt", ".log", ".trace", ".csv") or "trace" in path.name
        return True

    def log_message(self, format, *args):
        print("[flowviz] " + format % args, flush=True)


def find_port(host: str, preferred: int) -> int:
    if preferred != 0:
        return preferred
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind((host, 0))
        return int(sock.getsockname()[1])


def serve(
    data: dict[str, Any] | None,
    host: str,
    port: int,
    open_browser: bool,
    initial_arch: str | None = None,
    initial_trace: str | None = None,
    browse_root: str | Path | None = None,
) -> tuple[ThreadingHTTPServer, str]:
    web_dir = Path(__file__).resolve().parent / "web"
    root = str(Path(browse_root).resolve() if browse_root is not None else default_browse_root())
    actual_port = find_port(host, port)
    handler = functools.partial(
        FlowVizHandler,
        data=data,
        initial_arch=initial_arch,
        initial_trace=initial_trace,
        browse_root=root,
        directory=str(web_dir),
    )
    httpd = ThreadingHTTPServer((host, actual_port), handler)
    url = f"http://{host}:{actual_port}/"

    if open_browser:
        threading.Timer(0.3, lambda: webbrowser.open(url)).start()

    return httpd, url
