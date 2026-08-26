# 12 — The world state and the character sheets

Every object and character in the game — where it starts, who owns it, how much
life it has, which model set it wears — is one 32-byte record in a table of 256.
The table is not on a data track: it is **static data in the resident binary**,
and so are the character sheets it points at.

Reference implementation: [tools/world/worldx.py](../tools/world/worldx.py).

---

## 12.1 Finding them

Two routines run at startup and give the whole thing away, because they name
both the file address and the RAM address of each table in `movei` immediates
and then **relocate the pointers between them**:

```
$5310   copy $15458 -> $32660 (ws), 8192 bytes = 256 records of 32
        for each record:  if the long at +4 is non-zero,
                              subtract $17458, add $34760      ; wstSheet
                          if the long at +8 is non-zero,
                              subtract $15458, add $32660      ; wstParent
        step 32

$537A   copy $17458 -> $34760 (cs), 8192 bytes
        walk the chain: while the long at the head is non-zero,
                            subtract $17458, add $34760, follow it
```

`$32660` and `$34760` are the `ws` and `cs` the script VM's own module addresses
(§11.4), and the fields the relocation touches are at +4 and +8 with a 32-byte
stride, which is `wstSheet` and `wstParent` in `LOGICS.INC` exactly. There is no
room for a different reading.

## 12.2 The world-state record

The 32 bytes `ITEMS.MAC` builds, unchanged from July:

```
+0   .w  wstSet      the scene id masked to $FFC0, i.e. group * 64
+2   .w  wstRadius
+4   .l  wstSheet    -> a character sheet
+8   .l  wstParent   -> the world-state entry that owns this one, or 0
+12  .l  wstXpos     +16 .l wstYpos    +20 .l wstZpos
+24  3b  Xface Yface Zface
+27  5b  Sanity Person Str Life Flags
```

Flag bits, from `LOGICS.INC`: 0 `WSTRegistered`, 1 `WSTInanimate`, 3 `WSTAmmo`,
4 `WSTWeapon`, 5 `WSTCollectable`, 6 `WSTDeactivated`. Bits 2 and 7 are unnamed.

**197 of the 256 records are in use**, indices 0 to 196, and the first seven are
byte-for-byte July's: `WORLD_QUENTIN` is still `radius 100, 229, 7, 153, 255`;
`WORLD_CA_TURRET_KEY` is still at `(-2348, -3036)` with `wstParent` pointing at
record 2, `WORLD_CA_KEY_HUNTER`.

## 12.3 The character sheet

`cshRecord` from `LOGICS.INC` — a 16-byte header, then an array of longs the
game fills in as it loads pieces off the CD:

```
+0   .l  cshNext        0 ends the chain
+4   .w  cshFlags
+6   4b  cshModelOff cshModelNum cshAnimOff cshAnimNum
+10  4b  cshMiscOff  cshMiscNum  cshFileOff  cshFileNum
+14  .w  cshBehaviour
```

The `*Off` fields index the long array **from the start of the record**, which
is why the first is always 4 — straight past the header. `SCRIPT.GAS`'s
`animate` handler settles it: for animation *n* it reads
`sheet + (cshAnimOff + n) * 4`. The four runs are contiguous — model,
animation, miscellaneous, file — so the record's length is
`(cshFileOff + cshFileNum) * 4`, rounded up.

Forty sheets are chained from `$17458`. The head is a 772-byte record with all
counts zero: that is the **set sheet**, the one `sample` and `setanim` index at
`cs + 4` with a 12-byte stride. The other 39 are real:

| sheets | models | anims | what they look like |
|---|---:|---:|---|
| 1 | 15 | 30 | Quentin — the only one with 30 animations |
| 2-6 | 15 | 28 | the full-size characters; sheet 2 alone is used by 46 records |
| 7-14 | 15 | 3-4 | characters with a handful of animations |
| 15-21 | 1-3 | 0-1 | props and the menu hands |
| 22-39 | 1 | 0 | the collectable items |

**`cshModelNum` is 15 for every full character** — the same fifteen pieces the
model records and the animation records carry, now seen from a third side.

One sheet, `$181BC`, is referenced by two world records but is **not in the
chain**: it sits inside the 88 bytes of sheet 21 and is skipped by `cshNext`.
The two records that use it are the menu and language-screen hands, which never
need loading off the CD, so nothing goes looking for it.

## 12.4 Naming the records

