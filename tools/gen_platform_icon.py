#!/usr/bin/env python3
"""Generates dist/platforms/_images/mattel_football.bin -- the Pocket's
platform-browser icon. Run with no arguments to (re)generate the review
preview PNG; run with --write to also produce the final .bin (see
docs/superpowers/specs/2026-07-31-platform-icon-design.md).

Original artwork: a black background with a white 7-segment/LED-style
"FOOTBALL" wordmark and mid-gray yard-line tick marks. The segment font
below is a from-scratch original interpretation (not copied from any
existing 7-segment charset or Mattel source material) -- T and B use a
shorter "lowercase-style" form (no top segment) since uppercase T/B
have no clean, unambiguous 7-segment representation, the same
limitation real 7-segment alphanumeric displays have.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from platform_icon import LANDSCAPE_SIZE, encode_landscape_to_bin

REPO_ROOT = Path(__file__).resolve().parent.parent
OUT_BIN = REPO_ROOT / "dist" / "platforms" / "_images" / "mattel_football.bin"
PREVIEW_PNG = REPO_ROOT / "tools" / "platform_icon_preview.png"

BG = 0
FG = 255
TICK_GRAY = 90

# 7-segment cell geometry: one glyph is CHAR_W x CHAR_H, segment
# thickness SEG_T. Segments named per the standard a(top)/b(upper-right)/
# c(lower-right)/d(bottom)/e(lower-left)/f(upper-left)/g(middle)
# convention.
CHAR_W = 55
CHAR_H = 100
SEG_T = 10
CHAR_GAP = 8

FONT = {
    "F": {"a", "f", "g", "e"},
    "O": {"a", "b", "c", "d", "e", "f"},
    "T": {"d", "e", "f", "g"},
    "B": {"f", "g", "e", "c", "d"},
    "A": {"a", "b", "c", "e", "f", "g"},
    "L": {"d", "e", "f"},
}

WORD = "FOOTBALL"


def segment_rects(w: int, h: int, t: int) -> dict:
    half = h // 2
    return {
        "a": (0, 0, w, t),
        "g": (0, half - t // 2, w, half + t // 2),
        "d": (0, h - t, w, h),
        "f": (0, 0, t, half),
        "b": (w - t, 0, w, half),
        "e": (0, half, t, h),
        "c": (w - t, half, w, h),
    }


def draw_char(draw: ImageDraw.ImageDraw, x0: int, y0: int, ch: str) -> None:
    rects = segment_rects(CHAR_W, CHAR_H, SEG_T)
    for seg in FONT[ch]:
        x1, y1, x2, y2 = rects[seg]
        draw.rectangle([x0 + x1, y0 + y1, x0 + x2, y0 + y2], fill=FG)


def render() -> Image.Image:
    img = Image.new("L", LANDSCAPE_SIZE, BG)
    draw = ImageDraw.Draw(img)

    word_w = len(WORD) * CHAR_W + (len(WORD) - 1) * CHAR_GAP
    x0 = (LANDSCAPE_SIZE[0] - word_w) // 2
    y0 = (LANDSCAPE_SIZE[1] - CHAR_H) // 2
    for i, ch in enumerate(WORD):
        draw_char(draw, x0 + i * (CHAR_W + CHAR_GAP), y0, ch)

    # Yard-line ticks: short mid-gray marks above and below the
    # wordmark, echoing the in-game field's yard markers without
    # reproducing any specific device's field art.
    for tick_y in (y0 - 14, y0 + CHAR_H + 14):
        for i in range(5):
            tx = x0 + i * (word_w // 4)
            draw.line([(tx, tick_y - 4), (tx, tick_y + 4)], fill=TICK_GRAY, width=2)

    return img


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true",
                         help="write the final .bin (default: preview PNG only)")
    args = parser.parse_args()

    img = render()
    img.save(PREVIEW_PNG)
    print(f"Preview written to {PREVIEW_PNG}")

    if args.write:
        data = encode_landscape_to_bin(img)
        OUT_BIN.write_bytes(data)
        print(f"Wrote {OUT_BIN} ({len(data)} bytes)")
    else:
        print("Review the preview, then re-run with --write to produce the .bin.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
