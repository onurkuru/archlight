#!/usr/bin/env python3
"""Scan art-src/*.zip and report what's inside, so a real atlas packer can be
written against actual file names instead of guesses.

    python3 tools/inspect_art.py
"""

import struct
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ART_SRC = ROOT / "art-src"


def png_size(data: bytes):
    """IHDR width/height without a Pillow dependency."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    w, h = struct.unpack(">II", data[16:24])
    return w, h


def main():
    if not ART_SRC.exists():
        print(f"no {ART_SRC} yet - nothing dropped there")
        return 0

    zips = sorted(ART_SRC.glob("*.zip"))
    if not zips:
        print(f"{ART_SRC} exists but has no .zip files yet")
        return 0

    only = sys.argv[1] if len(sys.argv) > 1 else None
    if only:
        zips = [z for z in zips if only.lower() in z.name.lower()]

    for zp in zips:
        print(f"\n=== {zp.name} ===")
        with zipfile.ZipFile(zp) as z:
            names = [n for n in z.namelist() if "__MACOSX" not in n]
            pngs = [n for n in names if n.lower().endswith(".png")]
            others = [n for n in names
                     if not n.endswith("/") and not n.lower().endswith(".png")]

            for n in sorted(pngs):
                data = z.read(n)
                dims = png_size(data)
                dims_s = f"{dims[0]}x{dims[1]}" if dims else "?"
                print(f"  {dims_s:>10}  {n}")

            license_files = [n for n in others if "licen" in n.lower()
                             or "read" in n.lower()]
            for n in sorted(license_files):
                print(f"  [doc]      {n}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
