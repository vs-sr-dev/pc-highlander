# 10 — The set track: scenes, doorways, collision, events

Track 3 holds the 48 sets, one per 56-block slot. Everything below is read off
the retail disc and cross-checks against `STRUCDEF.INC` and `EVENT.GAS`.
Reference implementation: [tools/set/setx.py](../tools/set/setx.py).

---

## 10.1 Set header

```
+0   .l  Hinum
+4   .l  Lonum
+8   .l  EventOffset
+12  .l  CollOffset
+16  .l  InitOffset
+20  .l  SceneOffset
+24  .l  ScriptOffset
+28  ..   a list of 12-byte records, ending where SceneOffset points
+52  ..   the tables, laid out scene / init / event / collision / script
```

The seven-long header is exactly July's `Set data structure`. What follows it is
not 24 fixed bytes: it is a **list whose length varies**, and `MENU` is the set
that proves it, because its `SceneOffset` is 64 rather than 52 and the extra
twelve bytes are one more record of the same shape. Forty-six sets carry two,
`MENU` and `LANG` carry three, and `H` carries one.

Each record is three longs, `4, block, size`:

```
CA      4, $70,  0          4, $2A0, $10000
DUN1    4, $0,   0          4, $460, $10000
TENT1   4, $0,   0          4, $7A8, $10000
MENU    4, $620, $10000     4, $818, $200000     4, 0, 0
```

Every `block` is a multiple of 56, and track 3 is exactly `48 * 56` blocks long,
so they are set slots. The first record takes one of only five values across the
disc - slots 0, 1, 2, 3 and 28 - and groups the sets into families: the whole
`CNY` chain names slot 2, the `C`/`D` sets name slot 1, the dungeons and tents
name slot 0. The second is nearly unique per set. They read as a **load list**,
which is what the leading `4` and the trailing `$10000` suggest, and what they
name exactly is still open - but "24 bytes, purpose unknown" is not what is
there.

## 10.2 Scene table — and the scene id

A long count, then 8 bytes per scene:

```
+0  .w  scene id
+2  .w  zero
+4  .l  CD block offset on the PICT track
```

Every block offset is an exact multiple of 110, the scene stride, and the id is
**the same 16-bit value the scene's own camera footer carries at offset 0**
(§7.5). That is what ties the two tracks together.

The id is structured:

```
id = group * 64 + camera
```

with groups numbered 1 to 48 — one per set — and the camera index counting views
within it. Set 0's own scenes run `$0100`..`$0116`, group 4 cameras 0 to 22.

Across the 48 sets the tables reference **all 672 scenes with nothing left
over**, 792 references in total. The difference is doorways: 113 scenes belong
to more than one set, because a set also lists the first view of each set you
can walk into from it. Those borrowed entries are easy to spot — their group is
not the set's own.

**And the scene says so itself.** The camera footer's long at +32, which
session 3 recorded as a constant 672 from one sample, is the set's own block
offset on this track: a multiple of 56, the slot size, so dividing by 56 names
the set. For all 672 scenes the set it names is one whose table lists that
scene id, and every one of the 48 sets is named by at least one view
([07-scene-format.md](07-scene-format.md) 7.5). That is a direct link where
10.6 below has to vote, and it settles the sets too small to vote on.

## 10.3 Init table — the doorways

A long count, then 12 bytes each:

```
+0  .w  scene id
+2  .w  flags
+4  .l  x
+8  .l  z
```

These are arrival points, and phase 4 needed them exactly rather than nearly.
Both fields decode, and the direction the table runs in is the thing to get
right.

**The id is the view you are *leaving*, not the one you arrive in.** Not one of
the 153 entries on the disc is keyed on its own set's group; every one names a
scene belonging to somewhere else, or `$FFFF`. And the pairing is exact: of the
123 with a real id, **all 123** have, in the set that owns that id, a `SCENE`
event fired from that very view that cuts to a view of the arriving set's
group. So a doorway is two halves - the departing set's event names the view
you arrive at, and the arriving set's init entry, keyed on the view you left,
names where you stand. `$FFFF` is the arrival for a view the far side does not
list, and 41 of the disc's 129 cross-set cuts use it.

