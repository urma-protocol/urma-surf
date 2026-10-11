#!/usr/bin/env python3
"""Rasterize the approved SVG without cropping, recoloring or added background.

Requires rsvg-convert (librsvg); only used to regenerate retained build inputs.
The Browser has no runtime dependency on this renderer.
"""
from pathlib import Path
import subprocess


def main():
    directory = Path(__file__).resolve().parent
    for size in (16, 22, 32, 44, 48, 64, 128, 256):
        subprocess.run([
            "rsvg-convert", "--width", str(size), "--height", str(size),
            "--output", str(directory / f"urma-mark-{size}.png"),
            str(directory / "urma-browser.svg"),
        ], check=True, timeout=30)


if __name__ == "__main__":
    main()
