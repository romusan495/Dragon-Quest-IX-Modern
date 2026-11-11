"""Level-5's packs in DQIX (GPC2 archives, e.g. /data/pack_lv5/chara_pc.gp2) and their compression
(the same as runtime/mapmarks.c l5_unpack / gpc_*)."""
import struct, zlib

def _lz(d, p, size):
    out = bytearray()
    while len(out) < size:
        f = d[p]; p += 1
        for b in range(8):
            if len(out) >= size: break
            if f & (0x80 >> b):
                x = d[p] << 8 | d[p + 1]; p += 2
                ln, dist = (x >> 12) + 3, (x & 0xfff) + 1
                for _ in range(ln):
                    if len(out) >= size: break
                    out.append(out[-dist])
            else:
                out.append(d[p]); p += 1
    return bytes(out)

def unpack(d, p=0):
    h = struct.unpack_from('<I', d, p)[0]; size, m = h >> 3, h & 7; p += 4
    if m == 0: return bytes(d[p:p + size])
    if m == 1: return _lz(d, p, size)
    if m in (2, 3):
        bits = 4 if m == 2 else 8
        tree = p; q = p + (d[p] + 1) * 2; pos = tree + 1; out = bytearray(); half = False
        while len(out) < size:
            w = struct.unpack_from('<I', d, q)[0]; q += 4
            for i in range(32):
                if len(out) >= size: break
                bit = (w >> (31 - i)) & 1; node = d[pos]
                nxt = (pos & ~1) + (node & 0x3f) * 2 + 2 + bit
                if node & (0x80 >> bit):
                    v = d[nxt]
                    if bits == 8: out.append(v)
                    elif not half: out.append(v & 15); half = True
                    else: out[-1] |= (v << 4) & 0xff; half = False
                    pos = tree + 1
                else: pos = nxt
        return bytes(out[:size])
    if m == 4:
        out = bytearray()
        while len(out) < size:
            f = d[p]; p += 1
            if f & 0x80: out += bytes([d[p]]) * ((f & 0x7f) + 3); p += 1
            else: n = (f & 0x7f) + 1; out += d[p:p + n]; p += n
        return bytes(out[:size])
    raise ValueError('level-5 method %d' % m)

def gpc(d):
    """GPC2 archive -> {name: bytes}"""
    assert d[:4] == b'GPC2'
    data = struct.unpack_from('<H', d, 10)[0] * 4
    lst = unpack(d, 0x14)
    names = unpack(d, struct.unpack_from('<H', d, 8)[0] * 4).split(b'\0')
    byc = {zlib.crc32(n) & 0xffffffff: n.decode('latin1') for n in names if n}
    out = {}
    for i in range(0, len(lst) - 11, 12):
        c, off = struct.unpack_from('<II', lst, i)
        at = data + (off & 0xffffff) * 4
        out[byc.get(c, '%08x' % c)] = (d, at)
    return out

def get(entry):
    d, at = entry; return unpack(d, at)
