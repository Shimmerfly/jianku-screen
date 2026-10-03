#!/usr/bin/env python3
"""Turn the supplied logo into every icon asset the project needs.

Run from the repository root:

    python3 scripts/build-branding.py            # uses ./logo.png
    python3 scripts/build-branding.py path.png   # or another source

Only the standard library plus ffmpeg is used, because this machine has neither
Pillow nor numpy and adding a dependency just to resize a PNG is not worth it.
The script is idempotent: every output is regenerated from the source each run, so
the repository never holds a half-updated icon set.

What it produces, and why each is needed:

  assets/branding/icon-source.png   the logo cropped to its own opaque bounds, so
                                    the macOS icon grid is not applied twice
  assets/branding/JiankuScreen.icns the application icon (Finder, Dock, About box)
  assets/branding/jianku-mark-*.png the in-app marks: the title bar, the permission
                                    guide's app tile and the tray menu
  assets/branding/jianku-menubar.png the menu bar template image (black + alpha, so
                                    macOS inverts it for light and dark menu bars)

The source logo already contains a finished macOS squircle with its own shadow, so
the icon is *not* masked to Apple's rounded-rectangle grid — doing that would put a
second rounded edge inside the first. What it does need is cropping: the supplied
file has ~7% transparent margin around the tile, which would make the icon look
smaller than every other icon in the Dock.
"""

from __future__ import annotations

import os
import struct
import subprocess
import sys
from typing import List, Tuple

# Alpha above this counts as "the tile": low-alpha values are the soft outer shadow.
# Taking 0 would include invisible noise; taking 128 would clip the anti-aliased rim.
ALPHA_MIN = 8

# The macOS icon grid: on a 1024 px canvas the rounded square covers 824 px (80.5%).
# The supplied logo fills 84.7% of its own canvas, so it is scaled to match rather
# than used as-is — otherwise it would sit visibly larger than its neighbours.
ICON_GLYPH_FRACTION = 824.0 / 1024.0

ICNS_SIZES = [16, 32, 64, 128, 256, 512, 1024]

# In-app marks. 22 px is the menu bar, 48 px the title bar, 128 px the permission
# guide's app tile and any future about panel.
MARK_SIZES = [22, 48, 128]

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(args: List[str]) -> None:
    result = subprocess.run(args, capture_output=True)
    if result.returncode != 0:
        raise SystemExit(f"{args[0]} failed: {result.stderr.decode()[:400]}")


def png_size(path: str) -> Tuple[int, int]:
    """Width and height straight out of the PNG IHDR."""
    with open(path, "rb") as handle:
        header = handle.read(24)
    if header[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path} is not a PNG")
    return struct.unpack(">II", header[16:24])


def rgba(path: str) -> Tuple[bytes, int, int]:
    """Raw RGBA bytes plus the image size, via ffmpeg."""
    width, height = png_size(path)
    result = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-f", "rawvideo", "-pix_fmt", "rgba", "-"],
        capture_output=True)
    if result.returncode != 0:
        raise SystemExit(f"decoding {path} failed: {result.stderr.decode()[:400]}")
    data = result.stdout
    if len(data) != width * height * 4:
        raise SystemExit(f"{path}: expected {width * height * 4} bytes, got {len(data)}")
    return data, width, height


def opaque_bounds(data: bytes, width: int, height: int) -> Tuple[int, int, int, int]:
    """Bounding box of everything above the alpha threshold."""
    min_x, min_y, max_x, max_y = width, height, -1, -1
    for y in range(height):
        row = y * width * 4
        for x in range(width):
            if data[row + x * 4 + 3] > ALPHA_MIN:
                if x < min_x:
                    min_x = x
                if x > max_x:
                    max_x = x
                if y < min_y:
                    min_y = y
                if y > max_y:
                    max_y = y
    if max_x < 0:
        raise SystemExit("the source image is fully transparent")
    return min_x, min_y, max_x, max_y


def crop(source: str, target: str, box: Tuple[int, int, int, int]) -> None:
    min_x, min_y, max_x, max_y = box
    width = max_x - min_x + 1
    height = max_y - min_y + 1
    run(["ffmpeg", "-v", "error", "-i", source, "-vf",
         f"crop={width}:{height}:{min_x}:{min_y}", "-y", target])


