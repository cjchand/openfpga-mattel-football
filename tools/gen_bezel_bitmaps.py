#!/usr/bin/env python3
"""Generates the label and field bitmap ROMs from the bezel overlay art.
Run manually when the overlay art (or field geometry constants below)
changes; outputs are committed (same pattern as tools/reverse_rbf.py's
build outputs).

The label bitmap is cropped straight from the overlay PNG (needs the real
anti-aliased text). The field bitmap is drawn procedurally instead of
cropped -- geometry and colors measured directly from a photo of the real
Mattel Electronics Football handheld (see
docs/superpowers/specs/2026-07-26-bezel-overlay-design.md), since the
overlay PNG's own field art undercounted the endzones and used a too-dark
endzone shade. Two endzone columns flank the 9 field columns (11 columns
total, not 9 with the outer two recolored).
"""
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageOps

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC_PNG = REPO_ROOT / "assets" / "bezel" / "overlay_400x360.png"

# Bands measured from the overlay (see
# docs/superpowers/specs/2026-07-26-bezel-overlay-design.md "Measured
# geometry", 400x360 canvas with letterbox padding baked in -- uniform
# scale of the source art, not stretched to fill the canvas).
BAR1_Y0, BAR1_Y1 = 54, 73
BAR2_Y0, BAR2_Y1 = 127, 146
WIDTH = 400

# Each cropped bar (20px tall in the source) is downscaled to this height --
# the scoreboard (label+digit-window+label) read too tall next to a photo
# of the real device, which measures its "Down/Home" column at roughly
# 145x40px, a ~3.625:1 width:height ratio (see src/video_renderer.v's
# window-band comment for how this height, plus the digit-window band's,
# add up to the target total). Downscaled with LANCZOS (not nearest-
# neighbor) since this is real anti-aliased text, not flat color regions.
# Bumped 8->10 (25%): the text read visibly squished at 8 (width stays
# 400 throughout, so only the vertical scale shrinks -- a bigger target
# height eases that distortion, though it doesn't eliminate it).
BAR_OUT_H = 10

# Font tuning on top of the BAR_OUT_H resize: narrower (85% width, applied
# at the source crop's full 400px resolution before downscaling, then the
# whole condensed crop is downscaled+centered in the 400-wide bar). Bold
# (a 1px MIN-per-channel dilation, see bolden()) tried and reverted -- it
# read as blurry rather than bold once LANCZOS-downscaled this hard, so
# BAR_BOLD_PASSES is 0 (bolden() is a no-op at 0 passes). The contrast
# punch (punch_up_contrast()) stays on even with bolding off -- it's also
# what deepens the label blue, which was liked independently of the bold
# experiment.
BAR_WIDTH_SCALE = 0.85
BAR_BOLD_PASSES = 0
BAR_CONTRAST_K = 2.5

# bar2's crop has a few stray green pixels bled in from the field art below
# (confirmed by sampling: (24,126,50)-ish green through x=25/x=374, fading
# to a transitional not-quite-gray through x=27/x=372) -- painted flat gray
# here (not black) so the label bitmap's corners are genuinely clean AND
# match the digit-window band's own C_GRAY margin outside its (now 1px)
# corner accent in video_renderer.v, rather than showing a black-then-gray
# seam once that corner mask shrank to 1px.
LABEL_CORNER_CLEAN_W = 28
LABEL_CORNER_CLEAN_RGB = (203, 203, 203)  # matches video_renderer.v's C_GRAY

LABEL_COLORS = 128  # 7bpp
FIELD_COLORS = 256  # 8bpp

# --- Field geometry (see src/video_renderer.v FIELD_Y0/dash_x/DASH_Y for
# the matching canvas placement and LED overlay geometry) ---
# STRIP height is 108 (144*0.75, a 25% reduction from the original -- the
# playfield read as too tall next to a photo of the real device, which
# measures roughly 485x133px, a ~3.6:1 width:height ratio; 400:108=3.70:1
# lands right next to that without changing the field's width or
# recalculating the whole layout from scratch).
FIELD_W, FIELD_H = 400, 124
GREEN_RGB    = (14, 138, 3)     # field-oval background
BLACK_RGB    = (0, 0, 0)        # field cell fill
DIVIDER_RGB  = (203, 203, 203)  # column dividers -- matches video_renderer.v's C_GRAY
ENDZONE_RGB  = (13, 98, 188)    # sampled from a photo of the real device's blue endzones

