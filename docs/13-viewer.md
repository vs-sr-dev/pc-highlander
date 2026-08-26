# 13 — The viewer, and the three things it had to settle

Status: **phase 3 done, phase 4 under way.** `src/` builds `hlview`, which opens
a backdrop, shows its Z-buffer, spins a model extracted from the disc, and
composites that model into a scene depth-tested against the backdrop's own Z
half. It also loads the set a view belongs to, draws its collision mesh over the
picture, and stands objects on the floor (13.7, 13.8) — and, from session 8,
assembles a character out of fifteen pieces and walks it about, which is
[14-characters.md](14-characters.md).

Everything before this phase could be read. Nothing here could: the roadmap left
three conventions explicitly unresolved, because none of them can be tested
without drawing something. All three are now settled, each against evidence that
does not depend on the other two.

---

## 13.1 The camera matrix is row major, and the world has Y up

The scene footer's nine words are the **rows** of the rotation, applied as
`v = M · (w − T)` with `T` the camera's own world position — the `ORP-VRP`
of `3DENGINE.GAS`, the object position less the view reference point, rotated.

Three independent things say so:

* **`m[1]` is zero in all 672 scenes.** Read as rows, the first row is the
  camera's *right* vector, and a right vector with no world-Y component is a
  camera with no roll. Read as columns that zero means nothing in particular.
  672 out of 672 is not a coincidence.
* **The camera's own translation has a small positive Y** where x and z are
  thousands — `CA_CAM03` is at `(-1438, 274, 2622)`. That is a camera height.
* **The collision mesh lands on the floor.** Projecting set `CA`'s 144 mesh
  vertices, at their triangle heights, into `CA_CAM03` puts the whole walkable
  area on the sand, with its far edge at the foot of the gate and its right edge
  along the base of the round building. The column reading scatters the same
  points across the sky and the rock face.

So the world is **y up, with the floor plan in x and z** — which is also what
the rest of the disc says: the collision mesh is `(x, z)` pairs with a height
per triangle, `FORMMAT.GAS` calls the rotation about y the *azimuth*, and all
197 world records have `Ypos` zero while x and z span the map.

## 13.2 The models are stored on their side, and the world records say so

The item models linked into the binary are **z-long**: the wine bottle at
`$011190` spans 19 units in x, 18 in y and 69 in z. Standing it up in a y-up
world needs a quarter turn about x, and that turn is **in the world table**.

`FORMMAT.GAS` builds an object's matrix from three angles on a **256-step
circle** — elevation about x, azimuth about y, twist about z — and 29 of the 197
world records carry `face = (192, 0, 0)`. 192 is −90°, exactly the rotation that
takes model z to world y. All 29 are radius-50 records, that is items, never
characters.

The characters need no such thing: the models on track 5 are **y-long** (a
character's fifteen pieces run from y = −141 to +140, about 280 units, against
a camera height of 274 and a collision radius of 100), and their world records
carry azimuth only — `(0, b, 0)`, the direction they face.

It is not uniform, though, and that is worth recording: of the five wine
bottles, `CA`, `G2` and `TENT3` carry the 192 and `G1` and `G3` carry zero,
which would leave them lying flat. Several other items — the turret key, a
cheese, the locket — carry zero too. Whether that is deliberate dressing or a
default something else overrides is open. `hlview` defaults an item model to
elevation 192 when it is placed by hand, and uses the record's own `face` when
it is placed by name.

## 13.3 The Z-buffer holds the view z, negated

**The stored 16-bit value is `65536 − |z|`**: the view-space z, which is
negative in front of the camera, written as a two's-complement word. Nearer is
therefore *larger*, and the distance in front of the camera is `65536 − depth`.

Measured, not assumed. For every set, for every scene that set owns, project the
collision mesh's vertices — whose distance from the camera is known exactly from
geometry alone — and read the backdrop's depth at the pixel each one lands on.
That gives **35,603 pairs** across the 48 sets, and the ratio

```
(65536 − depth) / |z|
```

