"""DQVIII (3DS) townsperson model (.bch) -> .npc3 for the game (runtime/npc3d.c): idle and walk baked."""
import os, numpy as np
import bch, npc3d_export

ANIMS = (('idle', ('_tati',)), ('run', ('_aruki', '_hasiri')))

def export(path, out, step=2):
    m = bch.load(path)
    A = bch.animations(m)
    tex = dict(m['textures'])
    texnames = []
    parts = []
    for pi, p in enumerate(m['parts']):
        if not p['tex'] or p['tex'] not in tex: continue
        if p['tex'] not in texnames: texnames.append(p['tex'])
        parts.append((pi, texnames.index(p['tex']), 0, p['tris'].reshape(-1), p['uv']))
    nverts = sum(len(x[3]) for x in parts)
    frames, alist = [], []
    for want, keys in ANIMS:
        name = next((n for k in keys for n in sorted(A) if k in n), None)
        if name is None: continue
        a = A[name]; nf = max(1, int(a['frames'] // step)); first = len(frames)
        for k in range(nf):
            frames.append([p['pos'] for p in bch.pose(m, a, k * step)])
        alist.append((want, nf, first))
    if not frames:
        frames.append([p['pos'] for p in m['parts']]); alist.append(('bind', 1, 0))
    npc3d_export.write(out, alist, frames, texnames, tex, parts, nverts)
    return dict(verts=nverts, frames=len(frames), anims=alist, size=os.path.getsize(out))
