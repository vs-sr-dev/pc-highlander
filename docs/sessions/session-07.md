# Session 7 — The floor: the ground question closed, and phase 4 opened

Session 6 ended with one thing it could not explain: an object placed at its
world position, at the height the collision mesh gives, **sinks into the ground
the backdrop draws**. It was written down as "50 to 130 units, set-dependent,
cause unknown", and handed to phase 4 as its first question.

It is now answered, and answering it turned out to hand us a tool the rest of
the port will keep using.

---

## 1. Invert the Z-buffer

Phase 3 settled enough to run the projection backwards. Every pixel of a
backdrop is a surface the renderer saw; the footer gives the rotation and the
camera's world position, the depth half gives the distance in front as
`65536 − depth`, and the projection is the engine's own. So

```
d     = ((col − 159) / 300,  ((199 − row) − 99) / 246,  −1)
world = campos + (65536 − depth) * (Mᵀ · d)
```

recovers the world point behind each of the 64,000 pixels.
→ [tools/scene/backproj.py](../../tools/scene/backproj.py)

**Checking the check.** Two cameras looking at the same set must reconstruct the
same world, and that test needs no model, no collision data and no assumption
about which pixels are floor. The first version of it was too loose — comparing
the lowest 5% of the points in each cell, which compares two different things
whenever either camera was looking at a jumble — and gave a median difference of
0 with an interquartile range of 223, which proves nothing. Restricting it to
cells where *each* camera saw one flat surface leaves 552 shared cells on DUN1,
of which **71% agree within 25 units, median 0.0, interquartile range 0.0**.
Sets built in storeys, like D1, disagree by design: one cell in plan holds more
than one floor.

## 2. The collision height is the floor's world y, at 1:1

D1 is the set that can prove it, because it is built vertically. Comparing each
collision triangle's height word against the surface its backdrops actually draw
above it:

```
drawn floor = 0.962 * collision height + 36.7      r = 0.944, 239 triangles

height   1 -> floor    44        height  1401 -> floor  1407
height 401 -> floor   400        height  1673 -> floor  1680
height 801 -> floor   775        height  2473 -> floor  2468
```

Over 2,500 units of vertical range, no scale factor and no offset that matters.

**So the sinking is the art, not the format.** What is left over is relief the
flat mesh approximates — about 12 units in DUN1, about 70 under the wine bottle
in TENT6, where the tent floor is a raised mound. There is nothing to decode.
The engine should do what the original did, take the triangle's height as
`GROUNDHEIGHT`, and a 69-unit bottle will sit a little low in the bumpiest sets.

The other candidate — a vertical datum inside the model — is ruled out for
items: the nineteen models in the binary carry **no origin points at all**.

→ [13-viewer.md](../13-viewer.md) 13.6

## 3. What the origin points turned out to be

Chasing that ruled-out candidate found something phase 4 wants anyway. The
track-5 character models *do* carry origin points, and they are numbered: the
torso publishes origins **128, 135, 138 and 141**, and the pieces that follow it
declare themselves anchored at 135, 136, 137. They are the skeleton — the
mechanism that chains a character's fifteen parts together — and 128 looks like
the root, sitting at (0, 0, −1) in the torso's own coordinates.

That is the thing to build on when assembling a character.

## 4. Phase 4, started: the set loads

`src/game/set.c` reads a set off track 3 the way the engine will — views,
doorways, events, and the floor mesh — and agrees with `setx.py` set for set
(DUN1: set 19, 55 views, 13 doorways, 80 events, 248 triangles over 217
vertices). `hlview --mesh` draws the mesh over the backdrop, and that is the
view the rest of this phase will be built against: in `CA_CAM03` its right-hand
boundary runs along the base of the round building and its far edge stops at the
foot of the gate; in `DUN1_CAM00` the red wall line sits exactly at the foot of
the palisade.

It is drawn *without* the depth test, deliberately, and the reason is §2: the
collision plane is under the drawn ground, so depth-testing the mesh against the
backdrop hides all of it.

Three things about the mesh, read across all 5,342 triangles of the 48 sets:

* **neighbour slot *k* is the edge (v[k], v[k+1])**, in all 10,876 links, every
  one symmetric, no exceptions anywhere on the disc;
* **the winding is not consistent** — 2,729 triangles turn one way and 2,613 the
  other — so "inside" has to be the weak test, no two edges disagreeing, not a
  sign test against a fixed winding;
* **19 of the 48 meshes come in more than one piece**, so a search across the
  adjacency cannot reach everything from everywhere.

→ [10-set-track.md](../10-set-track.md) §10.4

## 5. `FINDTRI` is a search over a list, and that is not a detail

The first implementation stepped across whichever edge the point was beyond,
keeping a visited list — the obvious thing. `FINDTRI.GAS` says not to, and says
why:

> so we can't just keep jumping to the first edge which the test point is
> outside of, because it would be possible to generate a structure where a
> point couldn't ever be reached (due to looping)

Measured on the shipped meshes, that is not hypothetical: **greedy fails on 258
of the 5,342 triangles; the list search fails on none.**

`hlview --check-mesh` is the regression test, and it reports enough to show it
is not a vacuous one — a search that always gave up into a full scan would
"agree" with the scan every time:

```
mesh walk vs scan: 5342 triangles, 0 disagreements, 0 centroids the scan
  itself could not place
  from an arbitrary start: 5295 found across the adjacency, 84.3 triangles
  examined on average, 47 gave up into a full scan
  from five triangles away, which is where movement always starts: 5342
  searches, 0 wrong, 0 gave up, 5.1 triangles examined on average
```

The 47 are the islands of §4, not failures.

---

## Still open

* **`ZMODELT`** — the sense of the original's depth comparison, as session 6
  left it.
* **The `face` elevation is not uniform**: three of the five wine bottles carry
  the 192 that stands them up and two carry zero, along with several other
  items. Dressing, or a default overridden elsewhere.
* Track 9, the three unreferenced films, the 125 unnamed world records, the
  seventeen unnamed GPU modules, the `SLP` payload, `cshBehaviour`,
  `gvar[0..2]`.

## TODO for session 8 — a character on the floor, then moving

The floor is in and checked, so the next two steps of phase 4 are the ones the
roadmap lists, in order:

1. **A character, assembled.** Fifteen pieces from one track-5 bundle, chained
   through the origin points of §3, at one animation frame, standing on a
   triangle. This is the first time the model format, the animation format and
   the character sheet all have to agree with each other, so expect it to
   correct one of the three.
2. **Movement.** Walk the mesh with `set_walk` from the character's current
   triangle — 5.1 triangles examined per step, so it is cheap enough to do every
   frame — with collision against the edges that have no neighbour, and the
   ground height following the triangle underfoot. `SMOOTH.TXT` is the reference
   for stairs.
3. **The camera cuts.** 62 of `CA`'s 66 events are `SCENE` events, so the data
   to drive the cuts is already extracted and the set loader already reads it.
   That is the roadmap's success criterion for the phase: walk around `DUN1` and
   have the camera cut where it should.

Worth an hour whenever there is one: **the frame comparator** — render a scene,
run the same scene under an emulator, diff. It only gets harder to retrofit.
