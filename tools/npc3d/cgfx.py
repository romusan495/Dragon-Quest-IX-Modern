"""Minimal CGFX (.bcmdl) reader: model meshes in bind pose, materials' first texture, textures decoded."""
import struct
import numpy as np

class R:
    def __init__(s, d): s.d = d
    def u32(s, o): return struct.unpack_from('<I', s.d, o)[0]
    def i32(s, o): return struct.unpack_from('<i', s.d, o)[0]
    def f32(s, o): return struct.unpack_from('<f', s.d, o)[0]
    def rel(s, o):
        v = s.i32(o)
        return o + v if v else None
    def cstr(s, o): return s.d[o:s.d.index(b'\0', o)].decode('latin1')
    def lst(s, o):                       # count, rel ptr -> array of rel ptrs
        n = s.u32(o); p = s.rel(o + 4)
        return [s.rel(p + 4 * i) for i in range(n)] if n else []
    def dict(s, o):                      # count, rel ptr -> DICT
        n = s.u32(o); p = s.rel(o + 4)
        if not n: return []
        assert s.d[p:p + 4] == b'DICT'
        out = []
        for i in range(s.u32(p + 8)):
            e = p + 0x1c + i * 16
            out.append((s.cstr(s.rel(e + 8)), s.rel(e + 12)))
        return out
    def mat34(s, o):
        return np.array(struct.unpack_from('<12f', s.d, o), dtype=np.float64).reshape(3, 4)

SMOOTH = False
GLT = {0x1400: ('b', 1), 0x1401: ('B', 1), 0x1402: ('h', 2), 0x1403: ('H', 2), 0x1406: ('f', 4)}

def _morton(x, y):
    m = 0
    for i in range(3):
        m |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1)
    return m

def _etc1_block(blk, alpha):
    """blk: 64-bit int (little endian already); returns 4x4x4 uint8"""
    out = np.zeros((4, 4, 4), np.uint8)
    hi = blk >> 32; lo = blk & 0xffffffff
    flip = (hi >> 0) & 1; diff = (hi >> 1) & 1
    t1 = (hi >> 5) & 7; t2 = (hi >> 2) & 7
    tbl = [[2, 8], [5, 17], [9, 29], [13, 42], [18, 60], [24, 80], [33, 106], [47, 183]]
    if diff:
        def c5(sh):
            b = (hi >> sh) & 0x1f; dd = (hi >> (sh - 3)) & 7
            if dd >= 4: dd -= 8
            b2 = b + dd
            return (b << 3 | b >> 2), ((b2 & 31) << 3 | (b2 & 31) >> 2)
        r1, r2 = c5(27); g1, g2 = c5(19); b1, b2 = c5(11)
    else:
        def c4(sh):
            a = (hi >> sh) & 15; b = (hi >> (sh - 4)) & 15
            return a * 17, b * 17
        r1, r2 = c4(28); g1, g2 = c4(20); b1, b2 = c4(12)
    for y in range(4):
        for x in range(4):
            i = x * 4 + y
            sub = (y >= 2) if flip else (x >= 2)
            base = (r2, g2, b2) if sub else (r1, g1, b1)
            t = tbl[t2 if sub else t1]
            msb = (lo >> (16 + i)) & 1; lsb = (lo >> i) & 1
            m = t[lsb] * (-1 if msb else 1)
            out[y, x, :3] = [min(255, max(0, c + m)) for c in base]
            out[y, x, 3] = alpha[y, x] if alpha is not None else 255
    return out

