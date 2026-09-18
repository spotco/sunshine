#!/usr/bin/env python3
"""Cross-platform documentation harness mirroring the PowerShell matrix."""
from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from pathlib import Path


@dataclass
class Cell:
    codec: str
    hdr: bool
    fps: int
    width: int
    height: int
    two_pass: bool
    display: str


MATRIX = [
    Cell("h264", False, 30, 1920, 1080, False, "physical"),
    Cell("hevc", False, 60, 1920, 1080, True, "physical"),
    Cell("hevc", True, 60, 1920, 1080, False, "physical"),
    Cell("av1", False, 60, 2560, 1080, False, "physical"),
    Cell("hevc", False, 60, 1920, 1080, False, "virtual"),
]


def main() -> None:
    print(json.dumps([asdict(c) for c in MATRIX], indent=2))
    print("Use spotcobuild/harness/reproduction_matrix.ps1 on the Windows host to execute.")


if __name__ == "__main__":
    main()
