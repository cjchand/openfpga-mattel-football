# Bezel overlay design

## Context

The design spec's original "Presentation" section called for a **full handheld**
style — a static background image of the device face (bezel art) with the LED
layer composited on top — with a "field only" fallback toggle. Plan 3
implemented only the field-only fallback (procedural geometry, no background
image); the full-handheld background was deferred.

The user has now supplied an original overlay recreation (640x402 PNG,
vector-style redraw — not a photo scan of Mattel's printed graphics, per the
design spec's IP guidance) showing the device's label bars, digit windows,
green field, and column dividers. This spec covers integrating it.

## Non-goals

- Replicating the physical hardware's LED "ghost" (dim, always-visible
  unlit-segment) look. Per explicit user direction: only two states are
  rendered, dim (defender) and bright (player) — nothing at level 0.
- Pixel-exact reproduction of the real device's typography or proportions.
- Changing the core's fixed 12.288MHz/no-PLL video clock. All canvas sizing
  decisions here work within the existing `H_TOTAL x V_TOTAL = 204800`
  constraint (204800 * 60 = 12,288,000 Hz), so 60.000Hz holds with no PLL
  changes — same constraint already validated on hardware in Plan 4.

## Asset preparation (one-time, offline)

Starting from the user-supplied 640x402 overlay:

1. **Remove baked-in LED marks.** The source image had both dim "ghost" and
   one bright demo segment baked in. Detected via a red-channel-dominance
   threshold (`R > G+5 and R > B+5`, catching everything from faint
   anti-aliased ghosts to a fully-saturated demo "lit" mark) and inpainted
   using a nearest-clean-background-pixel replacement (Euclidean distance
   transform against the image's two pure background colors, black
   `(0,0,0)` and field green `(24,126,50)`). Verified zero red-dominant
   pixels remain afterward.
2. **Scale to 502x360.** The source's ~1.59:1 aspect ratio doesn't fit the
   existing 400x360 canvas without either distortion or growing the canvas
   past what the fixed-clock timing budget allows. Widening `H_ACTIVE` from
   400 to 502 (the max that fits `H_TOTAL=512` minus the existing 10px back
   porch, with `V_TOTAL`/`V_ACTIVE` unchanged from the hardware-verified
   values) gets us to ~1.39:1 — a mild ~12% horizontal squeeze from native,
   chosen over re-deriving new `H_TOTAL`/`V_TOTAL` divisor pairs (which
   would reopen video-timing risk for a purely cosmetic gain).
3. **Measure geometry programmatically** from the scaled, cleaned image
   (exact-color-match row/column scans), rather than eyeballing — see
   Geometry below. This replaces the current `video_renderer.v`'s
   `mfootb.lay`-derived guesses with coordinates matching this specific art.

## Rendering architecture

`video_renderer.v` composites three layers per pixel, first match wins:

1. **LED segments/dashes** (existing mechanism, simplified): the same
   per-segment/per-dash rectangle test as today, recalibrated to the
   measured geometry. `levels[...]==0` draws nothing (falls through to
   layer 2/3); `1`=dim, `2`=bright, using the existing `C_DIM`/`C_BRIGHT`
   colors. `C_GHOST` is removed entirely.
