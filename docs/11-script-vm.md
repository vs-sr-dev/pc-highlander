# 11 — The script VM

The game's puzzles, cutscene triggers, menus and cheat codes are not 68000 code.
They are bytecode for a small virtual machine that runs **on the GPU**, one
command per dispatch, several processes interleaved, one command per process per
game frame when a process yields.

July's dump contains the whole specification: `SCRIPT.GAS` is the interpreter,
`OPCODES.INC` fixes the opcode order, `SCRIPT.MAC` defines the encoding, and
`MENU.SCT`, `CHEAT.SCT`, `SAMPLE.SCT`, `DOME1.SCT` are worked examples. The
retail disc ships the same machine with eight opcodes added.

Reference implementation: [tools/script/scriptx.py](../tools/script/scriptx.py).

---

## 11.1 Where the machine lives

The interpreter is **GPU module `$2F310`** — 2,976 bytes, posted from `$545A`
and `$8CF2`, one of the eighteen that were unidentified after session 4. It was
found by searching the binary for `condtable`, the five-long condition mask
table `0, $11, $22, $44, $05` that `SCRIPT.GAS` ends with. `scripttable`, the
dispatch table, follows it immediately.

```
$2FD8C   condtable      5 longs
$2FDA0   scripttable   70 longs, GPU addresses $F033xx..$F03Cxx
$2FEB8   MAINSCRIPT    the resident script, 516 bytes, runs to $30130
```

Two things fall straight out of that table:

* it has **70 entries**, July's had 62, and the entries are numbered from
  `sc_exg` = 13, so the retail machine's last opcode is **83**, not 75;
* sorting the 70 handler addresses puts them in *exactly* the order the
  handlers appear in `SCRIPT.GAS`, with eight new ones interleaved. So every
  opcode from 0 to 74 kept its July number and the eight additions were
  **appended as 75 to 82**. The decoder confirms the bottom end independently:
  it still branches on `cmpq #3`, `cmpq #9` and `cmpq #13`, the boundaries
  between July's operand classes.

## 11.2 Encoding

One command is a big-endian long:

```
31..24  opcode
23..20  addressing mode      (for a branch: the condition code)
19..16  register             (0..14)
15..0   operand              sign-extended when it is an offset
```

which is `combine` in `SCRIPT.MAC` byte for byte:
`(opcode<<24) | (addmode<<20) | (register<<16) | value`.

Thirteen commands take extra longs, read through the program counter:

| extra | commands |
|---|---|
| 8 bytes, x and z | `goto` `turnto` `testdist` `slideto` |
| 4 bytes | `cinepak` `redbook` `testprox` `keymask` `patch` `triangle_height` |
| 4 bytes if mode = 0 | `eventmask` |
| 4 bytes if mode ≠ 0 | `andi` |
| 4 bytes, *not stepped over* | `fake_scene` |

`fake_scene` is the odd one: both July's handler and the retail one read the
long at the program counter and never advance past it, so the block number would
be executed as a command afterwards. No shipped script uses the opcode.

**Flow.** `bra`, `bsr` and `spawn` are pc-relative, target = address + 4 +
operand. `kill` names another process by its byte offset from the start of the
script. `quit` is **not** a terminator — it yields for this game frame and
resumes at the next command — so only `rts`, `suicide` and an unconditional
`bra` end a trace. That, plus the fact that a script is followed by slot
padding, is enough to find the end of every script exactly.

**Conditions.** A compare leaves the GPU flags of `register - operand`, and
`condtable` masks them. The mnemonics below are `SCRIPT.MAC`'s, and two of them
do not read the way you would expect:

| code | name | true when |
|---:|---|---|
| 0 | *(none)* | always |
| 1 / 9 | `eq` / `ne` | register = operand / ≠ |
| 2 / 10 | `cs` / `cc` | carry set / clear |
| 3 / 11 | `mi` / `pl` | register < operand / ≥ |
| 4 / 12 | `lt` / `ge` | register **>** operand / ≤ |

## 11.3 The opcodes

Zero to 74 are `OPCODES.INC` unchanged. The interesting ones:

```
 0-12  not neg abs | add sub cmp copy mult div | inc dec sett cmpi
13-21  exg bra quit bsr rts spawn suicide kill pause
22-38  select swapchar chartoreg regtochar animate animhack setanim
       chase attack face goto turnto attplay freeze release
       waitforit waitforanim
39-45  cinepak redbook sample camera charchange eventbit waitevent
46-51  wstread wstwrite actread actwrite citread citwrite
52-59  testowner testset testscene testbit setbit testprox testdist testevent
60-74  waitredbook eventmask keytest keymask slideto restore sat patch
       activation reset fake_scene pickup default poke triangle_height
```

`testevent` (59) shares its handler with `quit` in both builds: it was never
written.

The three table commands share one operand layout — mode is `(size<<2) | flags`,
with size 0 = byte, 1 = word, 2 = long, flag bit 0 = "the character is in a
register", flag bit 1 = "the operand's low byte is a world-state index" — and
the operand is `(field offset << 8) | index`. Field offsets are `LOGICS.INC`'s
`wst*` / `act*` / `cit*`.

### The eight retail additions

Named here from the shipped handlers; July has no source for them.

| # | name | what the handler does |
|---:|---|---|
| 75 | `testuse` | compares `ws + oper*32` with the latch at `$4490` and clears it; sets the flags. "Did the player just use world-state entry N?" |
| 76 | `random` | steps a 32-bit seed at `$4E18`, scales the result to `1..oper` when the operand is non-zero, writes it to the register |
| 77 | `getglobal` | reads `gvar[oper]` — the long array at `$4DF4`, shared by all processes — into the register |
| 78 | `setbitreg` | `setbit` with the bit number taken from the register; falls straight into the `setbit` handler |
| 79 | `waveexit` | writes function code 4 to the DSP wave interface at `$F1B390`, which is `waveExit` in `WAVE.INC` — it shuts the sample player down, and every use is immediately before a film or a `reset` |
| 80 | `andi` | ANDs the register with an immediate, word or long |
| 81 | `waitkey` | in mode 0, waits for one pad bit pressed **alone**, and quits to `pc + (operand >> 5)` if a different key is pressed; in mode ≠ 0, waits until nothing is pressed |
| 82 | `gvar` | byte / word / long read or write of the variable block at `$4E14`, which is `gvar[8]` of the same array `getglobal` walks |

`waitkey` exists because July's `CHEAT.SCT` spent a whole `waitforkey`
subroutine — `quit / keytest / branch / keymask / branch` — on what one command
now does. `activation` (68) also changed: retail decodes its mode the way the
table commands do, bit 1 = direct world-state index, bit 0 = register, and takes
the new flag from `mode >> 2`.

## 11.4 The RAM the machine touches

Read out of the module's `movei` immediates, so these are retail addresses:

```
$4120   pad_now                     $4408   gamestate, the game-state bit array
$4490   "object being used" latch   $42300  setdata, the current set's header
$44E0   currws     $44E4  currmod   $44E8   gameover
$45BC   eventmode, the event mask   $45C0   scriptblock
$45E6   CUS, current scene          $45E8   CurrSet
$45F0   statevectors, the process table, 128 bytes each
$4DF0   scriptscene  $4DF2 scriptevent
$4DF4   gvar[0..], the script global longs
$4E14   the byte/word variable block (= gvar[8])   $4E18  the random seed
$4F18   ScriptSet
$2FEB8  MAINSCRIPT
$32660  ws, the world state, 32 bytes per entry
$34760  cs, the character sheet chain
$F1B390 WaveInterface (DSP)         $F1B618 redbookdetect (DSP)
```

`ws` starting at `$32660` and `cs` at `$34760` leaves room for 260 world-state
entries; the highest index any script names is 196.

The event codes the machine writes into `scriptevent` are `EVENT_TYPE_* + 1`,
and they agree with the numbering `setx` already used for the event table:
`SCENE` 0, `CINEPAK` 2, `CDAUDIO` 4, `CHARCHANGE` 14.

**`$4E14` is the language.** The 68000 routine at `$00667E` waits for the "used"
latch to hold for five frames, then reads `$4E14`, scales it by four and indexes
a table of string pointers at `$24F70` — which holds `YOU CAN'T USE THAT HERE.`
then `NE SERT PAS ICI.` then `DAS HIER KANNST DU NICHT BENUTZEN.` So 0 is
English, 1 French, 2 German, in `textx`'s order, and it is the LANG set's script
that writes it.

## 11.5 What is on the disc

Twenty-seven of the 48 sets have a script, plus `MAINSCRIPT`: **1,173 commands
in about 5 KB**, and every one of them decodes — no undecodable long anywhere,
across every script.

