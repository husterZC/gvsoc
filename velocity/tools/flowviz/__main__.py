from __future__ import annotations

import argparse
import json
from pathlib import Path

from .server import default_browse_root, serve
from .topology import build_flow_model


def main() -> int:
    parser = argparse.ArgumentParser(description="Visualize Velocity packet flow traces.")
    parser.add_argument("--arch", help="Path to Velocity arch.py")
    parser.add_argument("--trace", help="Path to packet_flow_trace.txt")
    parser.add_argument(
        "--browse-root",
        type=Path,
        default=default_browse_root(),
        help="Initial server-side directory for the browser file picker",
    )
    parser.add_argument("--host", default="127.0.0.1", help="HTTP bind host")
    parser.add_argument("--port", type=int, default=8765, help="HTTP port, or 0 for any free port")
    parser.add_argument("--export", type=Path, help="Write normalized flow model JSON")
    parser.add_argument("--no-server", action="store_true", help="Only parse/export; do not start the web server")
    parser.add_argument("--no-browser", action="store_true", help="Do not open a browser automatically")
    args = parser.parse_args()

    if args.export or args.no_server:
        if not args.arch or not args.trace:
            parser.error("--arch and --trace are required with --export or --no-server")

    data = None
    if args.arch and args.trace:
        data = build_flow_model(args.arch, args.trace)

    if args.export:
        args.export.parent.mkdir(parents=True, exist_ok=True)
        args.export.write_text(json.dumps(data, indent=2))
        print(f"[flowviz] exported {args.export}", flush=True)

    if args.no_server:
        return 0

    httpd, url = serve(
        data,
        args.host,
        args.port,
        not args.no_browser,
        initial_arch=args.arch,
        initial_trace=args.trace,
        browse_root=args.browse_root,
    )
    print(f"[flowviz] serving {url}", flush=True)
    print("[flowviz] press Ctrl-C to stop", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
