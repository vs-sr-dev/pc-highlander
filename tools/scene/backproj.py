#!/usr/bin/env python3
"""backproj - turn a scene's Z-buffer back into world coordinates.

Every pixel of a backdrop is a surface the renderer saw, and phase 3 settled
enough to invert it: the camera footer gives the rotation and the camera's world
position, the depth half gives the distance in front of the camera as
`65536 - depth` ([docs/13-viewer.md](../../docs/13-viewer.md) 13.3), and the
projection is the engine's.  So

    d     = ((col - 159) / 300,  ((199 - row) - 99) / 246,  -1)
    world = campos + (65536 - depth) * (M^T . d)

recovers, for every one of the 64,000 pixels, the point in the world it shows.

That is worth having as a tool for two reasons.

**It checks the engine.**  Two cameras looking at the same set must reconstruct
the same world.  `--check SET` measures that, and it uses no model, no collision
data and no assumption about what is floor.  It compares only cells where each
camera saw *one flat surface*, since a cell where either was looking at a jumble
compares two different things.  On DUN1 that leaves 552 shared cells, of which
71% agree within 25 units - median difference 0.0, interquartile range 0.0.
Sets built in levels, like D1, disagree by design: a cell in plan holds more
than one floor.

**It measures the ground.**  `--ground SET` compares the height word of each
collision triangle against the height of the surface actually drawn above it.
The two agree in scale - fitting D1, which is built in storeys, gives
`floor = 0.962 * height + 36.7` with r = 0.94 over 239 triangles and heights
running from 1 to 2473, where the top storey's floor reconstructs to 2468 - so
the collision height is the world y of the floor, at 1:1, over the whole range.
What is left over is relief in the art that the flat mesh approximates: about
12 units in DUN1, about 70 under the wine bottle in TENT6.

Usage
    python tools/scene/backproj.py TRACK4 --boot TRACK2 --scene CA_CAM03 --probe X,Z
    python tools/scene/backproj.py TRACK4 --boot TRACK2 --check DUN1
    python tools/scene/backproj.py TRACK4 --boot TRACK2 --ground D1
    python tools/scene/backproj.py TRACK4 --boot TRACK2 --scene CA_CAM03 --xyz OUT.txt

--check and --ground need assets/manifest.json and assets/sets.json; point at
them with --manifest and --sets if they are not where they usually are.
"""

import argparse
import collections
import json
import struct
import sys

import numpy as np

SLOT = 258720
HALF = 128000
W, H = 320, 200
CX, CY = 159, 99
XSCALE, YSCALE = 300, 246
KEY_ADDR, KEY_LEN, CODE_BASE = 0x30610, 8192, 0x5000

COL, ROW = np.meshgrid(np.arange(W), np.arange(H))
RAY = np.dstack([(COL - CX) / float(XSCALE),
                 ((H - 1 - ROW) - CY) / float(YSCALE),
                 -np.ones((H, W))])


def find_key(boot):
    tag = b"CODE" * 16
    i = boot.find(tag)
    if i < 0:
        raise SystemExit("no CODE section in the boot track - wrong file?")
    off = i + len(tag) + (KEY_ADDR - CODE_BASE)
    return boot[off:off + KEY_LEN]


def read_scene(fh, n, key):
    fh.seek(n * SLOT)
    slot = fh.read(SLOT)
    pad = np.resize(np.frombuffer(key, np.uint8), HALF)
    a = np.frombuffer(slot[:256000], np.uint8).copy()
    a[:HALF] ^= pad
    a[HALF:] ^= pad
    words = np.frombuffer(a.tobytes(), ">u2")
    depth = words[64000:].reshape(H, W).astype(np.float64)
    foot = slot[256000:256048]
    m = np.array(struct.unpack_from(">9h", foot, 2), float).reshape(3, 3) / 16384.0
    t = np.array(struct.unpack_from(">3i", foot, 20), float)
    return depth, m, t


def world_points(depth, m, t, near=60, far=20000):
    """Every pixel as a world point, plus a mask of the ones worth believing."""
    u = 65536.0 - depth
    pts = t + u[..., None] * (RAY @ m)      # M^T . d is RAY @ M for row-major M
    return pts, (u > near) & (u < far)


