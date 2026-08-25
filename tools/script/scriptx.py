#!/usr/bin/env python3
"""scriptx - disassemble the Highlander script VM.

The game runs a little bytecode machine on the GPU.  Its source survives in the
July dump as `SCRIPT.GAS` (the interpreter), `OPCODES.INC` (the opcode order)
and `SCRIPT.MAC` (the assembler macros that define the encoding).  The retail
build ships the same machine, as GPU module `$2F310`, and its dispatch table -
70 longs at `$2FD8C`+20 - shows the retail opcode set is July's with **eight
new opcodes appended**, 75 to 82; every opcode from 0 to 74 kept its number.

Encoding.  One command is a big-endian long:

    31..24  opcode
    23..20  addressing mode  (for a branch: the condition code)
    19..16  register
    15..0   operand          (sign-extended where it is an offset)

A handful of commands are followed by extra longs, which the interpreter reads
through the program counter; see extra_len below.

Flow.  `bra`/`bsr`/`spawn` are pc-relative, target = address + 4 + operand.
`kill` names another process by its offset from the start of the script.
`quit` is not a terminator: it yields for this game frame and resumes at the
next command.  Only `rts`, `suicide` and an unconditional `bra` end a trace.

Usage
    python tools/script/scriptx.py TRACK3 --set 30
    python tools/script/scriptx.py TRACK3 --all --out DIR
    python tools/script/scriptx.py TRACK2 --main
"""

import argparse
import json
import os
import struct
import sys

SLOT = 56 * 2352
SETS = 48

# MAINSCRIPT, the always-resident script, sits in the retail binary right
# behind the script VM module and runs to the next module header.
MAIN_MEM = 0x2FEB8
MAIN_END = 0x30130
MAIN_FILE = MAIN_MEM - 0x5000 + 0x12600

# opcode -> (mnemonic, operand shape).  Names are OPCODES.INC's, except
# 75..82, which do not exist in July and are named from the retail handlers.
OPS = [
    ("not",       "reg"),        # 0
    ("neg",       "reg"),
    ("abs",       "reg"),
    ("add",       "rr"),         # 3  r[oper] += r[reg]
    ("sub",       "rr"),
    ("cmp",       "rr"),
    ("copy",      "rr"),
    ("mult",      "rr"),
    ("div",       "rr"),
    ("inc",       "vr"),         # 9  r[reg] += oper
    ("dec",       "vr"),
    ("sett",      "vr"),
    ("cmpi",      "vr"),
    ("exg",       "rr"),         # 13
    ("bra",       "branch"),
    ("quit",      "none"),
    ("bsr",       "branch"),
    ("rts",       "none"),
    ("spawn",     "branch"),
    ("suicide",   "none"),
    ("kill",      "kill"),       # 20
    ("pause",     "imm"),
    ("select",    "charimm"),    # 22
    ("swapchar",  "reg"),
    ("chartoreg", "reg"),
    ("regtochar", "reg"),
    ("animate",   "charimm"),
    ("animhack",  "reg"),        # 27 anim_direct: animation number from a reg
    ("setanim",   "charimm"),
    ("chase",     "charimm"),    # 29
    ("attack",    "charimm"),
    ("face",      "charimm"),
    ("goto",      "xz"),         # 32
    ("turnto",    "xz"),
    ("attplay",   "charnone"),
    ("freeze",    "charnone"),
    ("release",   "charnone"),
    ("waitforit", "charnone"),
    ("waitforanim", "charnone"),
    ("cinepak",   "block"),      # 39
    ("redbook",   "block"),
    ("sample",    "imm"),
    ("camera",    "scene"),      # 42
    ("charchange", "charimm"),
    ("eventbit",  "bit01"),      # 44 mode 1 = set, 0 = clear
    ("waitevent", "none"),
    ("wstread",   "wst"),        # 46
    ("wstwrite",  "wst"),
    ("actread",   "act"),
    ("actwrite",  "act"),
    ("citread",   "cit"),
    ("citwrite",  "cit"),
    ("testowner", "charimm"),    # 52
    ("testset",   "imm"),
    ("testscene", "scene"),
    ("testbit",   "imm"),
    ("setbit",    "bit01"),      # 56 mode 1 = set, 0 = clear
    ("testprox",  "prox"),
    ("testdist",  "xz"),
    ("testevent", "imm"),        # 59 never implemented: falls into quit
    ("waitredbook", "none"),
    ("eventmask", "evmask"),     # 61
    ("keytest",   "key"),
    ("keymask",   "mask"),
    ("slideto",   "xz"),         # 64
    ("restore",   "none"),
    ("sat",       "sat"),
    ("patch",     "patch"),      # 67
    ("activation", "activation"),
    ("reset",     "none"),
    ("fake_scene", "fakescene"),  # 70
    ("pickup",    "imm"),
    ("default",   "charnone"),
    ("poke",      "poke"),       # 73
    ("triangle_height", "trih"),
    # --- retail additions, named from the shipped handlers ---
    ("testuse",   "imm"),        # 75  ws[oper] == the latched "used" entry?
    ("random",    "reg_range"),  # 76  reg = random, scaled to 1..oper
    ("getglobal", "reg_idx"),    # 77  reg = globalreg[oper]
    ("setbitreg", "bit01reg"),   # 78  setbit, bit number from a register
    ("wavestop",  "none"),       # 79  posts command 4 to the DSP wave player
    ("andi",      "andi"),       # 80  reg &= immediate
    ("waitkey",   "waitkey"),    # 81  wait for one pad key, branch on another
    ("gvar",      "gvar"),       # 82  read/write the global variable block
]
LASTOP = 83