STRIP_Y0, STRIP_Y1 = 8, 116  # exclusive end; 108px tall (divisible by 3, see hash marks below)
MARGIN_X = 6      # green shows on both outer edges
EZ_W     = 23     # endzone width
FIELD_X0 = MARGIN_X + EZ_W  # 29: where the 9-column field region starts
COL_PITCH = 38    # 36px cell + 2px divider, x9 columns = 342px, fits exactly between the endzones
DIV_W = 2

# Solid border framing the whole black/blue strip, between it and the
# green -- present on the real device (photo shows a light gray/white
# line separating the field rectangle from the green oval) but missing
# from our earlier procedural art. Drawn as a filled rect the strip's own
# content is painted over, so it only shows where nothing else covers it.
# 4px (vs. the 2px dividers/hash ticks) so it reads as clearly thicker.
# Sized to fit inside the existing green margins (4 < MARGIN_X=6 and
# STRIP_Y0=8) so it doesn't need any extra canvas space.
BORDER_W = 4
BORDER_RGB = DIVIDER_RGB

# Two rows of hash-mark ticks split the strip into 3 equal thirds (top
# sideline-to-hash1, hash1-to-hash2, hash2-to-bottom-sideline) -- LED rows
# in video_renderer.v's DASH_Y0..2 are each centered in one third, matching
# the real device. Ticks are short gray bars straddling each column
# divider, same color as the dividers -- only at the 8 INTERNAL dividers
# (between field columns), not the 2 dividers bordering the endzones, so
# no tick reaches into the endzones themselves.
HASH_Y1 = STRIP_Y0 + (STRIP_Y1 - STRIP_Y0) // 3        # 44
HASH_Y2 = STRIP_Y0 + 2 * (STRIP_Y1 - STRIP_Y0) // 3     # 80
HASH_H = 2
HASH_REACH = 4  # px the tick extends beyond the divider on each side


def write_bitmap(im, colors, bitmap_path, palette_path, bits_per_pixel):
    quantized = im.quantize(colors=colors, method=Image.MEDIANCUT)
    palette_bytes = quantized.getpalette()[: colors * 3]
    # PIL may return a palette shorter than requested when the image has
    # fewer actual unique colors than `colors` -- pad with black so every
    # index up to `colors` is always defined (the Verilog ROM's array is
    # sized to `colors` entries regardless of how many are actually used).
    palette_bytes = palette_bytes + [0] * (colors * 3 - len(palette_bytes))

    with open(palette_path, "w") as f:
        for i in range(colors):
            r, g, b = palette_bytes[i * 3 : i * 3 + 3]
            f.write(f"{(r << 16) | (g << 8) | b:06x}\n")

    pixels = quantized.load()
    hex_digits = (bits_per_pixel + 3) // 4
    with open(bitmap_path, "w") as f:
        for y in range(quantized.height):
            for x in range(quantized.width):
                f.write(f"{pixels[x, y]:0{hex_digits}x}\n")

    print(f"wrote {bitmap_path} ({quantized.width * quantized.height} pixels, {bits_per_pixel}bpp)")
    print(f"wrote {palette_path} ({colors} colors)")


