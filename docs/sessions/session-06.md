# Session 6 — Phase 3: something on screen

Phase 2 read the disc. This session drew it. `src/` now builds **`hlview`**, an
SDL3 viewer over a 320x200 RGB16 framebuffer, and the roadmap's phase-3 success
criterion — *the model passes correctly behind scenery* — is met and, better
than met, **counted**.

The session's real work was not the rasteriser. It was the three conventions
session 5 refused to guess at, each of which had to be settled against evidence
before anything drawn could be trusted.

---

## 1. The camera matrix is row major, over a world with Y up

Session 5 called the row/column order "a coin flip until a scene comes out
right". It is not a coin flip: **`m[1]` is zero in all 672 scenes.** Read as
rows, that word is the y component of the camera's own right vector, and a right
vector with no vertical component is a camera with no roll — which every fixed
camera in a game like this has. Read as columns it is an accident that would
have to happen 672 times.

Two more, independent:

* the translation's y is small and positive where x and z are thousands —
  `CA_CAM03` sits at `(-1438, 274, 2622)`, which is a camera height;
* projecting set `CA`'s collision mesh through `v = M · (w − T)` lays the whole
  walkable area on the sand, its far edge at the foot of the gate and its right
  edge along the base of the round building. The column reading throws the same
  144 points into the sky and across the rock face.

So the world is **y up with the floor plan in x and z**, which is what the rest
of the disc had been saying all along: the collision mesh is `(x, z)` pairs with
a height per triangle, `FORMMAT.GAS` calls the rotation about y the *azimuth*,
and all 197 world records have `Ypos` = 0 while x and z span the map.

## 2. The Z-buffer holds the view z, negated — and it is measured

**`depth = 65536 − |z|`**: the view-space z, negative in front of the camera,
stored as a two's-complement word. Nearer is *larger*.

The measurement is the point. A collision-mesh vertex has a world position, so
its distance from any camera follows from geometry alone, with no reference to
the depth half at all. Project every set's mesh into every scene that set owns,
read the stored depth at the pixel each vertex lands on, and you have **35,603
pairs** of (known `|z|`, stored depth) spread over the 48 sets and two decades
of distance. The ratio

```
(65536 − depth) / |z|
```

peaks in the bin **0.99 to 1.00**. Its median is 0.80, and that is the shape of
the evidence rather than a defect: a mesh corner is often under a wall or a
crate, and an occluder can only make the reading nearer, never further. The
unoccluded floor is the peak, and the peak is on 1.

One thing does not reconcile and is written down rather than smoothed over:
`3DENGINE.GAS` draws with `ZMODELT`, "write where the source is less than the
destination", over a vertex list carrying the same negative z in the same
encoding — which taken literally keeps the *further* pixel. Either the mode does
not mean what its name says or the engine negates somewhere we have not read.

## 3. The item models are stored on their side, and the world table stands them up

Session 5 carried "models are Z up" forward from §3.3. That is true of the
nineteen item models linked into the binary — the wine bottle spans 19 units in
x, 18 in y and 69 in z — and **false of the 220 characters on track 5**, whose
fifteen pieces run along y from −141 to +140.

The world table reconciles them, and says so explicitly. `FORMMAT.GAS` builds an
object's matrix from three angles on a **256-step circle**, elevation about x
first, and **29 of the 197 world records carry `face = (192, 0, 0)`** — −90° of
elevation, exactly the quarter turn that takes model z to world y. All 29 are
radius-50 records, that is items; every character carries azimuth only, the
direction it faces.

So the two model conventions and the one world convention are consistent, and it
is the data that says how, not us.

Not uniformly, though: three of the five wine bottles carry the 192 and two do
not, and the turret key, a cheese and the locket carry zero as well. Dressing,
or a default overridden elsewhere — open.

## 4. The engine

```
src/main.c        the viewer and, for now, the whole front end
src/game/         scene.c   slot, XOR key, colour, depth, camera footer
                  model.c   the SKELSKIN polyhedron format
src/r3d/          r3d.c     projection, matrices, scanline fill, Z-buffer
src/platform/     window.c  the only file that knows SDL exists
src/util/         io.c, json.c
```

One decision worth recording: **the engine reads the disc, not the tools'
output.** It decodes a scene slot itself and parses the model format itself,
rather than loading the PNGs and OBJs `scenex` and `modelx` write. The only
derived file it reads is `assets/manifest.json`, which was always meant to be
the engine's index — so it needed a JSON reader, and got a small one. As a
side-effect the C model scanner finds the same 19 models at the same nineteen
addresses as `modelx.py`, which is a free cross-check on both.

