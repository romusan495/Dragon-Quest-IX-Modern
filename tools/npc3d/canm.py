"""CGFX skeletal animation (CANM) reader + evaluator."""
import struct, collections
import numpy as np
import cgfx

SLOTS = ['sx', 'sy', 'sz', 'rx', 'ry', 'rz', 'rw', 'tx', 'ty', 'tz']
STATS = collections.Counter()

def read_curve(r, c):
    d = r.d
    start, end = r.f32(c), r.f32(c + 4)
    nseg = r.u32(c + 0x10); segs = []
    # segment list: count at +0x10, rel ptr at +0x14 -> array of rel ptrs? (observed: inline)
    p = r.rel(c + 0x14)
    for i in range(nseg):
        s = r.rel(p + 4 * i) if nseg > 1 else p
        segs.append(read_segment(r, s))
    return dict(start=start, end=end, segs=segs)

def read_segment(r, s):
    d = r.d
    sf, ef, fl = r.f32(s), r.f32(s + 4), r.u32(s + 8)
    q = (fl >> 5) & 7; interp = fl & 3
    STATS[(fl)] += 1
    keys = []
    if fl & 1 and False: pass
    if fl & 0x10:            # single constant value? (seen?)
        pass
    n = r.u32(s + 0xc)
    if q == 3:   # UnifiedHermite96: frame, value, slope
        o = s + 0x14
        for i in range(n):
            f, v, sl = struct.unpack_from('<3f', d, o + 12 * i)
            keys.append((f, v, sl, sl))
    elif q == 0:  # Hermite128
        o = s + 0x14
        for i in range(n):
            f, v, a, b = struct.unpack_from('<4f', d, o + 16 * i)
            keys.append((f, v, a, b))
    elif q == 6:  # StepLinear64: frame f32, value f32
        o = s + 0x14
        for i in range(n):
            f, v = struct.unpack_from('<2f', d, o + 8 * i)
            keys.append((f, v, 0, 0))
    else:
        # quantized: scale, offset, framescale after invduration
        sc, off, fsc = struct.unpack_from('<3f', d, s + 0x14)
        o = s + 0x20
        for i in range(n):
            if q == 4:   # UnifiedHermite48: u16 frame, u16 value, s16 slope
                f, v, sl = struct.unpack_from('<HHh', d, o + 6 * i)
                keys.append((f / 32.0 * fsc, v * sc + off, sl / 256.0 * sc, sl / 256.0 * sc))
            elif q == 5:  # UnifiedHermite32: u8 frame, u12 value, s12 slope
                w = struct.unpack_from('<I', d, o + 4 * i)[0]
                f = w & 0xff; v = (w >> 8) & 0xfff; sl = (w >> 20)
                if sl & 0x800: sl -= 0x1000
                keys.append((f * fsc, v * sc + off, sl / 32.0 * sc, sl / 32.0 * sc))
            elif q == 1:  # Hermite64
                w = struct.unpack_from('<Q', d, o + 8 * i)[0]
                f = w & 0xfff; v = (w >> 12) & 0xfffff; a = (w >> 32) & 0xffff; b = (w >> 48) & 0xffff
                a = a - 0x10000 if a & 0x8000 else a; b = b - 0x10000 if b & 0x8000 else b
                keys.append((f * fsc, v * sc + off, a / 256.0 * sc, b / 256.0 * sc))
            elif q == 2:  # Hermite48
                f, v, a, b = struct.unpack_from('<BHhh', d, o + 7 * i)  # unsure
                keys.append((f * fsc, v * sc + off, a / 256.0, b / 256.0))
            elif q == 7:  # StepLinear32: u12 frame, u20 value
                w = struct.unpack_from('<I', d, o + 4 * i)[0]
                keys.append(((w & 0xfff) * fsc, (w >> 12) * sc + off, 0, 0))
    return dict(start=sf, end=ef, flags=fl, q=q, interp=interp, keys=keys)

def read_track(r, c, n):
    start, end = r.f32(c), r.f32(c + 4)
    o = c + 0xc
    first = r.u32(o)
    cnt = 1 if first & 1 else int(round(end - start)) + 1
    st = 4 + 4 * n
    vals = [struct.unpack_from('<%df' % n, r.d, o + st * i + 4) for i in range(cnt)]
    flags = [r.u32(o + st * i) for i in range(cnt)]
    return dict(start=start, vals=np.array(vals), flags=flags)