def load_index(args):
    manifest = json.load(open(args.manifest))
    sets = json.load(open(args.sets))
    return manifest, sets


def scenes_of(manifest, setname):
    st = [s for s in manifest["sets"] if s["name"] == setname]
    if not st:
        raise SystemExit("no set called %s" % setname)
    by_id = {s["id"]: s for s in manifest["scenes"]}
    return [by_id[i] for i in st[0]["own_scenes"] if i in by_id]


def cmd_probe(fh, key, manifest, name, target, radius):
    sc = [s for s in manifest["scenes"] if s["name"] == name]
    if not sc:
        raise SystemExit("no scene called %s" % name)
    depth, m, t = read_scene(fh, sc[0]["scene"], key)
    pts, ok = world_points(depth, m, t)
    d = np.hypot(pts[:, :, 0] - target[0], pts[:, :, 2] - target[1])
    sel = ok & (d < radius)
    print("%s: %d drawn pixels within %g units of (%g, %g)"
          % (name, sel.sum(), radius, target[0], target[1]))
    if sel.sum():
        y = pts[:, :, 1][sel]
        print("  surface y  min %8.1f  p10 %8.1f  median %8.1f  p90 %8.1f  max %8.1f"
              % (y.min(), np.percentile(y, 10), np.median(y),
                 np.percentile(y, 90), y.max()))


