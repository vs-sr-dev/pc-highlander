#!/usr/bin/env python3
"""manifest - one JSON describing everything on the disc.

This is the deliverable phase 2 was for: a single file the engine can read
instead of re-deriving the disc layout. It calls the extractors rather than
re-implementing them, so it stays honest by construction - if `setx` learns
something new, the manifest learns it too.

It contains, all cross-referenced by name and index:

```
disc      the nine tracks, their types and sizes
sets      48, with their scene tables, doorway counts, event and collision
          totals, and a digest of the script each one runs
scenes    672, each with its id, group, set name, CD block, and the sets
          that list it
scripts   the 27 set scripts and MAINSCRIPT: size, command count, opcode
          histogram, and the films, cameras, game bits and world entries
          each one touches
world     197 objects and characters: set, sheet, owner, position, stats
sheets    40 character sheets: model / animation / miscellaneous / file counts
models    the 19 linked into the binary and the 220 on track 5
anims     327: 285 on track 5 and 42 on track 8
films     36, with size, chunk count, extracted audio length, and every place
          on the disc that triggers them
waves     78 sound-effect bundles across tracks 5 and 6
text      the localised item strings, English / French / German
```

Nothing here is copied from the original material: it is offsets, counts, names
and structure. The extracted bytes stay in `assets/`.

Usage
    python tools/manifest.py TRACKDIR --out assets/manifest.json
    python tools/manifest.py TRACKDIR --out assets/manifest.json \\
                             --world path/to/WORLD.S
"""

import argparse
import collections
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
for sub in ("set", "script", "world", "model", "anim", "cinepak", "wave",
            "text"):
    sys.path.insert(0, os.path.join(HERE, sub))

import setx                                                  # noqa: E402
import scriptx                                               # noqa: E402
import worldx                                                # noqa: E402
import modelx                                                # noqa: E402
import animx                                                 # noqa: E402
import filmls                                                # noqa: E402
import filmwav                                               # noqa: E402
import wavex                                                 # noqa: E402

TRACKS = {2: "track02_00004000.bin", 3: "track03_data.bin",
          4: "track04_pict.bin", 5: "track05_data.bin", 6: "track06_data.bin",
          7: "track07_1111.bin", 8: "track08_data.bin"}
SCENE_STRIDE = 110


def find(d, n):
    """The extracted track n, whatever suffix jcdinfo gave it."""
    want = TRACKS.get(n, "track%02d" % n)
    p = os.path.join(d, want)
    if os.path.exists(p):
        return p
    for f in sorted(os.listdir(d)):
        if f.startswith("track%02d" % n):
            return os.path.join(d, f)
    return None


def read(d, n):
    p = find(d, n)
    return open(p, "rb").read() if p else None