**The flags word is the arrival pose.** All 153 entries are exactly
`facing * 256 + c` with `c` 0 or 1: the top byte is the facing on the game's
256-step circle, and the bottom bit says which character. 44 ids carry both
halves of that pair. The `.MAP` format (§10.7) says the same thing in words -
a doorway is two `START` blocks, `START QUENTIN` and `START RAMIREZ`, sharing
one `FROM`, each with its own `ORIENTATION` in degrees. `ORIENTATION 315` is
`315 * 256 / 360 = 224`, and 224 is one of the twenty facings the disc uses,
appearing ten times.

`hlview --check-doors` runs the whole thing: 153 of 153 entries decode and stand
on their own set's collision mesh, and all 129 `SCENE` events that leave their
set find an arrival on the far side - 88 keyed on the view being left, 41 by the
default - with none of the arrivals off the arriving mesh.

It also found a bug that had nothing to do with doorways: the engine was reading
a triangle's vertex indices into a byte, and `CNY01` has 348 vertices, `D1` 300
and `NEOSW` 293. Their meshes were silently folded, and `--check-mesh` could not
see it because the search and the scan it is compared against were both reading
the same folded mesh. Three of the 153 arrivals landed off the floor; with the
index read as the word it is, none do.

## 10.4 Collision

```
+0   .w  triangle offset, relative to CollOffset
+2   .w  triangle count
+4   .w  vertex count
+6   .w  pad

vertex     2 longs: x, z          - the mesh is 2D, height lives on the triangle
triangle   7 words: ground height, 3 vertex indices, 3 neighbouring triangles
```

Fourteen bytes per triangle, and the adjacency is symmetric — triangle 1 lists 2
as a neighbour and triangle 2 lists 1. `-1` means no neighbour, i.e. a wall.
`GROUNDHEIGHT` in the character table (§3.1) is this height.

Phase 4 needed the mesh precisely rather than approximately, and reading it as
the engine does settles three things about it, over all 5,342 triangles of the
48 sets:

* **Neighbour slot *k* is the edge (v[k], v[k+1])**, in all 10,876 links, and
  every one of them is symmetric. There is no exception anywhere on the disc,
  which is what lets the triangle search step directly from an edge to the
  triangle beyond it.
* **The winding is not consistent** — 2,729 triangles turn one way and 2,613
  the other. So "is the point inside" cannot be a sign test against a fixed
  winding; it has to be the weaker "no two edges disagree".
* **19 of the 48 meshes come in more than one piece.** A search across the
  adjacency cannot reach every triangle from every other one, and code that
  assumes it can will hang or lie.

**The height word is the world y of the floor, at 1:1.** Reconstructing what
the backdrops actually draw and comparing (see
[13-viewer.md](13-viewer.md) 13.6) fits D1's storeys as
`floor = 0.962 * height + 36.7` with r = 0.94, over heights from 1 to 2,473 —
at the top of which the drawn floor comes back as 2,468.

The triangle list ends exactly where `ScriptOffset` begins, to within 8-byte
alignment, in **all 48 sets** — the self-consistency check session 3 ran on set
0, now confirmed across the track.

## 10.5 Events

A long count, then 36 bytes each:

```
+0   .w  scene number, $FFFF for "any scene in this set"
+2   .w  type          EVENT_TYPE_* from EVENT.GAS
+4   .l  x
+8   .l  z
+12  .l  height          0 = ignore height
+16  .l  radius squared  0 = always colliding
+20  .w  status          bit 0 = colliding, bit 15 = delayed, bits 14-1 a count
+22  .w  condition type
+24  3.w condition data
+30  .w  data 1          for a SCENE event, the target scene id
+32  .w  data 2
+34  .w  data 3          for a CINEPAK event, the film's CD block offset
```

An event is a circle on the floor plan: the GPU module walks the list every
frame and fires anything the player is inside. The status word is written back
into the data, which is how "already triggered" and the delayed-event countdown
are stored.

Three details of `EVENT.GAS` that phase 4 needed exactly
([14-characters.md](14-characters.md) 14.6): the height word, when it is not
zero, is a **ceiling** and the event fires from at or below it; a radius of zero
means always colliding; and a `SCENE` event fires even when the status word
already says the player is inside, which no other type does.

