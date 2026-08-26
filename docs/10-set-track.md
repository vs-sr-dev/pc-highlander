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
+28  24   six longs, purpose unknown
+52  ..   the tables, laid out scene / init / event / collision / script
```

The seven-long header is exactly July's `Set data structure`. The 24 bytes after
it are not: they read `4, N, 0, 4, M, $10000` with `M = 6 * N`, and what they are
for is open.

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

## 10.3 Init table — the doorways

A long count, then 12 bytes each:

```
+0  .w  scene id
+2  .w  flags
+4  .l  x
+8  .l  z
```

These are arrival points, and they are addressed by the *borrowed* ids, which is
what identifies them as the other side of a doorway. Set 0 has five, into groups
40, 38 (twice) and 10 (twice).

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

## 10.7 Using the tool

```
python tools/set/setx.py TRACK3
python tools/set/setx.py TRACK3 --set 0 --scenes --events
python tools/set/setx.py TRACK3 --json assets/sets.json
```
