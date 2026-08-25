# 13 — The viewer, and the three things it had to settle

Status: **phase 3 done.** `src/` builds `hlview`, which opens a backdrop, shows
its Z-buffer, spins a model extracted from the disc, and composites that model
into a scene depth-tested against the backdrop's own Z half.

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

Two things are ours rather than the game's, and are marked as such in the code:
**the lighting** — one directional light and an ambient term, because the set's
own light list has not been located — and **backface culling by screen-space
signed area**, because the shipped models are not consistently wound.

## 13.5 What the viewer measures

The success criterion is counted rather than eyeballed. `hlview` draws the model
twice: once against an empty Z-buffer, for the silhouette, and once against the
backdrop's. The difference is what the scenery hid.

```
$ hlview --scene TENT6_CAM01 --model boot:6 --pos -100,100,-100 --face 192,0,0
silhouette 74 px, 74 visible, 0 hidden by the scene
$ hlview --scene TENT6_CAM01 --model boot:6 --pos 35,100,-100 --face 192,0,0
silhouette 74 px, 33 visible, 41 hidden by the scene
$ hlview --scene TENT6_CAM01 --model boot:6 --pos 10,100,-100 --face 192,0,0
silhouette 74 px, 0 visible, 74 hidden by the scene
```

Three positions on one tent floor, 135 units apart at the widest: the bottle
in the open, the bottle with its left half cut along the edge of the tent pole, and the
bottle entirely behind the pole. Same code, same scene, one number moved.

## 13.6 Open: objects sit below the ground the backdrop draws

Placing an item at its world position with `y` = the collision triangle's height
buries it. It is not subtle — in `SHANR2_CAM05` the wine bottle keeps 29 pixels
of 135, the rest below the cobbles; in `TENT6_CAM01` a bottle at height 1 is
gone entirely, and only clears the floor at about y = 100.

The offset is set-dependent and in the range of roughly 50 to 130 units, against
a character 280 tall. Two explanations fit and we cannot yet separate them:

* the backdrops model ground with relief — cobbles, sand, a raised tent floor —
  and the collision mesh is a flat plane through the bottom of it;
* the engine places a model's *origin* somewhere above the ground, which it must
  do anyway: a character's origin is mid-body, and a bottle's is 18 units above
  its base.

Phase 4 has to answer this to stand a character on a floor, and the character
sheets are the place to look.

## 13.7 Building and running

SDL3, C99, one Makefile. On Windows that means an MSYS2 mingw64 shell
(`pacman -S mingw-w64-x86_64-sdl3`).

```
make                            # -> build/hlview
build/hlview --scene CA_CAM03                       # a backdrop
build/hlview --scene CA_CAM03 --depth               # its Z half as grey
build/hlview --model boot:6 --spin                  # the wine bottle, turning
build/hlview --scene TENT6_CAM01 --object #190 --model boot:6   # that tent's own bottle
build/hlview --list-scenes | --list-models | --list-objects
```

`--shot FILE.ppm` writes a frame, `--no-window` renders without opening one, so
every picture in this document is reproducible from a command line. Scenes are
named from `assets/manifest.json` — `CA_CAM03`, not an index — and world records
by their July names, `CA_WINE` or `#5`.

In the window: `[` and `]` step through scenes, `z` toggles the depth view,
arrow keys nudge the object over the floor, `space` starts and stops the spin,
`s` writes a screenshot, `q` quits.