Some of what they turn out to be:

* **`MAINSCRIPT`** is three processes and a handler. One waits for the player's
  `Life` to reach zero, then plays film 6 and resets the machine. Two are cheat
  codes on the Jaguar keypad, entered as digit-then-release pairs. A fourth
  block is the "item used" handler — play animation `r0`, add `r1` to `Life`,
  saturate, optionally `restore` the item — and nothing in any script spawns it,
  so its registers and its start are set from outside the bytecode.
* **`CODE` and `CODE2`** are three-tumbler combination locks. Four positions per
  tumbler, joypad left/right to change tumbler, up/down within it, fire to step
  it, and the answer is compared against `gvar[0]`, `gvar[1]`, `gvar[2]` — so the
  combination is set from outside the script. Getting it right plays a film and
  sets a game bit; getting it wrong sets a different one.
* **`SE`**, the sewers, is a sluice puzzle: four camera views, each raising one
  pair of collision triangles to height 885 and dropping another pair to 0 with
  `triangle_height`, so the walkways move. A second process drains one point of
  `Life` every sixteen ticks while game bit 33 is set — that is drowning.
* **`SHANR1`** watches the player's `Ypos`. Below 2,000 you have fallen: more
  than 50 life left and you lose 50 and get put back at `SHANR1_CAM03` after a
  film; 50 or less and it plays the death film and resets.
* **`CA`** waits for game bit 27, then `testuse #6` — world-state entry 6, which
  July's `WORLD.INC` calls `WORLD_CA_TURRET_KEY` — then plays a film, clears the
  entry's parent and deactivates it. The key is consumed.
* **`PRI`** ends the game: on game bit 37 it writes 1 to `$4E16`, plays film 10
  and film 5, and calls `reset`.

The two menu sets are worth their own paragraph, because they settle a naming
question left open in §10.6.

## 11.6 The two menu sets

Sets 29 and 30 each have two or three scenes, so the majority vote `setx` used
to decide which scene group a set owns cannot separate them. Their scripts can.

Set 29 runs a three-item selector with a `pause 65535 / reset` timeout process
beside it — exactly the shape of July's `MENU.SCT`, timeout and all. Its third
item plays film 5 and its first releases the player and teleports him into the
world. That is the **main menu**, so set 29 owns group 30, whose one scene is the
"START GAME / LANGUAGE / CREDITS" backdrop session 4 decoded.

Set 30 runs the same selector but each item only writes 0, 1 or 2 to `$4E14`
before leaving. That is the **language screen**, so set 30 owns group 31, and
decoding group 31's one scene gives "SELECT LANGUAGE / ENGLISH / FRANCAIS /
DEUTSCHE" over the same stormy hill. Group 31 was one of the three groups with no
July counterpart; it now has a name.

The same argument fixes sets 10 and 11: both drive a combination lock and both
leave by a `camera` to somewhere else — set 10 to `TA_CAM03`, set 11 to
`D2_CAM12` and `PRI_CAM00` — so the lock panels are theirs and they own groups
14 (`CODE`) and 15 (`CODE2`). `setx` carries the four as an explicit override.

## 11.7 The films

Every `cinepak` operand on the disc is a CD block offset that matches a film in
the track-7 inventory **exactly**, in all 36 script sites and all three
`CINEPAK` events. That places **32 of the 36 films**:

| film | block | KB | triggered by |
|---:|---:|---:|---|
| 0 | 0 | 1119 | SECUR script |
| 1 | 543 | 2115 | DUN1 script |
| 2 | 1520 | 6044 | SE event, twice |
| 3 | 4208 | 3252 | PRI script |
| 4 | 5680 | 2031 | TENT5 script |
| 5 | 6621 | 13040 | MENU script, PRI script |
| 6 | 12355 | 10040 | SHANR1 script, MAINSCRIPT |
| 7 | 16782 | 9321 | C3 script |
| 8 | 20897 | 5252 | CNY07 script |
| 9 | 23240 | 34701 | MENU event |
| 10 | 38406 | 16147 | PRI script |
| 11 | 45492 | 2298 | C3 script |
| 12 | 46549 | 4018 | C1 script |
| 13 | 48355 | 1479 | SHANR1 script |
| 14 | 49055 | 1420 | SHANR1 script |
| 15 | 49730 | 15129 | — |
| 16 | 56373 | 758 | set 27 script |
| 17 | 56759 | 4320 | CODE script |
| 18 | 58697 | 17639 | DUN2 script |
| 19 | 66433 | 1430 | SE script, four times |
| 20 | 67112 | 2575 | TENT5 script |
| 21 | 68290 | 1079 | DUN1 script |
| 22 | 68816 | 6910 | SHANR3 script |
| 23 | 71881 | 4393 | D3 script |
| 24 | 73850 | 1926 | CA script |
| 25 | 74745 | 1054 | C1 script |
| 26 | 75260 | 1283 | CNY01 script |
| 27 | 75875 | 2247 | CODE2 script |
| 28 | 76909 | 21501 | — |
| 29 | 86327 | 4618 | SHANR3 script, twice |
| 30 | 88394 | 2743 | SE script |
| 31 | 89645 | 4353 | NEOSW script |
| 32 | 91596 | 4530 | NEOSW script |
| 33 | 93625 | 1513 | — |
| 34 | 94340 | 14851 | the boot sequence, from the 68000 (§9.2) |
| 35 | 100863 | 2771 | CA script |

