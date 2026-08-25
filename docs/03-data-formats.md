# 03 — Data structures and formats

Every structure below is documented explicitly in the source
(`STRUCDEF.INC`, `LOGICS.INC`, `DATA.INC`, `WORLD.INC`, `OPCODES.INC`).
Big-endian, long-aligned where noted.

---

## 3.1 The four world tables

The game separates **persistent state**, **live instance** and **draw data**
cleanly.

### World State Table (WST) — `ws`, 16 KB, 512 entries of 32 bytes
The **persistent** state of every entity in the world, characters and objects
alike. It survives scene and set changes, and it is what ends up in a save game.

```
wstSet     .w   set id
wstRadius  .w   collision radius
wstSheet   .l   pointer to character sheet (0 = empty entry)
wstParent  .l   pointer to parent WST record (e.g. object held in hand)
wstXpos    .l   32-bit position
wstYpos    .l
wstZpos    .l
wstXface   .b   orientation
wstYface   .b
wstZface   .b
wstSanity  .b   [0-100]
wstPerson  .b   personality [0-100]
wstStr     .b   strength
wstLife    .b   life points [0-100]
wstFlags   .b
```

Flags (`wstFlags`): `Registered`, `Inanimate`, `Ammo`, `Weapon`, `Collectable`,
`Deactivated`, `Collidable`.

`WORLD.INC` lists all **116 entries** with symbolic names: `WORLD_QUENTIN` (0),
`WORLD_RAMIREZ` (1), then every NPC and object grouped by set. Several NPCs are
named after team members (Dave, Kev, Mark, Carl, Matt, Chris...) — developer
cameos.

### Active Character Table (ACT) — `activchar`, 8 KB, 128 entries of 64 bytes
Who is **active right now** and how they behave.

```
actWorld     .l  -> WST
actInst      .l  -> CIT
actFlags     .w  (ACTCreated, ACTControlled)
actStatus    .w
actJoypad    .l  current joypad action
actAction    .l  previous joypad action
actCount     .l
actAICommand .l
actAIData1..4 .l
```

AI commands (`LOGICS.INC`): `aiNop`, `aiGotoPosition`, `aiGotoPerson`,
`aiFacePosition`, `aiFacePerson`, `aiAttackPerson`, `aiAttackPlayer`,
`aiFacePlayer`, `aiGotoPlayer`, `aiFollowPlayer`, `aiShootPerson`,
`aiShootPlayer`, `aiDefault`, `aiFollowPerson`.

### Character Instance Table (CIT) — `chartbl`
The **animation and physics** state for the current frame. Same structure that
`STRUCDEF.INC` describes as the character table:

```
citSheet    .l  -> character sheet
citDraw     .l  -> draw data area
citWorld    .l  -> WST
citModelNum .w  number of pieces to animate
citStance   .b  stance (left / right / neutral)
citTween    .b  tweening amount; bit 7 = height reset, 15 = compute tween,
                0-3 = use 0-3 tween steps
citAnimate  .l  -> animation file
citFrame    .w  frame in 8.8 fixed point, the NEXT one to display
citOldFrame .b  last frame displayed (for interpolation)
citFlags    .b
citFacing   .b
citMoving   .b
citHeight   .w  height of the current collision triangle
citTriangle .w  current triangle within the set's collision mesh
citGravity  .w
citSpeed    .w  animation playback speed
citStyle    .w  0 = play once, 1 = loop
citCollision .w
citXmoveback/Ymoveback/Zmoveback/Tmoveback .w
```

`citFlags`: `COLLIDE`, `COLTURN`, `PLAYER`, `PICKUP`, `ANIMEND`,
`KNOCKBACK1`, `KNOCKBACK2` (00 = backward, 10 = forward).

### Draw Data Area (DDA) — 4 KB, 256 entries of 16 bytes
The draw list handed to the 3D engine.

```
ddaNext  .l  -> next entry (0 = end)
ddaFlags .l  bit 0 = invisible, bit 2 = no lighting
ddaInst  .l  -> Instance Data Area (3x3 matrix + position + facing, 15 words)
ddaModel .l  -> polyhedron
```

---

## 3.2 Character Sheet (CSH)

Describes **which resources a character needs**. It is the key to the streaming
system: on loading a set, the engine walks the active sheets and marks
everything else as garbage.

