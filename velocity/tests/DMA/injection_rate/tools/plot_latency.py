#!/usr/bin/env python3
import argparse
import csv
import re
from pathlib import Path


RESULT_RE = re.compile(
    r"DMA_INJECTION_RESULT\s+(?P<status>\w+)\s+"
    r"clusters=(?P<clusters>\d+)\s+"
    r"gap=(?P<gap>\d+)\s+"
    r"transfers=(?P<transfers>\d+)\s+"
    r"latency_ns=(?P<latency_ns>\d+)\s+"
    r"error=(?P<error>\d+)"
)


def parse_logs(log_dir: Path):
    rows = []
    for log in sorted(log_dir.glob("injection_gap_*.log")):
        text = log.read_text(errors="replace")
        match = RESULT_RE.search(text.replace("\r", "\n"))
        if match is None:
            rows.append({
                "gap": int(log.stem.split("_")[-1]),
                "status": "MISSING_RESULT",
                "clusters": 0,
                "transfers": 0,
                "latency_ns": 0,
                "injection_rate_txn_per_us": 0.0,
                "error": -1,
                "log": str(log),
            })
            continue

        data = match.groupdict()
        latency_ns = int(data["latency_ns"])
        transfers = int(data["transfers"])
        rate = (transfers * 1000.0 / latency_ns) if latency_ns else 0.0
        rows.append({
            "gap": int(data["gap"]),
            "status": data["status"],
            "clusters": int(data["clusters"]),
            "transfers": transfers,
            "latency_ns": latency_ns,
            "injection_rate_txn_per_us": rate,
            "error": int(data["error"]),
            "log": str(log),
        })

    return sorted(rows, key=lambda row: row["gap"])


def write_csv(rows, csv_path: Path):
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "gap",
        "status",
        "clusters",
        "transfers",
        "latency_ns",
        "injection_rate_txn_per_us",
        "error",
        "log",
    ]
    with csv_path.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def write_svg(rows, svg_path: Path):
    points = [
        (row["injection_rate_txn_per_us"], row["latency_ns"])
        for row in rows
        if row["status"] == "PASS" and row["latency_ns"] > 0
    ]
    width = 800
    height = 500
    margin = 70

    if not points:
        svg_path.write_text("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" height=\"120\"><text x=\"20\" y=\"60\">No PASS data</text></svg>\n")
        return

    xs = [point[0] for point in points]
    ys = [point[1] for point in points]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    if min_x == max_x:
        max_x = min_x + 1.0
    if min_y == max_y:
        max_y = min_y + 1.0

    def sx(x):
        return margin + (x - min_x) * (width - 2 * margin) / (max_x - min_x)

    def sy(y):
        return height - margin - (y - min_y) * (height - 2 * margin) / (max_y - min_y)

    polyline = " ".join(f"{sx(x):.2f},{sy(y):.2f}" for x, y in points)
    circles = "\n".join(
        f"<circle cx=\"{sx(x):.2f}\" cy=\"{sy(y):.2f}\" r=\"4\" fill=\"#1f77b4\" />"
        for x, y in points
    )

    svg_path.write_text(f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">
<rect width="100%" height="100%" fill="white"/>
<line x1="{margin}" y1="{height - margin}" x2="{width - margin}" y2="{height - margin}" stroke="black"/>
<line x1="{margin}" y1="{margin}" x2="{margin}" y2="{height - margin}" stroke="black"/>
<text x="{width / 2}" y="{height - 20}" text-anchor="middle">Achieved injection rate (txn/us)</text>
<text x="20" y="{height / 2}" transform="rotate(-90 20 {height / 2})" text-anchor="middle">Latency (ns)</text>
<text x="{width / 2}" y="30" text-anchor="middle">Velocity DMA latency vs injection rate</text>
<polyline points="{polyline}" fill="none" stroke="#1f77b4" stroke-width="2"/>
{circles}
</svg>
""")


def write_plot(rows, figure_path: Path):
    passed = [row for row in rows if row["status"] == "PASS" and row["latency_ns"] > 0]
    if not passed:
        write_svg(rows, figure_path.with_suffix(".svg"))
        return

    try:
        import matplotlib.pyplot as plt
    except Exception:
        write_svg(rows, figure_path.with_suffix(".svg"))
        return

    x = [row["injection_rate_txn_per_us"] for row in passed]
    y = [row["latency_ns"] for row in passed]
    labels = [row["gap"] for row in passed]

    figure_path.parent.mkdir(parents=True, exist_ok=True)
    plt.figure(figsize=(7, 4.5))
    plt.plot(x, y, marker="o")
    for xi, yi, gap in zip(x, y, labels):
        plt.annotate(f"gap={gap}", (xi, yi), textcoords="offset points", xytext=(5, 5))
    plt.xlabel("Achieved injection rate (txn/us)")
    plt.ylabel("All-to-all latency (ns)")
    plt.title("Velocity DMA latency vs injection rate")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(figure_path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--logs", required=True, type=Path)
    parser.add_argument("--csv", required=True, type=Path)
    parser.add_argument("--figure", required=True, type=Path)
    args = parser.parse_args()

    rows = parse_logs(args.logs)
    write_csv(rows, args.csv)
    write_plot(rows, args.figure)


if __name__ == "__main__":
    main()

