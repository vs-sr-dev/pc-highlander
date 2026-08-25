#!/usr/bin/env python3
"""worldx - the world-state table and the character sheets.

Both are static tables in the resident binary, copied into their runtime homes
at startup by the routines at `$5310` and `$537A`:

```
$5310   $15458 -> ws  ($32660), 8192 bytes = 256 records of 32
        then, for each record, relocate  wstSheet  from base $17458 to $34760
                                  and    wstParent from base $15458 to $32660
$537A   $17458 -> cs  ($34760), 8192 bytes
        then walk the cshNext chain relocating it the same way
```

That relocation is what identifies the two source addresses beyond doubt: the
code names both file bases and both RAM bases in immediates, and the stride it
walks is 32 bytes with the two pointer fields at +4 and +8, exactly `wstSheet`
and `wstParent` in `LOGICS.INC`.

**World state**, 32 bytes, the record `ITEMS.MAC` builds:

```
+0   .w  wstSet      the scene id masked to $FFC0, so group * 64
+2   .w  wstRadius
+4   .l  wstSheet    -> a character sheet
+8   .l  wstParent   -> the world-state entry that owns this one, or 0
+12  .l  wstXpos     +16 .l wstYpos    +20 .l wstZpos
+24  3b  Xface Yface Zface
+27  5b  Sanity Person Str Life Flags
```

197 of the 256 records are in use. The first seven are byte-for-byte July's:
`WORLD_QUENTIN` still has `229, 7, 153, 255`, and `WORLD_CA_TURRET_KEY` still
sits at `(-2348, -3036)` with its parent pointing at `WORLD_CA_KEY_HUNTER`.

**Character sheet**, `cshRecord` from `LOGICS.INC`, 16 bytes then an array of
longs the game fills from the CD:

```
+0   .l  cshNext        0 ends the chain
+4   .w  cshFlags
+6   4b  cshModelOff cshModelNum cshAnimOff cshAnimNum
+10  4b  cshMiscOff cshMiscNum cshFileOff cshFileNum
+14  .w  cshBehaviour
```

The `*Off` fields index the long array **from the start of the record**, so the
first is always 4, and the four runs are contiguous: model, animation,
miscellaneous, file. `SCRIPT.GAS`'s `animate` reads
`sheet + (cshAnimOff + n) * 4` for animation *n*, which is what fixes that
reading. Forty sheets are in the chain, and every full character declares
`cshModelNum = 15` - the same fifteen pieces the model and animation records
carry, seen from a third side.

Naming. Pass `--world WORLD.S` (from the 1995 dump, which this repository does
not contain) to align the retail table against July's by exact coordinates and
carry the `WORLD_*` names across.

Usage
    python tools/world/worldx.py TRACK2
    python tools/world/worldx.py TRACK2 --sheets
    python tools/world/worldx.py TRACK2 --world path/to/WORLD.S --json out.json
"""

import argparse
import json
import os
import re
import struct
import sys

BASE, OFF = 0x5000, 0x12600         # load address, and where it starts in track 2
WS_SRC, WS_DST = 0x15458, 0x32660
CS_SRC, CS_DST = 0x17458, 0x34760
WS_COUNT, WS_REC = 256, 32

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "set"))
try:
    from setx import GROUP_NAMES, scene_name
except ImportError:                                  # stand alone is fine
    GROUP_NAMES, scene_name = {}, None

# July's ITEMS.MAC, one macro per kind of thing: where x and z sit in the
# argument list.  Used only to read WORLD.S when the user points at it.
JULY_XZ = {"itemCharacter": (3, 5), "itemTank": (1, 3), "itemTurret": (1, 3),
           "itemSword": (2, 4), "itemGun": (2, 4), "itemLocket": (2, 4),
           "itemWaterWheel": (2, 4), "itemKey": (2, 4), "itemCheese": (2, 4),
           "itemLoaf": (2, 4), "itemWine": (2, 4), "itemRedWater": (1, 3),
           "itemGreenWater": (1, 3), "itemBlueWater": (1, 3),
           "itemHand": (1, 3)}

# LOGICS.INC names bits 0, 1, 3, 4, 5 and 6; 2 and 7 it does not.
WST_FLAGS = ["WSTRegistered", "WSTInanimate", "bit2", "WSTAmmo",
             "WSTWeapon", "WSTCollectable", "WSTDeactivated", "bit7"]


def fo(mem):
    return mem - BASE + OFF


def world(d):
    out = []
    for i in range(WS_COUNT):
        o = fo(WS_SRC) + i * WS_REC
        s, radius = struct.unpack_from(">HH", d, o)
        sheet, parent = struct.unpack_from(">II", d, o + 4)
        x, y, z = struct.unpack_from(">iii", d, o + 12)
        xf, yf, zf, san, per, stg, life, flags = struct.unpack_from(">8B", d, o + 24)
        out.append(dict(
            index=i, group=s >> 6, set=(GROUP_NAMES.get(s >> 6) or "").rstrip("!")
            or None, radius=radius, sheet=sheet, parent=parent,
            x=x, y=y, z=z, face=[xf, yf, zf], sanity=san, personality=per,
            strength=stg, life=life, flags=flags,
            used=bool(s or sheet or x or z or san or per or stg or life
                      or flags)))
    # the table is a prefix: everything up to the last live record is in use,
    # including entry 1, which is all zeros but for its stats
    last = max(i for i, e in enumerate(out) if e["used"])
    for i, e in enumerate(out):
        e["used"] = i <= last
    return out


