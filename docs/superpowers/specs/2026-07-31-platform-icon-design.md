# Platform icon design

## Context

`dist/platforms/_images/mattel_football.bin` — the icon shown in the Pocket's
platform browser — is still the unmodified `open-fpga/core-template`
placeholder graphic. It was only renamed (`git mv` from `ex_platform.bin`)
during the branding pass, never redrawn. The user wants original artwork
here instead, with no risk of visually referencing Mattel's own trademarked
packaging/branding.

## Asset format (confirmed against real cores)

- File: `dist/platforms/_images/mattel_football.bin`, raw bitmap, no header.
- Dimensions: 521×165 — confirmed by cross-referencing file size (171,930
  bytes for every platform icon on a real, populated Pocket SD card at
  `/Volumes/Pocket/Platforms/_images/`) against `alphamission.png`
  (521×165), a real core's source image found alongside its `.bin` on the
  same card.
- **Encoding confirmed** by decoding a real reference file
  (`/Volumes/Pocket/Platforms/_images/alphamission.bin`) with various
  candidate formats and visually matching the result against its
  known-good `.png` counterpart: the `.bin` is stored **portrait**
  (165 wide × 521 tall — the Pocket panel's native orientation, not the
  521×165 "landscape" aspect a human would compose art in), row-major,
  2 bytes/pixel **little-endian**, low byte = 8-bit grayscale value
  (0-255), high byte always `0x00`. Art should be authored/reviewed in
  the landscape 521×165 orientation (matching how the icon actually
  reads on-screen) and rotated 90° for storage — confirmed as an exact,
  lossless `Image.transpose()` operation (no interpolation), verified by
  round-tripping the reference file back to its original bytes.
- **Grayscale only.** Sampled two real reference images
  (`alphamission.png`, `amstrad.png`) — every pixel has R=G=B (one stray
  anti-aliased pixel in one image, otherwise exact). The Pocket evidently
  tints these monochrome icons with its own UI theme color at render time,
  so our source art must be authored in grayscale, not color.

## Content

Original, algorithmically-generated artwork — not traced, scanned, or
adapted from any Mattel packaging, manual, or marketing art:

- Black background.
- Bright white "FOOTBALL" wordmark, centered, drawn in a 7-segment/LED-style
  block font — generated programmatically (segment on/off per character),
  not sourced from an existing font or Mattel's own display typography.
  Deliberately omits "Mattel" to stay clear of the trademark (separate
  concern from the ROM/hardware copyright already addressed elsewhere in
  this project) — the core/platform's *name* can still say "Mattel
  Football" in Pocket UI text elsewhere, which is descriptive/nominative
  use, not this artwork.
- A couple of thin mid-gray horizontal yard-line tick marks as a subtle
  accent, echoing the in-game field's yard markers without reproducing any
  specific device's field graphics.

This deliberately reuses the visual language already established for the
in-game bezel (LED segments, dark background) so the icon reads as "this
device" while being entirely our own original composition.

## Non-goals

- `dist/icon.bin` (root-level, not `dist/platforms/_images/`). Not
  referenced by anything under `dist/Cores/` or `dist/platforms/` that we
  actually ship — appears to be unused `core-template` scaffolding. Left
  untouched unless evidence turns up that it's actually displayed
  somewhere.
- Color. Ruled out by the grayscale format constraint above.
- Any change to the in-game bezel/field art — this spec only touches the
  platform-browser icon.

## Implementation approach

1. Write a small Python generation script (sibling to the existing
   `tools/gen_bezel_bitmaps.py`) that:
   - Determines the real `.bin` pixel encoding by decoding a known
     reference file against its `.png` counterpart and confirming a match.
   - Renders the wordmark + yard-line ticks to a 521×165 grayscale PNG for
     human visual review.
   - Converts the approved PNG into the confirmed raw encoding, writing
     `dist/platforms/_images/mattel_football.bin`.
2. Human reviews the rendered PNG before conversion.
3. After conversion, decode the produced `.bin` back to an image and diff
   against the approved PNG to confirm the round-trip is lossless.
4. Human does a final on-hardware check (SD card, Pocket's platform
   browser) — the actual visual result of a 16bpp-grayscale-on-Pocket
   render can't be fully verified off-device.

## Testing

No RTL/simulation involved — this is a static asset. Verification is the
decode-and-diff round-trip check (step 3 above) plus the human visual
checks (PNG preview, then on-hardware).