def extra_len(op, mode):
    """Bytes the command consumes beyond its own long."""
    if op in (32, 33, 58, 64):           # goto turnto testdist slideto: x, z
        return 8
    if op in (39, 40, 57, 63, 67, 74):   # cinepak redbook testprox keymask
        return 4                         # patch triangle_height
    if op == 70:                         # fake_scene: the VM does NOT skip it
        return 4
    if op == 61:                         # eventmask: the long form is mode 0
        return 4 if mode == 0 else 0
    if op == 80:                         # andi: the long form is mode != 0
        return 4 if mode != 0 else 0
    return 0


TERMINATORS = (17, 19)                   # rts, suicide

# Condition codes, from validcond in SCRIPT.MAC; the names are that macro's.
# A compare leaves the GPU flags of `register - operand`, and condtable tests
# them, so what the names actually mean is:
#   eq  =        ne  !=        mi / le  reg < oper      pl / gt  reg >= oper
#   lt  reg > oper             ge  reg <= oper          cs / cc  carry
CONDS = {0: "", 1: "eq", 2: "cs", 3: "mi", 4: "lt",
         9: "ne", 10: "cc", 11: "pl", 12: "ge"}

WST = {0: "Set", 2: "Radius", 4: "Sheet", 8: "Parent", 12: "Xpos", 16: "Ypos",
       20: "Zpos", 24: "Xface", 25: "Yface", 26: "Zface", 27: "Sanity",
       28: "Person", 29: "Str", 30: "Life", 31: "Flags"}
ACT = {0: "World", 4: "Inst", 8: "Flags", 10: "Status", 12: "Joypad",
       16: "Action", 20: "Count", 24: "AICommand", 28: "AIData1",
       32: "AIData2", 36: "AIData3", 40: "AIData4"}
CIT = {0: "Sheet", 4: "Draw", 8: "World", 12: "ModelNum", 14: "Stance",
       15: "Tween", 16: "Animate", 20: "Frame", 22: "OldFrame", 23: "Flags",
       24: "Facing", 25: "Moving", 26: "Height", 28: "Triangle", 30: "Gravity",
       32: "Speed", 34: "Style", 36: "Collision", 38: "Xmoveback",
       40: "Ymoveback", 42: "Zmoveback", 44: "Tmoveback"}
SIZES = {0: "b", 1: "w", 2: "l", 3: "l?"}

# joypad bits.  readpad in JOY.S assembles the long and documents its layout
# as  xxApxxBx RLDU741* xxCxxxox 2580369#  , bit 31 leftmost.
PAD = {0: "PAD_HASH", 1: "PAD_9", 2: "PAD_6", 3: "PAD_3", 4: "PAD_0",
       5: "PAD_8", 6: "PAD_5", 7: "PAD_2", 9: "OPTION", 13: "FIRE_C",
       16: "PAD_STAR", 17: "PAD_1", 18: "PAD_4", 19: "PAD_7",
       20: "JOY_UP", 21: "JOY_DOWN", 22: "JOY_LEFT", 23: "JOY_RIGHT",
       25: "FIRE_B", 28: "PAUSE", 29: "FIRE_A"}