```
cshNext      .l  -> next sheet (0 = last)
cshFlags     .w  (CSHLoaded, CSHLocked, CSHMarked)
cshModelOff  .b  model offset and count
cshModelNum  .b
cshAnimOff   .b  animation offset and count
cshAnimNum   .b
cshMiscOff   .b
cshMiscNum   .b
cshFileOff   .b
cshFileNum   .b
cshBehaviour .w
```

Combat-sound extension (`STRUCDEF.INC`): `soundKIA` (killed), `soundHIT` (hit),
`soundATT` (attack), `soundPAR` (parry).

The 28 sheets are listed in `DATA.INC`: `SHEET_SET` (0), `SHEET_QUENTIN`,
`SHEET_HUNTER_SWORD`, `SHEET_HUNTER_OFFICER`, `SHEET_HUNTER_GUN`, `SHEET_CLAW`,
`SHEET_RAMIREZ`, `SHEET_WOMAN`, `SHEET_MANGUS`, `SHEET_ARAK`, `SHEET_KORTAN`,
`SHEET_CLYDE`, `SHEET_DUNDEEB`, `SHEET_DUNDEED`, `SHEET_MACLEODSWORD`,
`SHEET_GASGUN`, `SHEET_WINE`, `SHEET_CHEESE`, `SHEET_LOAF`, `SHEET_KEY`,
`SHEET_LOCKET`, `SHEET_HAND`, `SHEET_REDWATER`, `SHEET_GREENWATER`,
`SHEET_BLUEWATER`, `SHEET_GRINDER`, `SHEET_TANK`, `SHEET_TURRET`.

---

## 3.3 3D models (polyhedra)

