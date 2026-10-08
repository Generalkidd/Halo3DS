"""Turn a raw 3DS front-buffer capture into a correctly oriented PNG.

The historical .rgb filename describes three bytes per pixel, but libctru's
GSP_BGR8_OES front buffer stores those bytes in blue, green, red order.
"""

import argparse
from pathlib import Path

from PIL import Image


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--bottom", action="store_true", help="decode the 320 pixel bottom screen")
    args = parser.parse_args()
    height = 320 if args.bottom else 400
    pixels = args.capture.read_bytes()
    expected = 240 * height * 3
    if len(pixels) != expected:
        parser.error(f"expected {expected} bytes, got {len(pixels)}")
    image = Image.frombytes("RGB", (240, height), pixels, "raw", "BGR")
    image.rotate(90, expand=True).save(args.output)


if __name__ == "__main__":
    main()
