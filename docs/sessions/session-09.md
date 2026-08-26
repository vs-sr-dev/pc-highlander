# Session 9 — The floor was never the problem; the pad, and the doors

The three things session 8 left, in its order:

1. the ground gap, which "blocks anything that looks right on screen",
2. the player driven by the joypad rather than by a chosen animation,
3. the doorways — crossing into another set.

All three are in. The first turned out to be a measurement rather than a
mystery: **session 8's per-set gap table was an artefact of its own estimator**,
and once it is measured properly the character is ankle deep, not hip deep. The
other two are engineering, and the surviving `.MAP` files and `SHEET.S` gave more
than expected on the way.

---

## 1. The ground gap: what it was, and what it wasn't

Session 8 read a per-set gap between the collision plane and the drawn floor
running from −50 in `SHANR3` to +348 in `PRI`, with `CA` at 155, and concluded
that a 414-unit character stands buried to the hips. Four measurements later,
none of that survives except the small numbers.

**It is not per set.** Run session 8's own estimator per camera instead of
pooling a set's cameras and `DUN1` alone gives figures from −259 to +283.
Cameras that share a position give byte-identical answers, so this is not noise.

**It was measuring the room, not the floor.** The estimator took the modal
reconstructed height over every pixel inside a collision triangle's plan
footprint — which holds the wall standing on the triangle's edge and the crate in
the middle of it. On the *same triangle from two cameras* it disagrees with
itself by a **median of 300 units**, more than the gap it was reporting.

**Measured properly.** Nothing is below the floor, so the floor is the lowest
flat surface a camera sees in a patch of plan, and the cameras that can see it
agree. Over 43 sets that gives median −5, sd 61 — no per-set structure — with
`PRI` falling from 348 to 45 and `CA` from 155 to 89.

**Measured a fourth time, by the engine.** `hlview` already counts how many
pixels of a model survive the backdrop's Z-buffer, so raise a model until nothing
of it is hidden and the height at which that happens *is* the drawn floor, with
no backprojection anywhere. In `PRI` the 69-unit bottle at collision height 1
keeps 2 of 23 pixels and is completely clear by y = 60. Run at several places in
one set it does not agree with itself either — `CA` gives 20, 50, 110, 190 — and
that disagreement is the answer.

| set | session 8 | re-measured | engine probe (lowest of several places) |
|---|---:|---:|---:|
| `DUN1` | 12 | 13 | 10 |
| `CA` | 155 | 89 | 20 |
| `PRI` | 348 | 45 | 59 |

**So the gap is local relief in the art, of a few tens of units.** At
`PRI_CAM00`'s 2,200-unit distance, 59 units is 6.6 pixels against a character 46
pixels tall.

Two things were ruled out on the way, and both are worth having. The **24 bytes
after the set header** are not 24 bytes: they are a list of 12-byte records that
ends where `SceneOffset` points, and `MENU` proves it by having three of them and
a `SceneOffset` of 64. Each is `4, block, size` with every block a multiple of
56, and track 3 is exactly 48 × 56 blocks — a load list, not a datum. And the
**projection constants** are not the cause: fitting them from camera-to-camera
agreement alone over 220 cameras leaves `XSCALE` and the principal column where
the viewer has them and `YSCALE` flat from 236 to 261. The principal *row* looks
on that criterion like it wants 90 rather than 99 — which would matter a great
deal — but an independent test kills it: a collision triangle is flat by
construction, so the surface drawn over it must reconstruct level, and minimising
that tilt puts the row at 104, on the *other* side of 99. Two criteria
disagreeing means the first was measuring its own flatness filter.

→ [14-characters.md](../14-characters.md) 14.7, [13-viewer.md](../13-viewer.md)
13.6, [10-set-track.md](../10-set-track.md) 10.1

## 2. The `.MAP` files, which say why there was nothing to find

The two surviving maps are ASCII, written by *Map Editor 1.211b* — Matthew
Jesson's tool — and they are the authoring form of the whole set track. A set is
a plan traced over a bitmap; one shared vertex list in plan pixels; and

