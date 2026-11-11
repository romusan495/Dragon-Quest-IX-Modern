"""Minimal BCH (H3D, 3DS) reader for DQVIII models: relocation, textures, meshes, skeleton, skeletal animations."""
import struct
import numpy as np

class B:
    def __init__(s, d): s.d = d
    def u8(s, o): return s.d[o]
    def u16(s, o): return struct.unpack_from('<H', s.d, o)[0]
    def u32(s, o): return struct.unpack_from('<I', s.d, o)[0]
    def i32(s, o): return struct.unpack_from('<i', s.d, o)[0]
    def f32(s, o): return struct.unpack_from('<f', s.d, o)[0]
    def fs(s, o, n): return struct.unpack_from('<%df' % n, s.d, o)
    def cstr(s, o): return s.d[o:s.d.index(b'\0', o)].decode('latin1')
    def str_at(s, o): return s.cstr(s.u32(o))

def relocate(d):
    d = bytearray(d)
    u = lambda o: struct.unpack_from('<I', d, o)[0]
    def add(o, v, orbit=0): struct.pack_into('<I', d, o, ((u(o) + v) & 0xffffffff) | orbit)
    bw = d[4]
    hdr = [u(8 + 4 * i) for i in range(14)]
    mh, st, gc, do, de, rt = hdr[:6]
    rtl = hdr[11]
    for o in range(rt, rt + rtl, 4):
        v = u(o); off = v & 0x1ffffff; fl = v >> 25
        if fl == 0: add(off * 4 + mh, mh)
        elif fl == 1: add(off + mh, st)
        elif fl == 2: add(off * 4 + mh, gc)
        elif fl in (7, 0xc): add(off * 4 + mh, do)
        p = off * 4 + gc
        if fl in (0x25, 0x26, 0x28): add(p, do)       # texture, vertex buffer, 8-bit index buffer
        elif fl == 0x27: add(p, do, 1 << 31)        # 16-bit index buffer
    return bytes(d), dict(main=mh, strings=st, gpu=gc, data=do, dataext=de)

SECTIONS = ['models', 'materials', 'shaders', 'textures', 'materialsLUT', 'lights', 'cameras', 'fogs',
            'skeletalAnimations', 'materialAnimations', 'visibilityAnimations', 'lightAnimations', 'cameraAnimations', 'fogAnimations', 'scene']

def content(b, mh):
    out = {}
    for i, n in enumerate(SECTIONS):
        o = mh + i * 12
        out[n] = (b.u32(o), b.u32(o + 4), b.u32(o + 8))
    return out

def pica(b, o, n):
    """PICA200 command list -> list of (register, value) in order"""
    w = [b.u32(o + 4 * k) for k in range(n)]
    out = []; i = 0
    while i + 1 < len(w):
        p, hd = w[i], w[i + 1]; i += 2
        reg = hd & 0xffff; extra = (hd >> 20) & 0x7ff; cons = hd >> 31
        vals = [p] + w[i:i + extra]; i += extra
        for k, v in enumerate(vals): out.append((reg + (k if cons else 0), v))
        if i % 2: i += 1
    return out

FMT = {0: ('b', 1), 1: ('B', 1), 2: ('h', 2), 3: ('f', 4)}

def euler(r):
    x, y, z = r
    cx, sx, cy, sy, cz, sz = np.cos(x), np.sin(x), np.cos(y), np.sin(y), np.cos(z), np.sin(z)
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]]); Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]); Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Rz @ Ry @ Rx

def local(s, r, t):
    M = np.eye(4); M[:3, :3] = euler(r) * np.asarray(s)[None]; M[:3, 3] = t
    return M