July's `WORLD.S` names 116 entries. Aligning the two tables by **exact (x, z)
coordinates** — a longest-common-subsequence pass, so the pairing stays monotone
— matches **72 of them**. The rest are entries whose characters were moved
between July and October, so their coordinates no longer agree. Filling the gaps
between anchors gains only six more and is not worth the risk, so `worldx`
reports the 72 and leaves the rest unnamed.

```
python tools/world/worldx.py TRACK2 --world path/to/WORLD.S
```

`WORLD.S` is not in this repository; point the tool at your own copy.

## 12.5 What it confirms

The world table and the script disassembly were read independently and they
agree everywhere they touch:

* **The combination locks.** The `CODE` script drives world records 71, 72 and
  73 as its three tumblers and 74 as the cursor. The world table puts exactly
  four records in group `CODE`, at `z` = 5, 53 and 101 for the tumblers — the
  same three values the script's `slideto` commands use — and record 74 is
  `WORLD_CODE_HAND` by the July alignment. `CODE2` repeats it at 75 to 78.
* **The menus.** The main-menu script slides record 143 first to `(38, -128)`;
  the world table starts record 143 at `(38, -128)`. The language script's
  default position is `(49, -72)`; record 144 starts at `(49, -72)`.
* **The keys.** `CA`'s script fires on `testuse #6`, and record 6 is
  `WORLD_CA_TURRET_KEY`, flags `WSTInanimate|WSTCollectable`, owned by record 2.

## 12.6 It corrects the set names

The world table carries a second, independent way to name the scene groups.
Every record's `wstSet` is its group, and every July entry's first macro
argument is a `SCENE_<SET>_CAMnn` constant. Running the names of the 72 anchored
entries through both gives **28 July set names, each voting for exactly one
retail group, with no set naming two groups and no group taking two names**.

Twenty-five agree with session 4's camera-count alignment. Three do not, and the
world table wins, because coordinates are a far stronger key than camera counts:

| group | was | is |
|---:|---|---|
| 24 | `DUN6` | **`G1`** |
| 25 | `G1` | **`G2`** |
| 26 | `G2` | **`G3`** |
| 28 | `G3` | *(a set added after July)* |

`DUN5` keeps group 23, which its camera count already anchored, and **`DUN6`
turns out to have no retail group at all** — a second deletion beside `D2_12B`.
The three corrections all land on entries the camera-count pass had itself
flagged as carried-by-position rather than anchored, which is the outcome you
would hope for.

With `LANG` at 31 (§11.6), the sets with no July counterpart are groups **19, 27
and 28**.

## 12.7 Still open

* ~~The long arrays in the character sheets are zero in the binary and filled
  from the CD, so nothing statically links a sheet to a model bundle on track 5.~~
  **Settled from the source.** `SHEET.S` is the sheets' own source and the run at
  `cshFileOff` is a table of 8-byte records, `entry position .w, data type .w,
  block offset .l`, which says exactly which CD blob fills which slot of the long
  array. Quentin's reads: the models from `MODELDATA` at `BO_MODEL_QUENTIN`, then
  animations into slots 0, 14 and 28 from `BO_ANIM_QUENTIN_HAND1..3`, then
  `CHARDATA` at `BO_LOGICS_1` and a sound bundle. So his thirty animations are
  three loads of 14, 14 and 2, and the sword and gun banks are not his at all -
  they live on the weapon's sheet, which is what `AICTRL.GAS` reaches for first
  when the player is armed. That is session 8's "four banks, 114 in all". The
  `.w` data type is `VIDSTUFF.INC`'s: 1 `CHARDATA` ("jakes joypad logics"),
  2 `MODELDATA`, 3 `ANIMDATA`, 4 `SCENEDATA`, 5 `SAMPLEDATA`, 6 `SETDATA`,
  7 `REDDATA`, 8 `BITMAPDATA`, 9 `HIRESDATA` ("640 x 400 pics"), 10 `WSCSDATA`,
  11 `CINEDATA`, 12 `RUNDATA`. The block offsets are July's and the retail disc
  moved them (6.5), so the table gives the shape and the names, not the
  addresses.
* `cshBehaviour` takes eleven distinct values — 0, 10, 20, 30, 40, 250, 1536, 1792,
  2304, 2816, 3840 — and is presumably an AI selector; `AICTRL.GAS` is the place
  to look.
* 125 of the 197 world records have no name.