2. **Label text bitmap** (new): the six label strings ("DOWN", "FIELD
   POSITION", "YARDS TO GO", "HOME", "TIME REMAINING", "VISITOR") stored as
   a small indexed-color bitmap in a dedicated block RAM, covering just the
   two label-bar strips (measured ~58px combined height x 502px width).
   Sized at 4bpp (16-color) that's ~113Kbit / ~14KB — negligible against
   the Cyclone V 5CEBA4's ~3,344Kbit total embedded memory, vs. ~1.4Mbit
   (~42% of the whole chip) a naive full-canvas bitmap would cost.
3. **Bezel background** (new, procedural): gray label-bar fills, black
   digit windows, green field strip, white field dividers, and the black
   corner accents flanking the label bar — plain rectangles at the measured
   coordinates below, same technique the renderer already uses for LED
   rects. Falls back to solid black (today's behavior) if the bezel is
   disabled (see Settings toggle).

### Measured geometry (502x360 canvas, from the cleaned/scaled overlay)

| Element | Bounds |
|---|---|
| Label bar 1 (DOWN / FIELD POSITION / YARDS TO GO) | y 0-28 |
| Digit windows (3, black) | y 31-101; x [40-166], [177-324], [335-461] |
| Label bar 2 (HOME / TIME REMAINING / VISITOR) | y 103-131 |
| Green field strip | y 136-181 |
| Field grid (9 columns) | y 191-344, dividers ~55px apart |
| Corner accents (black) | x [0-29] and [472-501], alongside the label-bar/digit-window band |

Existing LED segment-cell and dash-cell rectangles (currently authored
against `mfootb.lay`-derived guesses) are recalibrated to sit inside the
digit windows and field grid rows measured above.

## Settings toggle

`interact.json` gains one `"check"`-type variable ("Presentation": Full
Bezel [default on] / Field Only), per Analogue's documented schema
(`https://www.analogue.co/developer/docs/core-definition-files/interact-json`).
It's backed by a fixed address in the `32'hF8xx2xxx` datatable range that
`core_bridge_cmd.v` already implements (dual-port `mf_datatable` BRAM; port
`a` is the host/bridge side already wired in `core_bridge_cmd.v`, port `b`
is `core_top.v`'s `datatable_addr`/`datatable_wren`/`datatable_data`/
`datatable_q`, already instantiated but unused until now).

`core_top.v` drives a fixed `datatable_addr` matching the variable's address
and registers `datatable_q`'s relevant bit into a `bezel_enable` wire each
cycle, fed into `video_renderer` as a new input. When off, layers 2 and 3 are
skipped (plain black background, matching today's field-only look); LED
rendering (layer 1) is unaffected either way.

This is the first use of the datatable/settings mechanism in this codebase —
new integration surface, but read-only from our side and isolated from
CPU/ROM-loading/video-timing, so it carries materially less risk than those
did.

## Files touched

- `src/video_renderer.v` — new layered compositing, recalibrated geometry,
  `bezel_enable` input, drops `C_GHOST`.
- New: label bitmap source data (generated `.mif` + a small ROM module, or
  inlined depending on toolchain fit — decided during implementation).
- `src/fpga/core/core_top.v` — canvas resize (`H_ACTIVE` 400->502,
  `video.json`-matching), datatable read for `bezel_enable`. `px_x` stays
  9 bits (502 fits in 9 bits; max representable is 511).
- `dist/Cores/cjchand.Mattel Football/video.json` — width 502, updated
  aspect ratio.
- `dist/Cores/cjchand.Mattel Football/interact.json` — new "Presentation"
  variable.
- `sim/video_renderer_tb.cpp` — updated/new pixel assertions for bezel
  layers, recalibrated LED geometry, `bezel_enable` on/off cases.
- `docs/superpowers/plans/2026-07-25-display-pipeline.md` /
  `2026-07-25-mattel-football-core-design.md` — not modified (historical
  plan records); this spec supersedes their "field-only only" outcome for
  presentation.

## Testing

- `sim/video_renderer_tb.cpp`: sample-point assertions for each bezel
  region's color, label-bitmap legibility spot checks, LED rect alignment
  regression (reusing/adapting existing test patterns), `bezel_enable=0`
  falls back to today's plain-black behavior.
- `make frames`: full-system regeneration for human review of composited
  gameplay frames (same review gate used in Plan 3), covering both
  `bezel_enable` states.
- Real-hardware confirmation on the Pocket (both toggle states, at least a
  quick visual check) before considering this done, given the settings
  mechanism is new and its runtime behavior can't be simulated end-to-end.

## Open implementation details (resolved during planning/implementation, not blocking spec approval)

- Exact 4bpp palette indices for the label bitmap and `.mif` generation
  tooling (likely a small Python script added to `tools/`, mirroring
  `tools/reverse_rbf.py`'s style).
- Precise `datatable_addr` value chosen for the Presentation variable (any
  free slot in the `32'hF8xx2xxx` range works; picked to not collide with
  any future settings added later).
