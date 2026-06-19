from __future__ import annotations

import importlib.util
from pathlib import Path
from typing import Any

from .model import ArchInfo


def _load_arch_object(path: Path) -> Any:
    spec = importlib.util.spec_from_file_location("velocity_flowviz_arch", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load architecture file: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not hasattr(module, "VelocityArch"):
        raise RuntimeError(f"{path} does not define VelocityArch")
    return module.VelocityArch()


def load_arch(path: str | Path) -> ArchInfo:
    arch_path = Path(path).resolve()
    arch = _load_arch_object(arch_path)
    attrs = dict(vars(arch))
    return ArchInfo(
        path=str(arch_path),
        num_chip=int(getattr(arch, "num_chip", 1)),
        num_cluster=int(getattr(arch, "num_cluster", 1)),
        onchip=getattr(arch, "onchip", None),
        offchip=getattr(arch, "offchip", None),
        attrs=attrs,
    )

