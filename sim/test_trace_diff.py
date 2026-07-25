"""Regression fixture for tools/golden/trace_diff.py."""
import subprocess
import sys
import tempfile
from pathlib import Path

TOOL = Path(__file__).resolve().parent.parent / "tools" / "golden" / "trace_diff.py"

def run(a_lines, b_lines, extra=()):
    with tempfile.TemporaryDirectory() as d:
        a = Path(d) / "a.tr"
        b = Path(d) / "b.tr"
        a.write_text("\n".join(a_lines) + "\n")
        b.write_text("\n".join(b_lines) + "\n")
        return subprocess.run(
            [sys.executable, str(TOOL), str(a), str(b), *extra],
            capture_output=True, text=True,
        )

def main() -> int:
    # (a) identical traces -> exit 0
    assert run(["T,1,2", "T,3,4"], ["T,1,2", "T,3,4"]).returncode == 0, "identical"
    # (b) one mismatched T-line -> exit 1
    assert run(["T,1,2", "T,3,4"], ["T,1,2", "T,9,9"]).returncode == 1, "mismatch"
    # (c) clean prefix but below --min -> exit 1
    r = run(["T,1,2", "T,3,4"], ["T,1,2", "T,3,4"], extra=("--min", "10"))
    assert r.returncode == 1, "below --min"
    # clean prefix meeting --min -> exit 0
    r = run(["T,1,2", "T,3,4"], ["T,1,2", "T,3,4"], extra=("--min", "2"))
    assert r.returncode == 0, "meets --min"
    # (d) non-T lines ignored
    assert run(["disasm noise", "T,1,2"], ["other noise", "T,1,2"]).returncode == 0, \
        "non-T lines ignored"
    print("PASS: test_trace_diff")
    return 0

if __name__ == "__main__":
    sys.exit(main())
