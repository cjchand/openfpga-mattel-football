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