Four films have no trigger on the disc: 15, 28, 33 and 34. Three of them are
among the longest on the track, which is what you would expect of an ending and
a title sequence played by the 68000 rather than by a set.

Four are named by what the code does with them, not by matching a list:

* **film 6 is the death film** — `MAINSCRIPT` plays it when `Life` hits zero,
  and `SHANR1` plays it when you fall too far with too little life left.
* **film 5 is the credits** — it is the main menu's third item.
* **film 10 is the ending** — `PRI` plays it, then the credits, then `reset`.
* **film 9 is the intro** — the main menu set's `CINEPAK` event fires it, and at
  35 MB it is the longest film on the disc.

July's film list is alphabetical, and `CREDITS`, `DEAD` and `ENDINGA` sit at
positions 5, 6 and 10 of a retail list of 36 if the retail track is alphabetical
too and sixteen names were added. That is consistent, and `DUNTRO` — July's
alias for `INTRO` — would then be film 9, which is what the menu plays. It is
consistency, not proof: the remaining names still need a second source.

## 11.8 Using the tool

```
python tools/script/scriptx.py TRACK3 --set 29
python tools/script/scriptx.py TRACK3 --all --out assets/scripts
python tools/script/scriptx.py TRACK2 --main
```

The disassembler traces the flow from offset 0, follows `bra` / `bsr` / `spawn`
/ `kill` / `waitkey` targets, sweeps whatever the trace did not reach, and stops
at the padding. It resolves scene ids against `sets.json` and `cinepak` blocks
against `films.tsv` when those files are present.

## 11.9 The machine, ported

`src/script/vm.c` is the same machine with the handlers written out rather than
printed, and `src/game/act.c` is the table it operates on.

### What the VM needed underneath it

Almost every command that does something to the world does it to an **active
character table record** — `LOGICS.INC`'s `actRecord` — so that table had to
exist before the interpreter could. It is one record per character who is in
the world right now: which world entry he is, the joypad he is pressing, the AI
that may be pressing it, and the instance standing on the floor.

That is also where `AICTRL.GAS`'s two master loops belong, and putting them
there is what makes the script commands short. `chase` is four lines because
all it does is write `aiGotoPerson` into a record the AI loop is already
walking; `freeze` is `aiNop`; `release` clears one bit. The bit is
`ACTControlled`, and it is deliberately *not* "is he the player": the test that
picks `ComputerControl` over `PlayerControl` looks at it first, so a script
takes the pad away from the player and gives it back with the same flag.

| script command | what it writes |
|---|---|
| `chase` / `attack` / `face` | `aiGotoPerson` / `aiAttackPerson` / `aiFacePerson`, and the target world entry in `AIData1` |
| `goto` / `turnto` | `aiGotoPosition` / `aiFacePosition`, and an (x, z) in `AIData1` and `2` |
| `attplay` | `aiAttackPlayer`, and *clears* `ACTControlled` |
| `freeze` | `aiNop`, and sets it |
| `release` | clears it, and nothing else |
| `default` | `aiDefault`, which reloads `cshBehaviour` |

### Where the port and the original differ

Three places, all of them stated here rather than hidden:

