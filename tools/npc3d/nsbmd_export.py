"""DQIX character (/data/chara_sub/<name>.chr in the player's own ROM) -> .npc3 for the game
(runtime/npc3d.c): stand and walk baked, one frame per animation frame."""
import os, numpy as np
import nds, nsbmd, npc3d_export

ANIMS = (('idle', 'stand.nsbca'), ('run', 'walk.nsbca'))

def export(rom, name, out):
    d = nds.rom_file(rom, '/data/chara_sub/%s.chr' % name)
    if d is None: raise ValueError('no /data/chara_sub/%s.chr' % name)
    files = nds.narc(d)
    m = nsbmd.Model(next(v for k, v in files.items() if k.endswith('.nsbmd')))
    tex, texnames, keep = {}, [], []
    for mi in sorted(m.run()):
        t = m.texture(mi)
        if t is None: continue
        n = '%d' % mi; tex[n] = t[0]; texnames.append(n); keep.append(mi)
    def frame(local):
        geo = m.run(local); P = []; meta = []
        for ti, mi in enumerate(keep):
            img, (w, h), (fs, ft), _, _ = m.texture(mi); info = m.matinfo(mi)
            for pos, uv, tris in geo.get(mi, []):
                if not len(tris): continue
                sc, tr = info['sc'], info['tr']
                u = np.stack([uv[:, 0] * sc[0] + tr[0] * w, uv[:, 1] * sc[1] + tr[1] * h], 1) / [w * (2 if fs else 1), h * (2 if ft else 1)]
                P.append(pos); meta.append((ti, tris.reshape(-1), u))
        return P, meta
    frames, alist = [], []
    for want, fn in ANIMS:
        if fn not in files: continue
        a = nsbmd.Anim(files[fn]); first = len(frames)
        for k in range(max(1, a.frames)):
            P, meta = frame(nsbmd.posed(m, a, k)); frames.append(P)
        alist.append((want, max(1, a.frames), first))
    if not frames:
        P, meta = frame(None); frames.append(P); alist.append(('bind', 1, 0))
    _, meta = frame(None)
    lo = min(float(p[:, 1].min()) for p in frames[0])          # feet at y 0
    frames = [[p - (0, lo, 0) for p in fr] for fr in frames]
    parts = [(pi, ti, 0, idx, uv.astype(np.float32)) for pi, (ti, idx, uv) in enumerate(meta)]
    nverts = sum(len(x[3]) for x in parts)
    npc3d_export.write(out, alist, frames, texnames, tex, parts, nverts)
    return dict(verts=nverts, frames=len(frames), anims=alist, size=os.path.getsize(out))