def sheets(d):
    out = []
    p = CS_SRC
    seen = set()
    while p and p not in seen:
        seen.add(p)
        o = fo(p)
        nxt, flags = struct.unpack_from(">IH", d, o)
        f = struct.unpack_from(">8B", d, o + 6)
        beh = struct.unpack_from(">H", d, o + 14)[0]
        out.append(dict(index=len(out), addr=p, next=nxt, flags=flags,
                        model_off=f[0], models=f[1], anim_off=f[2], anims=f[3],
                        misc_off=f[4], miscs=f[5], file_off=f[6], files=f[7],
                        behaviour=beh, bytes=(nxt - p) if nxt else None))
        p = nxt
    return out


def july_entries(path):
    """(name, x, z) for each entry of the 1995 WORLD.S, in file order."""
    out, name = [], None
    for line in open(path):
        m = re.match(r"^;(WORLD_\w+)\s+entry\s+\d+", line)
        if m:
            name = m.group(1)
            continue
        m = re.match(r"^\s*(item\w+)\s+(.*?)\s*$", line)
        if m and name and m.group(1) in JULY_XZ:
            a = [t.strip() for t in m.group(2).split(",")]
            xi, zi = JULY_XZ[m.group(1)]
            try:
                out.append((name, int(a[xi]), int(a[zi])))
            except (ValueError, IndexError):
                out.append((name, None, None))
            name = None
    return out


def align(july, retail):
    """Longest common subsequence on the (x, z) key: a monotone pairing."""
    a = [(x, z) for _, x, z in july]
    b = [(e["x"], e["z"]) for e in retail]
    n, m = len(a), len(b)
    L = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n - 1, -1, -1):
        for j in range(m - 1, -1, -1):
            L[i][j] = (L[i + 1][j + 1] + 1 if a[i] == b[j]
                       else max(L[i + 1][j], L[i][j + 1]))
    pairs, i, j = [], 0, 0
    while i < n and j < m:
        if a[i] == b[j]:
            pairs.append((i, j))
            i += 1
            j += 1
        elif L[i + 1][j] >= L[i][j + 1]:
            i += 1
        else:
            j += 1
    return pairs


def flagstr(v):
    return "|".join(WST_FLAGS[b] for b in range(8) if v >> b & 1) or "-"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("track2")
    ap.add_argument("--sheets", action="store_true")
    ap.add_argument("--world", metavar="WORLD.S",
                    help="the 1995 world table, to carry its names across")
    ap.add_argument("--json", metavar="FILE")
    a = ap.parse_args()

    d = open(a.track2, "rb").read()
    ws = world(d)
    cs = sheets(d)
    by_addr = {s["addr"]: s["index"] for s in cs}
    for e in ws:
        e["sheet_index"] = by_addr.get(e["sheet"])
        e["parent_index"] = ((e["parent"] - WS_SRC) // WS_REC
                             if e["parent"] else None)

    if a.world:
        july = july_entries(a.world)
        pairs = align(july, [e for e in ws if e["used"]])   # a prefix of ws
        for ji, ri in pairs:
            ws[ri]["july_name"] = july[ji][0]
        print("# %d of July's %d entries pair with the retail table by exact "
              "(x, z)" % (len(pairs), len(july)))

    used = [e for e in ws if e["used"]]
    if a.sheets:
        print("%d character sheets, chained from $%05X" % (len(cs), CS_SRC))
        print("  #  addr    bytes  models  anims  misc  files  behaviour  uses")
        uses = {}
        for e in used:
            uses[e["sheet_index"]] = uses.get(e["sheet_index"], 0) + 1
        for s in cs:
            print("  %2d $%05X %6s  %2d @%-3d %3d @%-3d %2d @%-3d %2d @%-3d %8d %5d"
                  % (s["index"], s["addr"], s["bytes"], s["models"],
                     s["model_off"], s["anims"], s["anim_off"], s["miscs"],
                     s["misc_off"], s["files"], s["file_off"], s["behaviour"],
                     uses.get(s["index"], 0)))
    else:
        print("%d world-state records in use, of %d" % (len(used), WS_COUNT))
        print("  #  group set      sheet parent      x       z   rad  life  "
              "flags")
        for e in used:
            print("%3d  %3d  %-8s %5s %6s %7d %7d %5d %5d  %-28s %s"
                  % (e["index"], e["group"], e["set"] or "-",
                     e["sheet_index"] if e["sheet_index"] is not None
                     else ("$%05X" % e["sheet"] if e["sheet"] else "-"),
                     e["parent_index"] if e["parent_index"] is not None else "-",
                     e["x"], e["z"], e["radius"], e["life"], flagstr(e["flags"]),
                     e.get("july_name", "")))

    if a.json:
        json.dump(dict(world=ws, sheets=cs), open(a.json, "w"), indent=1)
        print("wrote %s" % a.json)


if __name__ == "__main__":
    main()
