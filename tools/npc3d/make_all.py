"""Make the townspeople's 3D models for the game (runtime/npc3d.c).

usage: make_all.py <DQVII romfs (or its CHARACTER folder) | -> <DQVIII romfs (or its data/Model/npc folder) | -> <game npc3d folder> [DQIX ROM]

For each line of townsfolk.txt whose game was given: the DQVII, DQVIII or DQIX
character (model and animations, from the player's own romfs, with its own
colours) -> <out>/<model>-<type>.npc3, and <out>/map.txt. Nothing made here is
ever kept in the project (it comes from the games)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lz11, npc3d_export, bch_export, nsbmd_export

def unlz(path):
    d = open(path, 'rb').read()
    return lz11.lz11(d) if d[:1] == b'\x11' else d

def folder(path, sub):
    """the game's model folder: the romfs itself or that folder"""
    if not path or path == '-' or not os.path.isdir(path): return None
    return os.path.join(path, sub) if os.path.isdir(os.path.join(path, sub)) else path

def main(dq7, dq8, out, dq9=None):
    src = {'dq7': folder(dq7, 'CHARACTER'), 'dq8': folder(dq8, os.path.join('data', 'Model', 'npc'))}
    if dq9 and os.path.isfile(dq9): src['dq9'] = open(dq9, 'rb').read()
    os.makedirs(out, exist_ok=True)
    table = [l.split() for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'townsfolk.txt')) if l.strip() and not l.startswith('#')]
    tmp = os.path.join(out, '.tmp'); os.makedirs(tmp, exist_ok=True)
    lines = []
    for typ, game, mdl in table:
        d = src.get(game)
        if d is None: print('%s: skipped (no %s folder)' % (typ, game.upper())); continue
        name = '%s-%s' % (mdl, typ)
        try:
            if game == 'dq7':
                mp, pp = os.path.join(d, mdl + '.bcmdl.lz'), os.path.join(d, mdl + '.pack.lz')
                if not os.path.exists(mp) or not os.path.exists(pp): print('%s: skipped (missing %s)' % (typ, mdl)); continue
                m, p = os.path.join(tmp, mdl + '.bcmdl'), os.path.join(tmp, mdl + '.pack')
                open(m, 'wb').write(unlz(mp)); open(p, 'wb').write(unlz(pp))
                r = npc3d_export.export(m, p, os.path.join(out, name + '.npc3'))
            elif game == 'dq9':
                r = nsbmd_export.export(d, mdl, os.path.join(out, name + '.npc3'))
            else:
                mp = os.path.join(d, mdl + '.bch')
                if not os.path.exists(mp): print('%s: skipped (missing %s)' % (typ, mdl)); continue
                r = bch_export.export(mp, os.path.join(out, name + '.npc3'))
        except Exception as e:
            print('%s: failed (%s)' % (typ, e)); continue
        lines.append('%s %s\n' % (typ, name))
        print('%s: %s (%s), %d vertices, %d frames' % (typ, name, game.upper(), r['verts'], r['frames']))
    open(os.path.join(out, 'map.txt'), 'w').writelines(lines)
    for f in os.listdir(tmp): os.remove(os.path.join(tmp, f))
    os.rmdir(tmp)

if __name__ == '__main__':
    if len(sys.argv) not in (4, 5): sys.exit(__doc__)
    main(*sys.argv[1:])
