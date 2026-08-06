#!/usr/bin/env python3
"""Compare our display levels against MAME's, cell by cell.

Both inputs are CSVs of per-frame brightness levels (0=off, 1=dim, 2=bright)
for the 9x11 matrix mfootb's PWM_DISPLAY drives -- MAME's from
tools/golden/dump_display.lua, ours from sim/display_parity_tb.cpp.

The two runs are NOT phase-locked: MAME reclassifies on its own 60 Hz frame
timer while led_capture.v reclassifies every 1167 ce ticks (59.98 Hz), and
neither starts its first window at the same point in the ROM's multiplex
loop. Comparing frame N to frame N is therefore meaningless. What is
comparable is each cell's steady behaviour over a stretch of frames, so this
compares two things per cell:

  * its modal level -- what the cell reads as most of the time; and
  * whether it is steady or blinking, i.e. whether it holds one level for
    the whole capture or changes.

A cell that blinks in both is counted as agreeing regardless of phase: the
two are running the same blink at different offsets, which is expected and
is exactly the one cell FB2's equivalent test could never match. A cell that
blinks in one and holds in the other is a real mismatch and is reported.
"""
import argparse
import csv
import sys
from collections import Counter


def load(path):
    """-> {cell_name: [level per frame]}. Raises if the file has no rows."""
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise SystemExit(f"{path}: no frames -- capture produced nothing")
    cells = [k for k in rows[0] if k != "frame"]
    return {c: [int(r[c]) for r in rows] for c in cells}


def summarize(series):
    counts = Counter(series)
    return counts.most_common(1)[0][0], len(counts) > 1


def share(series, level):
    """Fraction of frames this cell spent at `level`."""
    return series.count(level) / len(series)


def describe(series):
    counts = Counter(series)
    if len(counts) == 1:
        return f"steady {next(iter(counts))}"
    parts = (f"{lvl}:{share(series, lvl):.0%}" for lvl in sorted(counts))
    return "blinks " + " ".join(parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mame_csv")
    ap.add_argument("ours_csv")
    ap.add_argument("--min-frames", type=int, default=60,
                    help="fail if either capture is shorter than this")
    ap.add_argument("--max-mismatch", type=int, default=0,
                    help="tolerated mismatching cells (default 0)")
    ap.add_argument("--blink-tol", type=float, default=0.15,
                    help="for cells that change in both captures, the largest "
                         "allowed difference in the fraction of frames spent "
                         "at any one level (default 0.15). Absorbs the phase "
                         "offset and the 59.98 vs 60 Hz drift between the two "
                         "runs, which no amount of alignment removes.")
    ap.add_argument("--skip-ours", type=int, default=0,
                    help="drop this many leading frames from our capture "
                         "(MAME's are already dropped by dump_display.lua)")
    args = ap.parse_args()

    mame, ours = load(args.mame_csv), load(args.ours_csv)
    if args.skip_ours:
        ours = {c: v[args.skip_ours:] for c, v in ours.items()}
        if not next(iter(ours.values())):
            raise SystemExit(f"--skip-ours {args.skip_ours} discarded every frame")

    for name, data, path in (("MAME", mame, args.mame_csv),
                             ("ours", ours, args.ours_csv)):
        n = len(next(iter(data.values())))
        if n < args.min_frames:
            raise SystemExit(f"{path}: only {n} frames, need >= {args.min_frames}")
        print(f"{name}: {len(data)} cells x {n} frames")

    # Blink detection is "did this cell ever change", so a longer capture has
    # more chances to see a change. Truncate both to the shorter length or the
    # asymmetry alone can read as a mismatch.
    n = min(len(next(iter(mame.values()))), len(next(iter(ours.values()))))
    mame = {c: v[:n] for c, v in mame.items()}
    ours = {c: v[:n] for c, v in ours.items()}
    print(f"comparing {n} frames from each")

    if set(mame) != set(ours):
        raise SystemExit("cell sets differ -- check the 9x11 matrix layout")

    mismatches = []
    for cell in sorted(mame, key=lambda c: tuple(int(p) for p in c.split("."))):
        m_lvl, m_blink = summarize(mame[cell])
        o_lvl, o_blink = summarize(ours[cell])
        if m_blink != o_blink:
            mismatches.append((cell, describe(mame[cell]), describe(ours[cell])))
        elif m_blink:
            # Both change, so the levels cannot be lined up frame for frame.
            # What IS phase-independent is which levels the cell visits and
            # how much of the time it spends at each -- a cell that MAME
            # blinks off/bright and we blink off/dim differs, and so does one
            # we hold bright three times as long. Checking only "both blink"
            # would exempt exactly the cells this test exists to check: on
            # the fwd scenario that is all ten moving dash cells.
            if set(mame[cell]) != set(ours[cell]) or \
                    max(abs(share(mame[cell], l) - share(ours[cell], l))
                        for l in set(mame[cell]) | set(ours[cell])) > args.blink_tol:
                mismatches.append((cell, describe(mame[cell]), describe(ours[cell])))
        elif m_lvl != o_lvl:
            mismatches.append((cell, describe(mame[cell]), describe(ours[cell])))

    total = len(mame)
    print(f"matched {total - len(mismatches)}/{total} cells")
    if mismatches:
        print("\ncell   MAME                      ours")
        for cell, m_desc, o_desc in mismatches:
            print(f"{cell:<6} {m_desc:<25} {o_desc}")

    if len(mismatches) > args.max_mismatch:
        print(f"\nFAIL: {len(mismatches)} mismatching cells "
              f"(tolerance {args.max_mismatch})")
        return 1
    print("\nPASS: display parity")
    return 0


if __name__ == "__main__":
    sys.exit(main())
