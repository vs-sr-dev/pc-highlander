#!/usr/bin/env python3
"""textx - pull the localised game text out of the resident binary.

The retail build ships English, French and German.  The strings live in one
block at address $19760, separated by "~", with "|" marking a line break inside
a string.  Each item is addressed by a triple of longs - English, French, German
- and those triples sit in the data area alongside the code that uses them,
not in one central table.  This tool finds the triples, follows them and prints
the three languages side by side.

Accented capitals are encoded as lowercase letters, because the game font puts
them where the lowercase glyphs would be.  Three are attested in the shipped
text:  b = A-umlaut,  d = O-umlaut,  f = U-umlaut.  The French text carries no
accents at all.

Usage
    python tools/text/textx.py TRACK2 [--format tsv|json|text] [--raw]
"""

import argparse
import json
import re
import struct
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

CODE_TAG = b"CODE" * 16
CODE_BASE = 0x5000
TEXT_LO, TEXT_HI = 0x19760, 0x1B12B

ACCENTS = {"b": "Ä", "d": "Ö", "f": "Ü"}
LANGS = ("en", "fr", "de")


def load(path):
    boot = open(path, "rb").read()
    i = boot.find(CODE_TAG)
    if i < 0:
        raise SystemExit("no CODE section - is this the boot track?")
    code_off = i + len(CODE_TAG)
    return boot, code_off


def string_at(boot, code_off, addr):
    o = code_off + (addr - CODE_BASE)
    end = boot.index(b"~", o)
    return boot[o:end]


def decode(raw, keep_raw=False):
    s = raw.decode("latin-1")
    if not keep_raw:
        s = "".join(ACCENTS.get(c, c) for c in s)
    return s


def find_triples(boot, code_off):
    """Longs at +0/+4/+8 all pointing into the text block, in language order."""
    out = []
    seen = set()
    end = len(boot) - 12
    for o in range(code_off, end, 2):
        a, b, c = struct.unpack_from(">III", boot, o)
        if not all(TEXT_LO <= v < TEXT_HI for v in (a, b, c)):
            continue
        if not a < b < c:
            continue
        if a in seen:
            continue
        seen.add(a)
        out.append((CODE_BASE + (o - code_off), a, b, c))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("boot")
    ap.add_argument("--format", choices=("tsv", "json", "text"), default="text")
    ap.add_argument("--all", action="store_true",
                    help="dump every record of the block positionally, per "
                         "language, instead of following the pointer triples")
    ap.add_argument("--raw", action="store_true",
                    help="keep the game's accent encoding instead of decoding it")
    args = ap.parse_args()

    boot, code_off = load(args.boot)

    if args.all:
        lo = code_off + (TEXT_LO - CODE_BASE)
        hi = code_off + (TEXT_HI - CODE_BASE)
        recs = boot[lo:hi].split(b"~")
        # the block holds three consecutive lists: 64 English, 65 French
        # (one string carries a stray separator), 64 German
        for name, group in (("en", recs[0:64]), ("fr", recs[64:129]),
                            ("de", recs[129:193])):
            for i, r in enumerate(group):
                print("%s %3d  %s" % (name, i, decode(r, args.raw)))
        return

    triples = find_triples(boot, code_off)
    triples.sort(key=lambda t: t[1])

    rows = []
    for table, *ptrs in triples:
        row = {"table": "$%06X" % table}
        for lang, p in zip(LANGS, ptrs):
            row[lang] = decode(string_at(boot, code_off, p), args.raw)
            row[lang + "_addr"] = "$%06X" % p
        rows.append(row)

    if args.format == "json":
        json.dump(rows, sys.stdout, ensure_ascii=False, indent=1)
    elif args.format == "tsv":
        print("table\ten_addr\ten\tfr\tde")
        for r in rows:
            print("\t".join([r["table"], r["en_addr"]] +
                            [r[l].replace("|", "\\n") for l in LANGS]))
    else:
        for r in rows:
            print("%s  %s" % (r["table"], r["en_addr"]))
            for l in LANGS:
                print("  %s  %s" % (l, r[l].replace("|", "\n      ")))
            print()
    print("%d entries" % len(rows), file=sys.stderr)


if __name__ == "__main__":
    main()
