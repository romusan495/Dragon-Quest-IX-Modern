#!/usr/bin/env python3
"""Modern job icons: the game's 13 job badges (the grey 12x12 pictures next to
each party member in battles) redrawn at 8x, smoothed and in colour, one
colour per job. They are written as an HD sprite pack (textures/<name>.png,
see runtime/hdsprite.h): the game shows them instead of the originals when
"HD textures" is on.

    job_icons.py <rom.nds> <out dir> [style]

style: "plate" (default: each job's symbol in polished metal on a dark
rounded plate, drawn for this project after the game's badges:
tools/job_icons_plate/<job>.png, 96x96, the badge's 12x12 at 8x), "white"
(a white symbol on a badge in the job's colour) or "dark" (the symbol dark,
the badge in a light shade of the job's colour, closer to the original).
The same sprites are the job's sign in the menus too (a party member's bar in
Equip, Attributes...): one pack for both.

Nothing from the game is kept in this project: the icons are made from the
player's ROM when the game is built. Only the standard library is used (the
build's Python has no extra packages)."""
import os
import struct
import sys
import zlib

PAC = "data/ani/obj_gl.pac"     # the battle screen's sprites
FIRST = 0x7D0                   # the job badges: 13 sprites of 16x16, 4 bits a pixel
COUNT = 13
# the colours the battle screen gives them (sprite palette 0), 15-bit
PALETTE = {1: 0x18C6, 9: 0x6318, 11: 0x0000, 12: 0x4631, 14: 0x739C}
BADGE, DARK, MID, LIGHT = 14, (1, 11), 12, 9

# each badge in the order of the file: its job and colour (r, g, b)
JOBS = [
    ("none", (150, 156, 170)),
    ("warrior", (214, 64, 56)),
    ("priest", (64, 168, 220)),
    ("mage", (150, 84, 214)),
    ("martial artist", (232, 128, 40)),
    ("thief", (64, 168, 92)),
    ("minstrel", (226, 86, 150)),
    ("gladiator", (168, 92, 52)),
    ("armamentalist", (40, 178, 186)),
    ("paladin", (70, 110, 200)),
    ("sage", (98, 70, 170)),
    ("luminary", (228, 176, 40)),
    ("ranger", (104, 140, 48)),
]

SCALE = 8                       # HD pixels per DS pixel
SS = 2                          # drawn at twice that, then averaged (smooth edges)


def rom_file(rom, path):
    """a file of the ROM's file system, by its path"""
    fnt, _, fat, _ = struct.unpack_from("<IIII", rom, 0x40)
    parts = path.split("/")

    def find(dir_id, depth):
        off, first, _ = struct.unpack_from("<IHH", rom, fnt + (dir_id & 0xFFF) * 8)
        p, f = fnt + off, first
        while True:
            n = rom[p]; p += 1
            if n == 0:
                return None
            name = rom[p:p + (n & 0x7F)].decode("latin-1"); p += n & 0x7F
            if n & 0x80:
                sub = struct.unpack_from("<H", rom, p)[0]; p += 2
                if name == parts[depth] and depth + 1 < len(parts):
                    return find(sub, depth + 1)
            else:
                if name == parts[depth] and depth + 1 == len(parts):
                    return f
                f += 1
    fid = find(0xF000, 0)
    if fid is None:
        return None
    start, end = struct.unpack_from("<II", rom, fat + fid * 8)
    return rom[start:end]


