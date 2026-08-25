#!/usr/bin/env python3
"""disgpu - disassembler for the Atari Jaguar RISC (GPU / DSP) instruction set.

The ISA is fixed-width 16 bits: opcode[15:10], reg1[9:5], reg2[4:0].  Only MOVEI
is longer - it is followed by a 32-bit immediate stored low half first.  That
makes a linear sweep exact as long as the start is word aligned, which module
payloads always are.

Usage
    python tools/gpu/disgpu.py FILE --off 0x34c18 --len 4096 [--base 0xf03000]
                               [--dsp] [--out listing.gas]

The module header the 68000 side uses is a long holding the destination address
followed by a long holding the size; --header reads those two longs from the
file position given by --off and disassembles the payload that follows.
"""

import argparse
import struct
import sys

# ---------------------------------------------------------------------------
# opcode table.  Names marked (GPU) / (DSP) differ between the two cores.
# ---------------------------------------------------------------------------
OPS = [
    ("add", "rr"), ("addc", "rr"), ("addq", "nr"), ("addqt", "nr"),
    ("sub", "rr"), ("subc", "rr"), ("subq", "nr"), ("subqt", "nr"),
    ("neg", "r"), ("and", "rr"), ("or", "rr"), ("xor", "rr"),
    ("not", "r"), ("btst", "nr"), ("bset", "nr"), ("bclr", "nr"),
    ("mult", "rr"), ("imult", "rr"), ("imultn", "rr"), ("resmac", "r"),
    ("imacn", "rr"), ("div", "rr"), ("abs", "r"), ("sh", "rr"),
    ("shlq", "sl"), ("shrq", "sq"), ("sha", "rr"), ("sharq", "sq"),
    ("ror", "rr"), ("rorq", "nr"), ("cmp", "rr"), ("cmpq", "qr"),
    ("sat8", "r"), ("sat16", "r"), ("move", "rr"), ("moveq", "nr"),
    ("moveta", "rr"), ("movefa", "rr"), ("movei", "i"), ("loadb", "ld"),
    ("loadw", "ld"), ("load", "ld"), ("loadp", "ld"), ("load14n", "x"),
    ("load15n", "x"), ("storeb", "st"), ("storew", "st"), ("store", "st"),
    ("storep", "st"), ("store14n", "y"), ("store15n", "y"), ("movepc", "r"),
    ("jump", "j"), ("jr", "b"), ("mmult", "rr"), ("mtoi", "rr"),
    ("normi", "rr"), ("nop", "-"), ("load14r", "lr"), ("load15r", "lr"),
    ("store14r", "sr2"), ("store15r", "sr2"), ("sat24", "r"), ("pack", "p"),
]

DSP_OVERRIDE = {32: "subqmod", 33: "sat16s", 42: "sat32s", 48: "mirror",
                62: "illegal", 63: "illegal"}

CC = {0x00: "t", 0x01: "ne", 0x02: "eq", 0x04: "cc", 0x05: "hi", 0x06: "cc_eq",
      0x08: "cs", 0x09: "cs_ne", 0x0A: "cs_eq", 0x14: "pl", 0x15: "pl_ne",
      0x16: "pl_eq", 0x18: "mi", 0x19: "mi_ne", 0x1A: "mi_eq", 0x1F: "nv"}

# Jaguar addresses worth naming when they turn up in a MOVEI.
def sym(v):
    import os
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "..", "m68k"))
    try:
        import jagsyms
    except ImportError:
        return None
    return jagsyms.name_for(v)


def rname(n):
    return "r%d" % n


