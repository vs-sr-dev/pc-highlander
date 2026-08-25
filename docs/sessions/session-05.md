# Session 5 — The script VM, the audio, and the world state

The biggest thing left on the disc was the bytecode at `ScriptOffset`. It is
open: twenty-seven sets have a script, plus a resident one in the binary, and
**1,173 commands decode with nothing left over**, every film reference in them
landing on a real film. The audio came out too — the 78 sound-effect bundles,
and the **dialogue**, which turned out to be interleaved with the video inside
the films: eighteen minutes of it. And the world-state table and the character
sheets, which were never on the disc at all — they are static data in the
binary, and finding them corrected three of session 4's set names.

---

## 1. Finding the machine

July's `SCRIPT.GAS` ends with `condtable`, five longs reading
`0, $11, $22, $44, $05`. That sequence occurs **exactly once** in the retail
binary, at `$2FD8C`, which puts the interpreter inside GPU module `$2F310` —
one of the eighteen overlays that had no name after session 4.

Its dispatch table follows the condition table, and two facts fall out of it:

* the table has **70 entries** where July's had 62, and it is indexed from
  `sc_exg` = 13, so the retail machine's last opcode is 83;
* sorting the 70 handler addresses reproduces **exactly** the order the handlers
  appear in `SCRIPT.GAS`, with eight new ones interleaved between them. So every
  opcode from 0 to 74 kept its July number and the additions were appended as
  **75 to 82**.

The decoder confirms the bottom end on its own: it still branches on `cmpq #3`,
`cmpq #9` and `cmpq #13`, July's operand-class boundaries, and it still falls
back to `sc_suicide` = 19 on a bad opcode.

The eight new opcodes were then read out of their handlers: `testuse`, `random`,
`getglobal`, `setbitreg`, `wavestop`, `andi`, `waitkey`, `gvar`.
`waitkey` is the giveaway for why they exist — July's `CHEAT.SCT` spends an
entire `waitforkey` subroutine on what that one command now does.

→ [11-script-vm.md](../11-script-vm.md),
[tools/script/scriptx.py](../../tools/script/scriptx.py)

## 2. Four independent checks that the decoding is right

* **The menu scripts.** Sets 29 and 30 decode to three-item selectors with
  `slideto` between three fixed positions, `keytest` on two pad bits, and
  `keymask $22002000`. `MENU.SCT` writes that mask as `FIRE_ABC`, and
  `readpad` in `JOY.S` documents the joypad long as `xxApxxBx RLDU741*
  xxCxxxox 2580369#` — which makes `FIRE_ABC` exactly `$22002000`. The two pad
  bits it tests are then `JOY_UP` and `JOY_DOWN`, which is what a menu wants.
* **Every film reference.** Thirty-six `cinepak` operands across the scripts,
  and all three `CINEPAK` events, land on a film in the track-7 inventory **to
  the block**.
* **`testuse #6` in the `CA` script.** World-state entry 6 is
  `WORLD_CA_TURRET_KEY` in July's `WORLD.INC` — a set-`CA` script testing a
  set-`CA` key. The script then clears the entry's parent and deactivates it,
  which is what consuming a key looks like.
* **`CHEAT.SCT`.** `MAINSCRIPT` turns out to be the cheat handler, and its
  structure is July's prototype rewritten around `waitkey`: a digit, a release,
  a digit, a release, and a `poke` of 1 into a flag byte — `allow_cheats` in
  July's source.

## 3. What the scripts turn out to be

* **`MAINSCRIPT`** is three processes plus a handler. One watches the player's
  `Life` and, at zero, plays film 6 and resets the machine. Two are keypad cheat
  codes; one of them writes the player's world-state pointer into the `Parent`
  of eleven objects, which is a give-all-items cheat. The fourth block is the
  "item used" handler and nothing in any script spawns it.
* **`CODE` and `CODE2`** are three-tumbler combination locks — four positions
  per tumbler, joypad left/right between tumblers, fire to step one, and the
  answer compared against `gvar[0..2]`, so the combination is set from outside
  the script.
* **`SE`**, the sewers, is a sluice puzzle. Four camera views, each raising one
  pair of collision triangles to height 885 and dropping another pair to 0 with
  `triangle_height`, so walkways rise and fall. A parallel process drains a
  point of `Life` every sixteen ticks while game bit 33 is set: drowning.
* **`SHANR1`** watches `Ypos`. Below 2,000 you have fallen; with more than 50
  life you lose 50 and are put back, with less you get the death film.
* **`PRI`** ends the game — a flag, film 10, film 5, `reset`.

## 4. The films

Thirty-six script references plus three events place **32 of the 36 films**, and
four are named by what the code does rather than by matching a list:

| film | it is | how we know |
|---:|---|---|
| 5 | the credits | the main menu's third item |
| 6 | the death film | played when `Life` hits zero, and on a fatal fall |
| 9 | the intro | the main menu set's `CINEPAK` event, and the longest film |
| 10 | the ending | `PRI` plays it, then the credits, then `reset` |

