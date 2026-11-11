import numpy as np
import canm

def world_from_local(model, L):
    """L: {bone name: 4x4 local}; returns {bone index: 3x4 world}"""
    bones = model['bones']
    byidx = {j: (n, p) for j, n, p, mt in bones}
    W = {}
    def w(i):
        if i in W: return W[i]
        n, p = byidx[i]
        M = L[n]
        if p >= 0: M = w(p) @ M
        W[i] = M
        return M
    for j in byidx: w(j)
    return {i: M[:3] for i, M in W.items()}

def pose_model(model, anim, t):
    L = canm.local_mats(model['bind'], anim, t)
    W = world_from_local(model, L)
    B = model['bindworld']
    inv = {}
    for i, M in B.items():
        M4 = np.vstack([M, [0, 0, 0, 1]]); inv[i] = np.linalg.inv(M4)
    parts = []
    for p in model['parts']:
        raw = p['raw']; n = len(raw)
        P = raw.copy(); off = p['off']
        bl = p['blist']
        if bl:
            if p['skin'] != 2:
                bi = p['bidx'][:, 0].astype(int) if p['bidx'] is not None else np.zeros(n, int)
                for k in set(bi.tolist()):
                    Mw = W.get(bl[k])
                    if Mw is None: continue
                    sel = bi == k
                    P[sel] = raw[sel] @ Mw[:, :3].T + Mw[:, 3]
            else:
                acc = np.zeros((n, 3)); tw = np.zeros(n)
                for k in range(p['bidx'].shape[1]):
                    wk = p['bwt'][:, k] if p['bwt'] is not None else (np.ones(n) if k == 0 else np.zeros(n))
                    bi = p['bidx'][:, k].astype(int)
                    for b in set(bi.tolist()):
                        sel = (bi == b) & (wk > 0)
                        if not sel.any(): continue
                        Mw = W.get(bl[b])
                        if Mw is None: continue
                        S = np.vstack([Mw, [0, 0, 0, 1]]) @ inv[bl[b]]
                        acc[sel] += wk[sel, None] * ((raw[sel] + off) @ S[:3, :3].T + S[:3, 3])   # (model space: the shape's offset included)
                        tw[sel] += wk[sel]
                ok = tw > 0
                P[ok] = acc[ok] / tw[ok, None] - off
        q = dict(p); q['pos'] = P + p['off']
        parts.append(q)
    m = dict(model); m['parts'] = parts
    return m
