"""Make every townsperson's 3D model for the game (runtime/npc3d.c).

usage: make_all.py <DQIX .nds> <DQVII romfs (or its CHARACTER folder)> <game npc3d folder>

For each line of townsfolk.txt: the DQIX townsperson's pictures (front and
back, from its sprite sheet in the DQIX ROM), the DQVII character (model and
animations, from the player's own DQVII romfs), repainted with the pictures'
colours -> <out>/<model>-<type>.npc3, and <out>/map.txt. Nothing made here
is ever kept in the project (it comes from the games)."""
import sys, os, struct
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lz11, dqspr, npc3d_export

VIEWS = {16: (1, 4), 8: (0, 1)}       # sprite sheet frames (by how many it has): standing, from the front / from behind

def rom_files(rom):
    fnt, _, fat, _ = struct.unpack_from('<IIII', rom, 0x40)
    out = {}
    def walk(did, path):
        e = fnt + (did & 0xfff) * 8
        sub, fid = struct.unpack_from('<IH', rom, e); p = fnt + sub
        while True:
            l = rom[p]; p += 1
            if l == 0: break
            name = rom[p:p + (l & 0x7f)].decode('latin1'); p += l & 0x7f
            if l & 0x80:
                walk(struct.unpack_from('<H', rom, p)[0], path + name + '/'); p += 2
            else:
                s, e2 = struct.unpack_from('<II', rom, fat + fid * 8)
                out[path + name] = rom[s:e2]; fid += 1
    walk(0xf000, '')
    return out

def crop(pic):
    a = pic[..., 3] > 0
    ys, xs = np.nonzero(a)
    return pic[ys.min():ys.max() + 1, xs.min():xs.max() + 1]

def neck(pic):
    """the picture's neck (0 top, 1 feet): its narrowest row between head and shoulders"""
    a = pic[..., 3] > 0; h = a.shape[0]
    w = a.sum(1)
    lo, hi = int(h * 0.25), int(h * 0.45)
    y = lo + int(np.argmin(w[lo:hi + 1]))
    return min(max(y / h, 0.28), 0.42)

def unlz(path):
    d = open(path, 'rb').read()
    return lz11.lz11(d) if d[:1] == b'\x11' else d

def main(rom_path, chara, out):
    if os.path.isdir(os.path.join(chara, 'CHARACTER')): chara = os.path.join(chara, 'CHARACTER')   # the romfs itself
    files = rom_files(open(rom_path, 'rb').read())
    os.makedirs(out, exist_ok=True)
    table = [l.split() for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'townsfolk.txt')) if l.strip() and not l.startswith('#')]
    tmp = os.path.join(out, '.tmp'); os.makedirs(tmp, exist_ok=True)
    lines = []
    for typ, mdl in table:
        spr = files.get('data/ani/%s.spr' % typ)
        mp, pp = os.path.join(chara, mdl + '.bcmdl.lz'), os.path.join(chara, mdl + '.pack.lz')
        if spr is None or not os.path.exists(mp) or not os.path.exists(pp):
            print('%s: skipped (missing %s)' % (typ, 'sprite' if spr is None else mdl)); continue
        fr = dqspr.read(spr)
        if len(fr) not in VIEWS:
            print('%s: skipped (a sheet of %d pictures)' % (typ, len(fr))); continue
        front, back = (crop(fr[i]) for i in VIEWS[len(fr)])
        m, p = os.path.join(tmp, mdl + '.bcmdl'), os.path.join(tmp, mdl + '.pack')
        if not os.path.exists(m): open(m, 'wb').write(unlz(mp))
        if not os.path.exists(p): open(p, 'wb').write(unlz(pp))
        name = '%s-%s' % (mdl, typ)
        try:
            r = npc3d_export.export(m, p, os.path.join(out, name + '.npc3'), front=front, back=back, pic_neck=neck(front))
        except Exception as e:
            print('%s: failed (%s)' % (typ, e)); continue
        lines.append('%s %s\n' % (typ, name))
        print('%s: %s, %d vertices, %d frames' % (typ, name, r['verts'], r['frames']))
    open(os.path.join(out, 'map.txt'), 'w').writelines(lines)
    for f in os.listdir(tmp): os.remove(os.path.join(tmp, f))
    os.rmdir(tmp)
    print('%d townspeople' % len(lines))

if __name__ == '__main__':
    main(*sys.argv[1:4])
