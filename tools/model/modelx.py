#!/usr/bin/env python3
"""modelx - find and extract the polygon models.

The format is the one `SKELSKIN.EXE` emitted in 1995 and it did not change for
the retail build.  A model is a 24-byte header, a vertex list and a facet list:

    +0   .w   total length in bytes
    +2   .b   origin number
    +3   .b   number of origins
    +4   .w   vertex count
    +6   .w   facet count
    +8   .l   VLP -> vertex list      always header + 24
    +12  .l   FLP -> facet list       always VLP + (vertices + origins) * 8
    +16  .l   SLP                     0 if absent; otherwise it points at the
                                       byte right after the facet list, and the
                                       declared length covers that payload too
    +20  .l   CLP                     0 on every model seen so far

    vertex   4 words: x, y, z, 1
    facet    colour, Nx, Ny, Nz, vertex count, word count   (6 words)
             then <word count> longs of byte vertex indices

Models turn up in two places.  Track 5 holds them as CD-loadable records, each
preceded by a long holding its length and each linked at a load base of $4000,
so every VLP reads $4018.  The item models - the wine bottle, the cheese, the
loaf, the key, the locket - are not on that track at all: they are linked into
the resident binary in the boot track, at their real addresses.  This tool finds
both, by checking the pointers against the counts and then walking the facet
list to see whether it lands where the declared length says it should.

Usage
    python tools/model/modelx.py FILE [--list] [--obj DIR] [--png DIR]
                                 [--json FILE] [--index N] [--min-verts N]

The models are stored with **Z up**: comparing the shipped wine bottle against
`MERLOT79.INC` shows the exporter swapped the Y and Z axes and rescaled by about
0.28, leaving the facet list untouched.
"""

import argparse
import json
import os
import struct
import sys

HEADER = 24


