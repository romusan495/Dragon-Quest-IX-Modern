#!/usr/bin/env python3
"""Redraws a job's plate (tools/job_icons_plate/<job>.png, 96x96) and coat of
arms (tools/job_crests/<job>.png, 400x500) from its symbol here (<job>.svg):
the symbol in polished silver on the dark plate, in gold on the shield. The
old symbol is taken off first (the plate's rows are one colour each; the
shield's field is filled again from its sides). A tool for this project's
artists only (it needs cairosvg, numpy and Pillow); the PNGs are committed.

    render.py <job>"""
import io, os, sys
import cairosvg, numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
SILVER = dict(C0="#fbfbfd", C1="#c9ccd2", C2="#8d9098", C3="#e4e6ea", OUTLINE="#2c2d31", LINE="#6d7077", RAY="#b9bcc3", NAIL="#f4f5f7")
GOLD = dict(C0="#fff2b4", C1="#e8bc48", C2="#b07a1c", C3="#f3d27a", OUTLINE="#5e3c0c", LINE="#8a5c14", RAY="#f0cf72", NAIL="#fff4c8")


def symbol(job, colours, size):
    svg = open(os.path.join(HERE, job + ".svg")).read()
    for k, v in colours.items():
        svg = svg.replace(k, v)
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=size * 4, output_height=size * 4)
    return Image.open(io.BytesIO(png)).convert("RGBA").resize((size, size), Image.LANCZOS)


def with_shadow(sym, off, blur, alpha):
    a = sym.split()[3].filter(ImageFilter.GaussianBlur(blur)).point(lambda v: int(v * alpha))
    sh = Image.new("RGBA", sym.size, (0, 0, 0, 0)); sh.putalpha(a)
    out = Image.new("RGBA", sym.size, (0, 0, 0, 0)); out.alpha_composite(sh, (off, off)); out.alpha_composite(sym)
    return out


def plate(job):
    path = os.path.join(TOOLS, "job_icons_plate", job + ".png")
    p = np.array(Image.open(path).convert("RGBA"))
    for y in range(8, 89):                                    # the old symbol off: each row its colour at 21 (not the corners' marks)
        if 18 <= y <= 78:                                     # (from 10: a wide old symbol can cross 21)
            p[y, 12:85] = p[y, 10]
        else:
            p[y, 19:78] = p[y, 21]
    out = Image.fromarray(p)
    out.alpha_composite(with_shadow(symbol(job, SILVER, 96), 1, 1.2, 0.8))
    out.save(path)


def crest(job):
    path = os.path.join(TOOLS, "job_crests", job + ".png")
    c = np.array(Image.open(path).convert("RGBA")).astype(float)
    r, g, b = c[..., 0], c[..., 1], c[..., 2]
    gold = (r > 200) & (g > 150) & (b < 150)                  # the old symbol's bright gold (not an orange field)
    mask = np.zeros(gold.shape, bool); mask[110:360, 120:280] = gold[110:360, 120:280]   # (the symbol: the middle of the field, not its gold lines)
    ys, xs = np.where(mask)
    m = np.zeros(gold.shape, bool)                            # (its box, its shadow under and right of it too)
    if len(ys):
        m[ys.min() - 6:ys.max() + 16, xs.min() - 8:xs.max() + 16] = True
    h = c.copy(); v = c.copy()                                # the field again: from its sides and from above and below,
    for y in range(c.shape[0]):                               # averaged (the field is lighter in its middle)
        xs = np.where(m[y])[0]
        if len(xs):
            x0, x1 = xs.min() - 1, xs.max() + 1
            t = ((np.arange(x0 + 1, x1) - x0) / (x1 - x0))[:, None]
            h[y, x0 + 1:x1] = c[y, x0] * (1 - t) + c[y, x1] * t
    for x in range(c.shape[1]):
        ys = np.where(m[:, x])[0]
        if len(ys):
            y0, y1 = ys.min() - 1, ys.max() + 1
            t = ((np.arange(y0 + 1, y1) - y0) / (y1 - y0))[:, None]
            v[y0 + 1:y1, x] = c[y0, x] * (1 - t) + c[y1, x] * t
    c[m] = (h[m] + v[m]) / 2
    out = Image.fromarray(c.astype(np.uint8))
    s = symbol(job, GOLD, 270)
    out.alpha_composite(with_shadow(s, 3, 3, 0.7), (65, 90))
    out.save(path)


if __name__ == "__main__":
    plate(sys.argv[1]); crest(sys.argv[1])
