"""Generates res/app.ico (16/24/32/48/64/128/256 px PNG entries) without third-party modules."""
import struct
import zlib
import math
import os


def png_bytes(size, pixels):
    raw = b"".join(b"\x00" + bytes(pixels[y * size * 4:(y + 1) * size * 4]) for y in range(size))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    cx, cy = ax + t * dx, ay + t * dy
    return math.hypot(px - cx, py - cy)


def render(size):
    ss = 4
    out = []
    radius = 0.22
    for y in range(size):
        for x in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    u = (x + (sx + 0.5) / ss) / size
                    v = (y + (sy + 0.5) / ss) / size
                    # rounded square mask
                    qx = max(abs(u - 0.5) - (0.5 - radius), 0.0)
                    qy = max(abs(v - 0.5) - (0.5 - radius), 0.0)
                    if math.hypot(qx, qy) > radius:
                        continue
                    # navy -> blue vertical gradient
                    r = 0.06 + 0.10 * v
                    g = 0.09 + 0.20 * v
                    b = 0.22 + 0.55 * v
                    # white "V" stroke
                    w = 0.085
                    d = min(seg_dist(u, v, 0.26, 0.27, 0.5, 0.76), seg_dist(u, v, 0.74, 0.27, 0.5, 0.76))
                    if d < w:
                        r, g, b = 1.0, 1.0, 1.0
                    acc[0] += r
                    acc[1] += g
                    acc[2] += b
                    acc[3] += 1.0
            n = ss * ss
            if acc[3] > 0:
                out += [int(255 * acc[0] / acc[3]), int(255 * acc[1] / acc[3]), int(255 * acc[2] / acc[3]), int(255 * acc[3] / n)]
            else:
                out += [0, 0, 0, 0]
    return png_bytes(size, out)


def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [render(s) for s in sizes]
    header = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    entries = b""
    for s, img in zip(sizes, images):
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(img), offset)
        offset += len(img)
    path = os.path.join(os.path.dirname(__file__), "..", "res", "app.ico")
    with open(path, "wb") as f:
        f.write(header + entries + b"".join(images))
    print("wrote", os.path.normpath(path))


if __name__ == "__main__":
    main()
