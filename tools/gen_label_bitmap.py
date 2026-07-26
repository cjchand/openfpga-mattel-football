#!/usr/bin/env python3
"""Generates src/label_bitmap.mem and src/label_palette.mem from the
bezel overlay art. Run manually when the overlay art changes; outputs are
committed (same pattern as tools/reverse_rbf.py's build outputs)."""
import sys
from pathlib import Path

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC_PNG = REPO_ROOT / "assets" / "bezel" / "overlay_400x360.png"
OUT_BITMAP = REPO_ROOT / "src" / "label_bitmap.mem"
OUT_PALETTE = REPO_ROOT / "src" / "label_palette.mem"

# Label bar bands, measured from the overlay (see
# docs/superpowers/specs/2026-07-26-bezel-overlay-design.md "Measured
# geometry"): bar 1 y 0-28 (29 rows), bar 2 y 103-131 (29 rows). Unaffected
# by the 502->400 width revert (only x-axis geometry scales with width;
# the source height scale factor, 402->360, was unchanged throughout).
BAR1_Y0, BAR1_Y1 = 0, 28
BAR2_Y0, BAR2_Y1 = 103, 131
WIDTH = 400


def main():
    im = Image.open(SRC_PNG).convert("RGB")
    if im.size != (400, 360):
        sys.exit(f"expected 400x360, got {im.size}")

    bar1 = im.crop((0, BAR1_Y0, WIDTH, BAR1_Y1 + 1))
    bar2 = im.crop((0, BAR2_Y0, WIDTH, BAR2_Y1 + 1))
    combined = Image.new("RGB", (WIDTH, bar1.height + bar2.height))
    combined.paste(bar1, (0, 0))
    combined.paste(bar2, (0, bar1.height))

    quantized = combined.quantize(colors=16, method=Image.MEDIANCUT)
    palette_bytes = quantized.getpalette()[: 16 * 3]

    with open(OUT_PALETTE, "w") as f:
        for i in range(16):
            r, g, b = palette_bytes[i * 3 : i * 3 + 3]
            f.write(f"{(r << 16) | (g << 8) | b:06x}\n")

    pixels = quantized.load()
    with open(OUT_BITMAP, "w") as f:
        for y in range(quantized.height):
            for x in range(WIDTH):
                f.write(f"{pixels[x, y]:x}\n")

    print(f"wrote {OUT_BITMAP} ({WIDTH * quantized.height} pixels)")
    print(f"wrote {OUT_PALETTE} (16 colors)")


if __name__ == "__main__":
    main()