Produced by `SKELSKIN.EXE 3.6 (beta)` from 3D Studio. The source contains
**11 models in the clear** as assembly source, which make excellent parser test
fixtures: `MERLOT79.INC` (wine bottle), `CHEESE.INC`, `LOAF.INC`, `HKEY.INC`,
`LOCKET.INC` / `HLOCKET.INC`, `HSWORD_Q.INC` (Quentin's sword), `HWATAWEE.INC`,
`GASGUN.INC`, plus `COLINC.INC` with the palettes.

```
; header, 4 longs
.w  file length in bytes
.b  origin number
.b  number of origins
.w  vertex count
.w  facet count
.l  VLP  -> vertex list
.l  FLP  -> facet list
.l  SLP  (0 if absent)
.l  CLP  (0 if absent)
```

**Vertex list** — 4 words per vertex: `x, y, z, 1` (the last being the
homogeneous factor). Integer coordinates; the generated comment also records the
original floating-point value, for example `dc.w $fffb ; v0 x, = -22.630207`.
Scale is declared in the file header (`Using scaling factor of N mm per unit`).

**Facet list** — per facet:

```
(RGB | Nx) (Ny | Nz) (V | NP)      ; 3 longs
(v0|v1|v2|v3) ... (vu|vv|vx|vy)    ; groups of 4 byte indices
```

`V` = vertex count for this facet, `NP` = offset in longs to the next facet,
`RGB` = 16-bit colour, `Nx/Ny/Nz` = facet normal. Valid vertex indices are
0-254; **255 means "no reference"** (padding). Maximum 32 vertices per facet.

Colours are emitted as named constants with the 3D Studio material name in the
comment: `COLOURmerlot791 .equ $9a5e ; BEIGE MATTE r=155, g=122, b=74`. That
RGB-to-16-bit mapping is a useful cross-reference for pinning down the CRY/RGB
encoding in use.

---

## 3.4 Animations

Header:

```
ANIMSIZE   .w
OFFSETLOW  .w
FRAMESIZE  .w
ANIMMODELS .b   number of animated models (pieces)
NUMFRAMES  .b
ANIMFPS    .b
SOUNDSHEET .b
SOUNDENTRY .w
HEIGHTSTART .w
```

Frame:

```
animXmove .w    root motion
animYmove .w
animZmove .w
animYturn .b    rotation about Y
animFlags .b
animSpin  .b
animHit   .b    attack/defence value (negative = defence)
animRange .w    attack range
animDirAz .b
animDirEl .b
animSprAz .b
animSprEl .b
animHigh  .w    highest point of the character
animLow   .w    lowest point
animAngles ...  groups of 3 bytes (rotations) per model
```

So the animation model is **hierarchical rotations**: each frame carries only
three angles per piece plus root motion. Tweening between frames is computed at
runtime (`citTween`).

Combat comes out of the animation data itself: positive `animHit` is an attack,
negative is defence/parry, `animRange` is reach. See `PCOL.TXT` for the full
resolution algorithm.

Animation state flags (`LOGICS.INC`): `FSATurn`, `FSAPlay`, `FSALock`,
`FSAShield`, `FSAHit`.

---

## 3.5 Sets and scenes

A **set** is an environment (a "room" of the world); a **scene** is one fixed
camera view inside that set. `HIGH1.MAK` lists the **45 sets**:

```
CANYO CN1_MK2 CN2_MK2 CN3_MK2 CN4 CN5 CN6_MK2 CN7_MK3 CN8_MK3 CN9
CODER CODER2 COR_DOR COR2_DOR COR3_DOR DOME1 DOME2
DUN_4 DUN1 DUN2 DUN3 DUN5 DUN6
G_ROOM1 G_ROOM2 G_ROOM3 MENU NEWSEWER PRISON2 REST SECUR SEWER
SHANR1 SHANR2 SHANR3 TANK
TENT1 TENT2 TENT3 TENT4 TENT5 TENT6 TENT7 THRONE TRAIN
```

`CDLINK.INC` lists **594 scenes** (camera views) in the July build. The retail
disc appears to hold around 672 — see [06-jcd-format.md](06-jcd-format.md).

Set data header:

```
Hinum        .l
Lonum        .l
EventOffset  .l   -> event data
CollOffset   .l   -> collision data
InitOffset   .l   -> initialisation points
SceneOffset  .l   -> scene lookup table (CD block offsets)
ScriptOffset .l   -> set script
```

### Collision mesh

```
; header
TriOffset .w
NumTri    .w
NumVer    .w
(padding) .w
VertData  ...   vertex pairs (longs)

; each triangle, 14 bytes
triHeight .w    0 = infinite
triVert0  .w
triVert1  .w
triVert2  .w
triTri01  .w    triangle across edge 0-1, or $FFFF = wall
triTri12  .w
triTri20  .w
```

This is a **2D navigation mesh with explicit adjacency**: a character always
knows which triangle it is in (`citTriangle`), so finding the next one costs a
neighbourhood walk rather than a global search (`FINDTRI.GAS`).

---

## 3.6 The `.MAP` format (map editor)

These are **text** files generated by Matthew Jesson's "Map Editor 1.211b Beta".
Only two survive in the dump (`DUN1.MAP`, `DUN2.MAP`) but they document the
format completely.

Block types: `MAP`, `BACKGROUND`, `VERTEX`, `COLLISION`, `ORIGIN`, `SCALE`,
`EVENT`, `START`, `MARKER`, `CHARACTER`, `SCENES`, `CAMERAS`.

```
BLOCK VERTEX          NUM_VERTEX 117. / VERTEXn. x. y.      (2D coordinates)
BLOCK COLLISION       VERTEX_LIST a. b. c.  HEIGHT h.       (one triangle)
BLOCK ORIGIN          VERTEX n.  HEIGHT h.
BLOCK SCALE           VERTEX0 / VERTEX1 / DISTANCE 8382.    (world units)
BLOCK EVENT           VERTEX0 / VERTEX1 / HEIGHT / PRIORITY / SCENE <name>
                      optional IFBIT <world state bit>
BLOCK START           VERTEX / ORIENTATION / START <who> / FROM <scene>
BLOCK MARKER          VERTEX / HEIGHT                        (script targets)
BLOCK CHARACTER       VERTEX / HEIGHT / TYPE / ORIENTATION / RADIUS
                      optional SAN / PER / LIFE / STRENGTH
BLOCK SCENES          ( PICTURE <scene> CAMERA <cam>
                        CACHE_SCENE0 <scene> CACHE_SCENE1 <scene> )
BLOCK CAMERAS         THREEDSTUDIO F:\DUN2.3DS
```

Two things matter here:

1. **Events are line segments, not circles.** Each `EVENT` has two vertices and
   a `PRIORITY`: crossing that line switches the camera. Priority resolves
   overlaps (see `CACHE.TXT`).
2. **`CACHE_SCENE0`/`CACHE_SCENE1`** declare which two scenes to prefetch while
   the player is in the current one. This is the caching scheme that hides CD
   seek latency.

---

## 3.7 The script language

A **multitasking VM** running on the GPU (`SCRIPT.GAS`, 1,603 lines). Each
process occupies 128 bytes:

```
scriptPC     .l   0 = inactive
scriptStack  .l
scriptFlags  .l   copy of the GPU flags
scriptPause  .w   countdown (256ths of a second)
scriptAction .w   pending action handle (0 = none)
scriptChar   .l   primary character
scriptIdent  .l   unique identifier
scriptReg    .l x15   registers
scriptSpace  .l x11   short stack
```

The full instruction set is in `OPCODES.INC`, with assembler macros in
`SCRIPT.MAC`. By category:

| Category | Opcodes |
|---|---|
| Arithmetic | `not neg abs add sub cmp copy mult div inc dec sett cmpi exg` |
| Control flow | `bra quit bsr rts spawn suicide kill pause` |
| Characters | `select swapchar chartoreg regtochar animate animhack setanim chase attack face goto turnto attplay freeze release waitforit waitforanim` |
| Events | `cinepak redbook sample camera charchange eventbit waitevent waitredbook eventmask` |
| Data access | `wstread wstwrite actread actwrite citread citwrite` |
| Game state | `testowner testset testscene testbit setbit testprox testdist testevent` |
| Input | `keytest keymask` |
| Misc | `slideto restore sat patch activation reset fake_scene pickup default poke triangle_height` |

`quit` means "yield until the next frame": it is a cooperative coroutine VM, one
tick per game loop.

A real example (`DOME1.SCT`, reproduced in full):

```
    testbit WSB_PLAYED_DOME_CINEPAK
    branch  lExit, ne
    pause   2560
    setbit  WSB_PLAYED_DOME_CINEPAK
    cinepak BO_CINEPAK_DOME
lExit:
    quit
    abandon
```

`SAMPLE.SCT` and `MENU.SCT` are meatier scripts (main menu navigation, death
handling, a cheat code) and serve as the reference specification for the
semantics.

### World State Bits
`WSB.INC` plus `MATTSWSB.INC` and `ROBSWSB.INC` define the global progression
flags (a 128-byte `gamestate` area), for example `WSB_PRISON_DOOR_OPEN`,
`WSB_QUENTIN_HAS_CELL_KEY`, `WSB_SEEN_HOO`, `WSB_DUN2_OK_LEAVE`,
`WSB_COLLECT_PREVENT`.

---

## 3.8 CD data types

As defined in the July 1995 build:

```
DATA_TYPE_BOOT     0     DATA_TYPE_SETS      6
DATA_TYPE_LOGICS   1     DATA_TYPE_WAVES     7   (Red Book)
DATA_TYPE_MODELS   2     DATA_TYPE_BITMAPS   8
DATA_TYPE_ANIMS    3     DATA_TYPE_PICTURES  9   (640x400)
DATA_TYPE_SCENES   4     DATA_TYPE_SHEETS   10
DATA_TYPE_SOUNDS   5     DATA_TYPE_CINEPAKS 11
                         DATA_TYPE_CODES    12
```

The retail disc consolidated these to eight — see
[06-jcd-format.md](06-jcd-format.md) §6.5.

Scene event types:

```
EVENT_TYPE_SCENE 0   SOUND 1   CINEPAK 2   SOUNDLOOP 3   CDAUDIO 4
SOUNDOFF 5   RESTOREITEM 6   SETBIT 8   RESETBIT 9   CHARCHANGE 14
```

---

## 3.9 Save games (NVRAM)

`NVRAM.S`, `SAVE.TXT`, `NVRAM2.TXT`. Fixed file names imposed by Atari
(`GAME1`..`GAME9999`), a 128-byte gamestate area plus a subset of the WST for
each character and object. There is also a fallback path **without NVRAM**,
based on codes entered on the keypad (`NONNVRAM.TXT`). For the port we can
ignore the original format and simply serialise WST plus gamestate.