def rgb(px):
    """Jaguar RGB16 is R5 B5 G6."""
    return (((px >> 11) & 0x1F) * 255 // 31,
            (px & 0x3F) * 255 // 63,
            ((px >> 6) & 0x1F) * 255 // 31)


def parse(d, o):
    """Parse a model at file offset o, or return None if it is not one."""
    if o + HEADER > len(d):
        return None
    ln, onum, norig, nv, nf, vlp, flp, slp, clp = struct.unpack_from(
        ">HBBHHIIII", d, o)
    if not (HEADER < ln <= 0x20000 and 0 < nv < 4000 and 0 < nf < 8000):
        return None
    if flp != vlp + (nv + norig) * 8:
        return None
    base = vlp - HEADER                     # the address this model is linked at
    vo = o + HEADER
    if vo + (nv + norig) * 8 > len(d):
        return None
    verts = [struct.unpack_from(">4h", d, vo + i * 8) for i in range(nv + norig)]

    fo = vo + (nv + norig) * 8
    facets = []
    for _ in range(nf):
        if fo + 12 > len(d):
            return None
        colour, nx, ny, nz, nverts, nwords = struct.unpack_from(">6H", d, fo)
        if not 0 < nverts <= 32 or nwords * 4 < nverts or nwords > 8:
            return None
        idx = list(d[fo + 12:fo + 12 + nwords * 4])[:nverts]
        facets.append(dict(colour=colour, normal=[struct.unpack(">h", struct.pack(">H", v))[0]
                                                  for v in (nx, ny, nz)],
                           verts=idx))
        fo += 12 + nwords * 4
    consumed = fo - o
    # The declared length covers the facet list, and the SLP payload after it
    # when there is one.  SLP always points at the byte right past the facets.
    if consumed == ln:
        extra = b""
    elif slp == base + consumed and consumed < ln <= consumed + 0x400:
        extra = d[o + consumed:o + ln]
    else:
        return None
    return dict(offset=o, base=base, length=ln, origin=onum, origins=norig,
                vertices=verts[:nv], origin_points=verts[nv:], facets=facets,
                slp=slp, clp=clp, extra=extra.hex())


def scan(d, min_verts=1):
    out = []
    o = 0
    while o + HEADER < len(d):
        m = parse(d, o)
        if m and len(m["vertices"]) >= min_verts:
            out.append(m)
            o += m["length"]
        else:
            o += 2
    return out


def write_obj(m, path, name):
    with open(path + ".obj", "w") as f, open(path + ".mtl", "w") as mtl:
        f.write("# extracted by modelx from offset 0x%x, linked at $%06X\n" %
                (m["offset"], m["base"]))
        f.write("mtllib %s.mtl\n" % name)
        for x, y, z, _ in m["vertices"]:
            f.write("v %d %d %d\n" % (x, y, z))
        seen = {}
        for i, fa in enumerate(m["facets"]):
            c = fa["colour"]
            if c not in seen:
                seen[c] = "c%04X" % c
                r, g, b = rgb(c)
                mtl.write("newmtl %s\nKd %.4f %.4f %.4f\n\n" %
                          (seen[c], r / 255, g / 255, b / 255))
            f.write("usemtl %s\n" % seen[c])
            f.write("f " + " ".join(str(v + 1) for v in fa["verts"]) + "\n")


def write_png(m, path, size=(240, 300), up=2):
    """A quick painter's-algorithm view, so a model can be eyeballed at once."""
    from PIL import Image, ImageDraw
    V = [v[:3] for v in m["vertices"]]
    if not V:
        return
    axes = list(zip(*V))
    hor, dep = 0, ({0, 1, 2} - {0, up}).pop()
    W, H = size
    lo = (min(axes[hor]), min(axes[up]))
    hi = (max(axes[hor]), max(axes[up]))
    s = min((W - 20) / max(1, hi[0] - lo[0]), (H - 20) / max(1, hi[1] - lo[1]))

    def proj(v):
        return (10 + (v[hor] - lo[0]) * s, H - 10 - (v[up] - lo[1]) * s)

    img = Image.new("RGB", size, (24, 24, 28))
    dr = ImageDraw.Draw(img)
    for f in sorted(m["facets"],
                    key=lambda f: -sum(V[i][dep] for i in f["verts"]) / len(f["verts"])):
        n = f["normal"]
        ln = max(1.0, (n[0] ** 2 + n[1] ** 2 + n[2] ** 2) ** 0.5)
        shade = 0.45 + 0.55 * max(0.0, (0.4 * n[hor] + 0.5 * n[up] +
                                        0.75 * n[dep]) / ln)
        r, g, b = rgb(f["colour"])
        dr.polygon([proj(V[i]) for i in f["verts"]],
                   fill=(int(r * shade), int(g * shade), int(b * shade)))
    img.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--obj", metavar="DIR", help="write one OBJ+MTL per model")
    ap.add_argument("--json", metavar="FILE")
    ap.add_argument("--png", metavar="DIR", help="write a quick preview per model")
    ap.add_argument("--up", type=int, default=2, choices=(0, 1, 2),
                    help="which axis points up in the preview (default 2, Z)")
    ap.add_argument("--index", type=int, help="only this model")
    ap.add_argument("--min-verts", type=int, default=1)
    args = ap.parse_args()

    d = open(args.file, "rb").read()
    models = scan(d, args.min_verts)
    if args.index is not None:
        models = [models[args.index]]

    if args.list or not (args.obj or args.json):
        print("%-4s %-10s %-8s %6s %6s %7s %s" %
              ("#", "offset", "linked", "verts", "faces", "bytes", "origin"))
        for i, m in enumerate(models):
            print("%-4d 0x%08x $%06X %6d %6d %7d  %d/%d" %
                  (i, m["offset"], m["base"], len(m["vertices"]),
                   len(m["facets"]), m["length"], m["origin"], m["origins"]))

    if args.obj:
        os.makedirs(args.obj, exist_ok=True)
        for i, m in enumerate(models):
            name = "model%04d_%06X" % (i, m["base"])
            write_obj(m, os.path.join(args.obj, name), name)
        print("wrote %d models to %s" % (len(models), args.obj))

    if args.png:
        os.makedirs(args.png, exist_ok=True)
        for i, m in enumerate(models):
            write_png(m, os.path.join(args.png, "model%04d_%06X.png" %
                                      (i, m["base"])), up=args.up)
        print("wrote %d previews to %s" % (len(models), args.png))

    if args.json:
        with open(args.json, "w") as f:
            json.dump(models, f, indent=1)

    print("%d models" % len(models), file=sys.stderr)


if __name__ == "__main__":
    main()
