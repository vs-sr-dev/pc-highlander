"""jaguarkit.cart - Jaguar cartridge images (.j64, .rom).

A cartridge is mapped at $800000.  Its first $2000 bytes are not code: the
boot ROM reads a configuration long at $400 (the ROM's width and speed) and
the entry point at $404, and the region from $800 is the signed header the
boot ROM checks before it runs anything.

Checked on 76 retail cartridge images: every one has $04040404 at $400 and 75
enter at $802000 - the first byte after the header - with Battle Sphere Gold
the one exception, at $82F240.

Usage
    python -m jaguarkit.cart GAME.j64 [...]
    python -m jaguarkit.m68k GAME.j64 --cart          disassemble from the entry
"""

import os
import struct
import sys

BASE = 0x800000


class Cart:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        if len(self.data) < 0x2000:
            raise ValueError("%s: %d bytes is too small for a cartridge"
                             % (path, len(self.data)))
        self.config, self.entry, self.flags = struct.unpack_from(">III",
                                                                 self.data, 0x400)
        self.base = BASE

    @property
    def size(self):
        return len(self.data)

    def read(self, addr, n):
        o = addr - BASE
        return self.data[o:o + n]


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if not argv:
        print(__doc__)
        return 1
    for p in argv:
        c = Cart(p)
        ok = BASE + 0x2000 <= c.entry < BASE + c.size
        print("%-48s %8d bytes  config $%08X  entry $%06X%s" %
              (os.path.basename(p)[:48], c.size, c.config, c.entry,
               "" if ok else "  (outside the image)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
