from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


@dataclass
class ArchInfo:
    path: str
    num_chip: int
    num_cluster: int
    onchip: str | None = None
    offchip: str | None = None
    attrs: dict[str, Any] = field(default_factory=dict)
    trace_num_chip: int | None = None
    trace_num_cluster: int | None = None


@dataclass
class FlowEvent:
    index: int
    time_ps: int
    cycle: int
    path: str
    component_path: str
    scope: str
    chip: int | None
    component_name: str
    fields: dict[str, str]

    @property
    def packet_id(self) -> str:
        return self.fields.get("id", "unknown")

    @property
    def event(self) -> str:
        return self.fields.get("event", "")

    @property
    def direction(self) -> str:
        return self.fields.get("dir", "")

    @property
    def component(self) -> str:
        return self.fields.get("comp", "")


@dataclass
class Node:
    id: str
    kind: str
    label: str
    chip: int | None = None
    cluster: int | None = None
    scope: str = ""
    router: str | None = None


@dataclass
class Link:
    id: str
    source: str
    target: str
    label: str
    scope: str
    chip: int | None = None


@dataclass
class PacketInfo:
    id: str
    kind: str = "unknown"
    action: str = "unknown"
    phase: str = "unknown"
    op: str = "none"
    src: str = "na"
    dst: str = "na"
    root: str = "na"
    size: int = 0
    bytes: int = 0


@dataclass
class Segment:
    id: str
    packet_id: str
    kind: str
    component: str
    component_path: str
    direction: str
    start_cycle: int
    end_cycle: int
    source: str | None = None
    target: str | None = None
    node: str | None = None
    status: str = ""
    event_index: int = 0


def int_field(value: str | None, default: int = 0) -> int:
    if value is None or value == "" or value == "na":
        return default
    try:
        return int(value, 0)
    except ValueError:
        return default