def build_field_image():
    """Draws the field band procedurally: green oval background, a solid
    border framing the strip, 9 black field columns divided by gray
    dividers, a blue endzone column on each side IN ADDITION to the 9 (11
    columns total), and hash-mark ticks splitting the field into thirds --
    full-width at the 8 internal dividers, half-width (field side only,
    never reaching into the endzone) at the 2 dividers bordering the
    endzones."""
    im = Image.new("RGB", (FIELD_W, FIELD_H), GREEN_RGB)
    draw = ImageDraw.Draw(im)

    draw.rectangle(
        [MARGIN_X - BORDER_W, STRIP_Y0 - BORDER_W,
         FIELD_W - MARGIN_X - 1 + BORDER_W, STRIP_Y1 - 1 + BORDER_W],
        fill=BORDER_RGB,
    )

    draw.rectangle([MARGIN_X, STRIP_Y0, MARGIN_X + EZ_W - 1, STRIP_Y1 - 1], fill=ENDZONE_RGB)

    divider_xs = [FIELD_X0 + COL_PITCH * i for i in range(10)]
    for i, dx in enumerate(divider_xs):
        draw.rectangle([dx, STRIP_Y0, dx + DIV_W - 1, STRIP_Y1 - 1], fill=DIVIDER_RGB)
        if i < 9:
            cx0 = dx + DIV_W
            cx1 = FIELD_X0 + COL_PITCH * (i + 1) - 1
            draw.rectangle([cx0, STRIP_Y0, cx1, STRIP_Y1 - 1], fill=BLACK_RGB)

    ez2_x0 = divider_xs[9] + DIV_W
    draw.rectangle([ez2_x0, STRIP_Y0, ez2_x0 + (EZ_W - DIV_W) - 1, STRIP_Y1 - 1], fill=ENDZONE_RGB)

    for dx in divider_xs[1:9]:  # internal dividers: full-width ticks on both sides
        for hy in (HASH_Y1, HASH_Y2):
            draw.rectangle(
                [dx - HASH_REACH, hy - HASH_H // 2, dx + DIV_W - 1 + HASH_REACH, hy + HASH_H // 2 - 1],
                fill=DIVIDER_RGB,
            )
    # The 2 dividers bordering the endzones get a HALF-width tick -- it
    # reaches into the field side only, never into the endzone.
    for hy in (HASH_Y1, HASH_Y2):
        dx = divider_xs[0]
        draw.rectangle([dx, hy - HASH_H // 2, dx + DIV_W - 1 + HASH_REACH, hy + HASH_H // 2 - 1], fill=DIVIDER_RGB)
        dx = divider_xs[9]
        draw.rectangle([dx - HASH_REACH, hy - HASH_H // 2, dx + DIV_W - 1, hy + HASH_H // 2 - 1], fill=DIVIDER_RGB)

    return im


def bolden(im, passes):
    """Dilates the darker (text) pixels into their lighter (background)
    neighbors by combining 1px-shifted copies via per-channel MIN -- the
    label text is blue-on-gray, and blue's channel values are lower than
    the gray background's, so ImageChops.darker() (per-channel min)
    expands the text's footprint by a pixel each pass, thickening strokes
    without needing a real font renderer. Padded with the background gray
    before shifting and cropped back after -- ImageChops.offset() wraps
    circularly, which without padding would pull in dark pixels from the
    crop's opposite edge (the black corner-accent columns) and smear them
    across the visible text area."""
    pad = passes
    padded = ImageOps.expand(im, border=pad, fill=LABEL_CORNER_CLEAN_RGB)
    result = padded
    for _ in range(passes):
        for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            result = ImageChops.darker(result, ImageChops.offset(result, dx, dy))
    return result.crop((pad, pad, pad + im.width, pad + im.height))


def punch_up_contrast(im, k):
    """Pushes every channel value away from the background gray (203) by
    factor k, clamped to [0,255] -- LANCZOS-downscaling anti-aliased text
    this aggressively (400px source down to a ~10px-tall glyph) blends
    most of the stroke into semi-transparent gray-blue, which reads as
    blurry rather than bold. Steepening the falloff around the background
    level restores crisp, more solid-looking strokes without a real font
    renderer."""
    lut = [max(0, min(255, round(203 + (v - 203) * k))) for v in range(256)]
    return im.point(lut * 3)


# Each bar holds 3 labels. Cropping each by a fixed 1/3-of-the-bar column
# and centering THAT column on its digit window (the first attempt)
# doesn't actually center the TEXT unless the text happens to sit dead
# center within that column -- it doesn't ("DOWN" and "YARDS TO GO" are
# short words nowhere near the middle of a generously-wide crop). Bounds
# below are the real glyph extents, measured directly off
# assets/bezel/overlay_400x360.png (row-scanned for non-background,
# non-corner-accent pixels; padded 5px each side) -- see whichever
# revision of this file first added them for the measurement script. Bar1
# and bar2's wording differs ("FIELD POSITION" vs "TIME REMAINING" etc.)
# so their extents differ too. Target centers are the new (post-narrowing)
# digit window centers ((41+140)/2, (141+259)/2, (260+359)/2, see
# src/video_renderer.v's digit_x comment) -- unaffected by which bar,
# since both sit above/below the same 3 windows.
LABEL1_SRC_BOUNDS = [(56, 106), (141, 253), (266, 363)]   # DOWN / FIELD POSITION / YARDS TO GO
LABEL2_SRC_BOUNDS = [(58, 104), (137, 256), (285, 346)]   # HOME / TIME REMAINING / VISITOR
LABEL_COL_TARGET_CENTERS = [90, 200, 310]


def bold_and_narrow(crop, src_bounds):
    """Bolds each of the 3 labels at the source crop's full resolution (so
    the 1px dilation is meaningful before it gets downscaled), narrows
    each to BAR_WIDTH_SCALE of its own (tightly-cropped, see src_bounds)
    source width, and pastes it centered on its corresponding (possibly
    shifted) digit window, onto a full-width, panel-gray canvas. The
    crop's own corner columns must already be painted clean gray (see
    main()) before this runs, or the outer labels' narrowing+recentering
    would drag them inward from the edges into the middle of the visible
    bar."""
    canvas = Image.new("RGB", (WIDTH, BAR_OUT_H), LABEL_CORNER_CLEAN_RGB)
    for (src_x0, src_x1), target_center in zip(src_bounds, LABEL_COL_TARGET_CENTERS):
        seg = crop.crop((src_x0, 0, src_x1, crop.height))
        seg = bolden(seg, BAR_BOLD_PASSES)
        seg_w = max(1, round((src_x1 - src_x0) * BAR_WIDTH_SCALE))
        seg = seg.resize((seg_w, BAR_OUT_H), Image.LANCZOS)
        seg = punch_up_contrast(seg, BAR_CONTRAST_K)
        canvas.paste(seg, (round(target_center - seg_w / 2), 0))
    return canvas


def main():
    im = Image.open(SRC_PNG).convert("RGB")
    if im.size != (400, 360):
        sys.exit(f"expected 400x360, got {im.size}")

    bar1_crop = im.crop((0, BAR1_Y0, WIDTH, BAR1_Y1 + 1))
    bar2_crop = im.crop((0, BAR2_Y0, WIDTH, BAR2_Y1 + 1))
    for crop in (bar1_crop, bar2_crop):
        crop_draw = ImageDraw.Draw(crop)
        crop_draw.rectangle([0, 0, LABEL_CORNER_CLEAN_W - 1, crop.height - 1], fill=LABEL_CORNER_CLEAN_RGB)
        crop_draw.rectangle([WIDTH - LABEL_CORNER_CLEAN_W, 0, WIDTH - 1, crop.height - 1], fill=LABEL_CORNER_CLEAN_RGB)

    bar1 = bold_and_narrow(bar1_crop, LABEL1_SRC_BOUNDS)
    bar2 = bold_and_narrow(bar2_crop, LABEL2_SRC_BOUNDS)
    labels = Image.new("RGB", (WIDTH, bar1.height + bar2.height))
    labels.paste(bar1, (0, 0))
    labels.paste(bar2, (0, bar1.height))
    write_bitmap(
        labels, LABEL_COLORS,
        REPO_ROOT / "src" / "label_bitmap.mem", REPO_ROOT / "src" / "label_palette.mem",
        bits_per_pixel=7,
    )

    field = build_field_image()
    write_bitmap(
        field, FIELD_COLORS,
        REPO_ROOT / "src" / "field_bitmap.mem", REPO_ROOT / "src" / "field_palette.mem",
        bits_per_pixel=8,
    )


if __name__ == "__main__":
    main()