peaks sharply in the bin **0.99 to 1.00**. The median is 0.80 rather than 1.00
because a mesh corner is often under something the camera sees first — a wall, a
crate, a rock — and an occluder can only make the reading *nearer*, never
further. The peak is the unoccluded floor, and it sits on 1.

Two cross-checks:

* The depth half read as a picture is a proper depth image — sky darkest, near
  ground brightest — which is the same "larger is nearer" sense.
* Along a flat stretch of floor, `1/(65536 − depth)` comes out linear in the
  screen row, which is what a plane must do under a perspective divide. Fitting
  `CA_CAM03`'s sand that way is linear to a residual of 0.00025 over 15 rows.

**One thing does not reconcile.** `3DENGINE.GAS` blits its polygons with
`ZMODELT`, "write where the source z is less than the destination", and the
vertex list it feeds the blitter carries the same negative z in the same
encoding. Taken at face value that draws the *further* pixel. Either the
comparison is not what the name suggests or the engine negates somewhere we have
not read. It does not affect the viewer, which tests `|z|` and keeps the smaller,
but it is not understood.

## 13.4 The rasteriser

`src/r3d/` follows `3DENGINE.GAS` where the source says something and says so in
comments where it does not.

```
sx = 159 + x * 300 / max(|z|, 40)
sy =  99 + y * 246 / max(|z|, 40)        246 = 300 * $347A / 2^14
row = 199 - sy
```

The row flip is the engine's: "must reverse so y=0 is drawn at bottom, y=199 is
drawn at top". `z` is clamped at 40 for the divide and the engine culls anything
nearer; the far cull is `$7E00`. Polygons are clipped against the near plane in
view space before projection, filled by scanline with the depth interpolated
linearly across the span — affine, as the blitter's `B_ZINC` is — and tested
against the buffer one pixel at a time.

The framebuffer is **RGB16 the whole way**, R5 B5 G6 as the Jaguar had it, and
is converted to RGB888 only when SDL uploads it or a screenshot is written. The
backdrop therefore loads with a `memcpy`.

**The lighting is ours** — one directional light and an ambient term, because
the set's own light list has not been located — and it is marked as such in the
code.

**The culling was ours too, and session 8 removed it.** `3DENGINE.GAS` culls a
facet by transforming its stored normal and dropping it when the result points
away; the screen-space signed area is only its fallback, for facets flagged as
having no normal. But **every facet on the disc has a normal of (0,0,0)** — all
5,548 on track 5, all 1,273 in the binary — so the engine's test always passes
and nothing is ever culled. The Z-buffer does the work instead, which for a
closed model is the same picture and for an open one is a better one.

Culling by winding is therefore wrong, and the shipped models prove it by
disagreeing about which way round they are. Taking the whole silhouette drawn
with no culling as the truth:

```
                      no culling   cull back   cull front
the wine bottle           72 px      72 px       69 px
Quentin, DUN1_CAM00      327 px     254 px      321 px
```

The item models read one way and the characters the other, which is exactly what
the export in [03-data-formats.md](03-data-formats.md) 3.3 predicts: the item
models had two axes swapped, and a swap of two axes is a mirror, so their facet
lists — untouched — come out reversed. `--cull back` and `--cull front` are
still there; neither is the default any more.

## 13.5 What the viewer measures

The success criterion is counted rather than eyeballed. `hlview` draws the model
twice: once against an empty Z-buffer, for the silhouette, and once against the
backdrop's. The difference is what the scenery hid.

```
$ hlview --scene TENT6_CAM01 --model boot:6 --pos -100,100,-100 --face 192,0,0
silhouette 72 px, 72 visible, 0 hidden by the scene
$ hlview --scene TENT6_CAM01 --model boot:6 --pos 35,100,-100 --face 192,0,0
silhouette 77 px, 36 visible, 41 hidden by the scene
$ hlview --scene TENT6_CAM01 --model boot:6 --pos 10,100,-100 --face 192,0,0
silhouette 76 px, 0 visible, 76 hidden by the scene
```

