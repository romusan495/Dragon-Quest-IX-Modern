"""Repaint a DQVII model's surface texture with a DQIX townsperson's colours,
taken from its pictures (RGBA, transparent around them) seen from the front
and from the back: each texel takes the colour of the picture on the side its
bit of the model faces (the model faces +z), where that bit is seen, snapped
to the picture's own few colours (clean, flat areas like the DS art), keeping
a little of the model's own shading."""
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

def palette(pictures, k=14, iters=20):
    """the pictures' main colours (k-means)"""
    px = np.concatenate([p[..., :3][p[..., 3] > 128].astype(np.float32) for p in pictures])
    rng = np.random.default_rng(0)
    c = px[rng.choice(len(px), k, replace=False)]
    for _ in range(iters):
        lab = ((px[:, None] - c[None]) ** 2).sum(-1).argmin(1)
        for j in range(k):
            if (lab == j).any(): c[j] = px[lab == j].mean(0)
    return c

class View:
    def __init__(self, pic):
        sp = pic.astype(np.float32)
        a = sp[..., 3] > 128
        ys, xs = np.nonzero(a)
        self.x0, self.x1, self.y0, self.y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        self.col = fill_out(sp[..., :3], a)
    def sample(self, fx, fy):
        """bilinear, fx fy in 0..1 of the figure's box"""
        H, W = self.col.shape[:2]
        x = np.clip(self.x0 + fx * (self.x1 - self.x0) - 0.5, 0, W - 1.001)
        y = np.clip(self.y0 + fy * (self.y1 - self.y0) - 0.5, 0, H - 1.001)
        xi, yi = x.astype(int), y.astype(int); ax, ay = (x - xi)[..., None], (y - yi)[..., None]
        c = self.col
        return (c[yi, xi] * (1 - ax) + c[yi, xi + 1] * ax) * (1 - ay) + (c[yi + 1, xi] * (1 - ax) + c[yi + 1, xi + 1] * ax) * ay

def arm_profile(pic, pic_neck):
    """the front picture's arms (they hang at its sides): rows from the
    shoulder to the end of the hand, and the column down each arm's middle"""
    a = pic[..., 3] > 128
    rows = [y for y in range(a.shape[0]) if a[y].any()]
    l = {y: np.nonzero(a[y])[0].min() for y in rows}; r = {y: np.nonzero(a[y])[0].max() for y in rows}
    y0 = rows[0]; h = rows[-1] - y0 + 1
    yn = y0 + int(pic_neck * h)
    top = next((y for y in rows if y > yn and r[y] - l[y] > r[yn] - l[yn] + 3), yn)
    rmax = max(r[y] for y in rows if y >= top)
    end = max(y for y in rows if y >= top and r[y] >= rmax - 2)
    while top < end - 4 and pic[top, r[top] - 2, :3].astype(int).sum() < 300: top += 1   # its outline at the shoulder
    return top, end, l, r

def arm_t(q, pts):
    """how far along the arm (0 shoulder, 1 end of the hand) the point q is"""
    seg = [(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]
    lens = [np.linalg.norm(b - a) for a, b in seg]; tot = sum(lens)
    best = (1e9, 0)
    acc = 0
    for (a, b), L in zip(seg, lens):
        d = b - a
        u = np.clip(((q - a) @ d) / (d @ d), 0, 1)
        dist = np.linalg.norm(q - (a + u[..., None] * d), axis=-1)
        t = (acc + u * L) / tot
        best = (np.minimum(best[0], dist), np.where(dist < best[0], t, best[1]))
        acc += L
    return best[1]

def paint(model, tex, front, back, texname, neck, pic_neck=0.34, detail=0.25, arms=None):
    """neck: the model's neck height; pic_neck: the pictures' (0 top, 1 feet).
    The model's body (below its neck) takes the pictures' body, its head their head.
    arms: {+1 / -1 (side of x): [shoulder, elbow, wrist, end of the hand]} and each
    part's 'arm' (per vertex: +1, -1 or 0): an arm takes the picture's arm down
    its length (bands, gloves at the right place)."""
    t = tex[texname].astype(np.float32)
    H, W = t.shape[:2]
    views = (View(front), View(back))
    pal = palette((front, back))
    allp = np.concatenate([p['pos'] for p in model['parts']])
    mx0, mx1 = allp[:, 0].min(), allp[:, 0].max(); my0, my1 = allp[:, 1].min(), allp[:, 1].max()
    out = np.zeros((H, W, 3), np.float32); cnt = np.zeros((H, W), np.float32)
    if arms: atop, aend, al, ar = arm_profile(front, pic_neck)
    for p in model['parts']:
        if not p.get('texs') or p['texs'][0] != texname: continue
        P = p['pos']; uv = p['uvs'][0] if p['uvs'][0] is not None else p['uv']
        for tri in p['tris']:
            U = uv[tri]; Q = P[tri]
            side = 0
            if arms and p.get('arm') is not None:
                sv = p['arm'][tri]
                if (sv != 0).sum() >= 2: side = int(np.sign(sv.sum()))
            nz = np.cross(Q[1] - Q[0], Q[2] - Q[0])[2]
            view, flip = (views[0], False) if nz >= 0 else (views[1], True)
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
            if flip: fx = 1 - fx                       # seen from behind
            fy = np.where(myp < neck, pic_neck + (1 - pic_neck) * (neck - myp) / (neck - my0),
                          pic_neck * (my1 - myp) / (my1 - neck))
            if side:                                   # down the picture's arm (seen from the front: +x on its right)
                q = np.stack([mxp, myp, a * Q[0, 2] + b * Q[1, 2] + c * Q[2, 2]], -1)
                yy = np.clip((atop + arm_t(q, arms[side]) * (aend - atop)).astype(int), atop, aend)
                xx = np.array([[ar[v] - 2 if side > 0 else al[v] + 2 for v in row] for row in yy])
                cc = front[yy, xx, :3].astype(np.float32)
            else:
                cc = view.sample(fx, fy)
            tx = X.astype(int) % W; ty = Y.astype(int) % H
            np.add.at(out, (ty[ins], tx[ins]), cc[ins]); np.add.at(cnt, (ty[ins], tx[ins]), 1)
    hit = cnt > 0
    newc = out[hit] / cnt[hit, None]
    newc = pal[((newc[:, None] - pal[None]) ** 2).sum(-1).argmin(1)]      # the pictures' own colours
    lum = t[..., :3].mean(-1)
    ref = np.median(lum[hit]) if hit.any() else 128
    k = np.clip(1 + detail * (lum[hit] - ref) / max(ref, 1), 0.75, 1.2)
    res = t.copy()
    res[..., :3][hit] = np.clip(newc * k[:, None], 0, 255)
    res = res.astype(np.uint8)
    # texels no triangle covers: the nearest painted colour (no old colours bleeding in at seams)
    rgb = fill_out(res[..., :3].astype(np.float32), hit, steps=8)
    res[..., :3] = rgb.astype(np.uint8)
    return res
