"""Pictures of .npc3 models (checking): front and back, first frame."""
import struct, math, sys
import numpy as np
from PIL import Image

def load(path):
    d = open(path, 'rb').read(); p = 8
    na = struct.unpack_from('<I', d, p)[0]; p += 4 + na * 24
    nf, nv = struct.unpack_from('<II', d, p); p += 8
    nt = struct.unpack_from('<I', d, p)[0]; p += 4
    tex = []
    for _ in range(nt):
        w, h = struct.unpack_from('<II', d, p); p += 8
        tex.append(np.frombuffer(d, np.uint8, w * h * 4, p).reshape(h, w, 4)); p += w * h * 4
    npart = struct.unpack_from('<I', d, p)[0]; p += 4
    parts = [struct.unpack_from('<IIII', d, p + 16 * i) for i in range(npart)]; p += 16 * npart
    nu, scale = struct.unpack_from('<If', d, p); p += 8
    idx = np.frombuffer(d, '<u2', nv, p).astype(int); p += nv * 2
    uv = np.frombuffer(d, np.float32, nu * 2, p).reshape(nu, 2)[idx]; p += nu * 8
    pos = np.frombuffer(d, '<i2', nu * 3, p).reshape(nu, 3)[idx] * scale
    return tex, parts, uv, pos

def render(path, size=128, yaw=0.0, pitch=-0.3):
    tex, parts, uv, pos = load(path)
    cy, sy_ = math.cos(yaw), math.sin(yaw); c2, s2 = math.cos(pitch), math.sin(pitch)
    R = np.array([[1, 0, 0], [0, c2, -s2], [0, s2, c2]]) @ np.array([[cy, 0, sy_], [0, 1, 0], [-sy_, 0, cy]])
    q = pos @ R.T
    lo, hi = q.min(0), q.max(0); sc = (size - 8) / max(hi[0] - lo[0], hi[1] - lo[1])
    cx, cyy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
    img = np.zeros((size, size, 4), np.uint8); img[:] = (40, 44, 52, 255); zb = np.full((size, size), -1e9)
    for ti, layer, first, cnt in sorted(parts, key=lambda x: x[1]):
        T = tex[ti]; th, tw = T.shape[:2]
        for k in range(first, first + cnt, 3):
            Q = q[k:k + 3]; U = uv[k:k + 3]
            sx = (Q[:, 0] - cx) * sc + size / 2; sy = size / 2 - (Q[:, 1] - cyy) * sc; z = Q[:, 2] + 0.002 * layer
            x0, x1 = int(max(0, math.floor(sx.min()))), int(min(size - 1, math.ceil(sx.max())))
            y0, y1 = int(max(0, math.floor(sy.min()))), int(min(size - 1, math.ceil(sy.max())))
            den = (sy[1] - sy[2]) * (sx[0] - sx[2]) + (sx[2] - sx[1]) * (sy[0] - sy[2])
            if abs(den) < 1e-9 or x1 < x0 or y1 < y0: continue
            X, Y = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            a = ((sy[1] - sy[2]) * (X - sx[2]) + (sx[2] - sx[1]) * (Y - sy[2])) / den
            b = ((sy[2] - sy[0]) * (X - sx[2]) + (sx[0] - sx[2]) * (Y - sy[2])) / den
            c = 1 - a - b
            ins = (a >= 0) & (b >= 0) & (c >= 0)
            if not ins.any(): continue
            zz = a * z[0] + b * z[1] + c * z[2]
            u = a * U[0, 0] + b * U[1, 0] + c * U[2, 0]; v = a * U[0, 1] + b * U[1, 1] + c * U[2, 1]
            col = T[(np.floor(v * th).astype(int)) % th, (np.floor(u * tw).astype(int)) % tw]
            sub = zb[y0:y1 + 1, x0:x1 + 1]
            ok = ins & (zz > sub - (1e-3 if layer else 0)) & (col[..., 3] > 100)
            if layer: ok &= (u >= 0) & (u <= 1) & (v >= 0) & (v <= 1)      # (layers: clamped, not repeated)
            if not layer: ok &= zz > sub
            sub[ok] = zz[ok]; img[y0:y1 + 1, x0:x1 + 1][ok] = col[ok]
    return Image.fromarray(img)
