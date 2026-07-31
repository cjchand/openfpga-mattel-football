# Platform Icon Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the still-unmodified `open-fpga/core-template` placeholder at `dist/platforms/_images/mattel_football.bin` with original, grayscale, LED-style "FOOTBALL" wordmark artwork.

**Architecture:** Two small Python modules under `tools/`: `platform_icon.py` (pure encode/decode between a human-friendly landscape PIL Image and the Pocket's confirmed on-disk raw format) and `gen_platform_icon.py` (the actual artwork: a hand-coded 7-segment font renderer, no external font/image dependencies). A matching test lives in `sim/` following this repo's existing Python test convention (`sim/test_reverse_rbf.py`).

**Tech Stack:** Python 3, Pillow (already a project dependency via `tools/gen_bezel_bitmaps.py`).

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-31-platform-icon-design.md`.
- Output file: `dist/platforms/_images/mattel_football.bin`, exactly 171,930 bytes (165×521×2).
- On-disk format (confirmed, see spec): portrait 165w×521h, row-major, 2 bytes/pixel little-endian, low byte = grayscale (0-255), high byte always `0x00`.
- Art is authored/reviewed in landscape 521×165 orientation and converted via lossless 90° `Image.transpose()` — never `Image.rotate()` (which resamples).
- Grayscale only — no color anywhere in the source art.
- No traced, scanned, or otherwise-derived Mattel source material. All artwork is procedurally drawn from geometric primitives.
- Do not modify `dist/icon.bin` (out of scope per spec — unused template leftover).

---

### Task 1: Icon encode/decode module

**Files:**
- Create: `tools/platform_icon.py`
- Create: `sim/test_platform_icon.py`
- Modify: `Makefile:44-45` (add the new test to the `sim-python` target)

**Interfaces:**
- Produces: `LANDSCAPE_SIZE: tuple[int,int]` = `(521, 165)`, `PORTRAIT_SIZE: tuple[int,int]` = `(165, 521)`, `encode_landscape_to_bin(img: PIL.Image.Image) -> bytes`, `decode_bin_to_landscape(data: bytes) -> PIL.Image.Image` — all consumed by Task 2.

- [ ] **Step 1: Write `tools/platform_icon.py`**

```python
"""Encode/decode for Analogue Pocket platform-browser icons
(dist/platforms/_images/<platform_id>.bin).

Format confirmed empirically against a real, populated Pocket SD card
(see docs/superpowers/specs/2026-07-31-platform-icon-design.md):
stored portrait (165w x 521h, matching the Pocket panel's native
orientation), row-major, 2 bytes/pixel little-endian where the low byte
is an 8-bit grayscale value and the high byte is always 0. Art is
authored/reviewed in the human-friendly landscape orientation (521x165,
matching how the icon actually reads on-screen) and rotated for
storage.
"""
from PIL import Image

LANDSCAPE_SIZE = (521, 165)
PORTRAIT_SIZE = (165, 521)


def encode_landscape_to_bin(img: Image.Image) -> bytes:
    if img.size != LANDSCAPE_SIZE:
        raise ValueError(f"expected {LANDSCAPE_SIZE}, got {img.size}")
    portrait = img.convert("L").transpose(Image.Transpose.ROTATE_90)
    gray = portrait.tobytes()
    out = bytearray(len(gray) * 2)
    out[0::2] = gray
    return bytes(out)


def decode_bin_to_landscape(data: bytes) -> Image.Image:
    expected_len = PORTRAIT_SIZE[0] * PORTRAIT_SIZE[1] * 2
    if len(data) != expected_len:
        raise ValueError(f"expected {expected_len} bytes, got {len(data)}")
    gray = data[0::2]
    portrait = Image.frombytes("L", PORTRAIT_SIZE, bytes(gray))
    return portrait.transpose(Image.Transpose.ROTATE_270)
```

- [ ] **Step 2: Write the failing test `sim/test_platform_icon.py`**

```python
"""Platform icon encode/decode must round-trip losslessly and match the
confirmed on-disk format (see tools/platform_icon.py)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from PIL import Image
from platform_icon import (
    LANDSCAPE_SIZE,
    PORTRAIT_SIZE,
    decode_bin_to_landscape,
    encode_landscape_to_bin,
)


def main() -> int:
    # Deterministic synthetic gradient, not a blank/solid image -- a
    # solid-color test would pass even with the width/height transposed,
    # since every pixel is identical either way.
    img = Image.new("L", LANDSCAPE_SIZE)
    px = img.load()
    for x in range(LANDSCAPE_SIZE[0]):
        for y in range(LANDSCAPE_SIZE[1]):
            px[x, y] = (x + y * 3) % 256

    data = encode_landscape_to_bin(img)
    expected_len = PORTRAIT_SIZE[0] * PORTRAIT_SIZE[1] * 2
    assert len(data) == expected_len, f"size: {len(data)} != {expected_len}"

    assert set(data[1::2]) == {0}, "high byte must always be 0"

    decoded = decode_bin_to_landscape(data)
    assert decoded.size == LANDSCAPE_SIZE, decoded.size
    assert list(decoded.getdata()) == list(img.getdata()), "round trip must be lossless"

    print("PASS: test_platform_icon")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Run test to verify it fails (module doesn't exist yet — reorder if you wrote Step 1 first)**

If you followed the steps in order, `tools/platform_icon.py` already exists, so skip straight to Step 4. If practicing strict TDD, comment out Step 1's file content first, confirm an `ImportError`, then restore it.

- [ ] **Step 4: Run the test to verify it passes**

Run: `python3 sim/test_platform_icon.py`
Expected: `PASS: test_platform_icon`

- [ ] **Step 5: Wire into the `sim-python` Makefile target**

In `Makefile`, change:
```makefile
sim-python:
	python3 sim/test_reverse_rbf.py
	python3 sim/test_trace_diff.py
```
to:
```makefile
sim-python:
	python3 sim/test_reverse_rbf.py
	python3 sim/test_trace_diff.py
	python3 sim/test_platform_icon.py
```

- [ ] **Step 6: Run the full sim suite**

Run: `make sim`
Expected: all existing tests still `PASS`, plus `PASS: test_platform_icon`.

- [ ] **Step 7: Commit**

```bash
git add tools/platform_icon.py sim/test_platform_icon.py Makefile
git commit -m "feat: platform icon encode/decode module"
```

---

### Task 2: Wordmark generator

**Files:**
- Create: `tools/gen_platform_icon.py`

**Interfaces:**
- Consumes: `tools.platform_icon.{LANDSCAPE_SIZE, encode_landscape_to_bin}` (Task 1).
- Produces: `tools/platform_icon_preview.png` (human review artifact, gitignored — see Step 5) and, once approved, `dist/platforms/_images/mattel_football.bin`.

- [ ] **Step 1: Write `tools/gen_platform_icon.py`**

```python
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
```

- [ ] **Step 2: Generate the preview**

Run: `python3 tools/gen_platform_icon.py`
Expected: `Preview written to tools/platform_icon_preview.png` with no errors.

- [ ] **Step 3: Sanity-check the render before human review**

```bash
python3 -c "
from PIL import Image
img = Image.open('tools/platform_icon_preview.png')
assert img.size == (521, 165), img.size
colors = set(img.convert('RGB').getdata())
assert all(r == g == b for r, g, b in colors), 'must be grayscale'
assert len(colors) > 1, 'must not be blank'
print('sanity check OK:', len(colors), 'distinct gray levels')
"
```
Expected: `sanity check OK: N distinct gray levels` (N > 1), no assertion errors.

- [ ] **Step 4: Human review checkpoint**

Show `tools/platform_icon_preview.png` to the user for visual approval (readable "FOOTBALL" wordmark, correctly proportioned, yard-line ticks look right) before proceeding. Iterate on `CHAR_W`/`CHAR_H`/`SEG_T`/`CHAR_GAP`/tick geometry in Step 1 if requested, re-running Steps 2-4, until approved.

- [ ] **Step 5: Add the preview PNG to `.gitignore`**

It's a regeneratable review artifact (same category as `sim/*.ppm`), not something to commit. In `.gitignore`, add:
```
tools/platform_icon_preview.png
```

- [ ] **Step 6: Commit**

```bash
git add tools/gen_platform_icon.py .gitignore
git commit -m "feat: LED-style FOOTBALL wordmark generator for the platform icon"
```

---

### Task 3: Produce and stage the final icon

**Files:**
- Modify: `dist/platforms/_images/mattel_football.bin` (binary, regenerated)

**Interfaces:**
- Consumes: `tools/gen_platform_icon.py --write` (Task 2), `tools.platform_icon.decode_bin_to_landscape` (Task 1).

- [ ] **Step 1: Generate the final `.bin`**

Run: `python3 tools/gen_platform_icon.py --write`
Expected: `Wrote dist/platforms/_images/mattel_football.bin (171930 bytes)`.

- [ ] **Step 2: Verify the file size exactly**

Run: `wc -c "dist/platforms/_images/mattel_football.bin"`
Expected: `171930`.

- [ ] **Step 3: Decode-and-diff round-trip check against the approved preview**

```bash
python3 -c "
import sys
from pathlib import Path
sys.path.insert(0, 'tools')
from PIL import Image
from platform_icon import decode_bin_to_landscape

data = Path('dist/platforms/_images/mattel_football.bin').read_bytes()
decoded = decode_bin_to_landscape(data)
preview = Image.open('tools/platform_icon_preview.png').convert('L')
assert list(decoded.getdata()) == list(preview.getdata()), 'bin does not match approved preview'
print('round-trip OK: .bin matches the approved preview exactly')
"
```
Expected: `round-trip OK: .bin matches the approved preview exactly`.

- [ ] **Step 4: Run the full sim suite once more (regression check)**

Run: `make sim`
Expected: all tests `PASS`, unaffected by this change (no RTL touched).

- [ ] **Step 5: Human on-hardware checkpoint**

Copy the updated `dist/` onto the Pocket's SD card and confirm the platform-browser icon renders correctly (readable, positioned as expected, no distortion) on real hardware — a 16bpp-grayscale render can't be fully verified off-device. **Do not proceed to commit until confirmed.**

- [ ] **Step 6: Commit**

```bash
git add "dist/platforms/_images/mattel_football.bin"
git commit -m "feat: replace placeholder platform icon with FOOTBALL wordmark art"
```