Three positions on one tent floor, 135 units apart at the widest: the bottle
in the open, the bottle with its left half cut along the edge of the tent pole, and the
bottle entirely behind the pole. Same code, same scene, one number moved.

(The middle two counts were 75 and 74 before session 8 stopped culling by
winding, 13.4. The pixels the cull was losing are a back facet or two at the
silhouette's edge; the 41 the tent pole hides did not move.)

The same criterion applied to a character, which is fifteen models rather than
one, is [14-characters.md](14-characters.md):

```
$ hlview --scene CA_CAM03 --char 0 --anim 0
silhouette 443 px, 411 visible, 32 hidden by the scene
```

## 13.6 Why objects sit below the ground the backdrop draws

Placing an item at its world position with `y` = the collision triangle's height
buries it: in `SHANR2_CAM05` the wine bottle keeps 29 pixels of 135, the rest
below the cobbles, and in `TENT6_CAM01` a bottle at height 1 is gone entirely.

That was left open with two candidate explanations. It is now measured, and the
answer is the first of them, with a caveat.

**The Z-buffer inverts.** Every pixel of a backdrop is a surface the renderer
saw, and phase 3 settled enough to invert it — the footer gives the rotation and
the camera position, `65536 − depth` gives the distance in front, and the
projection is known. So `world = campos + (65536 − depth) * (Mᵀ · d)` recovers
the world point behind every one of the 64,000 pixels.
[tools/scene/backproj.py](../tools/scene/backproj.py) does it.

**The reconstruction is sound.** Two cameras looking at the same set must agree,
and on DUN1 — comparing only cells where each camera saw one flat surface, since
a cell where either was looking at a jumble compares two different things — 71%
of 552 shared cells agree within 25 units, median difference 0.0, interquartile
range 0.0. Sets built in storeys, like D1, disagree by design: one cell in plan
holds more than one floor.

**The height word is the world y of the floor, at 1:1.** D1 is the set that can
prove it, because it has real vertical structure. Comparing the height of each
collision triangle against the surface its backdrops draw above it:

```
drawn floor = 0.962 * collision height + 36.7      r = 0.944, 239 triangles
height   1 -> floor    44        height  1401 -> floor  1407
height 401 -> floor   400        height  1673 -> floor  1680
height 801 -> floor   775        height  2473 -> floor  2468
```

Over 2,500 units, no scale factor, no offset that matters.

**What is left is a gap between the collision plane and the drawn floor**, and
session 8 measured it per set — −50 in `SHANR3` to +348 in `PRI`, with `CA` at
155 and `DUN1` at 12. **That table is superseded.** The estimator behind it took
the modal reconstructed height over a collision triangle's whole plan footprint,
which holds the walls standing on the triangle and the props sitting on it as
well as the floor; measured on the same triangle from two cameras it disagrees
with itself by a median of 300 units, which is more than the gap it was reporting.

Re-measured as the lowest flat surface a camera sees in a patch of plan — nothing
is below the floor — and checked again by the engine itself, raising a model
until the backdrop's Z-buffer stops hiding any of it, the gap is **local relief
in the art of a few tens of units** rather than a per-set datum. `PRI` comes down
from 348 to about 50, `CA` from 155 to between 20 and 90 depending where in `CA`
you stand, and `DUN1` stays at about 12. The whole of it, and why there was never
an engine correction to find, is [14-characters.md](14-characters.md) 14.7 and
[10-set-track.md](10-set-track.md) 10.7.

The second candidate, a vertical offset in the model, is ruled out for items:
the nineteen models in the binary carry **no origin points at all**. The
characters do, but they are a skeleton rather than a datum — the torso publishes
origins numbered 128, 135, 138 and 141, and the next pieces are anchored at 135,
136, 137, which is how the fifteen parts chain together. That is phase 4's
business, not this question's.

## 13.7 The floor, in the viewer

The viewer loads the set a view belongs to — `src/game/set.c` reads track 3
directly and agrees with `setx.py` set for set — and uses it for two things.

`--mesh` draws the collision mesh over the backdrop, walls red and edges with a
neighbour green. In `CA_CAM03` its right-hand boundary runs along the base of
the round building and its far edge stops at the foot of the gate; in
`DUN1_CAM00` the red line sits exactly at the foot of the palisade. It is drawn
*without* the depth test, deliberately: the collision plane is under the drawn
ground by the amounts in 13.6, so testing it against the backdrop hides the
whole mesh.

An object is then put on the floor rather than at y = 0, which is what every one
of the 197 world records carries. `--no-ground` turns that off.

`--check-mesh` is the regression test for the triangle search, and 13.8 is what
it found.

## 13.8 The triangle search is a list, not a walk

`FINDTRI.GAS` says so in a comment, and says why:

> so we can't just keep jumping to the first edge which the test point is
> outside of, because it would be possible to generate a structure where a
> point couldn't ever be reached (due to looping)

Written the greedy way — step across whichever edge the point is beyond, keep a
visited list — it fails on **258 of the 5,342 triangles**. Written the way the
comment describes, as a search that puts the current triangle on a list and adds
each triangle's three neighbours as it goes, it fails on none of them.

`--check-mesh` measures both ends of that. For every triangle of every set it
takes the centroid, which is inside that triangle by construction, and searches
for it:

```
mesh walk vs scan: 5342 triangles, 0 disagreements, 0 centroids the scan
  itself could not place
  from an arbitrary start: 5295 found across the adjacency, 84.3 triangles
  examined on average, 47 gave up into a full scan
  from five triangles away, which is where movement always starts: 5342
  searches, 0 wrong, 0 gave up, 5.1 triangles examined on average
```

The 47 are not failures: 19 of the 48 meshes come in more than one piece, and an
arbitrary start can be on an island the target is not on. From a connected
start — the only case movement is ever in, since a character's previous triangle
is where the search begins — it is 5,342 out of 5,342.

## 13.9 Building and running

SDL3, C99, one Makefile. On Windows that means an MSYS2 mingw64 shell
(`pacman -S mingw-w64-x86_64-sdl3`).

```
make                            # -> build/hlview
build/hlview --scene CA_CAM03                       # a backdrop
build/hlview --scene CA_CAM03 --depth               # its Z half as grey
build/hlview --model boot:6 --spin                  # the wine bottle, turning
build/hlview --scene TENT6_CAM01 --object #190 --model boot:6   # that tent's own bottle
build/hlview --scene DUN1_CAM00 --mesh                 # the floor, over the art
build/hlview --check-mesh                             # the triangle search, checked
build/hlview --scene DUN1_CAM00 --char 0 --anim 10 --walk   # a character on it
build/hlview --check-char                             # the pose, checked
build/hlview --scene DUN1_CAM00 --char 0 --drive       # the pad drives him
build/hlview --scene DUN1_CAM00 --char 0 --drive --alone   # ...without Ramirez
build/hlview --check-doors | --check-follow           # the doorways; the follow
build/hlview --list-scenes | --list-models | --list-objects | --list-chars
build/hlview --list-sheets                            # the 40 character sheets
build/hlview --film 19                                # a Cinepak film, 12 fps
build/hlview --check-film                             # all 36 of them, decoded
```

A film is 320x240 where the game draws 320x200, so the window changes shape for
it and back again afterwards; `--scale` applies to both.

`--shot FILE.ppm` writes a frame and `--shot-at N` writes it at frame `N`
rather than the first, `--no-window` renders without opening one, so
every picture in this document is reproducible from a command line. Scenes are
named from `assets/manifest.json` — `CA_CAM03`, not an index — and world records
by their July names, `CA_WINE` or `#5`.

In the window: `[` and `]` step through scenes, `z` toggles the depth view,
arrow keys nudge the object over the floor, `space` starts and stops the spin,
`s` writes a screenshot, `q` quits.
