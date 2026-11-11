"""Nitro (Nintendo DS) NSBMD model and NSBCA skeletal animation reader, for the DQIX characters (/data/chara_sub/*.chr)."""
import struct, numpy as np
u8 = lambda d, o: d[o]
u16 = lambda d, o: struct.unpack_from('<H', d, o)[0]
s16 = lambda d, o: struct.unpack_from('<h', d, o)[0]
u32 = lambda d, o: struct.unpack_from('<I', d, o)[0]
s32 = lambda d, o: struct.unpack_from('<i', d, o)[0]

def rdict(d, o):
    """Nitro resource dictionary -> [(name, entry_offset)]"""
    n = d[o + 1]; e = o + u16(d, o + 6); unit = u16(d, e); ofsn = u16(d, e + 2)
    return [(d[e + ofsn + 16 * i:e + ofsn + 16 * i + 16].split(b'\0')[0].decode('latin1'), e + 4 + i * unit) for i in range(n)]

def rgb555(c):
    return ((c & 31) * 255 // 31, (c >> 5 & 31) * 255 // 31, (c >> 10 & 31) * 255 // 31)

def blocks(d):
    assert d[:4] == b'BMD0' or d[:4] == b'BTX0', d[:4]
    n = u16(d, 0xe)
    return {d[u32(d, 0x10 + 4 * i):u32(d, 0x10 + 4 * i) + 4]: u32(d, 0x10 + 4 * i) for i in range(n)}

def textures(d, t):
    """TEX0 block at t -> (texs {name:(param, w, h, fmt, c0t, raw...)}, pals {name: offset})"""
    texdict = t + u16(d, t + 0x0e); texdata = t + u32(d, 0x14 + t)
    cdata = t + u32(d, t + 0x24); cinfo = t + u32(d, t + 0x28)
    paldict = t + u32(d, t + 0x34); paldata = t + u32(d, t + 0x38)
    texs = {}
    for name, e in rdict(d, texdict):
        p = u32(d, e); off = (p & 0xffff) << 3; w = 8 << (p >> 20 & 7); h = 8 << (p >> 23 & 7); fmt = p >> 26 & 7
        texs[name] = dict(param=p, w=w, h=h, fmt=fmt, c0=bool(p >> 29 & 1), off=off)
    pals = {name: u16(d, e) << 3 for name, e in rdict(d, paldict)}
    return texs, pals, dict(texdata=texdata, cdata=cdata, cinfo=cinfo, paldata=paldata)

def decode(d, tx, paloff, base):
    w, h, fmt = tx['w'], tx['h'], tx['fmt']; out = np.zeros((h, w, 4), np.uint8)
    pd = base['paldata'] + (paloff or 0)
    pal = lambda i: rgb555(u16(d, pd + 2 * i))
    if fmt == 7:
        o = base['texdata'] + tx['off']
        a = np.frombuffer(d, '<u2', w * h, o).reshape(h, w)
        out[..., 0] = (a & 31) * 255 // 31; out[..., 1] = (a >> 5 & 31) * 255 // 31; out[..., 2] = (a >> 10 & 31) * 255 // 31
        out[..., 3] = np.where(a >> 15, 255, 0); return out
    if fmt == 5:
        o = base['cdata'] + tx['off']; io = base['cinfo'] + tx['off'] // 2
        for by in range(h // 4):
            for bx in range(w // 4):
                k = by * (w // 4) + bx; bits = u32(d, o + 4 * k); ix = u16(d, io + 2 * k)
                po = pd + ((ix & 0x3fff) << 2); mode = ix >> 14
                c = [np.array(rgb555(u16(d, po + 2 * j)) + (255,), np.int32) for j in range(4)]
                if mode == 0: c[3] = c[3] * 0
                elif mode == 1: c[2] = (c[0] + c[1]) // 2; c[3] = c[3] * 0
                elif mode == 3: c[2] = (5 * c[0] + 3 * c[1]) // 8; c[3] = (3 * c[0] + 5 * c[1]) // 8
                for yy in range(4):
                    for xx in range(4):
                        out[by * 4 + yy, bx * 4 + xx] = c[bits >> (2 * (4 * yy + xx)) & 3]
        return out
    o = base['texdata'] + tx['off']
    bpp = {1: 8, 2: 2, 3: 4, 4: 8, 6: 8}[fmt]
    raw = np.frombuffer(d, np.uint8, w * h * bpp // 8, o)
    if bpp == 8: idx = raw.astype(int)
    elif bpp == 4: idx = np.stack([raw & 15, raw >> 4], -1).reshape(-1).astype(int)
    else: idx = np.stack([raw & 3, raw >> 2 & 3, raw >> 4 & 3, raw >> 6], -1).reshape(-1).astype(int)
    alpha = np.full(idx.shape, 255)
    if fmt == 1: alpha = (idx >> 5) * 255 // 7; idx = idx & 31
    elif fmt == 6: alpha = (idx >> 3) * 255 // 31; idx = idx & 7
    elif tx['c0']: alpha = np.where(idx == 0, 0, 255)
    n = int(idx.max()) + 1; P = np.array([pal(i) for i in range(n)], np.uint8)
    out[..., :3] = P[idx].reshape(h, w, 3); out[..., 3] = alpha.reshape(h, w)
    return out

def fx16(v): return v / 4096.0

def node_local(d, o):
    flag = u16(d, o); m00 = s16(d, o + 2); p = o + 4
    T = np.zeros(3); R = np.eye(3); S = np.ones(3)
    if not flag & 1:
        T = np.array([s32(d, p + 4 * i) for i in range(3)]) / 4096.0; p += 12
    if not flag & 2:
        if flag & 8:
            a, b = fx16(s16(d, p)), fx16(s16(d, p + 2)); p += 4
            piv = flag >> 4 & 15; R = np.zeros((3, 3)); r, c = divmod(piv, 3)
            R[r, c] = -1 if flag & 0x100 else 1
            cc = -b if flag & 0x200 else b; dd = -a if flag & 0x400 else a
            rows = [i for i in range(3) if i != r]; cols = [i for i in range(3) if i != c]
            R[rows[0], cols[0]], R[rows[0], cols[1]], R[rows[1], cols[0]], R[rows[1], cols[1]] = a, b, cc, dd
        else:
            v = [m00] + [s16(d, p + 2 * i) for i in range(8)]; p += 16
            R = np.array(v, float).reshape(3, 3) / 4096.0
    if not flag & 4:
        S = np.array([s32(d, p + 4 * i) for i in range(3)]) / 4096.0; p += 24
    return T, R, S

def mat_col(T, R, S):
    """row-vector Nitro (v*S*R + T) -> column 4x4"""
    M = np.eye(4); M[:3, :3] = R.T @ np.diag(S); M[:3, 3] = T; return M

SBC_LEN = {0: 0, 1: 0, 2: 2, 3: 1, 4: 1, 0x24: 1, 0x44: 1, 5: 1, 6: 3, 0x26: 4, 0x46: 4, 0x66: 5,
           7: 1, 0x27: 2, 0x47: 2, 0x67: 3, 8: 1, 0x28: 2, 0x48: 2, 0x68: 3, 0x0a: 8, 0x0b: 0, 0x2b: 0, 0x0c: 2, 0x0d: 2}
GX_LEN = {0: 0, 0x10: 1, 0x11: 0, 0x12: 1, 0x13: 1, 0x14: 1, 0x15: 0, 0x16: 16, 0x17: 12, 0x18: 16, 0x19: 12, 0x1a: 9,
          0x1b: 3, 0x1c: 3, 0x20: 1, 0x21: 1, 0x22: 1, 0x23: 2, 0x24: 1, 0x25: 1, 0x26: 1, 0x27: 1, 0x28: 1, 0x29: 1,
          0x2a: 1, 0x2b: 1, 0x30: 1, 0x31: 1, 0x32: 1, 0x33: 1, 0x34: 32, 0x40: 1, 0x41: 0, 0x50: 1, 0x60: 1,
          0x70: 3, 0x71: 2, 0x72: 1}

class Model:
    def __init__(self, d):
        self.d = d; b = blocks(d); m0 = b[b'MDL0']
        (self.name, e), = rdict(d, m0 + 8)[:1]
        m = m0 + u32(d, e); self.m = m
        self.sbc = m + u32(d, m + 4); self.matb = m + u32(d, m + 8); self.shpb = m + u32(d, m + 0xc); self.evp = m + u32(d, m + 0x10)
        self.nnode = d[m + 0x17]; self.posscale = s32(d, m + 0x1c) / 4096.0
        ni = m + 0x40
        self.nodes = [(n, node_local(d, ni + u32(d, e))) for n, e in rdict(d, ni)]
        self.mats = [(n, self.matb + u32(d, e)) for n, e in rdict(d, self.matb + 4)]
        self.shps = [(n, self.shpb + u32(d, e)) for n, e in rdict(d, self.shpb)]
        mtex = {}; mpal = {}
        for n, e in rdict(d, self.matb + u16(d, self.matb)):
            lo = self.matb + u16(d, e)
            for i in range(d[e + 2]): mtex[d[lo + i]] = n
        for n, e in rdict(d, self.matb + u16(d, self.matb + 2)):
            lo = self.matb + u16(d, e)
            for i in range(d[e + 2]): mpal[d[lo + i]] = n
        self.mtex, self.mpal = mtex, mpal
        self.texs = None
        if b'TEX0' in b: self.texs = textures(d, b[b'TEX0'])
        self.sbc_end = self.matb

    def matinfo(self, i):
        d = self.d; o = self.mats[i][1]
        tip = u32(d, o + 0x14); flag = u16(d, o + 0x1e); pa = u32(d, o + 0x0c)
        p = o + 0x2c; sc = (1.0, 1.0); tr = (0.0, 0.0)
        if flag & 1:
            if not flag & 2: sc = (s32(d, p) / 4096, s32(d, p + 4) / 4096); p += 8
            if not flag & 4: p += 4
            if not flag & 8: tr = (s32(d, p) / 4096, s32(d, p + 4) / 4096); p += 8
        return dict(tip=tip, flag=flag, alpha=(pa >> 16 & 31), sc=sc, tr=tr, w=u16(d, o + 0x20), h=u16(d, o + 0x22))

    def invbind(self, i):
        d = self.d; o = self.evp + 84 * i
        v = [s32(d, o + 4 * k) / 4096.0 for k in range(12)]
        M = np.eye(4); M[:3, :3] = np.array(v[:9]).reshape(3, 3).T; M[:3, 3] = v[9:12]; return M

    def run(self, local=None):
        """Execute SBC; local: list of 4x4 column local matrices per node (default bind). -> {mat: [(pos(n,3), uv(n,2) texels, tris)]}"""
        d = self.d
        if local is None: local = [mat_col(*t) for _, t in self.nodes]
        world = [np.eye(4)] * len(self.nodes)
        stack = [np.eye(4)] * 32; cur = np.eye(4); mat = 0; out = {}
        p = self.sbc
        while p < self.sbc_end:
            c = d[p]; ln = SBC_LEN.get(c)
            if c == 9: n = d[p + 2]; ln = 2 + 3 * n
            if ln is None: raise ValueError('sbc %x at %x' % (c, p - self.sbc))
            a = d[p + 1:p + 1 + ln]; p += 1 + ln
            if c == 1: break
            if c in (6, 0x26, 0x46, 0x66):
                node, par = a[0], a[1]
                if c in (0x46,): cur = stack[a[3]]
                if c == 0x66: cur = stack[a[4]]
                world[node] = (world[par] if par != node else np.eye(4)) @ local[node]
                cur = world[node]
                if c in (0x26, 0x66): stack[a[3]] = cur
            elif c == 3: cur = stack[a[0]]
            elif c == 0x0b: cur = cur @ np.diag([self.posscale] * 3 + [1])
            elif c == 0x2b: cur = cur @ np.diag([1 / self.posscale] * 3 + [1])
            elif c in (4, 0x24, 0x44): mat = a[0]
            elif c == 9:
                M = np.zeros((4, 4))
                for k in range(a[1]):
                    si, ni, w = a[2 + 3 * k:5 + 3 * k]
                    M += (w / 256.0) * (stack[si] @ self.invbind(ni))
                stack[a[0]] = M
            elif c == 5:
                out.setdefault(mat, []).append(self.shape(a[0], stack, cur))
        return out

    def shape(self, i, stack, cur):
        d = self.d; o = self.shps[i][1]; dl = o + u32(d, o + 8); end = dl + u32(d, o + 0xc)
        P = []; UV = []; T = []; prim = 0; strip = []; v = np.zeros(3); uv = (0.0, 0.0); ps = self.posscale
        M = cur.copy()
        def emit(x):
            P.append((M @ np.append(x, 1))[:3]); UV.append(uv); strip.append(len(P) - 1)
            s = strip
            if prim == 0 and len(s) % 3 == 0: T.append(s[-3:])
            elif prim == 1 and len(s) % 4 == 0: T.extend([[s[-4], s[-3], s[-2]], [s[-4], s[-2], s[-1]]])
            elif prim == 2 and len(s) >= 3:
                T.append([s[-3], s[-2], s[-1]] if len(s) % 2 else [s[-2], s[-3], s[-1]])
            elif prim == 3 and len(s) >= 4 and len(s) % 2 == 0:
                T.extend([[s[-4], s[-3], s[-1]], [s[-4], s[-1], s[-2]]])
        q = dl
        while q + 4 <= end:
            cmds = d[q:q + 4]; q += 4
            for c in cmds:
                ln = GX_LEN.get(c)
                if ln is None: raise ValueError('gx %x' % c)
                prm = [u32(d, q + 4 * k) for k in range(ln)]; q += 4 * ln
                if c == 0x14: M = stack[prm[0] & 31].copy()
                elif c == 0x1b:
                    M = M @ np.diag([s32(d, q - 12 + 4 * k) / 4096 for k in range(3)] + [1])
                elif c == 0x1c:
                    Tm = np.eye(4); Tm[:3, 3] = [s32(d, q - 12 + 4 * k) / 4096 for k in range(3)]; M = M @ Tm
                elif c == 0x22: uv = (((prm[0] & 0xffff) ^ 0x8000) - 0x8000) / 16.0, ((prm[0] >> 16 ^ 0x8000) - 0x8000) / 16.0
                elif c == 0x40: prim = prm[0] & 3; strip = []
                elif c == 0x23:
                    a, b = prm
                    v = np.array([(a & 0xffff ^ 0x8000) - 0x8000, (a >> 16 ^ 0x8000) - 0x8000, (b & 0xffff ^ 0x8000) - 0x8000]) / 4096.0; emit(v)
                elif c == 0x24:
                    a = prm[0]; v = np.array([((a >> s & 1023) ^ 512) - 512 for s in (0, 10, 20)]) / 64.0; emit(v)
                elif c in (0x25, 0x26, 0x27):
                    a = prm[0]; x, y = (a & 0xffff ^ 0x8000) - 0x8000, (a >> 16 ^ 0x8000) - 0x8000
                    v = v.copy(); ij = {0x25: (0, 1), 0x26: (0, 2), 0x27: (1, 2)}[c]; v[ij[0]] = x / 4096; v[ij[1]] = y / 4096; emit(v)
                elif c == 0x28:
                    a = prm[0]; v = v + np.array([((a >> s & 1023) ^ 512) - 512 for s in (0, 10, 20)]) / 4096.0; emit(v)
        return np.array(P).reshape(-1, 3), np.array(UV).reshape(-1, 2), np.array(T, int).reshape(-1, 3)

    def texture(self, mi):
        """material index -> RGBA image (mirror flips expanded) and uv scale"""
        tn = self.mtex.get(mi)
        if tn is None or not self.texs: return None
        texs, pals, base = self.texs; tx = texs[tn]
        img = decode(self.d, tx, pals.get(self.mpal.get(mi)), base)
        tip = self.matinfo(mi)['tip']
        fs, ft = bool(tip >> 18 & 1), bool(tip >> 19 & 1)
        if fs: img = np.concatenate([img, img[:, ::-1]], 1)
        if ft: img = np.concatenate([img, img[::-1]], 0)
        return img, (tx['w'], tx['h']), (fs, ft), bool(tip >> 16 & 1), bool(tip >> 17 & 1)

def items(model, local=None):
    """-> litview items [(pos, tris, uv(0..1), RGBA, layer)]"""
    res = []
    geo = model.run(local)
    for mi, shapes in geo.items():
        t = model.texture(mi)
        info = model.matinfo(mi)
        for P, UV, T in shapes:
            if not len(T): continue
            if t is None:
                img = np.full((2, 2, 4), 200, np.uint8); uv = np.zeros_like(UV)
            else:
                img, (w, h), (fs, ft), rs, rt = t
                sc, tr = info['sc'], info['tr']
                uv = np.stack([UV[:, 0] * sc[0] + tr[0] * w, UV[:, 1] * sc[1] + tr[1] * h], 1) / [w * (2 if fs else 1), h * (2 if ft else 1)]
            res.append((P, T, uv, img, 0))
    return res

# ---------------- NSBCA (BCA0 / JNT0) skeletal animation ----------------
def rot5(d, o):
    v = [u16(d, o + 2 * i) for i in range(5)]
    xs = [((x ^ 0x8000) - 0x8000) >> 3 for x in v]
    z = (v[4] & 7) << 12 | (v[0] & 7) << 9 | (v[1] & 7) << 6 | (v[2] & 7) << 3 | (v[3] & 7)
    z &= 0x1fff; z = z - 0x2000 if z & 0x1000 else z
    a = np.array(xs[:3], float) / 4096; b = np.array(xs[3:5] + [z], float) / 4096
    return np.array([a, b, np.cross(a, b)])

def rot3(d, o):
    mode = u16(d, o); a, b = fx16(s16(d, o + 2)), fx16(s16(d, o + 4))
    piv = mode & 15; R = np.zeros((3, 3)); r, c = divmod(piv, 3)
    R[r, c] = -1 if mode & 16 else 1
    cc = -b if mode & 32 else b; dd = -a if mode & 64 else a
    rows = [i for i in range(3) if i != r]; cols = [i for i in range(3) if i != c]
    R[rows[0], cols[0]], R[rows[0], cols[1]], R[rows[1], cols[0]], R[rows[1], cols[1]] = a, b, cc, dd
    return R

class Anim:
    def __init__(self, d):
        b = blocks_any(d); j = b[b'JNT0']
        (self.name, e), = rdict(d, j + 8)[:1]
        a = j + u32(d, e); self.d = d; self.a = a
        assert d[a:a + 4] == b'J\0AC', d[a:a + 4]
        self.frames = u16(d, a + 4); self.nnode = u16(d, a + 6)
        self.r3 = a + u32(d, a + 0xc); self.r5 = a + u32(d, a + 0x10)
        self.tags = [a + u16(d, a + 0x14 + 2 * i) for i in range(self.nnode)]

    def _track(self, info_ofs, frame, kind):
        d = self.d; info = u32(d, info_ofs); off = self.a + u32(d, info_ofs + 4)
        step = info >> 30; w16 = info >> 29 & 1; last = info >> 16 & 0x1fff
        i = min(frame, last) >> step if step else min(frame, last)
        if kind == 'rot':
            return self._rot(u16(d, off + 2 * i))
        if kind == 'trans':
            return s16(d, off + 2 * i) / 4096 if w16 else s32(d, off + 4 * i) / 4096
        return s16(d, off + 4 * i) / 4096 if w16 else s32(d, off + 8 * i) / 4096

    def _rot(self, idx):
        if idx & 0x8000: return rot3(self.d, self.r3 + 6 * (idx & 0x7fff))
        return rot5(self.d, self.r5 + 10 * idx)

    def local(self, model, frame):
        """per model node: (T, R, S) at frame"""
        d = self.d; out = []
        for n in range(len(model.nodes)):
            T0, R0, S0 = model.nodes[n][1]
            if n >= self.nnode: out.append((T0, R0, S0)); continue
            p = self.tags[n]; fl = u32(d, p); p += 4
            if fl & 1: out.append((T0, R0, S0)); continue
            T = T0.copy(); R = R0; S = S0.copy()
            if fl & 2: T = np.zeros(3)
            elif fl & 4: pass
            else:
                T = np.zeros(3)
                for k, bit in enumerate((8, 16, 32)):
                    if fl & bit: T[k] = s32(d, p) / 4096; p += 4
                    else: T[k] = self._track(p, frame, 'trans'); p += 8
            if fl & 0x40: R = np.eye(3)
            elif fl & 0x80: pass
            elif fl & 0x100: R = self._rot(u16(d, p)); p += 4
            else: R = self._track(p, frame, 'rot'); p += 8
            if fl & 0x200: S = np.ones(3)
            elif fl & 0x400: pass
            else:
                S = np.ones(3)
                for k, bit in enumerate((0x800, 0x1000, 0x2000)):
                    if fl & bit: S[k] = s32(d, p) / 4096; p += 8
                    else: S[k] = self._track(p, frame, 'scale'); p += 8
            out.append((T, R, S))
        return out

def blocks_any(d):
    n = u16(d, 0xe)
    return {d[u32(d, 0x10 + 4 * i):u32(d, 0x10 + 4 * i) + 4]: u32(d, 0x10 + 4 * i) for i in range(n)}

def posed(model, anim, frame):
    return [mat_col(*t) for t in anim.local(model, frame)]