Films 15, 28, 33 and 34 have no trigger anywhere on the disc; three of them are
among the longest, which is what an ending and a title sequence played by the
68000 would look like.

July's film list is alphabetical, and `CREDITS`, `DEAD` and `ENDINGA` land on 5,
6 and 10 if the retail list is alphabetical too with sixteen names inserted.
Consistent, not proof.

## 5. Two menu sets, and a correction to session 4

Sets 29 and 30 have two or three scenes each, so the majority vote `setx` used
to decide which scene group a set *owns* could not separate them, and it got
three of them wrong. Their scripts settle it: set 29 runs the main menu (start
game, credits, and a `pause 65535 / reset` timeout, exactly July's `MENU.SCT`
shape)
and set 30 writes 0, 1 or 2 to a variable and leaves — the language screen.

So set 29 owns group 30 and set 30 owns group 31, and decoding group 31's single
scene gives **"SELECT LANGUAGE / ENGLISH / FRANCAIS / DEUTSCHE"**. That names
the third of the three post-July groups. The same argument gives sets 10 and 11
groups 14 (`CODE`) and 15 (`CODE2`). `setx` carries the four as an explicit
override.

## 6. Odds and ends the module gave up

The interpreter's `movei` immediates are a free map of the retail variables:
`pad_now` `$4120`, `gamestate` `$4408`, `setdata` `$42300`, `eventmode` `$45BC`,
`CUS` `$45E6`, `CurrSet` `$45E8`, `statevectors` `$45F0`, `ws` `$32660`,
`cs` `$34760`, `WaveInterface` `$F1B390`. `ws` at `$32660` and `cs` at `$34760`
leave room for 260 world-state entries; the highest any script names is 196.

**`$4E14` is the selected language.** The 68000 routine at `$00667E` scales it
by four and indexes a pointer table at `$24F70` holding
`YOU CAN'T USE THAT HERE.` / `NE SERT PAS ICI.` /
`DAS HIER KANNST DU NICHT BENUTZEN.` — so 0 English, 1 French, 2 German, the
order `textx` already uses, written by the LANG set's script.

## 7. The audio, all of it

Both remaining sound extractors landed, and between them they close the audio
side of phase 2.

**Sound effects.** `WAVE` records are `long total; 'WAVE'; long size; signed
8-bit samples`, with `total` = `size + 8` **rounded up to a multiple of four** —
which is why the first walk of the chain stopped after one record per slot. All
38 slots of track 6 hold one record; on track 5 the odd slots 1 to 17 and slot
28 hold **four each**, and four is exactly the number of combat sounds
`STRUCDEF.INC` gives a character: `soundKIA`, `soundHIT`, `soundATT`,
`soundPAR`. 78 waves, 98.7 seconds.

The rate comes out of `WAVE.DAS`, which drives the player off timer 1 with
`JPIT1 = 1` and `JPIT2 = $25A`: `26,590,906 / (2 * 603)` = **22,048 Hz**.
`SCLK = 19` in the same routine sets the I2S bit clock, not the sample rate.

**The dialogue.** It is inside the films, and the `STAB` table in each chunk
says where. The entry layout session 4 guessed at is right, and the two types
mean different things:

* type `$32` is a video frame and `offset` is where it **starts**;
* type `1` is an audio block and `offset` is where it **ends** — the block is
  `[offset - size, offset)`, and every one on the disc is 16,696 bytes.

The audio is signed 8-bit mono PCM at `audio_in` from `CINEPAK.INC`, **22,252
Hz**. The disc agrees: a one-second chunk carries one audio block and every
third carries two, so the long-run rate is `16,696 * 4/3` = 22,261 bytes a
second, and measured over the two longest films it comes to 21,900 and 22,250.

**And it is verified, not assumed.** Concatenating film 9's 227 blocks in `STAB`
order gives a signal whose mean absolute sample-to-sample step *across the block
joins* is 2.55, against 2.50 inside the blocks — the seams are invisible.
Shuffling the same blocks raises it to 6.93.

**18 minutes 23 seconds** of film audio, across 35 of the 36 films; film 16, five
chunks long, is silent. That is the dialogue budget July spent on 88 Red Book
lines, and it was hiding in the video all along.

→ [tools/wave/wavex.py](../../tools/wave/wavex.py),
[tools/cinepak/filmwav.py](../../tools/cinepak/filmwav.py),
[09-text-and-fmv.md §9.3](../09-text-and-fmv.md)

## 8. Track 9, still shut, but better bounded

Four new results, all negative, and one that reframes the search:

* The track's **header is intact** — the `ATRI` lead-in and the
  `ATARI APPROVED DATA HEADER` with its type byte read normally. Whatever
  happened began at offset 96, where the content tag should be.
