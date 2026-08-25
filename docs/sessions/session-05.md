# Session 5 — The script VM opens, and with it the films

The biggest thing left on the disc was the bytecode at `ScriptOffset`. It is
open. Twenty-seven sets have a script, plus a resident one in the binary;
**1,173 commands decode with nothing left over**, and every film reference in
them lands on a real film.

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

---

## Still open

* **Track 9.** Unchanged from session 4: 55,188 bytes of high-entropy payload, no
  loader found for data type `$27`.
* **Film audio.** `STAB` still undecoded; that is where the speech is.
* **The four unreferenced films** — 15, 28, 33, 34.
* **The world-state table.** Scripts address entries by number and July's
  `WORLD.INC` names 116 of them, but the retail table has more and the indices
  have drifted: `CA_TURRET_KEY` is 6 in both, `CODE_HAND` is 43 in July and 74
  on the disc. The drift is monotone, so the same alignment trick that named the
  sets should work — but it needs the retail table, which is built at runtime
  and has not been located yet.
* **`gvar[0..2]`**, the combination the code locks check, is written by
  something outside the scripts.
* Seventeen GPU modules, the `SLP` payload, eleven binary models, the remaining
  camera-footer fields — all as session 4 left them.

---

## TODO for session 6

1. **The waves extractor.** The format is known — `long total; 'WAVE'; long
   size; 8-bit samples` — 38 bundles on track 6 and 36 on track 5. An hour's
   work, and it finishes the sound-effect side.
2. **`STAB`.** Decode the sample table interleaved with each Cinepak chunk;
   16-byte entries (offset, size, timestamp, type), types 1 and `$32` seen,
   rates from `CINEPAK.INC`. This is the dialogue.
3. **The world-state table.** Find where the retail `ws` at `$32660` is filled
   from. If it is a linked table in the binary like the item models were, every
   character and object in every script gets a name, and the scripts stop being
   anonymous.
4. **The manifest.** One JSON tying scenes, sets, models, animations, films and
   now scripts together. It is the stated phase-2 deliverable and everything it
   needs now exists.
5. **Track 9**, still by widening `dis68k`'s recursion through the jump-table
   idiom at `$50A6`, or by emulator.
6. **Then phase 3** — the SDL3 viewer. Carry over: models are **Z up**, pixels
   are **R5 B5 G6**.
