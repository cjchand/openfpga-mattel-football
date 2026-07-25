#!/usr/bin/env python3
"""Diff a MAME golden trace against our Verilator trace.

Both inputs may contain non-T lines (MAME interleaves disassembly); only
lines starting with "T," are compared. Exits 0 if the shorter file is a
prefix of the longer, nonzero with context on first divergence.

Optional third argument --min N: after a clean prefix match, fail (exit 1)
if the matched instruction count is below N. Guards against a trivially
short match (e.g. both traces empty after truncation) reporting PASS.
"""
import sys

def t_lines(path):
    with open(path, errors="replace") as f:
        return [ln.rstrip("\n") for ln in f if ln.startswith("T,")]

def main() -> int:
    if len(sys.argv) not in (3, 5):
        print(__doc__, file=sys.stderr)
        return 2
    min_n = None
    if len(sys.argv) == 5:
        if sys.argv[3] != "--min":
            print(__doc__, file=sys.stderr)
            return 2
        min_n = int(sys.argv[4])
    a, b = t_lines(sys.argv[1]), t_lines(sys.argv[2])
    n = min(len(a), len(b))
    if n == 0:
        print("FAIL: no T-lines found", file=sys.stderr)
        return 1
    for i in range(n):
        if a[i] != b[i]:
            lo = max(0, i - 5)
            print(f"DIVERGENCE at executed-instruction #{i}:")
            for j in range(lo, i + 1):
                mark = ">>" if j == i else "  "
                print(f"{mark} mame:{a[j]}   ours:{b[j]}")
            return 1
    if min_n is not None and n < min_n:
        print(f"FAIL: matched only {n} instructions, below --min {min_n}",
              file=sys.stderr)
        return 1
    print(f"PASS: {n} instructions identical "
          f"(mame={len(a)} ours={len(b)} lines)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