ZERO = bytes(4)


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    return v - 0x100000000 if v & 0x80000000 else v


class Script(object):
    """One script: a flat byte string plus the names it can resolve."""

    def __init__(self, data, name, scenes=None, films=None):
        self.d = data
        self.name = name
        self.scenes = scenes or {}       # scene id -> name
        self.films = films or {}         # cd block -> film number
        self.kind = {}                   # offset -> code / operand
        self.labels = {}                 # offset -> label

    # -- naming -----------------------------------------------------------
    def scene_str(self, sid):
        n = self.scenes.get(sid)
        return "#%d%s" % (sid, "   ; %s" % n if n else "")

    def film_str(self, blk):
        n = self.films.get(blk)
        return "block %d%s" % (blk, "   ; film %d" % n if n is not None else "")

    # -- pass 1: follow the flow -----------------------------------------
    def trace(self, entries):
        todo = list(entries)
        while todo:
            pc = todo.pop()
            while True:
                if pc in self.kind or pc + 4 > len(self.d):
                    break
                cmd = struct.unpack_from(">I", self.d, pc)[0]
                op = cmd >> 24
                if op >= LASTOP or cmd == 0:
                    break
                mode, oper = (cmd >> 20) & 15, cmd & 0xFFFF
                n = 4 + extra_len(op, mode)
                self.kind[pc] = "code"
                for i in range(4, n, 4):
                    self.kind[pc + i] = "operand"

                if op in (14, 16, 18):                     # bra bsr spawn
                    t = pc + 4 + s16(oper)
                    if 0 <= t < len(self.d):
                        self.labels.setdefault(t, "L_%04X" % t)
                        todo.append(t)
                    if op == 14 and mode == 0:             # unconditional
                        break
                elif op == 20:                             # kill <offset>
                    if 0 <= oper < len(self.d):
                        self.labels.setdefault(oper, "L_%04X" % oper)
                        todo.append(oper)
                elif op == 81:                             # waitkey: side exit
                    t = pc + 4 + (s16(oper) >> 5)
                    if 0 <= t < len(self.d) and t != pc:
                        self.labels.setdefault(t, "L_%04X" % t)
                        todo.append(t)
                elif op == 67:                             # patch <target>
                    t = pc + 4 + s16(oper)
                    if 0 <= t < len(self.d):
                        self.labels.setdefault(t, "P_%04X" % t)
                if op in TERMINATORS:
                    break
                pc += n

    # a script is followed by slot padding: this many zero longs in a row is
    # taken as the end of it
    ZERO_RUN = 16

    def hard_end(self):
        """First long of the padding that follows the script."""
        n, run = len(self.d) & ~3, 0
        for pc in range(0, n, 4):
            if self.d[pc:pc + 4] == ZERO:
                run += 1
                if run == self.ZERO_RUN:
                    return pc - (self.ZERO_RUN - 1) * 4
            else:
                run = 0
        return n

    def end_of_script(self):
        """Where the script stops: the far end of the last command reached."""
        return max(self.kind) + 4 if self.kind else 0

    # -- pass 2: render ---------------------------------------------------
    def render(self, pc):
        cmd = struct.unpack_from(">I", self.d, pc)[0]
        op = cmd >> 24
        mode, reg, oper = (cmd >> 20) & 15, (cmd >> 16) & 15, cmd & 0xFFFF
        if op >= LASTOP:
            return "dc.l    $%08X" % cmd, 4
        name, shape = OPS[op]
        nex = extra_len(op, mode) // 4
        ex = [struct.unpack_from(">I", self.d, pc + 4 + i * 4) [0]
              for i in range(nex)]
        a = self.args(op, shape, mode, reg, oper, ex, pc)
        return ("%-16s%s" % (name, a)).rstrip(), 4 + nex * 4

    def args(self, op, k, mode, reg, oper, ex, pc):
        sop = s16(oper)
        if k == "none":
            return ""
        if k == "reg":
            return "r%d" % reg
        if k == "rr":
            return "r%d, r%d" % (reg, oper)
        if k == "vr":
            return "#%d, r%d" % (sop, reg)
        if k == "imm":
            return "#%d" % oper
        if k == "branch":
            t = pc + 4 + sop
            c = CONDS.get(mode, "?%d" % mode)
            return "%s%s" % (self.labels.get(t, "$%04X" % t),
                             ", " + c if c else "")
        if k == "kill":
            return self.labels.get(oper, "$%04X" % oper)
        if k == "charimm":
            # mode 1: the character comes from a register instead of the
            # instruction; the operand keeps its meaning either way
            return "#%d%s" % (oper, ", r%d" % reg if mode else "")
        if k == "charnone":
            return "r%d" % reg if mode else ""
        if k == "xz":
            xz = "#%d, #%d" % (s32(ex[0]), s32(ex[1]))
            if op == 64:                       # slideto also carries a count
                return "%s, steps=%d%s" % (xz, oper,
                                           ", r%d" % reg if mode else "")
            return "%s%s" % (xz, ", r%d" % reg if mode else "")
        if k == "prox":
            return "#%d, dist=%d%s" % (oper, ex[0],
                                       ", r%d" % reg if mode else "")
        if k == "block":
            return self.film_str(ex[0]) if op == 39 else "block %d" % ex[0]
        if k == "scene":
            return self.scene_str(oper)
        if k == "bit01":
            return "#%d, %s" % (oper, "set" if mode else "clear")
        if k == "bit01reg":
            return "r%d, %s" % (reg, "set" if mode else "clear")
        if k in ("wst", "act", "cit"):
            return self.table_args(k, mode, reg, oper)
        if k == "evmask":
            return "$%08X" % ex[0] if mode == 0 else "$%04X" % oper
        if k == "mask":
            bits = [PAD[b] for b in range(32) if ex[0] >> b & 1 and b in PAD]
            return "$%08X%s" % (ex[0], "   ; " + "|".join(bits) if bits else "")
        if k == "sat":
            return "r%d, %s" % (reg, "word" if mode else "byte")
        if k == "patch":
            t = pc + 4 + sop
            return "%s, at=$%04X, %d longs" % (
                self.labels.get(t, "$%04X" % t), ex[0] >> 16, ex[0] & 0xFFFF)
        if k == "activation":
            who = ("#%d" % oper if mode & 2 else
                   "r%d" % reg if mode & 1 else "char")
            return "%s, %s" % (who, "deactivate" if mode >> 2 else "activate")
        if k == "fakescene":
            return "%s, block %d   ; the VM does not step over this long" % (
                self.scene_str(oper), ex[0])
        if k == "poke":
            return "#%d, (r%d)" % (oper, reg)
        if k == "trih":
            return "tri %d, height %d" % (oper, s32(ex[0]))
        if k == "reg_range":
            return "r%d%s" % (reg, ", 1..%d" % oper if oper else "")
        if k == "reg_idx":
            return "r%d, gvar[%d]" % (reg, oper)
        if k == "andi":
            return ("r%d, $%08X" % (reg, ex[0]) if mode
                    else "r%d, $%04X" % (reg, oper))
        if k == "key":
            return "%s%s" % ("r%d" % reg if mode else
                             PAD.get(oper, "bit %d" % oper),
                             "" if not mode else "")
        if k == "waitkey":
            if mode:                        # wait until nothing is pressed
                return "release"
            t = pc + 4 + (sop >> 5)
            b = oper & 31
            return "%s, else %s" % (PAD.get(b, "bit %d" % b),
                                    self.labels.get(t, "$%04X" % t))
        if k == "gvar":
            # base $4E14, which is gvar[8] of the same long array getglobal
            # reads; the offset here is in bytes, not entries
            return "%s r%d, $%04X.%s" % (
                "write" if mode & 1 else "read", reg, 0x4E14 + oper,
                SIZES.get(mode >> 1, "?"))
        return "mode=%d, r%d, $%04X" % (mode, reg, oper)

    def table_args(self, k, mode, reg, oper):
        tbl = {"wst": WST, "act": ACT, "cit": CIT}[k]
        size = SIZES.get(mode >> 2, "?")
        off, lo = oper >> 8, oper & 0xFF
        field = tbl.get(off, "+%d" % off)
        if k == "wst" and mode & 2:
            who = "ws[%d]" % lo
        elif mode & 1:
            who = "ws(r%d)" % lo if k == "wst" else "(r%d)" % lo
        else:
            who = "char"
        return "%s.%s.%s, r%d" % (who, field, size, reg)

    # -- driver -----------------------------------------------------------
    def disassemble(self, base=0):
        limit = self.hard_end()
        self.trace([0])
        # anything the trace never reached but that still sits inside the
        # script gets its own sweep, so nothing is silently dropped
        for pc in range(0, limit, 4):
            if pc not in self.kind and self.d[pc:pc + 4] != ZERO:
                self.trace([pc])
        end = min(self.end_of_script(), limit)

        out = ["; %s - %d bytes" % (self.name, end)]
        pc = 0
        while pc < end:
            if pc in self.labels:
                out.append("%s:" % self.labels[pc])
            raw = " ".join("%02X" % b for b in self.d[pc:pc + 4])
            if self.kind.get(pc) == "code":
                txt, n = self.render(pc)
                out.append("  %04X  %s  %s" % (base + pc, raw, txt))
                for i in range(1, n // 4):
                    o = pc + i * 4
                    out.append("  %04X  %s  |" % (
                        base + o, " ".join("%02X" % b
                                           for b in self.d[o:o + 4])))
                pc += n
            else:
                v = struct.unpack_from(">I", self.d, pc)[0]
                out.append("  %04X  %s  dc.l    $%08X" % (base + pc, raw, v))
                pc += 4
        return "\n".join(out)


def load_films(path):
    """cd block -> film number, from filmls's TSV."""
    f = {}
    if not path or not os.path.exists(path):
        return f
    for i, line in enumerate(open(path)):
        p = line.split("\t")
        if i == 0 or len(p) < 2:
            continue
        f[int(p[1])] = int(p[0])
    return f


def load_scene_names(path):
    """scene id -> name, from setx's JSON."""
    n = {}
    if not path or not os.path.exists(path):
        return n
    for s in json.load(open(path)):
        for sc in s["scenes"]:
            n[sc["id"]] = sc["name"]
    return n


def set_script(track3, i):
    o = i * SLOT
    scr = struct.unpack_from(">I", track3, o + 24)[0]
    return None if scr == 0 else track3[o + scr:o + SLOT]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--set", type=int)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--main", action="store_true",
                    help="disassemble MAINSCRIPT out of the resident binary")
    ap.add_argument("--films", default="assets/films.tsv")
    ap.add_argument("--sets", default="assets/sets.json")
    ap.add_argument("--out")
    a = ap.parse_args()

    d = open(a.file, "rb").read()
    films = load_films(a.films)
    scenes = load_scene_names(a.sets)
    names = {}
    if os.path.exists(a.sets):
        for s in json.load(open(a.sets)):
            names[s["index"]] = s["name"] or "set%d" % s["index"]

    jobs = []
    if a.main:
        jobs.append(("main", d[MAIN_FILE:MAIN_FILE + MAIN_END - MAIN_MEM],
                     MAIN_MEM))
    elif a.set is not None:
        b = set_script(d, a.set)
        if b is None:
            sys.exit("set %d has no script" % a.set)
        jobs.append(("%d" % a.set, b, 0))
    elif a.all:
        for i in range(SETS):
            b = set_script(d, i)
            if b is not None:
                jobs.append(("%d" % i, b, 0))
    else:
        sys.exit("pick --set N, --all or --main")

    if a.out:
        os.makedirs(a.out, exist_ok=True)
    for tag, blob, base in jobs:
        title = ("MAINSCRIPT" if tag == "main" else
                 "set %s (%s)" % (tag, names.get(int(tag), "?")))
        s = Script(blob, title, scenes, films)
        txt = s.disassemble(base)
        if a.out:
            p = os.path.join(a.out, "script_%s.txt" % tag)
            open(p, "w").write(txt + "\n")
            print("%-22s -> %s" % (title, p))
        else:
            print(txt)


if __name__ == "__main__":
    main()