* **Slot padding ends a process.** A script runs to the end of what was written
  and the rest of its 56-block slot is zeros — and a zero command is `not r0`,
  which the real machine would execute for ever, spinning the GPU without
  stopping the game. Two sets do exactly that: set 4's `ScriptOffset` points
  straight at padding, and set 37's script is two commands (`sett #0, r1` and a
  `gvar` write that clears the byte `PRI` sets when the game ends) and then
  padding. The disassembler reached the same reading independently, measuring
  those two at 0 and 8 bytes, and **no shipped script uses `not` at all** —
  `neg` is the only one of the three single-register commands that appears.
* **A per-process command budget.** The original has none, and does not need
  one: a script with no `quit` in its loop hangs the GPU and that is that.
  Counting the overrun is how a check can see one, and it counts zero.
* **The `citRecord` and `actRecord` fields** are a struct here rather than a
  block of memory addressed by byte offset, so the offsets the shipped scripts
  actually use are mapped and anything else is counted. The disc's scripts
  touch eight fields in all — `Life`, `Ypos`, `Parent` and `Radius` on the
  world record, `Stance`, `Frame` and `Speed` on the instance, `World` on the
  ACT record — and the count of unmapped accesses is zero.

`cinepak`, `redbook`, `sample` and `waveexit` post their event and wait for the
host to clear it, which is the original's own handshake and all a script ever
sees of them. Playing the film is phase 6's and phase 7's problem, not the
machine's.

### The check

```
hlview --check-script
```

loads each of the twenty-seven set scripts in turn beside the resident one and
runs them for 1,200 game frames against a real set and a real character. Two
passes: the first is the set as you would walk into it, and most scripts spend
it sitting on a game bit that nothing has set — which is correct, and reaches
about a third of the machine. The second stirs the world underneath them, game
bits and the "used" latch and one pad key at a time, and starts MAINSCRIPT's
fourth block the way the engine does, from outside the bytecode with `r0`, `r1`
and `r2` already set.

```
scripts: 48 sets, 27 of them with one, plus MAINSCRIPT in every run
  585,226 commands executed, 46 of the 83 opcodes exercised
  0 past the dispatch table, 0 processes overran, 0 fields with no home
  2 processes ran into their slot's padding and stopped
  10 camera cuts asked for, 0 of them to a view the set does not list, 22 films
```

The four numbers that have to be zero are zero. **Nothing past the dispatch
table** matters most: the original abandons the process on a bad command, so a
decoder that drifts by a byte — a missed extra long, an operand class read at
the wrong boundary — shows up as scripts quietly dying rather than as an error.
**Nothing overran**, so every loop on the disc reaches a `quit`. And every
`camera` a script asked for named a view its own set lists, which is an operand
checked against a table the VM never reads.

Forty-six of 83 is against the whole machine; the disc's scripts contain 49
distinct opcodes, and the three of those never reached — `kill`, `actread` and
`poke` — sit behind branches this stimulus does not open. The rest of the 83
are opcodes no shipped script uses at all.

## 11.10 In the loop

`src/game/game.c` is one game frame, in the order the original runs it:

```
1  the script machine, one command per process until it yields
2  the events it posted, which the host acts on and clears
3  ControlCode and ActionCode over every active character
4  the set's own event lines, which cut the camera and open the doors
```

Step 2 is the whole of how a script reaches the outside world. The machine
writes `EVENT_TYPE + 1` into `scriptevent` and **will not run another command
until it is cleared**, so `camera`, `cinepak`, `redbook`, `sample` and
`restore` all wait on the same handshake — which is why a port that cannot yet
play a film does not have to pretend: it takes the request, clears the event,
and the script carries on.

The `eventmask` a script writes now gates step 4 as well, which is what it is
for: `eventmask $0000` turns the set's own event lines off for the length of a
cutscene and `eventmask $FFFF` puts them back.

### What it looks like from the outside

```
build/hlview --scene SE_CAM05 --char 0 --drive
```

```
frame    1: 3 script processes running
frame    2: 6 script processes running
frame    3: a script moved the floor: triangle 59 to 0 (4 written)
```

Three processes on the first frame are MAINSCRIPT's — the one that watches the
player's `Life` and the two cheat codes. The three more on the second are the
set's own, because the set script starts a frame after the resident one, which
is what `allnew` arranges by leaving behind a set number that cannot match.

And then the sewers' sluice runs. `SE`'s script tests which view you are
looking at and writes four collision triangle heights — one pair up to 885, one
pair down to 0 — so the walkway you can stand on changes with the camera. That
is a puzzle from the disc, on the disc's own data, moving the floor the
character is standing on.
