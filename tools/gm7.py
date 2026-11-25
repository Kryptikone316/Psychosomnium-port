"""Minimal Game Maker 6.1/7.0 exe data reader (format follows LateralGM's GmFileReader)."""
import struct, zlib

def make_decode_table(seed):
    a = 6 + (seed % 250); b = seed // 250
    enc = list(range(256))
    for i in range(1, 10001):
        j = 1 + ((i * a + b) % 254)
        enc[j], enc[j + 1] = enc[j + 1], enc[j]
    dec = [0] * 256
    for i in range(1, 256):
        dec[enc[i]] = i
    return dec

class Stream:
    def __init__(self, data, pos=0):
        self.d = data; self.p = pos; self.table = None; self.pos = 0
    def raw(self, n):
        b = self.d[self.p:self.p + n]
        if len(b) != n: raise EOFError('eof at %d' % self.p)
        self.p += n
        if self.table is not None:
            t = self.table
            b = bytes((t[x] - self.pos - i) & 0xFF for i, x in enumerate(b))
        self.pos += n
        return b
    def i4(self): return struct.unpack('<i', self.raw(4))[0]
    def bool(self): return self.i4() != 0
    def dbl(self): return struct.unpack('<d', self.raw(8))[0]
    def str(self): return self.raw(self.i4()).decode('latin-1')
    def skip(self, n): self.raw(n)
    def zlib(self):
        n = self.i4(); return zlib.decompress(self.raw(n))
    def set_seed(self, s):
        self.table = make_decode_table(s) if s >= 0 else None
