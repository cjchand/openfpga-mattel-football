# Porting FB2's authentic LED flicker to the FB1 core

**Status:** plan only, nothing implemented.
**Written:** 2026-08-06, by the agent that built the Football II core.
**Source of truth for everything referenced here:**
[`cjchand/openfpga-mattel-football-ii`](https://github.com/cjchand/openfpga-mattel-football-ii)
at commit
[`010c4a6`](https://github.com/cjchand/openfpga-mattel-football-ii/tree/010c4a667ba947ad06042f9eb1dc984f3a9a27ec)
(links below are pinned to that commit so they cannot rot).

---

## 1. What is being asked for

The FB2 core reproduces the real handheld's LED behaviour, including the
slight per-LED brightness variation and the genuine blink of a couple of
cells. On hardware this reads as authentic. The owner of both cores has
compared them and wants **FB1 to behave the same way**.

"Flicker" here does **not** mean video tearing or a rendering artifact. It
means the physical consequence of a multiplexed LED matrix: an LED that is
strobed at a low duty cycle looks dimmer, and one whose duty cycle changes
frame to frame visibly pulses. MAME models this, FB2 models it, and FB1
currently suppresses it.

## 2. Why FB1 does not flicker today

Two separate mechanisms, and **only the second one is the real target**.

### 2a. `led_capture.v` already measures duty correctly

[`src/led_capture.v`](https://github.com/cjchand/openfpga-mattel-football/blob/main/src/led_capture.v)
already integrates each LED's on-time over a ~60 Hz window and emits a 2-bit
level (off / dim / bright) with thresholds at 2% and 20% of the window. That
matches MAME's `set_bri_levels(0.02, 0.2)` for this driver. **This part is
right and should not be rewritten.**

Its header comment says levels "hold in between, so downstream video is
flicker-free". That is accurate but describes only the intended smoothing of
*within-window* strobing — it does not, by itself, remove frame-to-frame
variation.

### 2b. `video_renderer.v` forces every lit digit segment to full brightness

This is the actual suppression. In
[`src/video_renderer.v`](https://github.com/cjchand/openfpga-mattel-football/blob/main/src/video_renderer.v),
the digit-segment loop and the decimal point both do:

```verilog
lvl = levels[(d*11 + s)*2 +: 2];
if (lvl != 2'd0) lvl = 2'd2;   // any lit segment -> BRIGHT
```

The comment there is honest about it: the ROM multiplexes the status digits
at a lower duty cycle than the ball carrier, this is "faithful to hardware,
but the user wants the score display to read at full brightness". That was a
deliberate earlier decision **by the same owner who is now asking to reverse
it.** Treat it as a preference change, not a bug fix, and keep the change
reversible.

Note the dash field (player/defender LEDs) already passes its dim/bright
distinction through untouched.

## 3. What FB2 does differently

FB2's equivalent module is
[`src/pps41_display_pwm.v`](https://github.com/cjchand/openfpga-mattel-football-ii/blob/010c4a667ba947ad06042f9eb1dc984f3a9a27ec/src/pps41_display_pwm.v),
with a C++ twin at
[`sim/golden/mm77la_display_pwm.cpp`](https://github.com/cjchand/openfpga-mattel-football-ii/blob/010c4a667ba947ad06042f9eb1dc984f3a9a27ec/sim/golden/mm77la_display_pwm.cpp).

The one structural difference from FB1's `led_capture.v` is an **IIR-smoothed
duty estimate**, matching `pwm_display_device`'s default
`set_interpolation(0.5)` in MAME. FB2 keeps a `smooth[]` value per cell and
folds each window's raw count into it with alpha = 1/2 before thresholding,
rather than thresholding the raw count directly.

FB2's renderer,
[`src/video_renderer.v`](https://github.com/cjchand/openfpga-mattel-football-ii/blob/010c4a667ba947ad06042f9eb1dc984f3a9a27ec/src/video_renderer.v),
has **no force-to-bright override** — it maps level 0/1/2 straight to
background / `C_DIM` / `C_BRIGHT`.

## 4. The reference implementation

Correctness is defined by MAME, not by FB2. FB1's driver is **`mfootb` in
`src/mame/handheld/hh_rw5000.cpp`** (a Rockwell B6100 part — note this is a
*different* file and chip family from FB2's `mfootb2`, which lives in
`hh_pps41.cpp`). Its display configuration is:

- `B6100(config, m_maincpu, 280000)` — clock is explicitly "approximation"
- `PWM_DISPLAY(config, m_display).set_size(9, 11)`
- `m_display->set_bri_levels(0.02, 0.2)` — "player led is brighter"
- one digit has a DP (`set_segmask(0x08, 0xff)`)

`pwm_display_device`'s interpolation default (0.5) is in
`src/devices/video/pwm.cpp`. Read that file before implementing — the exact
order of accumulate / interpolate / threshold matters, and getting it
backwards produces a plausible-looking result that is subtly wrong.

## 5. Implementation steps

1. **Remove the force-to-bright override** in `video_renderer.v` — both the
   digit-segment loop and the decimal point. This alone will produce most of
   the visible change. Do this first and look at it before going further; it
   may be all that is wanted.

2. **Add MAME-matching interpolation to `led_capture.v`.** Mirror FB2's
   `smooth[]` approach: fold the window's raw count into a per-cell smoothed
   value with alpha = 1/2, then threshold the smoothed value. Keep the
   existing `DIM_MIN` / `BRIGHT_MIN` derivation.

3. **Extend the C++ golden model** in step with the RTL if FB1 has one
   (FB2 keeps `sim/golden/` and the RTL in lockstep). If FB1's model and RTL
   are compared cycle-by-cycle anywhere, both must change together or the
   lockstep test will fail for the right reason but the wrong cause.

4. **Add a display-parity test against MAME** — see §6.

### Constants that must NOT be copied from FB2

| Constant | FB2 | FB1 | Why |
|---|---|---|---|
| Window length | 1583 | **1167** | Different CPU clock: 380 kHz/4 vs 280 kHz/4 |
| Brightness levels | 0.015 / 0.2 | **0.02 / 0.2** | Different `set_bri_levels` per driver |
| Matrix size | 10 x 11 | **9 x 11** | Different `set_size` |
| Cell count | 110 | **99** | Follows from matrix size |

Copying FB2's numbers wholesale is the single most likely way to get this
wrong, because the result will still *look* plausible.

## 6. How to verify

FB2's display-parity test is
[`sim/golden/display_parity_test.cpp`](https://github.com/cjchand/openfpga-mattel-football-ii/blob/010c4a667ba947ad06042f9eb1dc984f3a9a27ec/sim/golden/display_parity_test.cpp).
It compares every display cell against brightness levels dumped from MAME
running the same ROM. On FB2 it matches **109 of 110 cells exactly**; the
single exception is a genuinely blinking cell where the two are not
phase-locked. Build the FB1 equivalent.

Dump MAME's levels via Lua: `manager.machine.output:get_value("y.x")` for
y = 0..8, x = 0..10 (9 x 11 for this driver).

To build a cut-down MAME for just this driver — far faster than a full
build — the recipe that worked for FB2 is in the header of
[`sim/golden/mame_parity_test.cpp`](https://github.com/cjchand/openfpga-mattel-football-ii/blob/010c4a667ba947ad06042f9eb1dc984f3a9a27ec/sim/golden/mame_parity_test.cpp),
including the macOS SDL link workaround. Substitute
`SOURCES=src/mame/handheld/hh_rw5000.cpp` and `SUBTARGET=rw5000`.

### Acceptance criteria

- Digit segments show dim vs bright rather than uniform full brightness.
- Cells that MAME blinks, blink; cells MAME holds steady, hold steady.
- The dash field's existing behaviour is **unchanged** (it was already
  correct — a regression here is the most likely collateral damage).
- No motion trail on the ball carrier (see lesson 2 below).
- FPGA utilisation and timing unchanged in any meaningful way; the smoothing
  adds one value per cell.

## 7. Lessons learned — please read before starting

These cost real time on FB2. Each one is a specific trap this task can fall
into.

**1. Do not "fix" flicker by smoothing harder.** FB2 set alpha to 1/8 to
suppress what looked like broad idle flicker. It worked, and it was wrong:
the flicker was mostly a *symptom of a CPU bug* (an EOB immediate truncated
to 2 bits made RAM banks unreachable), and the over-smoothing introduced a
visible ~133 ms motion trail on the ball — about 2.7 extra lit cells. Alpha
went back to 1/2 once the CPU was fixed. If the display looks wrong after
this change, suspect the thing feeding it before you touch the filter.
Background: [`docs/kick-tone-lockup-investigation.md`](https://github.com/cjchand/openfpga-mattel-football-ii/blob/010c4a667ba947ad06042f9eb1dc984f3a9a27ec/docs/kick-tone-lockup-investigation.md).

**2. A testbench that holds `ce` high is not testing the synthesised
design.** `led_capture.v` is clock-enabled. On hardware, `ce` fires roughly
once every 129 core clocks, and any combinational strobe stays asserted for
that whole period. At `ce = 1` — one clock per instruction — an effect applied
once and an effect applied 129 times are indistinguishable. This exact hole
let a real bug ship in FB2: each `IOS` was applied ~129 times, corrupting a
shift register and a modulo-3 counter, which produced wrong tone pitches and
an intermittent permanent stuck tone. A 1.2M-cycle real-ROM lockstep that
explicitly compared the affected registers **passed clean** at `ce = 1` and
diverges at cycle 220520 at the real ratio. FB2 now runs that lockstep at
both ratios, using an alternating 129/130 period because the bug's effect was
`period mod 3` and an exact divider hid it. See the `advance()` helper and
`--ce-period` in
[`sim/pps41_core_tb.cpp`](https://github.com/cjchand/openfpga-mattel-football-ii/blob/010c4a667ba947ad06042f9eb1dc984f3a9a27ec/sim/pps41_core_tb.cpp)
and the `core-rom-lockstep-test` target in
[`sim/Makefile`](https://github.com/cjchand/openfpga-mattel-football-ii/blob/010c4a667ba947ad06042f9eb1dc984f3a9a27ec/sim/Makefile).
**Rule of thumb:** a register clocked on `clk` but driven by a combinational
strobe is safe only if its assignments are idempotent. Set/clear a bit or
load a register — fine. Shift, toggle, increment — must be `ce`-qualified.

**3. Two ported copies agreeing proves nothing.** FB2 keeps an RTL module and
a C++ golden model in lockstep, but the RTL is a *port of* the model, so they
share any mistake by construction. Three CPU bugs hid behind exactly that
false confidence. The MAME comparison is what actually establishes
correctness. Get MAME running early — do not defer it to the end.

**4. Breadth is not coverage if none of it exercises the feature.** FB2's
PRO 1 / PRO 2 switch was declared non-functional on the strength of a sweep
across 12 input pins x 256 button combinations x 3 hold lengths — all of
which sat at idle and never started a game. The switch is read a few seconds
*into a play*. It works fine. An impressive-sounding test matrix that never
reaches the code under test is worse than no test, because it is believed.

**5. Check the instrument.** Any instrument whose failure mode is *silence*
deserves one check that it is not silent. FB2's stimulus loader accepted a
missing file without complaint, silently converting a scripted run into the
no-input scenario — which still passed. It now exits non-zero. Loud failures
mostly look after themselves; quiet ones do not.

**6. Prove the test can fail.** Every claim above was verified by
reintroducing the bug and watching the test go red. If you add a parity test
here, deliberately break the interpolation and confirm it fails. A test that
has never failed has not been shown to test anything.

## 8. Rollback

Keep the change on a branch until it has been looked at on real hardware —
the whole point is a subjective visual quality that cannot be judged from
simulation. FB2's practice is to keep the last hardware-verified
`bitstream.rbf_r` on the SD card outside the core folder, so the device owner
can roll back without a laptop by copying one file. Worth doing here too.

Bear in mind §2b: the force-to-bright behaviour was itself a deliberate
request. If the authentic look is judged too dim in the score window, the
middle ground is to keep the interpolation and raise only the *digit*
brightness mapping, rather than collapsing the levels again.