The framebuffer stays **RGB16 the whole way**, R5 B5 G6 exactly as the Jaguar
had it, and is converted to RGB888 only when SDL uploads it or a screenshot is
written; loading a backdrop is a `memcpy`. Two things in the rasteriser are ours
rather than the game's and are commented as such: the lighting, because the
set's own light list has not been located, and backface culling by screen-space
signed area, because the shipped models are not consistently wound.

## 5. The criterion, counted

`hlview` draws the model twice — once against an empty Z-buffer for the
silhouette, once against the backdrop's — so "passes behind scenery" is a
number, not an impression. Three positions on the floor of one tent, 135 units
apart at the widest:

```
--pos -100,100,-100     silhouette 74 px, 74 visible,  0 hidden by the scene
--pos   35,100,-100     silhouette 74 px, 33 visible, 41 hidden by the scene
--pos   10,100,-100     silhouette 74 px,  0 visible, 74 hidden by the scene
```

In the middle frame the bottle's left half is cut exactly along the edge of the
tent pole. Same code, same scene, one coordinate moved.

→ [13-viewer.md](../13-viewer.md), [src/README.md](../../src/README.md)

## 6. Two wrong turns, kept because they cost time

**The vertical projection that wasn't.** Assuming `CA_CAM03`'s sand is the
collision plane at height 1 and solving for the screen centre and the y scale
that make it flat gives `(66.8, 157.2)` with a residual of 0.00025 over fifteen
rows — a beautiful fit, and wrong. The fit is **degenerate**: scaling the camera
height rescales the required direction affinely, so any camera height admits an
equally perfect `(cy, ys)`, and a single flat surface cannot separate the three.
The collision-mesh overlay broke the tie in a second — at `ys = 157` the mesh
collapses into the bottom fifth of the frame and the whole mid-ground falls
outside the walkable area. The engine's `(99, 246)` stands, and `CA`'s sand
simply slopes.

**"Every facet culled".** Several `CA` cameras drew nothing at all with every
facet dropped, which read like a winding bug. It was the near clip doing its
job: those cameras have the bottle *behind* them, and `r3d_project` takes `|z|`
the way the GPU's divide does, so the probe happily reported a screen position
for a point that was never in front. The probe now reports the sign.

---

## Still open

* **Objects sink into the ground the backdrop draws.** Placed at the collision
  triangle's height, an item is 50 to 130 units below the visible floor,
  set-dependent: in `SHANR2_CAM05` the bottle keeps 29 pixels of 135, the rest
  under the cobbles, and in `TENT6_CAM01` it only clears the floor near y = 100.
  Either the backdrops model ground relief the flat collision mesh approximates,
  or the engine places a model's origin above the ground — which it must do
  anyway, since a character's origin is mid-body. **This is phase 4's first
  question** and the character sheets are where to look.
* **`ZMODELT`** (§2) — the depth comparison's sense in the original.
* **Track 9**, the three unreferenced films, the 125 unnamed world records, the
  seventeen unnamed GPU modules, the `SLP` payload, `cshBehaviour`, `gvar[0..2]`
  — all as session 5 left them. None of it blocks phase 4.

## TODO for session 7 — phase 4, the world exists

The roadmap's phase 4: port the data structures, set loading, the collision mesh
and its triangle search, character movement with ground height and stairs, and
cameras that cut when an event line is crossed. **Success criterion: you can
walk around `DUN1` and the camera cuts where it should.**

Build order, smallest first:

1. **A set loads.** `setx.py` already reads the whole set track; the engine needs
   its own reader for the scene table, the init table, the collision mesh and
   the event list. `assets/sets.json` is the reference to check it against.
2. **Stand something on the floor.** `FINDTRI.GAS`'s triangle search, then the
   ground height, then the offset question above — resolve it before anything
   else, because every later thing inherits it. The stairs in `SHANR2` and
   `SHANR3`, whose triangles step 37 to 38 units apart, are the test case.
3. **A character, assembled.** Fifteen pieces from one bundle in one place, at
   one animation frame. That is the first time the animation format and the
   character sheet have to agree with each other.
4. **Move.** Walk the mesh with collision against triangle edges, and the
   camera cutting on `SCENE` events — 62 of `CA`'s 66 events are that type, so
   the data to drive it is already extracted.

Two things worth doing whenever there is a spare hour, both of which make
everything after them easier to check:

* **A frame comparator.** Render a scene, run the same scene in an emulator,
  diff. It is the only way to keep "faithful" honest, and it gets harder to
  retrofit the further the engine goes.
* **Names.** 125 world records are anonymous; some will name themselves once a
  set can be walked and its items looked at.