**The circles are small.** Over the disc's 1,203 `SCENE` events the radius runs
from 4 to 107 units, median 27 — against a character 414 units tall who covers
21 units in one frame of the walk. They are thresholds to cross rather than
regions to occupy, and a path 76 units to one side of one misses it entirely.
Whether the retail exporter turned the `.MAP` format's event *line segments*
(3.6) into one circle each or into rows of them is not settled.

What is actually on the disc:

| type | count |
|---|---:|
| `SCENE` | 1,203 |
| `WAVLP` | 47 |
| `SETBIT` | 20 |
| `RESETBIT` | 9 |
| `CINEPAK` | 3 |

So the camera changes are events, and almost everything else is not — the
scripts at `ScriptOffset` carry the rest. All three `CINEPAK` events name films
in the track-7 inventory to the block: blocks 23,240 and 1,520, which are films
9 and 2. Session 5 read the scripts and found 36 more film references, all of
them landing on a film exactly (§11.7).

## 10.6 Naming the sets

July's `CDLINK.INC` names 46 sets and 594 scenes, emitted in alphabetical order.
The retail groups are in the same order, so the two lists align in a single
monotone pass. As it stands after session 5's corrections below, that pass has
**three insertions** — groups 19, 27 and 28 are sets added after July, and so is
31, `LANG` — and **two deletions**: `D2_12B`, a one-scene set folded into `D2`,
and `DUN6`, which retail dropped altogether.

Twenty-three groups are anchors, where the retail camera count equals July's
exactly, and they are spread through the list: `C2 C3 CA CN4 CN5 CNY02 CNY03
CNY06 CODE CODE2 D3 DUN2 DUN5 MENU NEOSW PRI REST SHANR1 SHANR3 TA TENT3 TENT5
TRAIN`. The rest are carried by position between anchors and should be read as
likely rather than certain.

One independent check settles the middle of the list: July's `MENU` set has a
single scene, and the alignment puts it at retail group 30, which also has a
single scene — retail scene 494. Decoding that scene gives the **main menu
screen**, "START GAME / LANGUAGE / CREDITS" over a stormy hill. It is the right
picture in the right place.

The table lives in `GROUP_NAMES` in `setx.py`, so every scene can be named
`<SET>_CAM<nn>` from its id alone. Note that the *set index* to group mapping is
weaker than the id scheme for the tiny sets: sets with two or three scenes have
no clear majority group, so a vote cannot say which of them the set owns. Scene
naming does not depend on that — it comes from the id.

**Session 5 settled the four tiny sets from their scripts** (§11.6), and
`setx` now carries them as an explicit `OWN_GROUP` override:

| set | owns | why |
|---:|---|---|
| 10 | 14 `CODE` | drives a combination lock, leaves by `camera TA_CAM03` |
| 11 | 15 `CODE2` | the same lock, leaving to `D2_CAM12` and `PRI_CAM00` |
| 29 | 30 `MENU` | the main menu: start game, credits, and a reset timeout |
| 30 | 31 `LANG` | writes 0/1/2 to the language variable and leaves |

That also names one of the three post-July groups: **group 31 is `LANG`**, and
its single scene decodes to "SELECT LANGUAGE / ENGLISH / FRANCAIS / DEUTSCHE"
over the same stormy hill as the main menu.

**Session 5 also checked the whole alignment against a second source and moved
three entries.** The retail world-state table (§12) gives every object a
`wstSet` field holding its group, and July's `WORLD.S` gives the same objects a
`SCENE_<SET>_CAMnn` argument; pairing the two tables by exact coordinates makes
28 July set names vote for a retail group, unanimously. Twenty-five agree with
the camera-count pass. Three do not, and coordinates beat camera counts:

| group | was | is |
|---:|---|---|
| 24 | `DUN6` | **`G1`** |
| 25 | `G1` | **`G2`** |
| 26 | `G2` | **`G3`** |
| 28 | `G3` | *(added after July)* |

`DUN5` keeps group 23, and **`DUN6` has no retail group at all** — a second
deletion beside `D2_12B`. All three corrections land on entries this section had
already flagged as carried-by-position rather than anchored.

