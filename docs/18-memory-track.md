# 18 — The Memory Track: saves in the retail binary

The save system was the one part of the 68000 code this project had not mapped:
[03-data-formats.md](03-data-formats.md) §3.9 described `NVRAM.S` from the
1995 source and moved on. **hyjinx187** mapped it in the retail binary, while
chasing why Load and Delete freeze on the RetroHQ GameDrive, and contributed
the map in [issue #1](https://github.com/vs-sr-dev/pc-highlander/issues/1).
This chapter is that map, checked against the bytes. Every address they gave
held. Reading the code around them adds the menu's dispatch (18.3), and that
changes what the freeze can be (18.5).

All addresses are runtime addresses. The main binary loads at `$5000`.
In `track02_00004000.bin` as `jcdinfo.py` extracts it, runtime =
file offset − `$D600`. The issue counts from the start of the track, which
gives − `$D6A2`. Both are the same bytes.

## 18.1 The BIOS

The cartridge's BIOS sits at `$2400` with the cookie `'_NVM'`. It is called
through `$2404` with an opcode word on the stack and the arguments after it,
and it returns in `d0`, negative on error. The opcodes and error codes are
`NVRAM.INC`'s:

| Opcode | Call | | Code | Error |
|---|---|---|---|---|
| 0 | Initialize(app, workspace) | | −1 | `ENOINIT` |
| 1 | Create(name, size) | | −2 | `ENOSPC` |
| 2 | Open(name) | | −3 | `EFILNF` |
| 3 | Close(handle) | | −4 | `EINVFN` |
| 4 | Delete(app, name) | | −5 | `ERANGE` |
| 5 | Read(handle, buf, len) | | −6 | `ENFILES` |
| 6 | Write(handle, buf, len) | | −7 | `EIHNDL` |
| 7 | SearchFirst(buf, …) | | | |
| 8 | SearchNext(buf) | | | |
| 9 | Seek(handle, offset, mode) | | | |

The names are at `$26FA0`: the application `HIGHLANDER ONE` in a 16-byte
field, then `SETTINGS`, then `GAME 1` to `GAME 5` in 10-byte fields from
`$26FBA`. The workspace handed to Initialize is `$126B00`.

## 18.2 The routines

| Address | What it does | NVM calls |
|---|---|---|
| `$7044` | the cookie test; sets `$4F0C` | — |
| `$7054` | Initialize(`$26FA0`, `$126B00`) | 0 |
| `$706E` | the slot list for SAVE: all five names, no call | — |
| `$70B2` | the slot list for LOAD and DELETE: Open each `GAME n`, Close it if it opened | 2, 3 |
| `$711A` | the file list for DELETE OTHER: everything on the cartridge | 7, 8 |
| `$716C` | copy the chosen 26-byte record (app, name) to `$4EF2`/`$4F02` | — |
| `$7186` | check a file's first chunk marker, then Seek back to 0 | 5, 9 |
| `$71D0` / `$71E4` / `$71F4` | Open / Close / Create(`$2000`) the chosen file | 2 / 3 / 1 |
| `$720E` | Delete the chosen file | 4 |
| `$727E` / `$72BC` | Read / Write the world table, `$1B80` bytes at `$32660` | 5 / 6 |
| `$7310`, `$735C` | read a chunk: its 2-byte marker, then its body into `$4EAA` | 5 |
| `$7378` | write a chunk | 6 |
| `$73A6` | Initialize, then Open `SETTINGS`, or Create it (`$200`) | 0, 2, (1) |
| `$73FC` | open SETTINGS and read the last slot used into `$4F0D` | via `$73A6`, `$735C` |
| `$7422` | read the settings chunk and apply it | via `$735C` |
| `$748A` | write the settings chunk (20 bytes) | via `$7378` |
| `$74BC` | read the settings at boot and after a load | via `$73A6`, `$7422` |
| `$75EA` | SAVE | 1, 6, 3 |
| `$762A` | LOAD | 2, 5, 9 |
| `$7894` | menu setup | via `$73FC` |
| `$79F6` | the menu, once a frame | — |

A chunk is a byte of length, a byte of length XOR `$99`, and the body. The
1995 `NVRAM.S` uses `$CF`. A game file holds four chunks: settings (20 bytes),
game state, scene (70 bytes), world table (7,040 bytes, 220 records of 32).
The settings chunk opens with two longs, `$4EA0` and `$4EA4`, that a GPU
routine computes (`$7030` posts command `$27568`). LOAD recomputes them once
everything is read. If they differ, it shows `FAILED`, **deletes the file**,
and restarts the game.

None of this is in the 1995 source, which predates the slot menu,
`SearchFirst` and the check value.

## 18.3 The menu

The pause screen (`$67E4`, opened from the pad handler at `$673E`) calls
`$7894` as it opens. Its save page calls `$79F6` once a frame. `$4F0E` is the
operation, 1 to 4, and `$4F0F` says whether the cursor is on the operations
or on the files. OPTION on an operation builds the list:

| Operation | List built by | NVM calls to list | OPTION on a file |
|---|---|---|---|
| 1 LOAD GAME | `$70B2` | up to 5 Open, a Close for each that opens | `$762A` |
| 2 SAVE GAME | `$706E` | none | `$75EA` |
| 3 DELETE GAME | `$70B2` | as LOAD | `$720E` |
| 4 DELETE OTHER | `$711A` | SearchFirst, SearchNext until negative | `$720E` |

After each operation the menu calls `$7894` again. Nothing points at `$79F6`
or at the three list builders except these branches: no data longs, no other
caller.

## 18.4 When it runs, and with what interrupts

**Boot.** `$5060` writes `$1F00` to `INT1`: it clears every pending source and
enables none. (`$5012`'s `$FFFF` to `VI` puts the video interrupt's line off
the screen. It is not a mask.) `$5198` then calls `$74BC`: Initialize,
Open SETTINGS, Read 2, Read 20. Nothing enables an interrupt source before
`$83C0` starts the game and calls `$8F1E`, which installs the handler at
`$8F42`, writes 3 to `INT1` and clears the SR mask. So boot's NVM calls run
with no interrupt able to arrive.

**The handler** at `$8F42` reads `INT1`, counts video interrupts at `$4F20`,
sets `$4F60` on a GPU interrupt, acknowledges both and returns. It touches
neither the cartridge nor the NVM BIOS.

**In game,** with interrupts on:

* opening the pause screen runs `$7894` → `$73FC` → `$73A6`: Initialize,
  Open SETTINGS, Read 2, Read 20, which is **the same sequence as boot**. It
  never closes SETTINGS. Every later Initialize starts over.
* SAVE: Create `GAME n` while SETTINGS is open, four Writes (one of them
  7,040 bytes), Close.
* LOAD and DELETE, listing: Open `GAME 1` to `GAME 5` while SETTINGS is open,
  and Close each one that opens.
* LOAD, the file chosen: Open, Read 2, Seek 0. Then the CD, GPU and DSP are
  shut down, memory is cleared (`$6546`), the CD BIOS is set up again, and the
  file, still open, is read to the end. It is never closed.

## 18.5 What this says about the GameDrive freeze

The issue's hypothesis is that the same code works at boot with interrupts
off and freezes in game with them on. The map does not bear that out:

* the code that lists slots for LOAD and DELETE (`$70B2`) never runs at boot.
  Boot runs `$74BC`;
* boot's sequence does run again in game with interrupts on, every time the
  pause screen opens;
* SAVE runs in the same menu, with the same interrupts, and its 7,040-byte
  Write is the longest transaction the game makes. On the GameDrive, SAVE
  works.

So interrupts being on is common to what works and to what freezes. What
only LOAD and DELETE do is **Open a `GAME n` file while SETTINGS is still
open, and Close it again**: an Open that may fail with `EFILNF` and an
Open/Close pair, each with a second handle outstanding. SAVE's Create also
opens a second handle, but through Create rather than Open.

This is a static reading. The game is not run on a GameDrive here. Still
open, and for whoever has the hardware:

* Does it freeze when LOAD is chosen (the listing) or when a slot is chosen
  (`$762A`)? These are different sequences.
* Does the listing's sequence freeze with interrupts masked? Initialize,
  Open SETTINGS, Read 2, Read 20, then Open `GAME 1`, then Close. If it does,
  the cause is the sequence and not the interrupts. The same with SETTINGS
  closed first separates the second handle from the rest.

The port saves its own way and needs none of this. It is written down because
the original's save files are a format, and the GameDrive question is a
question about this code.
