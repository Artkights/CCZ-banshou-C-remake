"""Convert a PNG into a 24bpp BMP for use as a Win32 BITMAP resource.

Only the subset needed by resources/target.png is supported: 8-bit RGB or RGBA,
no interlacing.  Uses only the standard library so the build stays dependency
free (the runtime cannot decode PNG without an extra library).
"""
from __future__ import annotations

import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_png(path: Path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("not a PNG file")
    offset = 8
    width = height = depth = colour = interlace = 0
    idat = bytearray()
    while offset < len(data):
        length = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(
                ">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if depth != 8 or colour not in (2, 6) or interlace != 0:
        raise SystemExit(f"unsupported PNG: depth={depth} colour={colour}")
    channels = 3 if colour == 2 else 4
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(height * stride)
    previous = bytearray(stride)
    position = 0
    for row in range(height):
        filter_type = raw[position]
        position += 1
        line = bytearray(raw[position:position + stride])
        position += stride
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            up_left = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                line[i] = (line[i] + left) & 0xFF
            elif filter_type == 2:
                line[i] = (line[i] + up) & 0xFF
            elif filter_type == 3:
                line[i] = (line[i] + ((left + up) >> 1)) & 0xFF
            elif filter_type == 4:
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                if pa <= pb and pa <= pc:
                    predictor = left
                elif pb <= pc:
                    predictor = up
                else:
                    predictor = up_left
                line[i] = (line[i] + predictor) & 0xFF
        out[row * stride:(row + 1) * stride] = line
        previous = line
    return width, height, channels, out


def write_bmp(path: Path, width: int, height: int, channels: int,
              pixels: bytearray):
    row_bytes = ((width * 3 + 3) // 4) * 4
    image_size = row_bytes * height
    header = struct.pack("<2sIHHI", b"BM", 14 + 40 + image_size, 0, 0, 14 + 40)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, image_size,
                       2835, 2835, 0, 0)
    body = bytearray()
    for row in range(height - 1, -1, -1):
        start = row * width * channels
        line = bytearray()
        for column in range(width):
            base = start + column * channels
            red, green, blue = pixels[base], pixels[base + 1], pixels[base + 2]
            alpha = pixels[base + 3] if channels == 4 else 255
            if alpha < 128:  # flatten transparency onto white
                red = green = blue = 255
            line += bytes((blue, green, red))
        line += b"\x00" * (row_bytes - len(line))
        body += line
    path.write_bytes(header + info + bytes(body))


def main() -> int:
    source = ROOT / "resources" / "target.png"
    target = ROOT / "resources" / "target.bmp"
    if not source.exists():
        print("target.png missing, skipped")
        return 0
    width, height, channels, pixels = read_png(source)
    write_bmp(target, width, height, channels, pixels)
    print(f"wrote {target.relative_to(ROOT)}: {width}x{height}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
