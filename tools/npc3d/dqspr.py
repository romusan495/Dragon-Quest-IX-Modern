"""DQIX townsperson sprite sheets (data/ani/nNNNx.spr):
u16 frames, 3; per frame u16 width, height, cells, 0, then per cell u16 x, y,
width and height (8 << n) and its pixels (4 bits each, by rows); then u32
colours and the palette (RGB555, colour 0 see-through)."""
import struct
import numpy as np

def read(d):
    nfr = struct.unpack_from('<H', d, 0)[0]
    p = 4; frames = []
    for f in range(nfr):
        W, H, cells, _ = struct.unpack_from('<4H', d, p); p += 8
        idx = np.zeros((H, W), np.uint8)
        for c in range(cells):
            cx, cy, wc, hc = struct.unpack_from('<4H', d, p); p += 8
            cw, ch = 8 << (wc & 3), 8 << (hc & 3)
            px = np.frombuffer(d, np.uint8, cw * ch // 2, p); p += cw * ch // 2
            v = np.stack([px & 15, px >> 4], -1).reshape(ch, cw)
            h, w = max(0, min(ch, H - cy)), max(0, min(cw, W - cx))
            idx[cy:cy + h, cx:cx + w] = v[:h, :w]
        frames.append(idx)
    ncol = struct.unpack_from('<I', d, p)[0]; p += 4
    pal = np.frombuffer(d, '<u2', min(ncol, 256), p).astype(int)
    rgb = np.stack([(pal & 31) << 3, ((pal >> 5) & 31) << 3, ((pal >> 10) & 31) << 3], -1)
    rgb = rgb | (rgb >> 5)
    out = []
    for idx in frames:
        im = np.zeros(idx.shape + (4,), np.uint8)
        im[..., :3] = rgb[np.minimum(idx, len(rgb) - 1)]
        im[..., 3] = np.where(idx > 0, 255, 0)
        out.append(im)
    return out
