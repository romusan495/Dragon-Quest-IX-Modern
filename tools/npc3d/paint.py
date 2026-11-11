"""Repaint a DQVII model's surface texture with a DQIX townsperson's colours,
taken from its front picture (RGBA, transparent around it): each texel takes
the colour of the picture where that bit of the model is seen from the front,
keeping a little of its own shading (its brightness against the average)."""
import numpy as np

def fill_out(img, mask, steps=64):
    """colour of the nearest masked pixel for every pixel (iterative dilation)"""
    img = img.copy(); m = mask.copy()
    for _ in range(steps):
        if m.all(): break
        acc = np.zeros_like(img); n = np.zeros(m.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sm = np.roll(m, (dy, dx), (0, 1)); si = np.roll(img, (dy, dx), (0, 1))
            acc += si * sm[..., None]; n += sm
        new = ~m & (n > 0)
        img[new] = acc[new] / n[new, None]; m = m | new
    return img

def paint(model, tex, sprite, texname, detail=0.45, neck=None, sprite_neck=0.36):
    """neck: the model's neck height (its head keeps its own look); sprite_neck: the picture's (0 top, 1 feet)"""
    t = tex[texname].astype(np.float32)
    H, W = t.shape[:2]
    sp = sprite.astype(np.float32)
    alpha = sp[..., 3] > 128
    ys, xs = np.nonzero(alpha)
    sx0, sx1, sy0, sy1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    col = fill_out(sp[..., :3], alpha)
    allp = np.concatenate([p['pos'] for p in model['parts']])
    mx0, mx1 = allp[:, 0].min(), allp[:, 0].max(); my0, my1 = allp[:, 1].min(), allp[:, 1].max()
    out = np.zeros((H, W, 3), np.float32); cnt = np.zeros((H, W), np.float32)
    for p in model['parts']:
        if not p.get('texs') or p['texs'][0] != texname: continue
        P = p['pos']; uv = p['uvs'][0] if p['uvs'][0] is not None else p['uv']
        for tri in p['tris']:
            U = uv[tri]; Q = P[tri]
            px = (U[:, 0] % 1.0) * W if U[:, 0].max() - U[:, 0].min() < 1 else U[:, 0] * W
            py = (1 - U[:, 1]) * H
            px = U[:, 0] * W; py = (1 - U[:, 1]) * H
            x0, x1 = int(np.floor(px.min())), int(np.ceil(px.max())); y0, y1 = int(np.floor(py.min())), int(np.ceil(py.max()))
            den = (py[1] - py[2]) * (px[0] - px[2]) + (px[2] - px[1]) * (py[0] - py[2])
            if abs(den) < 1e-9: continue
            X, Y = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            a = ((py[1] - py[2]) * (X - px[2]) + (px[2] - px[1]) * (Y - py[2])) / den
            b = ((py[2] - py[0]) * (X - px[2]) + (px[0] - px[2]) * (Y - py[2])) / den
            c = 1 - a - b
            ins = (a >= -0.05) & (b >= -0.05) & (c >= -0.05)
            if not ins.any(): continue
            mxp = a * Q[0, 0] + b * Q[1, 0] + c * Q[2, 0]; myp = a * Q[0, 1] + b * Q[1, 1] + c * Q[2, 1]
            fx = (mxp - mx0) / (mx1 - mx0)
            if neck is not None:
                ins &= myp < neck
                fy = sprite_neck + (1 - sprite_neck) * (neck - myp) / (neck - my0)
            else: fy = (my1 - myp) / (my1 - my0)
            sxp = np.clip((sx0 + fx * (sx1 - sx0)).astype(int), 0, sp.shape[1] - 1)
            syp = np.clip((sy0 + fy * (sy1 - sy0)).astype(int), 0, sp.shape[0] - 1)
            tx = (X.astype(int)) % W; ty = (Y.astype(int)) % H
            cc = col[syp, sxp]
            np.add.at(out, (ty[ins], tx[ins]), cc[ins]); np.add.at(cnt, (ty[ins], tx[ins]), 1)
    hit = cnt > 0
    newc = out[hit] / cnt[hit, None]
    lum = t[..., :3].mean(-1)
    ref = np.median(lum[hit]) if hit.any() else 128
    k = np.clip(1 + detail * (lum[hit] - ref) / max(ref, 1), 0.4, 1.5)
    res = t.copy()
    res[..., :3][hit] = np.clip(newc * k[:, None], 0, 255)
    return res.astype(np.uint8)