* Every other track's tag is one 4-byte value repeated sixteen times, which
  gives a known-plaintext test that does not need the tag's value: under a
  byte-wise XOR or additive cipher, `ct[i] op ct[i+4]` must equal
  `k[i] op k[i+4]` for 60 bytes. **No 64-byte window in the entire resident
  binary satisfies it**, under either operation. Best partial match: 5 of 60,
  which is chance.
* A 24-byte sample of the payload occurs **exactly once in the 456 MB image**, so
  it is not a stale copy of something else on the disc.
* Chi-square 291 on 255 degrees of freedom, and **not one 4-byte sequence
  repeats** in 55,185 overlapping positions. The plaintext is not the sparse,
  zero-filled kind the other data tracks hold.

The reframing: `sub_007E9E`, the data-type-to-track routine, has **exactly one
call site** in the whole binary, asking for type 5, the Cinepak track. Every
other read goes through the block number in `$4494` with its base computed
elsewhere, so there is no "ask for type 7" call to go looking for. The question
is which code path could ever compute a block inside track 9.

## 9. The world state and the character sheets

Both were "not yet located on the disc". They are not on the disc: they are
**static tables in the resident binary**, and the code that installs them says
so out loud. `$5310` copies 8,192 bytes from `$15458` to `ws` at `$32660` and
then walks the copy in 32-byte steps relocating the long at +4 from base
`$17458` to `$34760` and the long at +8 from base `$15458` to `$32660` — which
is `wstSheet` and `wstParent`, named and placed exactly as `LOGICS.INC` has
them. `$537A` does the same for `cs`, walking the `cshNext` chain.

So: **197 world-state records in use of 256**, and **40 character sheets**. The
first seven world records are byte-for-byte July's — `WORLD_QUENTIN` still
`100, 229, 7, 153, 255`, `WORLD_CA_TURRET_KEY` still at `(-2348, -3036)` with
its parent pointing at `WORLD_CA_KEY_HUNTER`.

The sheet format resolves too: a 16-byte header and then an array of longs
indexed **from the start of the record**, which is why `cshModelOff` is always
4. Every full character declares `cshModelNum = 15` — the same fifteen pieces
the models and the animations carry, now seen from a third side.

→ [12-world-and-sheets.md](../12-world-and-sheets.md),
[tools/world/worldx.py](../../tools/world/worldx.py)

### The two readings check each other

The world table and the script disassembly were done independently and agree
wherever they touch:

* the `CODE` script's three tumblers are world records 71, 72 and 73, and the
  table starts them at `z` = 5, 53 and 101 — the three `slideto` targets in the
  script, to the unit;
* the main-menu script's first `slideto` is `(38, -128)`, and record 143 starts
  at `(38, -128)`; the language screen's default is `(49, -72)`, and record 144
  starts at `(49, -72)`;
* `CA`'s `testuse #6` is `WORLD_CA_TURRET_KEY`, flagged
  `WSTInanimate|WSTCollectable` and owned by the key hunter.

### And it corrects the set names

Aligning the retail table against July's `WORLD.S` by **exact (x, z)
coordinates** pairs 72 of July's 116 entries. Each pairing carries a set name on
one side (`SCENE_<SET>_CAMnn`) and a group number on the other (`wstSet`), so
the 72 anchors vote 28 July set names onto retail groups — **unanimously**, no
name for two groups and no group for two names.

Twenty-five votes agree with session 4's camera-count alignment. Three do not,
and coordinates beat camera counts: **`G1`, `G2` and `G3` are groups 24, 25 and
26**, not 25, 26 and 28. `DUN5` keeps 23, and **`DUN6` turns out to have no
retail group at all** — a second deletion beside `D2_12B`. All three corrections
land on entries the camera-count pass had itself flagged as carried-by-position
rather than anchored.

---

## Still open

* **Track 9.** Better bounded (§8) but no nearer to opening: 55,188 bytes that
  measure as random, and no code path that could ask for it.
* **Three unreferenced films** — 15, 28 and 33. Film 34 is the boot sequence,
  played from the 68000.
* **125 of the 197 world records have no name** — their characters moved
  between July and October, so the coordinate match does not reach them.
* **The character sheets' long arrays** are zero in the binary and filled from
  the CD, so nothing statically links a sheet to a model bundle on track 5.
* **`cshBehaviour`** takes eleven distinct values and is presumably an AI
  selector; `AICTRL.GAS` is where to look.
* **`gvar[0..2]`**, the combination the code locks check, is written by
  something outside the scripts.
* Seventeen GPU modules, the `SLP` payload, eleven binary models, the remaining
  camera-footer fields — all as session 4 left them.

---

## TODO for session 6

1. **The manifest.** One JSON tying scenes, sets, models, animations, films and
   now scripts together. It is the stated phase-2 deliverable and everything it
   needs now exists.
2. **Track 9**, still by widening `dis68k`'s recursion through the jump-table
   idiom at `$50A6`, or by emulator.
3. **Then phase 3** — the SDL3 viewer. Carry over: models are **Z up**, pixels
   are **R5 B5 G6**.