def badge(data, k):
    """sprite k's colour indices, 16 rows of 16"""
    b = data[FIRST + k * 128:FIRST + (k + 1) * 128]
    g = [[0] * 16 for _ in range(16)]
    for t in range(4):
        for y in range(8):
            for x in range(8):
                v = b[t * 32 + y * 4 + x // 2]
                g[(t // 2) * 8 + y][(t % 2) * 8 + x] = v >> 4 if x & 1 else v & 15
    return g


def is_badge(g):
    return g[0][0] == 0 and g[0][1] == BADGE and g[12][0] == 0 and all(v in PALETTE or v == 0 for row in g for v in row)


def rgb15(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)


def key(g):
    """the name the game gives this picture (gl_render.c tex_key: FNV-1a 64
    of the size and the RGBA pixels, see-through ones as 0)"""
    k = 0xCBF29CE484222325
    for byte in struct.pack("<II", 16, 16):
        k = ((k ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    for row in g:
        for v in row:
            px = rgb15(PALETTE[v]) + (255,) if v else (0, 0, 0, 0)
            for byte in px:
                k = ((k ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return k


def scale2x(g):
    """EPX: twice the size, staircases become slopes, thin lines stay"""
    h, w = len(g), len(g[0])
    out = [[0] * (w * 2) for _ in range(h * 2)]
    for y in range(h):
        for x in range(w):
            p = g[y][x]
            a = g[y - 1][x] if y > 0 else p
            b = g[y][x + 1] if x < w - 1 else p
            c = g[y][x - 1] if x > 0 else p
            d = g[y + 1][x] if y < h - 1 else p
            e0 = e1 = e2 = e3 = p
            if c == a and c != d and a != b: e0 = a
            if a == b and a != c and b != d: e1 = b
            if d == c and d != b and c != a: e2 = c
            if b == d and b != a and d != c: e3 = d
            out[y * 2][x * 2], out[y * 2][x * 2 + 1] = e0, e1
            out[y * 2 + 1][x * 2], out[y * 2 + 1][x * 2 + 1] = e2, e3
    return out


def inside(x, y):
    """how far (DS pixels) the point is inside the badge: a rounded square
    where the original's 12 x 12 badge is (negative: outside)"""
    r = 1.6
    qx, qy = abs(x - 6.0) - (6.0 - r), abs(y - 6.0) - (6.0 - r)
    out = (max(qx, 0.0) ** 2 + max(qy, 0.0) ** 2) ** 0.5 + min(max(qx, qy), 0.0)
    return r - out


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def draw(g, colour, style):
    """the badge at SCALE x SS, RGBA rows (premultiplied floats)"""
    big = g
    n = 1
    while n < SCALE * SS:
        big = scale2x(big); n *= 2
    size = 16 * n
    rim = 0.75                                        # the badge's darker edge (DS pixels)
    top, bottom = mix(colour, (255, 255, 255), 0.25), mix(colour, (0, 0, 0), 0.2)
    shadow = int(0.5 * n)                             # the symbol's shadow, down right
    out = []
    for y in range(size):
        row = []
        t = y / (12 * n)
        base = mix(top, bottom, min(t, 1.0))
        for x in range(size):
            v = big[y][x]
            d = inside((x + 0.5) / n, (y + 0.5) / n)
            if d < 0:
                row.append((0.0, 0.0, 0.0, 0.0)); continue
            if v == 0:
                v = BADGE
            if style == "dark":
                bg = mix(mix(colour, (255, 255, 255), 0.55), mix(colour, (255, 255, 255), 0.3), min(t, 1.0))
            else:
                bg = base
            if d < rim:
                bg = mix(bg, (0, 0, 0), 0.35 if style != "dark" else 0.25)
            if v == BADGE:
                sv = big[y - shadow][x - shadow] if y >= shadow and x >= shadow else BADGE
                if style != "dark" and sv in DARK + (MID,):
                    bg = mix(bg, (0, 0, 0), 0.3)
                c = bg
            elif style == "dark":
                c = mix(colour, (0, 0, 0), 0.75) if v in DARK else mix(colour, (0, 0, 0), 0.45) if v == MID else mix(colour, (255, 255, 255), 0.15)
            else:
                c = (255, 255, 255) if v in DARK else mix(bg, (255, 255, 255), 0.7) if v == MID else mix(bg, (255, 255, 255), 0.4)
            row.append((c[0], c[1], c[2], 1.0))
        out.append(row)
    return out


def downsample(px, f):
    n = len(px) // f
    res = []
    for y in range(n):
        row = []
        for x in range(n):
            r = g = b = a = 0.0
            for j in range(f):
                for i in range(f):
                    p = px[y * f + j][x * f + i]
                    r += p[0] * p[3]; g += p[1] * p[3]; b += p[2] * p[3]; a += p[3]
            if a:
                row.append((round(r / a), round(g / a), round(b / a), round(255 * a / (f * f))))
            else:
                row.append((0, 0, 0, 0))
        res.append(row)
    return res


def png(px):
    h, w = len(px), len(px[0])
    raw = b"".join(b"\0" + bytes(v for p in row for v in p) for row in px)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def png_read(path):
    """an 8-bit RGBA PNG (not interlaced): (width, height, rows of RGBA bytes)"""
    with open(path, "rb") as f:
        b = f.read()
    p, w, h, idat = 8, 0, 0, b""
    while p < len(b):
        n, t = struct.unpack_from(">I4s", b, p)
        d = b[p + 8:p + 8 + n]
        if t == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", d)
            if depth != 8 or ctype != 6 or inter:
                raise ValueError("%s: not an 8-bit RGBA PNG" % path)
        elif t == b"IDAT":
            idat += d
        p += 12 + n
    raw, rows, prev, s = zlib.decompress(idat), [], bytearray(w * 4), w * 4 + 1
    for y in range(h):
        f, line = raw[y * s], bytearray(raw[y * s + 1:(y + 1) * s])
        for i in range(len(line)):
            a = line[i - 4] if i >= 4 else 0
            up = prev[i]
            c = prev[i - 4] if i >= 4 else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + up) & 255
            elif f == 3: line[i] = (line[i] + (a + up) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(up - c), abs(a - c), abs(a + up - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else up if pb <= pc else c)) & 255
        rows.append(line); prev = line
    return w, h, rows


def plate(name):
    """the job's plate icon in a 16x16 sprite at 8x: the badge's 12x12 at the top left"""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "job_icons_plate", name.replace(" ", "_") + ".png")
    if not os.path.exists(path):
        return None
    w, h, rows = png_read(path)
    n = 16 * SCALE
    px = [[(0, 0, 0, 0)] * n for _ in range(n)]
    for y in range(min(h, n)):
        for x in range(min(w, n)):
            px[y][x] = tuple(rows[y][x * 4:x * 4 + 4])
    return px


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    style = sys.argv[3] if len(sys.argv) > 3 else "plate"
    with open(sys.argv[1], "rb") as f:
        rom = f.read()
    data = rom_file(rom, PAC)
    if not data or len(data) < FIRST + COUNT * 128:
        sys.exit("job_icons: %s not found in the ROM" % PAC)
    os.makedirs(sys.argv[2], exist_ok=True)
    made = 0
    for k, (name, colour) in enumerate(JOBS):
        g = badge(data, k)
        if not is_badge(g):
            print("job_icons: sprite %d (%s) is not a job badge, left out" % (k, name))
            continue
        px = plate(name) if style == "plate" else None
        if px is None:                                            # (no plate for it, the guardian: drawn as before)
            px = downsample(draw(g, colour, "white" if style == "plate" else style), SS)
        with open(os.path.join(sys.argv[2], "%016x.png" % key(g)), "wb") as f:
            f.write(png(px))
        made += 1
    print("job_icons: %d icons in %s" % (made, sys.argv[2]))


if __name__ == "__main__":
    main()