```
k = DISTANCE * ZOOMDIV / (ZOOMMUL * |v0 − v1|)      world = (pixel − origin) * k
```

For `DUN1` that is 39.49 units per plan pixel, and measuring the exported mesh
against the plan independently gives 39.54 across x and 39.90 across z. 182 of
the disc's 217 collision vertices land within 20 units of where the plan puts
them, median 3.8; the rest is five weeks of editing between the file and the
build.

**And there is no vertical calibration in the format at all.** `SCALE` is one
horizontal segment. `ORIGIN` is one vertex whose `HEIGHT` is 1 in both files.
Every ground height in the game is an integer somebody typed into a `HEIGHT`
field by eye, against a 3D Studio scene the map editor could not read. There is
no datum in the pipeline from which an engine correction could have been derived,
which closes the question: the third of session 8's three candidates, that the
original lived with it, is the right one.

The maps also answer session 8's spare hour. **An event in the source format is a
line segment, not a circle.** DUN1's 64 segments became 77 circles on the disc;
the circles sit on the segments, median 4 units off, but the segments run 198 to
9,054 units long against radii of 13 to 57. Forty-six segments carry one circle,
ten carry two, one three, two four — so the exporter reduces a line to a handful
of points on it, and a row would have needed 125 circles for the longest one.
That is exactly why a walk 76 units to one side misses a doorway: the line was
continuous and what shipped is not.

→ [10-set-track.md](../10-set-track.md) 10.7

## 3. The player, driven

`src/game/control.c` is `AICTRL.GAS`'s two loops.

`PlayerControl` builds `actJoypad`, and two things happen that the pad does not
say. `actCount` counts frames since forward was last held, and while that count
is 1, 2 or 3 the code **forces JOY_UP back on** — in a branch delay slot, so it
happens whether or not the test after it is reached. And a press inside that
window sets JOY_DOUBLE: **the double tap is detected by the pad handler, not by
the animation table.** `slowtowalk` rewrites the stance in place, 4 into 3, jog
into walk — the only two stance numbers the source names, and the ones that fix
the rest.

`ActionCode` walks a logic table: tuples headed by a joypad mask, first match
wins, last mask zero. The matched tuple holds eight entries and `citStance & 7`
picks between them, so **what a button does depends on what the character is
already doing**. Turning is separate and additive, `4 * 20 / framerate` steps a
frame, unless the stance carries FSATurn and the animation is turning itself —
and that the two rates are the same rate is not assumed: Quentin's
turn-on-the-spot turns 62 steps over 15 frames, 4.1 a frame.

Which animation is which is read off the root motion, not guessed: 6 stands, 10
walks at 22.8 units a frame, 11 runs at 38.9, 12 walks backward, 15 and 16 turn.
Animation 10 is the one session 8's walk already used, which settles the sign.

```
frame    0: walk  animation 10
frame   42: jog   animation 11   (double tap)
frame  106: stand animation  6
frame  132: turn  animation 15
frame  162: walk  animation 10
```

The *table* is ours and says so in the code. The real ones are five CD files
`DATA.INC` names — `BO_LOGICS_1` "full" through `3B` "stand - no turn anims" — at
block offsets $0..$40 of the data type `VIDSTUFF.INC` calls `CHARDATA` and
annotates **"jakes joypad logics"**. Where the retail disc put them is not known;
scanning every track for the structure `ActionCode` reads turns up only a clamp
table in the binary.

→ [14-characters.md](../14-characters.md) 14.8

## 4. The doorways, and which way the init table runs

§10.3 had the init table as "arrival points addressed by the borrowed ids". The
direction matters and the data settles it outright:

* **not one** of the 153 entries is keyed on its own set's group;
* of the 123 with a real id, **all 123** have, in the set that owns that id, a
  `SCENE` event fired from that very view that cuts into the arriving set.

So the id is **the view you are leaving**, and a doorway is two halves: the
departing set's event names the view you arrive at, the arriving set's entry
names where you stand. `$FFFF` covers a view the far side does not list, and 41
of the disc's 129 cross-set cuts use it.