def read_baked(r, e):
    ch = {'baked': True}
    for k, off, n in (('rot', 0xc, 4), ('tr', 0x10, 3), ('sc', 0x14, 3)):
        c = r.rel(e + off)
        if c: ch[k] = read_track(r, c, n)
    return ch

def load(path):
    d = open(path, 'rb').read()
    base = d.index(b'CGFX')
    r = cgfx.R(d)
    out = {}
    for name, a in r.dict(base + 0x14 + 8 + 8 * 9):
        frames = r.f32(a + 0x14); loop = r.u32(a + 0x10)
        bones = {}
        for bn, e in r.dict(a + 0x18):
            fl = r.u32(e); ty = r.u32(e + 8)
            if ty == 8:
                bones[bn] = read_baked(r, e); continue
            if ty != 5:
                STATS[('type', ty)] += 1; continue
            ch = {}
            for i, sl in enumerate(SLOTS):
                o = e + 0xc + 4 * i
                if fl & (1 << (16 + i)): continue          # inexistent -> bind pose
                if fl & (1 << (6 + i)): ch[sl] = r.f32(o)    # constant
                else:
                    c = r.rel(o)
                    if c: ch[sl] = read_curve(r, c)
            bones[bn] = ch
        out[name] = dict(frames=frames, loop=loop, bones=bones)
    return out

def ev_curve(c, t):
    if not isinstance(c, dict): return c
    t = min(max(t, c['start']), c['end'])
    seg = c['segs'][0]
    for s in c['segs']:
        if s['start'] <= t <= s['end']: seg = s
    k = seg['keys']
    if not k: return 0.0
    if t <= k[0][0]: return k[0][1]
    if t >= k[-1][0]: return k[-1][1]
    for i in range(len(k) - 1):
        f0, v0, _, o0 = k[i]; f1, v1, i1, _ = k[i + 1]
        if f0 <= t <= f1:
            if seg['q'] in (6, 7) or seg['interp'] == 1:
                if seg['interp'] == 0 and seg['q'] in (6, 7):  # step
                    return v0
                u = (t - f0) / (f1 - f0) if f1 > f0 else 0
                return v0 + (v1 - v0) * u
            dt = f1 - f0; u = (t - f0) / dt if dt else 0
            h00 = 2*u**3 - 3*u**2 + 1; h10 = u**3 - 2*u**2 + u; h01 = -2*u**3 + 3*u**2; h11 = u**3 - u**2
            return h00 * v0 + h10 * dt * o0 + h01 * v1 + h11 * dt * i1
    return k[-1][1]

def rot3(x, y, z):
    cx, sx, cy, sy, cz, sz = np.cos(x), np.sin(x), np.cos(y), np.sin(y), np.cos(z), np.sin(z)
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Rz @ Ry @ Rx

def local_mats(bind, anim, t):
    """bind: {bone: (s, r, t)} ; returns {bone: 4x4 local}"""
    out = {}
    for bn, (s, ro, tr) in bind.items():
        s, ro, tr = list(s), list(ro), list(tr)
        ch = anim['bones'].get(bn, {}) if anim else {}
        if ch.get('baked'):
            M = np.eye(4)
            def at(tk):
                v = tk['vals']; i = int(round(t - tk['start']))
                return v[min(max(i, 0), len(v) - 1)]
            if 'sc' in ch: s = at(ch['sc'])
            if 'tr' in ch: tr = at(ch['tr'])
            if 'rot' in ch:
                x, y, z, w = at(ch['rot'])
                Rq = np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)], [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
            else: Rq = rot3(*ro)
            M[:3, :3] = Rq * np.array(s); M[:3, 3] = tr
            out[bn] = M; continue
        for i, k in enumerate(('sx', 'sy', 'sz')):
            if k in ch: s[i] = ev_curve(ch[k], t)
        for i, k in enumerate(('rx', 'ry', 'rz')):
            if k in ch: ro[i] = ev_curve(ch[k], t)
        for i, k in enumerate(('tx', 'ty', 'tz')):
            if k in ch: tr[i] = ev_curve(ch[k], t)
        M = np.eye(4); M[:3, :3] = rot3(*ro) * np.array(s); M[:3, 3] = tr
        out[bn] = M
    return out