def decode_tex(fmt, w, h, data):
    img = np.zeros((h, w, 4), np.uint8)
    if fmt in (12, 13):
        p = 0
        for ty in range(0, h, 8):
            for tx in range(0, w, 8):
                for by, bx in ((0, 0), (0, 4), (4, 0), (4, 4)):
                    alpha = None
                    if fmt == 13:
                        av = struct.unpack_from('<Q', data, p)[0]; p += 8
                        alpha = np.zeros((4, 4), np.uint8)
                        for i in range(16):
                            a = (av >> (4 * i)) & 15
                            alpha[i % 4, i // 4] = a * 17
                    blk = struct.unpack_from('<Q', data, p)[0]; p += 8
                    img[ty + by:ty + by + 4, tx + bx:tx + bx + 4] = _etc1_block(blk, alpha)
        return img[::-1]
    bpp = {0: 32, 1: 24, 2: 16, 3: 16, 4: 16, 5: 16, 6: 16, 7: 8, 8: 8, 9: 8, 10: 4, 11: 4}[fmt]
    p = 0
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for i in range(64):
                x = y = 0
                for b in range(3):
                    x |= ((i >> (2 * b)) & 1) << b; y |= ((i >> (2 * b + 1)) & 1) << b
                if bpp == 4:
                    v = (data[p + i // 2] >> (4 * (i & 1))) & 15
                else:
                    v = int.from_bytes(data[p + i * bpp // 8:p + (i + 1) * bpp // 8], 'little')
                if fmt == 0: px = (v >> 24 & 255, v >> 16 & 255, v >> 8 & 255, v & 255)
                elif fmt == 1: px = (v >> 16 & 255, v >> 8 & 255, v & 255, 255)
                elif fmt == 2: px = ((v >> 11 & 31) * 255 // 31, (v >> 6 & 31) * 255 // 31, (v >> 1 & 31) * 255 // 31, 255 if v & 1 else 0)
                elif fmt == 3: px = ((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31, 255)
                elif fmt == 4: px = ((v >> 12 & 15) * 17, (v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17)
                elif fmt == 5: px = (v >> 8, v >> 8, v >> 8, v & 255)
                elif fmt == 7: px = (v, v, v, 255)
                elif fmt == 8: px = (255, 255, 255, v)
                elif fmt == 9: px = ((v >> 4) * 17,) * 3 + ((v & 15) * 17,)
                elif fmt == 10: px = (v * 17,) * 3 + (255,)
                elif fmt == 11: px = (255, 255, 255, v * 17)
                else: px = (255, 0, 255, 255)
                img[ty + y, tx + x] = px
            p += 64 * bpp // 8
    return img[::-1]

def load(path):
    d = open(path, 'rb').read()
    r = R(d)
    assert d[:4] == b'CGFX'
    data = 0x14
    tops = [r.dict(data + 8 + 8 * i) for i in range(16)]
    textures = {}
    for name, t in tops[1]:
        if r.u32(t) != 0x20000011: continue
        h, w = r.u32(t + 0x18), r.u32(t + 0x1c); fmt = r.u32(t + 0x34)
        im = r.rel(t + 0x38)
        h2, w2, ln = r.u32(im), r.u32(im + 4), r.u32(im + 8)
        raw = d[r.rel(im + 12):r.rel(im + 12) + ln]
        try: textures[name] = decode_tex(fmt, w, h, raw)
        except Exception as e: print('tex', name, fmt, e)
    models = []
    for mname, m in tops[0]:
        assert d[m + 4:m + 8] == b'CMDL'
        meshes = r.lst(m + 0xb4); mats = r.dict(m + 0xbc); shapes = r.lst(m + 0xc4)
        skel = r.rel(m + 0xe0) if r.u32(m) & 0x80 else None
        bones = []
        if skel:
            for bn, b in r.dict(skel + 0x18):
                bones.append((r.u32(b + 8), bn, r.i32(b + 0xc), r.mat34(b + 0x74)))
            bones.sort()
            loc = {}
            bind = {}
            for bn, b in r.dict(skel + 0x18):
                loc[r.u32(b + 8)] = r.mat34(b + 0x44)
                bind[bn] = (struct.unpack_from('<3f', d, b + 0x20), struct.unpack_from('<3f', d, b + 0x2c), struct.unpack_from('<3f', d, b + 0x38))
        bw = {}
        def world(i):
            if i in bw: return bw[i]
            par = [p for j, n, p, mt in bones if j == i][0]
            L = np.vstack([loc[i], [0, 0, 0, 1]])
            Wm = (np.vstack([world(par), [0, 0, 0, 1]]) @ L) if par >= 0 else L
            bw[i] = Wm[:3]
            return bw[i]
        for j, n, p, mt in bones: world(j)
        mattex = []
        for mn, mt in mats:
            tex = []
            for k in range(3):
                mp = r.rel(mt + 0x274 + 4 * k)
                if not mp: break
                tr = r.rel(mp + 8)
                tex.append(r.cstr(r.rel(tr + 0x18)) if tr else None)
            mattex.append((mn, tex))
        parts = []
        for me in meshes:
            si, mi = r.u32(me + 0x18), r.u32(me + 0x1c)
            sh = shapes[si]
            # vertex buffers
            attrs = {}; fixed = {}; vb = None
            for v in r.lst(sh + 0x38):
                ty = r.u32(v)
                if ty == 0x40000002:
                    raw = d[r.rel(v + 0x18):r.rel(v + 0x18) + r.u32(v + 0x14)]
                    stride = r.u32(v + 0x24)
                    vb = (raw, stride)
                    for a in r.lst(v + 0x28):
                        attrs[r.u32(a + 4)] = (r.u32(a + 0x24), r.u32(a + 0x28), r.f32(a + 0x2c), r.u32(a + 0x30))
                elif ty == 0x80000000:
                    n = r.u32(v + 0x18)
                    sc = r.f32(v + 0x1c)
                    fixed[r.u32(v + 4)] = [r.f32(v + 0x20 + 4 * k) for k in range(n)]
                else:
                    print('vb type', hex(ty))
            raw, stride = vb
            nv = len(raw) // stride
            def col(an):
                fmt, el, sc, off = attrs[an]
                c, sz = GLT[fmt]
                arr = np.zeros((nv, el))
                for i in range(nv):
                    arr[i] = struct.unpack_from('<%d%s' % (el, c), raw, i * stride + off)
                return arr * sc
            pos = col(0)
            uvs = [col(a) if a in attrs else None for a in (4, 5, 6)]
            uv = uvs[0] if uvs[0] is not None else np.zeros((nv, 2))
            bidx = col(7) if 7 in attrs else (np.array([fixed[7]] * nv) if 7 in fixed else None)
            bwt = col(8) if 8 in attrs else None
            off = np.array(struct.unpack_from('<3f', d, sh + 0x20))
            for sm in r.lst(sh + 0x2c):
                nb = r.u32(sm); bp = r.rel(sm + 4)
                blist = [r.u32(bp + 4 * k) for k in range(nb)]
                skin = r.u32(sm + 8)
                for fc in r.lst(sm + 0xc):
                    for fd in r.lst(fc):
                        ifmt = r.u32(fd)
                        ib = d[r.rel(fd + 0xc):r.rel(fd + 0xc) + r.u32(fd + 8)]
                        idx = np.frombuffer(ib, np.uint8 if ifmt == 0x1401 else np.uint16)
                        P = pos.copy()
                        if skin != 2 and blist:
                            for i in set(idx.tolist()):
                                bi = blist[int(bidx[i][0])] if bidx is not None else blist[0]
                                M = bw.get(bi)
                                if M is not None:
                                    P[i] = M[:, :3] @ pos[i] + M[:, 3]
                        elif SMOOTH and skin == 2 and bidx is not None:
                            for i in set(idx.tolist()):
                                acc = np.zeros(3); tw = 0
                                for k in range(bidx.shape[1]):
                                    wgt = bwt[i][k] if bwt is not None else (1 if k == 0 else 0)
                                    if wgt <= 0: continue
                                    M = bw.get(blist[int(bidx[i][k])])
                                    if M is None: continue
                                    acc += wgt * (M[:, :3] @ pos[i] + M[:, 3]); tw += wgt
                                if tw > 0: P[i] = acc / tw
                        tris = idx[:len(idx) // 3 * 3].reshape(-1, 3)
                        texs = mattex[mi][1] if mi < len(mattex) else []
                        parts.append(dict(raw=pos, off=off, bidx=bidx, bwt=bwt, blist=blist, pos=P + off, uv=uv, uvs=uvs, tris=tris, tex=texs[0] if texs else None, texs=texs, skin=skin, mat=mattex[mi][0] if mi < len(mattex) else ''))
        models.append(dict(name=mname, parts=parts, bones=bones, bindworld=dict(bw), bind=bind if skel else {}))
    return models, textures
