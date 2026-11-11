"""Export one DQVII character (bcmdl + pack) to a simple baked file for the runtime.
usage: npc3d_export.py <model.bcmdl> <anims.pack> <out.bin> [fps_step]
Format (little endian):
  'NPC3' u32 version=2
  u32 nanims ; per anim: char name[16], u32 nframes, u32 first_frame (index into frames)
  u32 nframes_total, u32 nverts
  u32 ntex ; per tex: u32 w, h, then w*h*4 RGBA
  u32 nparts ; per part: u32 tex, u32 layer, u32 first_vert, u32 nverts (triangles list, 3 per tri)
  u32 nuniq, f32 scale ; u16 index[nverts] (into the unique vertices)
  uv: nuniq * 2 f32 (0..1, v down)
  frames: nframes_total * nuniq * 3 i16 (times scale: model units, y up)
"""
import sys, struct, os
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import cgfx, canm, pose

def head_height(m, an):
    a = next((v for k, v in an.items() if k.endswith('_idle')), None)
    L = canm.local_mats(m['bind'], a, 0); W = pose.world_from_local(m, L)
    names = {j: n for j, n, p, mt in m['bones']}
    for j, M in W.items():
        if names[j].lower() == 'head': return float(M[1, 3])
    return None

def arm_setup(m, pm, an):
    """the arms' bones (posed like pm) and which side's arm each vertex is on"""
    a = next((v for k, v in an.items() if k.endswith('_idle')), None)
    W = pose.world_from_local(m, canm.local_mats(m['bind'], a, 0))
    names = {j: n for j, n, p, mt in m['bones']}; at = {n: W[j][:, 3] for j, n in names.items() if j in W}
    arms = {}
    for side, s in ((+1, 'L'), (-1, 'R')):
        if not all(s + b in at for b in ('Bicep', 'Forearm', 'Hand')): return None
        sh, el, wr = at[s + 'Bicep'], at[s + 'Forearm'], at[s + 'Hand']
        arms[1 if wr[0] > 0 else -1] = [sh, el, wr, wr + (wr - el) * 0.5]
    for p in pm['parts']:
        if p.get('bidx') is None: p['arm'] = None; continue
        bl = list(p['blist']); idx = p['bidx'].astype(int)
        wt = p['bwt'] if p.get('bwt') is not None else np.ones(idx.shape)
        dom = [names.get(bl[i[k]], '') for i, k in zip(idx, np.argmax(wt, 1))]
        p['arm'] = np.array([(1 if d[0] == 'L' else -1) if d[1:] in ('Bicep', 'Forearm', 'Hand') else 0 for d in dom])
        if not any(at['LHand'][0] > 0 for _ in [0]): p['arm'] = -p['arm']
    return arms

def export(mdl, pack, out, step=2, anims=('idle', 'run'), front=None, back=None, pic_neck=0.34):
    models, tex = cgfx.load(mdl)
    m = models[0]
    an = canm.load(pack) if pack and os.path.exists(pack) else {}
    if front is not None:                      # repainted with the DQIX townsperson's colours
        import paint
        a = next((v for k, v in an.items() if k.endswith('_idle')), None)
        pm = pose.pose_model(m, a, 0)
        arms = arm_setup(m, pm, an); nk = head_height(m, an)
        tex = dict(tex)
        for base in sorted({p['texs'][0] for p in m['parts'] if p.get('texs') and p['texs'][0] in tex}):   # every surface texture (not the eyes, mouth)
            tex[base] = paint.paint(pm, tex, front, front if back is None else back, base, neck=nk, pic_neck=pic_neck, arms=arms)
    texnames = []
    def texid(n):
        if n not in texnames: texnames.append(n)
        return texnames.index(n)
    # parts expanded: (tex, layer, tri index array into part verts, uv)
    parts = []
    for pi, p in enumerate(m['parts']):
        texs = p.get('texs') or []
        for layer, tn in enumerate(texs[:3]):
            if tn is None or tn not in tex: continue
            uv = p['uvs'][layer] if p['uvs'][layer] is not None else p['uv']
            parts.append((pi, texid(tn), layer, p['tris'].reshape(-1), uv))
    nverts = sum(len(x[3]) for x in parts)
    frames = []; alist = []
    names = sorted(an)
    for want in anims:
        hit = [n for n in names if n.endswith('_' + want)]
        if not hit: continue
        a = an[hit[0]]
        nf = max(1, int(a['frames'] // step))
        first = len(frames)
        for k in range(nf):
            pm = pose.pose_model(m, a, k * step)
            frames.append([p['pos'] for p in pm['parts']])
        alist.append((want, nf, first))
    if not frames:
        frames.append([p['pos'] for p in m['parts']]); alist.append(('bind', 1, 0))
    # each part's own vertices once (u16 indices into them), positions as 16-bit steps of `scale`
    uniq, index, base = [], [], 0
    for pi, ti, layer, idx, uv in parts:
        u, inv = np.unique(idx, return_inverse=True)
        uniq.append((pi, u, uv)); index.append(inv + base); base += len(u)
    index = np.concatenate(index)
    uvs = np.concatenate([uv[u] for pi, u, uv in uniq]).astype(np.float32); uvs[:, 1] = 1 - uvs[:, 1]
    fpos = [np.concatenate([fr_parts[pi][u] for pi, u, uv in uniq]) for fr_parts in frames]
    scale = max(float(np.abs(np.array(fpos)).max()), 1e-6) / 32767
    with open(out, 'wb') as f:
        f.write(b'NPC3' + struct.pack('<I', 2))
        f.write(struct.pack('<I', len(alist)))
        for n, nf, first in alist: f.write(n.encode()[:15].ljust(16, b'\0') + struct.pack('<II', nf, first))
        f.write(struct.pack('<II', len(frames), nverts))
        f.write(struct.pack('<I', len(texnames)))
        for n in texnames:
            t = tex[n]; h, w = t.shape[:2]
            f.write(struct.pack('<II', w, h)); f.write(np.ascontiguousarray(t, np.uint8).tobytes())
        f.write(struct.pack('<I', len(parts)))
        first = 0
        for pi, ti, layer, idx, uv in parts:
            f.write(struct.pack('<IIII', ti, layer, first, len(idx))); first += len(idx)
        f.write(struct.pack('<If', base, scale))
        f.write(index.astype('<u2').tobytes())
        f.write(uvs.tobytes())
        for fp in fpos: f.write(np.round(fp / scale).astype('<i2').tobytes())
    return dict(verts=nverts, frames=len(frames), tex=len(texnames), anims=alist, size=os.path.getsize(out))

if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('model'); ap.add_argument('pack'); ap.add_argument('out')
    ap.add_argument('--step', type=int, default=2)
    ap.add_argument('--front', help='RGBA picture of the DQIX townsperson from the front: repaint with its colours')
    ap.add_argument('--back', help='the same from the back')
    ap.add_argument('--neck', type=float, default=0.34, help="the pictures' neck (0 top, 1 feet)")
    a = ap.parse_args()
    from PIL import Image
    pic = lambda f: np.array(Image.open(f).convert('RGBA')) if f else None
    print(export(a.model, a.pack, a.out, a.step, front=pic(a.front), back=pic(a.back), pic_neck=a.neck))
