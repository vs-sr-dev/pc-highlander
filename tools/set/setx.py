#!/usr/bin/env python3
"""setx - parse the set track: scene tables, entry points, collision, events.

A set is one environment; a scene is one fixed camera view inside it.  Track 3
holds 48 of them, one per 56-block slot, laid out as `STRUCDEF.INC` describes:

    +0   .l  Hinum
    +4   .l  Lonum
    +8   .l  EventOffset
    +12  .l  CollOffset
    +16  .l  InitOffset
    +20  .l  SceneOffset
    +24  .l  ScriptOffset
    +28  24   six longs, purpose unknown
    +52  ..   the tables, in the order scene / init / event / collision / script

**Scene table** - a long count, then 8 bytes per scene: a 16-bit scene id, a
zero word, and the scene's CD block offset as a long.  Every offset is an exact
multiple of 110, the scene stride, and the id is the same 16-bit value the
scene's own camera footer carries at offset 0.  Across the 48 sets the tables
reference all 672 scenes with nothing left over, 792 references in total,
because 113 scenes belong to more than one set - the doorway views you can see
from either side.

The id is structured: **id = group * 64 + camera**, with 48 groups matching the
48 sets.  A set's table holds its own group plus a handful of views borrowed
from the sets it connects to.

**Init table** - a long count, then 12 bytes each: scene id, flags, x, z.  These
are the arrival points, and they use the borrowed ids, which is what identifies
them as doorways.

**Collision** - a 8-byte header (triangle offset, triangle count, vertex count,
pad), then vertices as pairs of longs (x, z), then 14 bytes per triangle: ground
height, three vertex indices, three neighbouring triangle indices.  The
adjacency is symmetric.  The triangle list ends exactly where ScriptOffset
begins, to within 8-byte alignment, in all 48 sets.

**Events** - a long count, then 36 bytes each:

    +0   .w  scene number, $FFFF for "any scene in this set"
    +2   .w  type            EVENT_TYPE_* from EVENT.GAS
    +4   .l  x
    +8   .l  z
    +12  .l  height          0 = ignore height
    +16  .l  radius squared  0 = always colliding
    +20  .w  status          bit 0 = colliding, bit 15 = delayed, bits 14-1 count
    +22  .w  condition type
    +24  3.w condition data
    +30  .w  data 1          for a SCENE event, the target scene id
    +32  .w  data 2
    +34  .w  data 3          for a CINEPAK event, the film's CD block offset

The script at ScriptOffset is not decoded here.

Usage
    python tools/set/setx.py TRACK3 [--set N] [--events] [--scenes]
                             [--collision] [--json FILE]
"""

import argparse
import json
import struct
import sys

SLOT = 56 * 2352
SETS = 48

EVENT_TYPES = {
    0: "SCENE", 1: "WAV", 2: "CINEPAK", 3: "WAVLP", 4: "RED", 5: "ENDWAV",
    6: "RESTOREITEM", 7: "LOGIC", 8: "SETBIT", 9: "RESETBIT", 10: "GIVEOBJ",
    11: "GAMEOVER", 12: "ACTIVATE", 13: "KEYHANDLER", 14: "CHARCHANGE",
    15: "BITMAP",
}

SCENE_STRIDE = 110          # blocks per scene on the PICT track

# Retail scene-id group -> the set name July's CDLINK.INC used.  Derived by
# aligning the 48 retail groups against the 46 named July sets by camera count:
# the July list is alphabetical and the retail one is in the same order, so the
# alignment is a single monotone pass with three insertions and one deletion.
# A trailing "!" marks an anchor, where the retail camera count equals July's
# exactly - 23 of the 48.  The rest are carried by their position between
# anchors and should be treated as likely, not certain.  Groups 19, 27 and 31
# have no July counterpart: they are sets added after July.  The totals check
# out on both sides, 672 scenes against 594.
GROUP_NAMES = {
    1: "C1", 2: "C2!", 3: "C3!", 4: "CA!", 5: "CN4!", 6: "CN5!", 7: "CNY01",
    8: "CNY02!", 9: "CNY03!", 10: "CNY06!", 11: "CNY07", 12: "CNY08",
    13: "CNY09", 14: "CODE!", 15: "CODE2!", 16: "D1", 17: "D2", 18: "D3!",
    19: None, 20: "DUN1", 21: "DUN2!", 22: "DUN4", 23: "DUN5!", 24: "DUN6",
    25: "G1", 26: "G2", 27: None, 28: "G3", 29: "H", 30: "MENU!", 31: None,
    32: "NEOSW!", 33: "PRI!", 34: "REST!", 35: "SE", 36: "SECUR", 37: "SHANR1!",
    38: "SHANR2", 39: "SHANR3!", 40: "TA!", 41: "TENT1", 42: "TENT2",
    43: "TENT3!", 44: "TENT4", 45: "TENT5!", 46: "TENT6", 47: "TENT7",
    48: "TRAIN!",
}


