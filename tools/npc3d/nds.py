"""Files of a Nintendo DS ROM (.nds) and of NARC archives inside it."""
import struct

def rom_file(rom, path):
    """bytes of the ROM's file `path` (e.g. '/data/chara_sub/s066.chr'), or None"""
    fnt, _, fat, _ = struct.unpack_from('<IIII', rom, 0x40)
    did = 0xf000
    parts = [p for p in path.split('/') if p]
    for k, want in enumerate(parts):
        e = fnt + (did & 0xfff) * 8
        p = fnt + struct.unpack_from('<I', rom, e)[0]; fid = struct.unpack_from('<H', rom, e + 4)[0]
        found = False
        while rom[p]:
            l = rom[p]; n = rom[p + 1:p + 1 + (l & 0x7f)].decode('latin1'); p += 1 + (l & 0x7f)
            if l & 0x80:
                sub = struct.unpack_from('<H', rom, p)[0]; p += 2
                if n == want and k < len(parts) - 1: did = sub; found = True; break
            else:
                if n == want and k == len(parts) - 1:
                    s, t = struct.unpack_from('<II', rom, fat + fid * 8); return rom[s:t]
                fid += 1
        if not found: return None
    return None

def narc(d):
    """NARC archive -> {name: bytes}"""
    assert d[:4] == b'NARC'
    o = struct.unpack_from('<H', d, 0xc)[0]
    n = struct.unpack_from('<I', d, o + 8)[0]; fat = [struct.unpack_from('<II', d, o + 12 + 8 * i) for i in range(n)]
    o += struct.unpack_from('<I', d, o + 4)[0]
    fnt = o; o += struct.unpack_from('<I', d, o + 4)[0]
    img = o + 8
    names = []; p = fnt + 8 + struct.unpack_from('<I', d, fnt + 8)[0]
    while len(names) < n and p < o and d[p]:
        l = d[p]; p += 1; names.append(d[p:p + (l & 0x7f)].decode('latin1')); p += l & 0x7f
        if l & 0x80: p += 2
    names += ['%d' % i for i in range(len(names), n)]
    return {names[i]: d[img + s:img + e] for i, (s, e) in enumerate(fat)}
