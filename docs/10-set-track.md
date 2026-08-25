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
scripts at `ScriptOffset` carry the rest. Two of the three `CINEPAK` events name
films at blocks 23,240 and 1,520, which are films 10 and 2 in the track-7
inventory, to the block. That confirms the field but does not scale: naming the
other 34 films needs the script VM.

## 10.6 Naming the sets

July's `CDLINK.INC` names 46 sets and 594 scenes, emitted in alphabetical order.
The retail groups are in the same order, so the two lists align in a single
monotone pass: **three insertions** (groups 19, 27 and 31 are sets added after
July) and **one deletion** (`D2_12B`, a one-scene set folded into `D2`). Totals
agree on both sides — 672 against 594.

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
no clear majority group, so their name is a guess. Scene naming does not depend
on that — it comes from the id.

## 10.7 Using the tool

```
python tools/set/setx.py TRACK3
python tools/set/setx.py TRACK3 --set 0 --scenes --events
python tools/set/setx.py TRACK3 --json assets/sets.json
```