So the sets with no July counterpart are groups **19, 27, 28** and **31**
(`LANG`). 19 is a wooded garden reached from `DUN1` and `DUN4`; 27 is a 33-scene
cave system that connects to seven other sets; 28 has nine scenes and is the one
group 19 also borrows a view from.

## 10.7 The `.MAP` file: how a set was drawn

Two of them survive in the source dump, `DUN1.MAP` and `DUN2.MAP`, and they are
plain ASCII written by *Map Editor 1.211b, Designed and Coded by Matthew
Jesson*. They are the **authoring form of everything on this track**, and worth
reading in full because they say what the compiled tables cannot: what the
designer was actually given to work with.

A map is a **plan drawing over a bitmap**. `BLOCK BACKGROUND` names it -
`DUN1.BMP` - on a 1280 x 960 canvas, and `BLOCK VERTEX` holds one shared list of
2D points in that bitmap's pixels. Everything else refers to the list by index:

```
BLOCK COLLISION  NAME, NUM_VERTEX 3, VERTEX_LIST i j k, HEIGHT n
BLOCK EVENT      NAME, VERTEX0 i, VERTEX1 j, HEIGHT n, PRIORITY p, SCENE name
BLOCK START      NAME, VERTEX i, ORIENTATION deg, START who, FROM scene
BLOCK CHARACTER  NAME, VERTEX i, HEIGHT n, TYPE t, ORIENTATION deg, RADIUS r
BLOCK ORIGIN     NAME, VERTEX i, HEIGHT n
BLOCK SCALE      NAME, VERTEX0 i, VERTEX1 j, DISTANCE d
BLOCK SCENES     ( PICTURE, CAMERA, CACHE_SCENE0, CACHE_SCENE1 ) ...
BLOCK CAMERAS    THREEDSTUDIO C:\DISK1\DUN1.3DS
```

**How plan pixels become world units.** `ORIGIN` names the vertex that is the
world origin. `SCALE` names two vertices and the real distance between them,
which the designer measured in the 3D Studio scene. So

```
k = DISTANCE * ZOOMDIV / (ZOOMMUL * |v0 - v1|)
world = (pixel - origin) * k
```

For `DUN1` that is `39539 * 8 / (32 * 250.32) = 39.49` units per plan pixel, and
measuring the exported mesh against the plan independently gives 39.54 across x
and 39.90 across z. Reconstructing all 217 of the disc's collision vertices from
the plan puts 182 of them within 20 units of where the disc has them, median
3.8. The rest is five weeks of editing: this file is dated 10 July 1995 and has
249 collision blocks against the disc's 248.

**An event is a line, not a circle.** `VERTEX0` and `VERTEX1` are the two ends
of a segment the player crosses. What §10.5 left open - whether the exporter
turned each segment into one circle or into a row of them - is neither. DUN1's
64 segments became 77 circles on the disc; the circles sit **on** the segments,
median 4 units off, but the segments run from 198 to 9,054 units long while the
circles' radii run 13 to 57. Forty-six segments carry one circle, ten carry two,
one three and two four. A row would need 125 circles to cover the longest
segment. So the exporter reduces a line to a handful of points on it, and that
is exactly why a walk 76 units to one side misses a doorway: the line was
continuous and what shipped is not. (The doubled ones are doubled events rather
than doubled geometry - 87 of the disc's 1,092 `SCENE` circles carry two events,
always naming consecutive scene ids.)

**And there is no vertical calibration anywhere in the format.** `SCALE` is one
horizontal segment. `ORIGIN` is one vertex, and its `HEIGHT` is 1 in both files.
Every ground height in the set is the integer somebody typed into a `HEIGHT`
field on a collision block, by eye, against a 3D Studio scene the editor could
not read. That is the whole explanation of the ground gap (§13.6, §14.7): there
is no datum in the pipeline that could have made the collision plane agree with
the rendered floor, and no engine correction can be recovered because there is
nothing for it to have been derived from.

`CAMERAS` names the 3D Studio file the backdrops were rendered from. No `.3DS`
is in the dump, which is the one thing that would settle the floors outright.

## 10.8 Using the tool

```
python tools/set/setx.py TRACK3
python tools/set/setx.py TRACK3 --set 0 --scenes --events
python tools/set/setx.py TRACK3 --json assets/sets.json
```