def floor_cells(fh, key, scene, cell, min_pts=20, max_spread=40):
    """The height of the surface in each cell, but only where the camera saw
    one flat surface there.  Comparing cells where either camera was looking at
    a jumble compares different things and measures nothing."""
    depth, m, t = read_scene(fh, scene, key)
    pts, ok = world_points(depth, m, t)
    p = pts[ok]
    grid = collections.defaultdict(list)
    for x, y, z in p:
        grid[(int(x // cell), int(z // cell))].append(y)
    out = {}
    for k, v in grid.items():
        if len(v) < min_pts:
            continue
        v = np.array(v)
        lo, hi = np.percentile(v, [25, 75])
        if hi - lo <= max_spread:
            out[k] = np.median(v)
    return out


def cmd_check(fh, key, manifest, setname, cell, limit, args_min=20, args_spread=40):
    scenes = scenes_of(manifest, setname)[:limit]
    maps = [(s["name"], floor_cells(fh, key, s["scene"], cell, args_min, args_spread))
            for s in scenes]
    diffs = []
    for i in range(len(maps)):
        for j in range(i + 1, len(maps)):
            a, b = maps[i][1], maps[j][1]
            diffs += [a[k] - b[k] for k in set(a) & set(b)]
    if len(diffs) < 20:
        print("%s: not enough overlap between cameras" % setname)
        return
    d = np.array(diffs)
    print("%s: %d cameras, %d shared %g-unit cells where each camera saw one"
          " flat surface" % (setname, len(maps), len(d), cell))
    print("  camera-to-camera difference in the reconstructed surface:")
    print("    median %7.1f   within 25: %.1f%%   within 50: %.1f%%   IQR %.1f"
          % (np.median(d), 100 * (np.abs(d) < 25).mean(),
             100 * (np.abs(d) < 50).mean(),
             np.percentile(d, 75) - np.percentile(d, 25)))


def cmd_ground(fh, key, manifest, sets, setname, limit):
    st = [s for s in sets if s["name"] == setname]
    if not st:
        raise SystemExit("no set called %s in the sets file" % setname)
    st = st[0]
    verts = np.array(st["collision"]["vertices"], float)
    tris = st["collision"]["triangles"]
    scenes = scenes_of(manifest, setname)[:limit]
    pts = []
    for s in scenes:
        depth, m, t = read_scene(fh, s["scene"], key)
        p, ok = world_points(depth, m, t)
        pts.append(p[ok])
    pts = np.vstack(pts)
    plan = pts[:, [0, 2]]

    rows = []
    for i, tri in enumerate(tris):
        a, b, c = [verts[k] for k in tri["verts"]]
        d1 = (b[0] - a[0]) * (plan[:, 1] - a[1]) - (b[1] - a[1]) * (plan[:, 0] - a[0])
        d2 = (c[0] - b[0]) * (plan[:, 1] - b[1]) - (c[1] - b[1]) * (plan[:, 0] - b[0])
        d3 = (a[0] - c[0]) * (plan[:, 1] - c[1]) - (a[1] - c[1]) * (plan[:, 0] - c[0])
        inside = ~(((d1 < 0) | (d2 < 0) | (d3 < 0)) & ((d1 > 0) | (d2 > 0) | (d3 > 0)))
        if inside.sum() < 150:
            continue
        y = pts[inside, 1]
        hist, edges = np.histogram(y, bins=np.arange(y.min() - 20, y.max() + 40, 20))
        mode = (edges[hist.argmax()] + edges[hist.argmax() + 1]) / 2
        rows.append((tri["height"], mode, int(inside.sum())))

    if len(rows) < 4:
        print("%s: too few triangles with enough coverage" % setname)
        return
    a = np.array([(r[0], r[1]) for r in rows], float)
    print("%s: %d cameras, %d triangles covered" % (setname, len(scenes), len(rows)))
    if a[:, 0].std() > 0:
        design = np.vstack([a[:, 0], np.ones(len(a))]).T
        sol, _, _, _ = np.linalg.lstsq(design, a[:, 1], rcond=None)
        print("  drawn floor = %.3f * collision height + %.1f   r = %.3f"
              % (sol[0], sol[1], np.corrcoef(a[:, 0], a[:, 1])[0, 1]))
    by_h = collections.defaultdict(list)
    for h, mode, n in rows:
        by_h[h].append(mode)
    print("  height   triangles   drawn floor (median)   difference")
    for h in sorted(by_h):
        v = np.array(by_h[h])
        print("  %6d   %9d   %20.1f   %10.1f" % (h, len(v), np.median(v), np.median(v) - h))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("track4")
    ap.add_argument("--boot", required=True, help="extracted track 2")
    ap.add_argument("--manifest", default="assets/manifest.json")
    ap.add_argument("--sets", default="assets/sets.json")
    ap.add_argument("--scene", help="a scene name, for --probe and --xyz")
    ap.add_argument("--probe", metavar="X,Z", help="report the drawn surface there")
    ap.add_argument("--radius", type=float, default=40)
    ap.add_argument("--xyz", metavar="FILE", help="write the whole cloud as x y z")
    ap.add_argument("--check", metavar="SET", help="camera-to-camera agreement")
    ap.add_argument("--ground", metavar="SET", help="collision height vs drawn floor")
    ap.add_argument("--cell", type=float, default=150)
    ap.add_argument("--cameras", type=int, default=16, help="cap for --check/--ground")
    args = ap.parse_args()

    key = find_key(open(args.boot, "rb").read())
    fh = open(args.track4, "rb")
    manifest = json.load(open(args.manifest))

    if args.probe:
        if not args.scene:
            raise SystemExit("--probe needs --scene")
        x, z = (float(v) for v in args.probe.split(","))
        cmd_probe(fh, key, manifest, args.scene, (x, z), args.radius)
    if args.xyz:
        if not args.scene:
            raise SystemExit("--xyz needs --scene")
        sc = [s for s in manifest["scenes"] if s["name"] == args.scene][0]
        depth, m, t = read_scene(fh, sc["scene"], key)
        pts, ok = world_points(depth, m, t)
        np.savetxt(args.xyz, pts[ok], fmt="%.1f")
        print("wrote %d points to %s" % (ok.sum(), args.xyz))
    if args.check:
        cmd_check(fh, key, manifest, args.check, args.cell, args.cameras)
    if args.ground:
        cmd_ground(fh, key, manifest, json.load(open(args.sets)), args.ground,
                   args.cameras)
    if not (args.probe or args.xyz or args.check or args.ground):
        raise SystemExit("nothing to do - try --check DUN1")


if __name__ == "__main__":
    main()