def pad_square(source: str, target: str, size: int, glyph_fraction: float) -> None:
    """Fit the image into a transparent square with the macOS icon grid margin."""
    inner = max(1, int(round(size * glyph_fraction)))
    # Even dimensions keep the PNG encoders happy and avoid a half-pixel centre.
    if inner % 2:
        inner -= 1
    run(["ffmpeg", "-v", "error", "-i", source, "-vf",
         f"scale={inner}:{inner}:flags=lanczos,"
         f"pad={size}:{size}:{(size - inner) // 2}:{(size - inner) // 2}:color=0x00000000",
         "-y", target])


def resize(source: str, target: str, size: int) -> None:
    run(["ffmpeg", "-v", "error", "-i", source, "-vf",
         f"scale={size}:{size}:flags=lanczos", "-y", target])


def menubar_template(source: str, target: str, size: int) -> None:
    """Black glyph on transparency, for the menu bar.

    A menu bar template image is defined by its alpha channel alone: macOS paints it
    black on a light menu bar and white on a dark one, so it needs a *shape*, not a
    picture. Two ways of deriving one from this logo were tried and rejected:

      - the source's own alpha: it is a solid rounded tile, so the result is a filled
        square with no recognisable mark;
      - luminance: the tile is dark and the glyph is bright, which inverts the mark
        and also pulls in the inner screen area.

    What works is saturation — the mark's four brand colours are saturated while the
    tile and the screen opening are neutral graphite, so colourfulness isolates the
    mark. The result is then reduced to a silhouette, because a 22 px template has to
    read as one shape.
    """
    # geq works in 0..255, not 0..1 — a threshold of "0.18" would therefore be true
    # for any pixel that is not perfectly grey, which turns the whole tile opaque.
    # Measured on this logo: the neutral graphite is 0.067..0.075 saturation and the
    # mark's four colours are 0.54..0.94, so 0.25 of full scale separates them with
    # room on both sides.
    threshold = int(round(0.25 * 255))
    saturation = "(max(max(r(X,Y),g(X,Y)),b(X,Y))-min(min(r(X,Y),g(X,Y)),b(X,Y)))"
    run(["ffmpeg", "-v", "error", "-i", source, "-vf",
         f"scale={size}:{size}:flags=lanczos,format=rgba,"
         # A hard silhouette: saturated pixels become opaque black, everything else
         # transparent. At 22 pt the mark has to read as one shape.
         f"geq=r=0:g=0:b=0:a='if(gt({saturation},{threshold}),255,0)',"
         "format=rgba",
         "-y", target])


def main() -> int:
    source = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "logo.png")
    if not os.path.exists(source):
        raise SystemExit(f"source image not found: {source}")

    out_dir = os.path.join(REPO, "assets", "branding")
    os.makedirs(out_dir, exist_ok=True)

    data, width, height = rgba(source)
    box = opaque_bounds(data, width, height)
    min_x, min_y, max_x, max_y = box
    print(f"source {width}x{height}, tile {max_x - min_x + 1}x{max_y - min_y + 1} "
          f"at ({min_x},{min_y})")

    cropped = os.path.join(out_dir, "icon-source.png")
    crop(source, cropped, box)

    # --- application icon -------------------------------------------------
    iconset = os.path.join(out_dir, "JiankuScreen.iconset")
    os.makedirs(iconset, exist_ok=True)
    for size in ICNS_SIZES:
        # Retina variants: an @2x entry of N is a plain file of 2N.
        entries = [(f"icon_{size}x{size}.png", size)]
        if size * 2 <= 1024:
            entries.append((f"icon_{size}x{size}@2x.png", size * 2))
        for name, pixel in entries:
            target = os.path.join(iconset, name)
            pad_square(cropped, target, pixel, ICON_GLYPH_FRACTION)
    icns = os.path.join(out_dir, "JiankuScreen.icns")
    run(["iconutil", "-c", "icns", iconset, "-o", icns])
    print(f"wrote {os.path.relpath(icns, REPO)}")

    # --- in-app marks -----------------------------------------------------
    for size in MARK_SIZES:
        target = os.path.join(out_dir, f"jianku-mark-{size}.png")
        pad_square(cropped, target, size, ICON_GLYPH_FRACTION)
        print(f"wrote {os.path.relpath(target, REPO)}")

    # --- menu bar ---------------------------------------------------------
    menubar = os.path.join(out_dir, "jianku-menubar.png")
    menubar_template(cropped, menubar, 44)
    print(f"wrote {os.path.relpath(menubar, REPO)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
