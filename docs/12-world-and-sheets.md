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

## 12.7 What loads what

`SHEET.S` is the sheets' own source and it says the run at `cshFileOff` is a
table of 8-byte records — `entry position .w, data type .w, block offset .l` —
where the position counts longs **from `.models`**. That table is `dc.w`/`dc.l`
data, so unlike the long array it is in the retail binary and can simply be
read. Thirty-three records over the forty sheets, and every one of them is data
type 3.

**Track 5 is 33 slots of 56 blocks.** The thirty-three block offsets are 0, 56,
112, ... 1792 — all distinct, all multiples of 56, `33 * 56 = 1848` and the track
is 1849 blocks long. And they check out against the models: bundle 1 sits at
byte `$101404`, which is block 448 plus the four bytes of its length prefix, and
sheet 2's record says block 448.

**Even slot, odd slot.** The two records a full character carries land in
different places, and resolving the positions says which:

```
sheet  1  m4..18 a19..48 misc49..53   blk    0 -> models[0]   blk   56 -> misc[1]
sheet  2  m4..18 a19..46 misc47..51   blk  448 -> models[0]   blk  504 -> misc[1]
sheet 22  m4..4  a5..32  misc33..37   blk  112 -> anims[0]    blk  168 -> misc[1]
```

The even slot is the bundle — fifteen models and the animations — and the odd
slot begins `WAVE`: it is the sound bundle, which July loaded from a separate
`SAMPLEDATA` track and retail moved in beside the character. That is one of the
five data types the retail build collapsed (§6.5) seen from the other end.

**And it is checked by a number neither side controls.** Counting the animation
records that actually fall inside each slot and comparing with the `cshAnimNum`
the sheet declares agrees for **all 24 sheets that carry a file record**: 30 for
Quentin, 28 for the five other full characters and the three weapons, 4 for five
sheets, 3 for three, 1 for two, 0 for the rest. The odd slots hold no animations
at all, as a `WAVE` bundle should not.

**The weapons and the items do not load a model, because they already have one.**
Sheets 22 to 39 have `models[0]` **filled in in the binary**, and every one of the
eighteen addresses is exactly one of the nineteen item models §3.3 found there —
$EBA0, $EE98, $F848, $10140 and so on, one sheet each, none shared, only model 0
at $E1F8 unclaimed. So a weapon sheet's file record loads only its *animation*
bank (its first record lands on `anims[0]`, not `models[0]`), and an item sheet
loads nothing at all.

That also names some of the item models, through the world records that use each
sheet: model 6 is the **wine bottle** — which is `boot:6`, the one phase 3 matched
facet-for-facet against `MERLOT79.INC` — model 5 the **loaf**, 4 the **cheese**,
2 the **gas gun**, 7 the **key** and 8 the **locket**.

**What is still not loaded by anything is `misc[0]`**, and `misc[0]` is the one
`AICTRL.GAS` reads as the joypad logic table (§14.8). No file record fills it and
no sheet has it pre-filled, which is why scanning the data tracks for the
structure `ActionCode` reads turns up nothing: on the retail disc that table is
resident, written by code rather than loaded, and finding the code that writes it
is what the question has been narrowed to.

## 12.8 Still open

* ~~The long arrays in the character sheets are zero in the binary and filled
  from the CD, so nothing statically links a sheet to a model bundle on track 5.~~
  **Settled, and §12.7 is the answer.** Both halves of that sentence turn out to
  be wrong: the file table that does the linking is in the binary, and eighteen
  of the sheets have their model pointer filled in already.

* ~~`cshBehaviour` takes eleven distinct values — 0, 10, 20, 30, 40, 250, 1536,
  1792, 2304, 2816, 3840 — and is presumably an AI selector.~~ **Settled: it is
  two bytes, not one word.** The high byte is one of `AICTRL.GAS`'s fourteen AI
  commands — `aiNop` for the player, `aiAttackPlayer` for the hunters,
  `aiShootPlayer` for the one with a gun, `aiFacePlayer` for the shopkeepers,
  `aiFollowPlayer` for four sheets in a row where `SHEET.S` has RAMIREZ, FAVEB,
  MANGUA and ARAKA. The low byte is non-zero only on the eighteen item and three
  weapon sheets, which carry no behaviour at all: 10, 20, 30, 40 or 250, an item
  property sharing the word, and what it counts is still open.
  ([14-characters.md](14-characters.md) 14.10.)
* One sheet reads 15, which is past the fourteen commands `LOGICS.INC` numbers —
  a command the retail build added. Five world records use it.
* **Sheet 7 is Ramirez**, and every sheet in the chain now resolves to its model
  bundle: the file record's block times 2352, plus the slot's four-byte length
  prefix, is where the bundle begins, and sheets 1 to 16 land on bundles 0 to 15
  with none shared and none left over. `hlview --list-sheets` prints it.
* 125 of the 197 world records have no name.