class GpuDisassembler:
    def __init__(self, data, base, dsp=False):
        self.data = data
        self.base = base
        self.dsp = dsp
        self.rows = []          # (addr, words, text, target)
        self.labels = set()

    def op_name(self, op):
        if self.dsp and op in DSP_OVERRIDE:
            return DSP_OVERRIDE[op]
        return OPS[op][0]

    def decode(self):
        i = 0
        n = len(self.data)
        while i + 1 < n:
            addr = self.base + i
            w = struct.unpack_from(">H", self.data, i)[0]
            op = w >> 10
            r1 = (w >> 5) & 0x1F
            r2 = w & 0x1F
            name = self.op_name(op)
            kind = OPS[op][1]
            words = [w]
            target = None

            if kind == "rr":
                text = "%-8s %s, %s" % (name, rname(r1), rname(r2))
            elif kind == "r":
                text = "%-8s %s" % (name, rname(r2))
            elif kind == "nr":
                q = 32 if (r1 == 0 and name in ("addq", "addqt", "subq",
                                                "subqt")) else r1
                text = "%-8s #%d, %s" % (name, q, rname(r2))
            elif kind == "sl":                       # shlq encodes 32 - n
                q = 32 - r1 if r1 else 32
                text = "%-8s #%d, %s" % (name, q, rname(r2))
            elif kind == "sq":                       # shrq / sharq encode n
                text = "%-8s #%d, %s" % (name, r1 if r1 else 32, rname(r2))
            elif kind == "qr":                       # cmpq: signed 5 bit
                q = r1 - 32 if r1 & 0x10 else r1
                text = "%-8s #%d, %s" % (name, q, rname(r2))
            elif kind == "i":                        # movei #imm32
                lo = struct.unpack_from(">H", self.data, i + 2)[0]
                hi = struct.unpack_from(">H", self.data, i + 4)[0]
                imm = (hi << 16) | lo
                words += [lo, hi]
                s = sym(imm)
                text = "%-8s #$%08x, %s%s" % (name, imm, rname(r2),
                                              ("   ; " + s) if s else "")
            elif kind == "ld":
                text = "%-8s (%s), %s" % (name, rname(r1), rname(r2))
            elif kind == "st":
                text = "%-8s %s, (%s)" % (name, rname(r2), rname(r1))
            elif kind == "x":                        # load (r14/r15+n),Rd
                reg = "r14" if op == 43 else "r15"
                q = 32 if r1 == 0 else r1
                text = "%-8s (%s+%d), %s" % ("load", reg, q, rname(r2))
            elif kind == "y":                        # store Rs,(r14/r15+n)
                reg = "r14" if op == 49 else "r15"
                q = 32 if r1 == 0 else r1
                text = "%-8s %s, (%s+%d)" % ("store", rname(r2), reg, q)
            elif kind == "lr":
                reg = "r14" if op == 58 else "r15"
                text = "%-8s (%s+%s), %s" % ("load", reg, rname(r1), rname(r2))
            elif kind == "sr2":
                reg = "r14" if op == 60 else "r15"
                text = "%-8s %s, (%s+%s)" % ("store", rname(r2), reg, rname(r1))
            elif kind == "j":                        # jump cc,(Rs)
                text = "%-8s %s, (%s)" % ("jump", CC.get(r2, "cc$%02x" % r2),
                                          rname(r1))
            elif kind == "b":                        # jr cc,offset
                off = r1 - 32 if r1 & 0x10 else r1
                target = addr + 2 + off * 2
                self.labels.add(target)
                text = "%-8s %s, $%05x" % ("jr", CC.get(r2, "cc$%02x" % r2),
                                           target)
            elif kind == "p":
                text = "%-8s %s" % ("pack" if r1 == 0 else "unpack", rname(r2))
            elif kind == "-":
                text = "nop"
            else:
                text = "%-8s ???" % name

            self.rows.append((addr, words, text, target))
            i += 2 * len(words)

    def render(self, fh):
        for addr, words, text, _ in self.rows:
            lab = "L_%05x:" % addr if addr in self.labels else ""
            fh.write("%-12s %05x  %-14s %s\n" %
                     (lab, addr, " ".join("%04x" % w for w in words), text))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--off", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--len", dest="length", type=lambda s: int(s, 0))
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0xF03000)
    ap.add_argument("--header", action="store_true",
                    help="read dest/size longs at --off and use them")
    ap.add_argument("--dsp", action="store_true")
    ap.add_argument("--out")
    args = ap.parse_args()

    raw = open(args.file, "rb").read()
    off, base, length = args.off, args.base, args.length
    if args.header:
        base, length = struct.unpack_from(">II", raw, off)
        off += 8
    data = raw[off:off + length] if length else raw[off:]

    d = GpuDisassembler(data, base, dsp=args.dsp)
    d.decode()
    fh = open(args.out, "w") if args.out else sys.stdout
    fh.write("; %s  file offset $%x, %d bytes, loads at $%08x\n" %
             (args.file, off, len(data), base))
    d.render(fh)
    if args.out:
        fh.close()
        print("wrote %s: %d instructions" % (args.out, len(d.rows)))


if __name__ == "__main__":
    main()