def scene_name(sid):
    """A name for a scene id, as <SET>_CAM<nn>, or group<n>_CAM<nn>."""
    g, cam = sid >> 6, sid & 63
    n = GROUP_NAMES.get(g)
    return "%s_CAM%02d" % (n.rstrip("!") if n else "group%d" % g, cam)


def parse_set(d, n):
    o = n * SLOT
    hi, lo, ev, coll, init, so, scr = struct.unpack_from(">7I", d, o)

    count = struct.unpack_from(">I", d, o + so)[0]
    scenes = []
    for i in range(count):
        sid, _, blk = struct.unpack_from(">HHI", d, o + so + 4 + i * 8)
        scenes.append(dict(id=sid, group=sid >> 6, camera=sid & 63, block=blk,
                           scene=blk // SCENE_STRIDE, name=scene_name(sid)))

    count = struct.unpack_from(">I", d, o + init)[0]
    entries = []
    for i in range(count):
        sid, flags, x, z = struct.unpack_from(">HHii", d, o + init + 4 + i * 12)
        entries.append(dict(id=sid, flags=flags, x=x, z=z))

    count = struct.unpack_from(">I", d, o + ev)[0]
    events = []
    for i in range(count):
        b = o + ev + 4 + i * 36
        sid, typ = struct.unpack_from(">Hh", d, b)
        x, z, h, r2 = struct.unpack_from(">4i", d, b + 4)
        status, cond = struct.unpack_from(">2H", d, b + 20)
        cdata = struct.unpack_from(">3H", d, b + 24)
        d1, d2, d3 = struct.unpack_from(">3H", d, b + 30)
        events.append(dict(scene=sid, type=typ, name=EVENT_TYPES.get(typ, "?"),
                           x=x, z=z, height=h, radius2=r2, status=status,
                           condition=cond, condition_data=list(cdata),
                           data=[d1, d2, d3]))

    troff, ntri, nver, _ = struct.unpack_from(">4H", d, o + coll)
    verts = [struct.unpack_from(">2i", d, o + coll + 8 + i * 8)
             for i in range(nver)]
    tris = []
    for i in range(ntri):
        f = struct.unpack_from(">7h", d, o + coll + troff + i * 14)
        tris.append(dict(height=f[0], verts=list(f[1:4]), adjacent=list(f[4:7])))

    groups = {}
    for sc in scenes:
        groups[sc["group"]] = groups.get(sc["group"], 0) + 1
    main = max(groups, key=groups.get)
    return dict(index=n, group=main,
                name=(GROUP_NAMES.get(main) or "").rstrip("!") or None,
                hinum=hi, lonum=lo, script=scr, scenes=scenes,
                entries=entries, events=events,
                collision=dict(vertices=[list(v) for v in verts],
                               triangles=tris))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("track3")
    ap.add_argument("--set", type=int)
    ap.add_argument("--scenes", action="store_true")
    ap.add_argument("--events", action="store_true")
    ap.add_argument("--collision", action="store_true")
    ap.add_argument("--json", metavar="FILE")
    args = ap.parse_args()

    d = open(args.track3, "rb").read()
    sets = [parse_set(d, n) for n in range(min(SETS, len(d) // SLOT))]
    chosen = [sets[args.set]] if args.set is not None else sets

    for s in chosen:
        main_group = s["group"]
        name = GROUP_NAMES.get(main_group) or "-"
        print("set %2d  group %2d %-8s %3d scenes  %2d entries  %3d events  "
              "%3d collision triangles  script $%X" %
              (s["index"], main_group, name, len(s["scenes"]),
               len(s["entries"]),
               len(s["events"]), len(s["collision"]["triangles"]), s["script"]))
        if args.scenes:
            for sc in sorted(s["scenes"], key=lambda x: x["id"]):
                print("    scene %3d  id $%04X  block %6d  %s%s"
                      % (sc["scene"], sc["id"], sc["block"],
                         scene_name(sc["id"]),
                         "" if sc["group"] == main_group else "   (borrowed)"))
        if args.events:
            for i, e in enumerate(s["events"]):
                extra = ""
                if e["name"] == "SCENE":
                    extra = "  -> scene id $%04X" % e["data"][0]
                elif e["name"] == "CINEPAK":
                    extra = "  -> film at block %d" % e["data"][2]
                print("    event %3d  %-11s scene %04X  x %7d z %7d  r2 %9d%s"
                      % (i, e["name"], e["scene"], e["x"], e["z"], e["radius2"],
                         extra))
        if args.collision:
            c = s["collision"]
            print("    collision: %d vertices, %d triangles" %
                  (len(c["vertices"]), len(c["triangles"])))

    if args.json:
        with open(args.json, "w") as f:
            json.dump(chosen, f, indent=1)
        print("wrote %s" % args.json)

    print("%d sets, %d scene references" %
          (len(sets), sum(len(s["scenes"]) for s in sets)), file=sys.stderr)


if __name__ == "__main__":
    main()
