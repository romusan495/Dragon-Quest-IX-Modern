"""The player's Dragon Quest VII / VIII (3DS) files, as BUILD.bat finds them in
rom/3DS: a decrypted ROM (.3ds, .cci, .cxi or .cia) or a folder already
extracted (its romfs). Only the folders the townspeople need are copied out.

usage: rom3ds.py <rom/3DS folder> <out folder>   (prints "dq7 <dir>" / "dq8 <dir>")
"""
import sys, os, mmap, struct

NEED = {'dq7': 'CHARACTER', 'dq8': 'data/Model/npc'}       # what make_all.py reads
MEDIA = 0x200


class Error(Exception):
    pass


def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def u64(b, o): return struct.unpack_from('<Q', b, o)[0]
def align(x, a): return (x + a - 1) // a * a


def ncch_romfs(b, base):
    """offset of the romfs (IVFC) of the NCCH at base"""
    if b[base + 0x100:base + 0x104] != b'NCCH': raise Error('not a 3DS program (encrypted?)')
    if not b[base + 0x18F] & 4: raise Error('encrypted: decrypt it first (GodMode9: "Decrypt file")')
    off, size = u32(b, base + 0x1B0) * MEDIA, u32(b, base + 0x1B4) * MEDIA
    if not off or not size: raise Error('no romfs')
    return base + off


def romfs_of(b):
    """offset of the romfs in a .3ds/.cci (NCSD), .cxi (NCCH) or .cia file"""
    if b[0x100:0x104] == b'NCSD': return ncch_romfs(b, u32(b, 0x120) * MEDIA)
    if b[0x100:0x104] == b'NCCH': return ncch_romfs(b, 0)
    hdr = u32(b, 0)
    if hdr == 0x2020:                                          # CIA: header, certificates, ticket, TMD, then the contents
        o = align(hdr, 64) + align(u32(b, 8), 64) + align(u32(b, 0xC), 64) + align(u32(b, 0x10), 64)
        return ncch_romfs(b, o)
    raise Error('not a 3DS ROM')


class RomFS:
    def __init__(self, b, at):
        if b[at:at + 4] != b'IVFC': raise Error('romfs not found')
        bs3 = 1 << u32(b, at + 0x4C)
        self.b, self.l3 = b, at + align(0x60 + u32(b, at + 8), bs3)
        h = self.l3
        self.dmeta, self.fmeta, self.data = h + u32(b, h + 0x0C), h + u32(b, h + 0x1C), h + u32(b, h + 0x24)

    def _name(self, o, n): return bytes(self.b[o:o + n]).decode('utf-16-le')

    def dirs(self, d):
        o = u32(self.b, self.dmeta + d + 8)
        while o != 0xFFFFFFFF:
            yield self._name(self.dmeta + o + 0x18, u32(self.b, self.dmeta + o + 0x14)), o
            o = u32(self.b, self.dmeta + o + 4)

    def files(self, d):
        o = u32(self.b, self.dmeta + d + 0xC)
        while o != 0xFFFFFFFF:
            e = self.fmeta + o
            yield self._name(e + 0x20, u32(self.b, e + 0x1C)), self.data + u64(self.b, e + 8), u64(self.b, e + 0x10)
            o = u32(self.b, e + 4)

    def find(self, path):
        d = 0
        for part in path.split('/'):
            d = next((o for n, o in self.dirs(d) if n.lower() == part.lower()), None)
            if d is None: return None
        return d

    def extract(self, d, out):
        os.makedirs(out, exist_ok=True)
        for n, at, size in self.files(d):
            with open(os.path.join(out, n), 'wb') as f: f.write(self.b[at:at + size])
        for n, o in self.dirs(d): self.extract(o, os.path.join(out, n))


def from_rom(path, out):
    """{game: folder} for one ROM file (copied out to out/<game> once per ROM)"""
    with open(path, 'rb') as f:
        b = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        fs = RomFS(b, romfs_of(b))
        found = {}
        for game, sub in NEED.items():
            d = fs.find(sub)
            if d is None: continue
            dst = os.path.join(out, game)
            stamp = os.path.join(dst, '.from')
            mark = '%s %d %d' % (os.path.basename(path), os.path.getsize(path), int(os.path.getmtime(path)))
            if not (os.path.exists(stamp) and open(stamp).read() == mark):
                print('   %s: copying %s out of %s' % (game.upper(), sub, os.path.basename(path)), file=sys.stderr)
                fs.extract(d, dst)
                open(stamp, 'w').write(mark)
            found[game] = dst
        if not found: raise Error('neither Dragon Quest VII nor VIII')
        return found


def from_folder(path):
    """{game: folder} for a folder already extracted (the romfs, or a folder above it)"""
    found = {}
    for root, dirs, _ in os.walk(path):
        for game, sub in NEED.items():
            if game not in found and os.path.isdir(os.path.join(root, *sub.split('/'))): found[game] = root
        if os.path.relpath(root, path).count(os.sep) >= 3: dirs[:] = []
    return found


def find_all(folder, out):
    found = {}
    if not os.path.isdir(folder): return found
    for n in sorted(os.listdir(folder)):
        p = os.path.join(folder, n)
        try:
            if os.path.isdir(p): got = from_folder(p)
            elif n.lower().endswith(('.3ds', '.cci', '.cxi', '.cia')): got = from_rom(p, out)
            else: continue
        except (Error, OSError, ValueError, struct.error) as e:
            print('   %s: not used (%s)' % (n, e), file=sys.stderr); continue
        for g, d in got.items(): found.setdefault(g, d)
    return found


if __name__ == '__main__':
    if len(sys.argv) != 3: sys.exit(__doc__)
    for g, d in find_all(sys.argv[1], sys.argv[2]).items(): print(g, d)