def load(path, hands='paa'):
    """-> dict(bones=[(name, parent, s, r, t)], parts=[dict(pos(model space, bind), uv, tris, tex, mat, outline, bidx, bwt, blist, skin)], textures={name: RGBA})"""
    from cgfx import decode_tex
    raw = open(path, 'rb').read()
    d, h = relocate(raw); b = B(d)
    c = content(b, h['main'])
    textures = {}
    for i in range(c['textures'][1]):
        o = b.u32(c['textures'][0] + 4 * i)
        r = dict(pica(b, b.u32(o), b.u32(o + 4)))
        w, hh = r[0x82] >> 16, r[0x82] & 0xffff
        fmt = r[0x8e] & 0xf
        bpp = {0: 32, 1: 24, 2: 16, 3: 16, 4: 16, 5: 16, 6: 16, 7: 8, 8: 8, 9: 8, 10: 4, 11: 4, 12: 4, 13: 8}[fmt]
        try: textures[b.str_at(o + 0x1c)] = decode_tex(fmt, w, hh, d[r[0x85]:r[0x85] + w * hh * bpp // 8])
        except Exception as e: print('tex', fmt, e)
    mo = b.u32(c['models'][0])
    mats = []
    for i in range(b.u32(mo + 0x38)):
        m = b.u32(mo + 0x34) + i * 0x2c
        tex = [b.str_at(m + 0x1c + 4 * k) if b.u32(m + 0x1c + 4 * k) else None for k in range(3)]
        mats.append((b.str_at(m + 0x28), tex))
    bones = []
    for i in range(b.u32(mo + 0x74)):
        o = b.u32(mo + 0x70) + i * 0x64
        par = struct.unpack_from('<h', d, o + 4)[0]
        bones.append((b.str_at(o + 0x5c), par, b.fs(o + 8, 3), b.fs(o + 0x14, 3), b.fs(o + 0x20, 3)))
    world = []
    for name, par, s, r, t in bones:
        L = local(s, r, t)
        world.append(world[par] @ L if par >= 0 else L)
    parts = []
    for i in range(b.u32(mo + 0x44)):
        o = b.u32(mo + 0x40) + i * 0x38
        mi = b.u16(o)
        r = bch_regs = pica(b, b.u32(o + 8), b.u32(o + 12))
        R = dict(r)
        base = R[0x203]; fmts = R[0x201] | (R[0x202] & 0xffff) << 32
        order = R[0x204] | (R[0x205] & 0xffff) << 32
        stride = (R[0x205] >> 16) & 0xff; na = R[0x205] >> 28
        perm = R[0x2bb] | R[0x2bc] << 32
        unif = [struct.unpack('<f', struct.pack('<I', v))[0] for rr, v in r if rr == 0x2c1]
        u7 = unif[-8:] if len(unif) >= 8 else [1] * 8
        st = [u7[3], u7[2], u7[1], u7[0], u7[7], u7[6], u7[5], u7[4]]   # tex0 tex1 tex2 weight pos normal tangent color
        attrs = {}; off = 0
        for k in range(na):
            a = (order >> (4 * k)) & 0xf
            f = (fmts >> (4 * a)) & 0xf; ty, n = f & 3, (f >> 2) + 1
            c_, sz = FMT[ty]
            off = (off + sz - 1) // sz * sz
            attrs[(perm >> (4 * a)) & 0xf] = (off, c_, n, sz)
            off += n * sz
        nv = 0
        fo = b.u32(o + 0x10)
        faces = []
        for f in range(b.u32(o + 0x14)):
            q = fo + f * 0x34
            skin = b.u16(q); bl = [b.u16(q + 4 + 2 * k) for k in range(b.u16(q + 2))]
            F = dict(pica(b, b.u32(q + 0x2c), b.u32(q + 0x30)))
            ia, cnt = F[0x227], F[0x228]
            is16 = ia >> 31; ia &= 0x7fffffff
            idx = np.frombuffer(d, np.uint16 if is16 else np.uint8, cnt, ia).astype(int)
            faces.append((skin, bl, idx)); nv = max(nv, idx.max() + 1)
        def col(sem, scale=1.0):
            if sem not in attrs: return None
            ao, c_, n, sz = attrs[sem]
            arr = np.array([struct.unpack_from('<%d%s' % (n, c_), d, base + v * stride + ao) for v in range(nv)], np.float64)
            return arr * (scale if c_ != 'f' else 1.0)
        pos = col(0); uv = col(4, st[0]); bi = col(7); bw = col(8, st[3] or 0.01)
        if uv is None: uv = np.zeros((nv, 2))
        for skin, bl, idx in faces:
            P = pos.copy()
            if skin == 1 and bi is None and bw is not None:  # bone indices are a fixed attribute: 0, 1, ...
                bi = np.tile(np.arange(bw.shape[1], dtype=np.float64), (nv, 1))
            if skin != 1 or bi is None:                    # rigid: in its bone's space
                for v in np.unique(idx):
                    bn = bl[int(bi[v][0])] if bi is not None and len(bl) > 1 else bl[0]
                    M = world[bn]; P[v] = M[:3, :3] @ pos[v] + M[:3, 3]
            mn, tx = mats[mi] if mi < len(mats) else ('', [None])
            parts.append(dict(pos=P, raw=pos, uv=uv, uvs=[uv, None, None], tris=idx[:len(idx) // 3 * 3].reshape(-1, 3), tex=tx[0], texs=[t for t in tx if t],
                              mat=mn, outline='oline' in mn, bidx=bi, bwt=bw, blist=bl, skin=skin))
    if hands:                                     # alternate hand shapes (guu fist, paa open, sasi pointing): keep one
        import re
        def hand(p):
            if p['skin'] == 1 or len(p['blist']) != 1: return None
            g = re.match(r'[LR]_(guu|paa|sasi)', bones[p['blist'][0]][0]); return g and g.group(1)
        have = {hand(p) for p in parts} - {None}
        keep = hands if hands in have else next((h for h in ('guu', 'paa', 'sasi') if h in have), None)
        parts = [p for p in parts if hand(p) in (None, keep)]
    return dict(bones=bones, world=world, parts=parts, textures=textures, b=b, content=c)

def curve(b, p):
    """UnifiedHermite96 key frames -> (start, end, keys [(frame, value, slope)])"""
    w = b.u32(p + 0xc)
    quant, n = (w >> 8) & 0xff, w >> 16
    sc, of, fs = b.f32(p + 0x10), b.f32(p + 0x14), b.f32(p + 0x18)
    k = b.u32(p + 0x20)
    if quant != 3: raise ValueError('key frame quantization %d' % quant)
    keys = [b.fs(k + 12 * i, 3) for i in range(n)]
    return [(f * fs, v * sc + of, s * sc) for f, v, s in keys]

def ev(keys, t):
    if t <= keys[0][0]: return keys[0][1]
    if t >= keys[-1][0]: return keys[-1][1]
    for (f0, v0, s0), (f1, v1, s1) in zip(keys, keys[1:]):
        if f0 <= t <= f1:
            dt = f1 - f0
            if dt <= 0: return v1
            u = (t - f0) / dt; u2, u3 = u * u, u * u * u
            return (2 * u3 - 3 * u2 + 1) * v0 + (u3 - 2 * u2 + u) * dt * s0 + (-2 * u3 + 3 * u2) * v1 + (u3 - u2) * dt * s1
    return keys[-1][1]

def animations(m):
    """{name: dict(frames, bones={bone name: [9 x (const float | keys | None)]})}"""
    b, c = m['b'], m['content']
    out = {}
    for i in range(c['skeletalAnimations'][1]):
        a = b.u32(c['skeletalAnimations'][0] + 4 * i)
        name = b.str_at(a); frames = b.f32(a + 8); bt = b.u32(a + 0xc)
        bones = {}
        for j in range(b.u32(a + 0x10)):
            e = b.u32(bt + 4 * j)
            if b.u32(e + 4) != 0x40000: continue
            fl = b.u32(e + 8); el = []
            for k in range(9):
                cb = 6 + k if k < 6 else 13 + k - 6
                if fl >> cb & 1: el.append(b.f32(e + 0xc + 4 * k))
                else:
                    p = b.u32(e + 0xc + 4 * k)
                    el.append(curve(b, p) if p else None)
            bones[b.str_at(e)] = el
        out[name] = dict(frames=frames, bones=bones)
    return out

def pose(m, anim, t):
    """model parts' positions at time t of an animation (None: bind pose)"""
    world = []
    for name, par, s, r, tr in m['bones']:
        v = list(s) + list(r) + list(tr)
        if anim is not None and name in anim['bones']:
            for k, e in enumerate(anim['bones'][name]):
                if e is None: continue
                v[k] = e if isinstance(e, float) else ev(e, t)
        L = local(v[0:3], v[3:6], v[6:9])
        world.append(world[par] @ L if par >= 0 else L)
    inv = [np.linalg.inv(W) for W in m['world']]
    parts = []
    for p in m['parts']:
        P = p['raw']; out = p['pos'].copy()
        bl = p['blist']; bi, bw = p['bidx'], p['bwt']
        for v in np.unique(p['tris']):
            if p['skin'] == 1 and bi is not None:
                acc = np.zeros(3); tw = 0
                for k in range(bi.shape[1]):
                    w = bw[v][k] if bw is not None else (1 if k == 0 else 0)
                    if w <= 0: continue
                    j = bl[int(bi[v][k])]
                    M = world[j] @ inv[j]; acc += w * (M[:3, :3] @ P[v] + M[:3, 3]); tw += w
                if tw > 0: out[v] = acc / tw
            else:
                j = bl[int(bi[v][0])] if bi is not None and len(bl) > 1 else bl[0]
                M = world[j]; out[v] = M[:3, :3] @ P[v] + M[:3, 3]
        q = dict(p); q['pos'] = out; parts.append(q)
    return parts