**The flags word decodes too.** All 153 are exactly `facing * 256 + c`, `c` being
0 for the player and 1 for the companion, and 44 ids carry both. The `.MAP` says
it in words: two `START` blocks, `QUENTIN` and `RAMIREZ`, sharing one `FROM`,
each with an `ORIENTATION` in degrees — and 315° is 224 steps, one of the twenty
facings the disc uses.

```
cut to CNY08_CAM12 (id 780) at (-4304,3903) on triangle 38
  through the door into set 8: arriving at (6099,1290) facing 213, from view 832
```

`hlview --check-doors` runs it over the disc: 153 of 153 entries decode and stand
on their own mesh, and all 129 cross-set `SCENE` events find an arrival, none of
them off the far mesh.

**It found a real bug on the way**, and not in the doorways. The engine read a
triangle's vertex indices into a `uint8_t`; the disc stores words, and `CNY01`
has 348 vertices, `D1` 300 and `NEOSW` 293. Those three meshes were silently
folded. `--check-mesh` could not see it, because the search and the scan it is
checked against were both reading the same folded mesh — a check comparing two
readers of one corrupt input agrees with itself perfectly. Three arrivals landed
off the floor; with the index read as the word it is, none do.

→ [10-set-track.md](../10-set-track.md) 10.3,
[14-characters.md](../14-characters.md) 14.9

## 5. What `SHEET.S` closed for free

[12-world-and-sheets.md](../12-world-and-sheets.md) 12.7's first open item — that
nothing statically links a character sheet to a bundle on track 5 — is settled by
the sheets' own source. The run at `cshFileOff` is a table of 8-byte records,
`entry position .w, data type .w, block offset .l`. Quentin's says: models from
`MODELDATA`, then animations into slots 0, 14 and 28 from
`BO_ANIM_QUENTIN_HAND1..3`, then `CHARDATA` at `BO_LOGICS_1`, then a sound
bundle. So his thirty animations are three loads of 14, 14 and 2, and the sword
and gun banks are not his at all — they live on the weapon's sheet, which is what
`AICTRL.GAS` reaches for first when the player is armed. That is session 8's
"four banks, 114 in all", explained.

`VIDSTUFF.INC` names all thirteen data types, one of which is `HIRESDATA`,
"640 x 400 pics" — a lead for track 9 rather than an answer, since the retail
build collapsed thirteen types into eight and track 9's payload measures 7.996
bits per byte.

---

## Still open

* **The logic tables.** Five CD files, named and described in `DATA.INC`, not
  located on the retail disc. Until they are, the joypad→animation table is ours.
* **Track 9**, with `HIRESDATA` as a new suspect.
* The load-list records after the set header: the shape is settled, what the
  blocks name is not.
* Unchanged: `ZMODELT`'s sense; the non-uniform `face` elevation on the items;
  the three unreferenced films; the 125 unnamed world records; the seventeen
  unnamed GPU modules; the `SLP` payload; `cshBehaviour`; `gvar[0..2]`.

## TODO for session 10

1. **Ramirez.** The init table has been carrying his arrival point in bit 0 of
   every doorway all along, `AICTRL.GAS` has an `aiFollowPlayer` command, and
   `cshBehaviour` is the selector 12.7 has been waiting to identify. A second
   character who follows is the next thing that makes it a game.
2. **The scripts, running.** The VM is read (11) and every set carries one at
   `ScriptOffset`; the doorways now change sets, and a script is what is supposed
   to be watching when they do.
3. **The logic tables**, if an hour turns them up. The `.MAP`'s `SCENES` block
   gives `CACHE_SCENE0` and `CACHE_SCENE1` per view, which is a second thing the
   CD layout has to explain and might be found in the same place.

Still worth an hour whenever there is one: **the frame comparator** — render a
scene, run the same scene under an emulator, diff. This session spent most of a
day on a number that one frame diff would have settled in a minute.