def build(dirname, world_s=None):
    t2 = read(dirname, 2)
    t3 = read(dirname, 3)
    t5 = read(dirname, 5)
    t6 = read(dirname, 6)
    t7 = read(dirname, 7)
    t8 = read(dirname, 8)
    man = {}

    # ---- sets and scenes ------------------------------------------------
    sets = [setx.parse_set(t3, n) for n in range(setx.SETS)]
    scenes = {}
    for s in sets:
        for sc in s["scenes"]:
            e = scenes.setdefault(sc["id"], dict(
                id=sc["id"], group=sc["group"], camera=sc["camera"],
                block=sc["block"], scene=sc["block"] // SCENE_STRIDE,
                name=sc["name"], sets=[]))
            e["sets"].append(s["index"])
    man["scenes"] = [scenes[k] for k in sorted(scenes)]

    # ---- films, so scripts can name them --------------------------------
    film_off = filmls.films(t7)
    films = []
    for i, o in enumerate(film_off):
        info = filmls.describe(t7, o)
        pcm, ticks, _ = filmwav.film_audio(t7, o)
        films.append(dict(film=i, block=info["block"], bytes=info["bytes"],
                          chunks=info["chunks"], width=info["width"],
                          height=info["height"],
                          audio_bytes=len(pcm),
                          audio_seconds=round(len(pcm) / float(filmwav.RATE), 2),
                          triggers=[]))
    by_block = {f["block"]: f["film"] for f in films}

    # ---- scripts --------------------------------------------------------
    scene_names = {sc["id"]: sc["name"] for sc in man["scenes"]}
    scripts = []
    for n in range(setx.SETS):
        blob = scriptx.set_script(t3, n)
        if blob is None:
            continue
        sc = scriptx.Script(blob, "set %d" % n, scene_names, by_block)
        sc.disassemble()
        d = sc.summary()
        if not d["commands"]:
            continue
        d["set"] = n
        d["name"] = sets[n]["name"]
        scripts.append(d)
    main = t2[worldx.fo(scriptx.MAIN_MEM):
              worldx.fo(scriptx.MAIN_MEM) + scriptx.MAIN_END - scriptx.MAIN_MEM]
    sc = scriptx.Script(main, "MAINSCRIPT", scene_names, by_block)
    sc.disassemble()
    d = sc.summary()
    d["set"] = None
    d["name"] = "MAINSCRIPT"
    scripts.append(d)
    man["scripts"] = scripts

    # every film trigger we know of
    for s in scripts:
        for f in s["films"]:
            if f["film"] is not None:
                films[f["film"]]["triggers"].append(
                    dict(kind="script", where=s["name"]))
    for s in sets:
        for e in s["events"]:
            if e["name"] == "CINEPAK" and e["data"][2] in by_block:
                films[by_block[e["data"][2]]]["triggers"].append(
                    dict(kind="event", where=s["name"] or "set %d" % s["index"]))
    # the boot film: the 68000 writes its block straight into the film-offset
    # variable, as `move.l #<block>, $44BC.w`, encoded 21 FC <long> 44 BC
    i = 0
    while True:
        i = t2.find(bytes((0x21, 0xFC)), i)
        if i < 0:
            break
        if i % 2 == 0 and t2[i + 6:i + 8] == bytes((0x44, 0xBC)):
            blk = struct.unpack_from(">I", t2, i + 2)[0]
            if blk in by_block:
                films[by_block[blk]]["triggers"].append(
                    dict(kind="boot",
                         where="$%X" % (i - 0x12600 + 0x5000)))
        i += 2
    man["films"] = films

    # ---- sets, with their script digest ---------------------------------
    by_set = {s["set"]: s for s in scripts if s["set"] is not None}
    man["sets"] = [dict(
        index=s["index"], group=s["group"], name=s["name"],
        scenes=[sc["id"] for sc in s["scenes"]],
        own_scenes=[sc["id"] for sc in s["scenes"] if sc["group"] == s["group"]],
        entry_points=len(s["entries"]), events=len(s["events"]),
        event_types=dict(collections.Counter(e["name"] for e in s["events"])),
        collision=dict(vertices=len(s["collision"]["vertices"]),
                       triangles=len(s["collision"]["triangles"])),
        script=(dict(bytes=by_set[s["index"]]["bytes"],
                     commands=by_set[s["index"]]["commands"])
                if s["index"] in by_set else None))
        for s in sets]

    # ---- world state and character sheets -------------------------------
    ws = worldx.world(t2)
    cs = worldx.sheets(t2)
    by_addr = {s["addr"]: s["index"] for s in cs}
    for e in ws:
        e["sheet_index"] = by_addr.get(e["sheet"])
        e["parent_index"] = ((e["parent"] - worldx.WS_SRC) // worldx.WS_REC
                             if e["parent"] else None)
    ws = [e for e in ws if e["used"]]
    if world_s and os.path.exists(world_s):
        july = worldx.july_entries(world_s)
        for ji, ri in worldx.align(july, ws):
            ws[ri]["july_name"] = july[ji][0]
    man["world"] = ws
    man["sheets"] = cs

    # ---- models and animations ------------------------------------------
    man["models"] = dict(
        binary=[dict(offset=m["offset"], vertices=len(m["vertices"]),
                     facets=len(m["facets"])) for m in modelx.scan(t2)],
        track5=[dict(offset=m["offset"], vertices=len(m["vertices"]),
                     facets=len(m["facets"])) for m in modelx.scan(t5)])
    man["anims"] = dict(
        track5=[dict(offset=a["offset"], frames=a["frames"],
                     models=a["models"], fps=a["fps"]) for a in animx.scan(t5)],
        track8=[dict(offset=a["offset"], frames=a["frames"],
                     models=a["models"], fps=a["fps"]) for a in animx.scan(t8)])

    # ---- waves -----------------------------------------------------------
    waves = []
    for tn, data in ((5, t5), (6, t6)):
        for slot in range(len(data) // wavex.SLOT):
            o = slot * wavex.SLOT
            for i, (off, s) in enumerate(wavex.records(data, o, o + wavex.SLOT)):
                waves.append(dict(track=tn, slot=slot, index=i, offset=off,
                                  samples=len(s),
                                  seconds=round(len(s) / 22050.0, 3)))
    man["waves"] = waves

    # ---- text ------------------------------------------------------------
    try:
        import textx
        rows = []
        for table, *ptrs in textx.find_triples(t2, 0x12600 - 0x5000):
            row = dict(table=table)
            for lang, p in zip(textx.LANGS, ptrs):
                row[lang] = textx.decode(textx.string_at(t2, 0x12600 - 0x5000, p))
            rows.append(row)
        man["text"] = rows
    except Exception as exc:                                 # keep going
        man["text"] = []
        sys.stderr.write("text: %s\n" % exc)

    man["disc"] = dict(
        tracks={str(n): dict(file=os.path.basename(find(dirname, n) or ""),
                             bytes=len(read(dirname, n) or b""))
                for n in sorted(TRACKS)},
        scene_stride=SCENE_STRIDE, set_slot=setx.SLOT)
    return man


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trackdir")
    ap.add_argument("--out", default="assets/manifest.json")
    ap.add_argument("--world", metavar="WORLD.S")
    a = ap.parse_args()

    man = build(a.trackdir, a.world)
    json.dump(man, open(a.out, "w"), indent=1)
    named = sum(1 for e in man["world"] if e.get("july_name"))
    placed = sum(1 for f in man["films"] if f["triggers"])
    print("wrote %s" % a.out)
    print("  %d scenes, %d sets, %d scripts (%d commands)"
          % (len(man["scenes"]), len(man["sets"]), len(man["scripts"]),
             sum(s["commands"] for s in man["scripts"])))
    print("  %d world records (%d named), %d character sheets"
          % (len(man["world"]), named, len(man["sheets"])))
    print("  %d models, %d animations, %d films (%d with a trigger), %d waves"
          % (len(man["models"]["binary"]) + len(man["models"]["track5"]),
             len(man["anims"]["track5"]) + len(man["anims"]["track8"]),
             len(man["films"]), placed, len(man["waves"])))
    print("  %d localised text records" % len(man["text"]))


if __name__ == "__main__":
    main()
