"""jaguarkit.m68k - recursive-descent 68000 disassembler for Jaguar code.

Built on capstone (the one dependency outside bigpemu/).  Names the Jaguar's
hardware registers and, for CD games, the CD BIOS jump table; follows calls
and branches, labels every referenced address and prints the gaps as data.

Usage
    python -m jaguarkit.m68k FILE --base 0x4000 [--off 0] [--len N]
                             [--entry 0x4000 ...] [--out listing.asm] [--linear]
    python -m jaguarkit.m68k GAME.j64 --cart             base and entry from
                                                         the cartridge header
    python -m jaguarkit.m68k DISC.cue --boot             the CD boot image, at
                                                         its load address

With no --entry the base address is the sole entry point.  --linear adds a
plain sweep of everything the recursive pass did not reach, which is useful
for a first look at a binary whose entry points are unknown.
"""

import argparse
import os
import re
import sys

from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

from . import hw

HEX = re.compile(r"\$([0-9a-fA-F]+)")

CONDITIONAL = re.compile(r"^(b(ra|sr|hi|ls|cc|cs|ne|eq|vc|vs|pl|mi|ge|lt|gt|le)"
                         r"|db[a-z]{2,3}|jsr|jmp)(\.[bwsl])?$")


class Disassembler:
    def __init__(self, data, base, cd=True):
        self.cd = cd
        self.data = data
        self.base = base
        self.end = base + len(data)
        self.md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
        self.md.detail = False
        self.insns = {}        # addr -> capstone instruction
        self.xrefs = {}        # addr -> set of referring addresses
        self.subs = set()      # addresses reached by jsr / bsr

    # -- helpers -------------------------------------------------------------
    def inside(self, addr):
        return self.base <= addr < self.end

    def off(self, addr):
        return addr - self.base

    def note_ref(self, target, src):
        self.xrefs.setdefault(target, set()).add(src)

    # -- decoding ------------------------------------------------------------
    def decode_one(self, addr):
        o = self.off(addr)
        for ins in self.md.disasm(self.data[o:o + 12], addr, count=1):
            return ins
        return None

    def targets(self, ins):
        """Control-flow targets, and whether the instruction falls through."""
        m = ins.mnemonic
        base = m.split(".")[0]
        if base in ("rts", "rte", "rtr", "illegal"):
            return [], False
        tgts = []
        falls = base not in ("bra", "jmp")
        if CONDITIONAL.match(m):
            hits = HEX.findall(ins.op_str)
            if hits and not ins.op_str.startswith("("):
                tgts.append((int(hits[-1], 16), base in ("jsr", "bsr")))
            elif base in ("jmp", "jsr"):
                falls = base == "jsr"          # jmp (a0) - target unknown
        return tgts, falls

    def trace(self, entries):
        work = list(entries)
        while work:
            addr = work.pop()
            while True:
                if addr in self.insns or not self.inside(addr) or addr & 1:
                    break
                ins = self.decode_one(addr)
                if ins is None or ins.size == 0:
                    break
                self.insns[addr] = ins
                tgts, falls = self.targets(ins)
                for tgt, is_call in tgts:
                    self.note_ref(tgt, addr)
                    if self.inside(tgt):
                        if is_call:
                            self.subs.add(tgt)
                        if tgt not in self.insns:
                            work.append(tgt)
                if not falls:
                    break
                addr += ins.size

    def linear(self):
        addr = self.base
        while addr < self.end:
            if addr in self.insns:
                addr += self.insns[addr].size
                continue
            ins = self.decode_one(addr)
            if ins is None or ins.size == 0:
                addr += 2
                continue
            self.insns[addr] = ins
            addr += ins.size

    # -- rendering -----------------------------------------------------------
    def label_of(self, addr):
        n = hw.name_for(addr, self.cd)
        if n:
            return n
        if addr in self.subs:
            return "sub_%06X" % addr
        return "loc_%06X" % addr

    def annotate(self, ins):
        out = ins.op_str
        for h in set(HEX.findall(out)):
            v = int(h, 16)
            n = hw.name_for(v, self.cd)
            if n:
                out = out.replace("$" + h, n)
            elif self.inside(v) and (v in self.insns or v in self.xrefs):
                out = out.replace("$" + h, self.label_of(v))
        return out

    def render(self, fh, show_bytes=True):
        addr = self.base
        while addr < self.end:
            if addr in self.insns:
                ins = self.insns[addr]
                if addr in self.xrefs:
                    refs = sorted(self.xrefs[addr])
                    more = " +%d" % (len(refs) - 6) if len(refs) > 6 else ""
                    fh.write("\n%s:      ; xrefs: %s%s\n" % (
                        self.label_of(addr),
                        ", ".join("$%06X" % r for r in refs[:6]), more))
                b = ins.bytes.hex() if show_bytes else ""
                fh.write("%06X  %-20s  %-8s %s\n" %
                         (addr, b, ins.mnemonic, self.annotate(ins)))
                addr += ins.size
                continue
            start = addr
            while addr < self.end and addr not in self.insns:
                addr += 2
            self.dump_data(fh, start, addr)

    def dump_data(self, fh, start, stop):
        if start in self.xrefs:
            fh.write("\n%s:      ; xrefs: %s\n" % (
                self.label_of(start),
                ", ".join("$%06X" % r for r in sorted(self.xrefs[start])[:6])))
        blob = self.data[self.off(start):self.off(stop)]
        if blob and not blob.strip(b"\0"):
            fh.write("%06X  ; %d bytes of zero\n" % (start, len(blob)))
            return
        for i in range(0, len(blob), 16):
            row = blob[i:i + 16]
            txt = "".join(chr(c) if 32 <= c < 127 else "." for c in row)
            fh.write("%06X  dc.b %-47s ; %s\n" %
                     (start + i, " ".join("$%02x" % c for c in row), txt))


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m jaguarkit.m68k")
    ap.add_argument("file")
    ap.add_argument("--cart", action="store_true",
                    help="a cartridge image: base $800000, entry from $404")
    ap.add_argument("--boot", action="store_true",
                    help="a CD image: disassemble its boot image")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x4000)
    ap.add_argument("--off", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--len", dest="length", type=lambda s: int(s, 0))
    ap.add_argument("--entry", type=lambda s: int(s, 0), action="append")
    ap.add_argument("--linear", action="store_true")
    ap.add_argument("--no-bytes", action="store_true")
    ap.add_argument("--out")
    args = ap.parse_args(argv)

    cd = not args.cart
    if args.boot:
        from .disc import Disc
        t = next(t for t in Disc(args.file).tracks if t.boot)
        args.base, data = t.boot_image()
    else:
        data = open(args.file, "rb").read()
    if args.cart:
        from .cart import Cart
        c = Cart(args.file)
        args.base = c.base
        args.entry = args.entry or [c.entry]
    data = data[args.off:args.off + args.length] if args.length else data[args.off:]

    d = Disassembler(data, args.base, cd)
    d.trace(args.entry or [args.base])
    covered = sum(i.size for i in d.insns.values())
    if args.linear:
        d.linear()

    fh = open(args.out, "w") if args.out else sys.stdout
    fh.write("; %s  off=$%x len=$%x base=$%x\n" %
             (os.path.basename(args.file), args.off, len(data), args.base))
    fh.write("; recursive pass covered %d/%d bytes (%.1f%%), %d subroutines\n" %
             (covered, len(data), 100.0 * covered / len(data), len(d.subs)))
    d.render(fh, show_bytes=not args.no_bytes)
    if args.out:
        fh.close()
        print("wrote %s: %d insns, %d subs, recursive coverage %.1f%%" %
              (args.out, len(d.insns), len(d.subs), 100.0 * covered / len(data)))


if __name__ == "__main__":
    main()
