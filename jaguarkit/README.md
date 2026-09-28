# jaguarkit

A game-agnostic toolkit for Atari Jaguar reverse engineering and native PC
ports: Jaguar CD disc images and cartridges, the 68000 and the Jaguar RISC
(GPU and DSP), the hardware's address map, Atari's Cinepak film container
with a decoder in Python and in C, and BigPEmu as an oracle.

The same idea as [wiikit](https://github.com/vs-sr-dev/wiikit),
[saturnkit](https://github.com/vs-sr-dev/saturnkit) and
[ps2kit](https://github.com/vs-sr-dev/pc-extermination/tree/main/ps2kit),
for the Jaguar. Each Jaguar game has its own engine and formats, but a large
part of every port is the *same* work: the same two-session disc with no
filesystem, the same Atari data headers, the same 68000 talking to the same
TOM and JERRY, and on CD very often the same Cinepak player. jaguarkit
collects that shared part. It grows inside the ports: each piece is written
because a game needed it, then kept free of that game's knowledge. Game
formats and game fixes live in the ports.

It is not a recompiler. The one port it serves so far is a reimplementation
(the 1995 engine read and rewritten in C, the data read off the disc), and
the Jaguar's code is too entangled with its GPU and blitter for a 68000
recompiler to go far on its own. What a port of either kind needs first is
here: the disc, the code mapped, the films, and a way to check against the
real machine.

## Ports built on it

| Port | Game | What it asked of jaguarkit |
|---|---|---|
| [pc-highlander](https://github.com/vs-sr-dev/pc-highlander) | Highlander: The Last of the MacLeods (1995, Jaguar CD) | everything so far: the `.jcd` container, the Atari data header, the boot image, the 68000 and RISC disassemblers, the register map, the Cinepak container and decoder (Python and C), the RGB16 rounding measured on the emulator, the BigPEmu probe and the framebuffer finder |

Written from Highlander's tools and checked on four more discs (Baldies,
Battle Morph, Iron Soldier 2, Myst) and 76 cartridges, which is what turned
several of Highlander's constants into parameters: see *Checks* below.

## Using it

For now jaguarkit lives inside pc-highlander, at `jaguarkit/`, the way ps2kit
lives inside pc-extermination: one Jaguar game does not yet make a platform.
When a second Jaguar port begins it is split out with its history
(`git subtree split --prefix=jaguarkit`), as wiikit was out of pc-victorious,
and every port then takes it as a git submodule at `jaguarkit/`.

Either way it sits at the port's root, so that `python -m jaguarkit.…` works
from there and an engine can compile `jaguarkit/runtime/*.c`.

```sh
python -m jaguarkit.disc GAME.cue                          # tracks, headers, tags, boot
python -m jaguarkit.disc GAME.cue --extract build/tracks   # every track from block 0, audio as WAV
python -m jaguarkit.disc GAME.cue --extract build/tracks --payload   # header to tailer only
python -m jaguarkit.disc GAME.cue --boot build/boot.bin    # what the CD BIOS loads
python -m jaguarkit.disc GAME.jcd --same-as GAME.cue       # two images, byte for byte
python -m jaguarkit.cart GAME.j64                          # the cartridge header
python -m jaguarkit.hw F02238 3018 --map                   # names and regions
python -m jaguarkit.m68k GAME.cue --boot --out boot.asm    # the boot image, disassembled
python -m jaguarkit.m68k GAME.j64 --cart --out cart.asm    # a cartridge from its entry
python -m jaguarkit.m68k FILE --base 0x4000 --entry 0x5000 --linear
python -m jaguarkit.risc FILE --off 0x36e90 --header       # a GPU module
python -m jaguarkit.risc FILE --off 0x1000 --len 0x800 --base 0xF1B000 --dsp
python -m jaguarkit.film GAME.cue --track 7                # the films on a track
python -m jaguarkit.film GAME.cue --track 7 --check        # every frame decoded
python -m jaguarkit.film GAME.cue --track 7 --film 3 --frame 30 --ppm f.ppm
python -m jaguarkit.film GAME.cue --track 7 --wav build/audio --pcm s8
python -m jaguarkit.bigpemu.config --disc GAME.cue --script jk_ramdump
python -m jaguarkit.bigpemu.findfb Scripts/jkdump/ram7.bin --ppm shot.ppm
cc -O2 -std=c99 -o jkfilm jaguarkit/runtime/jkfilm.c jaguarkit/runtime/jk_film.c jaguarkit/runtime/jk_cvid.c
jkfilm build/tracks/track07_1111.bin                       # the C runtime's own check
```

A disc image is either a `.jcd` or a Redump `.cue`/`.bin` set; everything
takes both. Prefer the `.cue`: it is what BigPEmu runs, and it keeps the
bytes a `.jcd` drops past a track's tailer (which nothing reads, but a
byte-exact archive should have).

## What a Jaguar CD is

Worth writing down, because nothing else does it in one place and every port
starts here. Checked on five discs:

* **Two sessions.** Session 1 is audio a CD player can play: one track on most
  discs, the whole soundtrack on some (Iron Soldier 2 keeps eleven there).
  Session 2 is the game, every track of it - data included - written as
  2352-byte **audio** sectors: no Mode 1, no ECC, no filesystem. A game
  addresses its data as (track, block).
* **Every data track** is `ATRI` x16, `ATARI APPROVED DATA HEADER ATRI` and a
  marker byte (`' '` on the first data track, counting up: `!`, `"`...), the
  track's own bytes, `ATARI APPROVED DATA TAILER ATRI` with the same marker,
  `ATRI` x16, and zero. 42 of 42 data tracks close that way. Block 0 of a
  track is its lead-in, so a game's block *b* is byte *b* x 2352 of the track.
* **The boot track** is the first data track of session 2: after the header,
  a load address, a length, and that many bytes of 68000 code the CD BIOS
  copies and runs - at $4000 (Highlander, Baldies), $4400 (Battle Morph),
  $5000 (Myst), $6000 (Iron Soldier 2). The rest of the track is the game's.
* **The last data track** of session 2 is 67 blocks of high-entropy data on
  all five discs; nothing in a game reads it.
* **Many data tracks open with a sync pad** of their own - a four-byte tag
  sixteen times (`DATA`, `PICT`, `1111`, `CPK0`, `TR03`, `AAAA`) - which is how
  a game's CD code finds its place after a seek. `disc` reports it; its
  meaning is the game's.
* **Byte order.** The stream is big-endian. A `.jcd` stores every 32-bit long
  reversed, a Redump `.bin` every 16-bit word swapped, and a `.jcd`'s audio
  is one 16-bit sample out of phase with Redump's.

## Layers

| Layer | Question it answers | Now | Next |
|---|---|---|---|
| 1. Recognise | What is on this disc or cartridge? | `disc` (sessions, tracks, the Atari header and tailer, sync tags, the boot image), `cart` (config, entry) | a `fingerprint`: Atari's CD and Cinepak libraries, sound drivers, JagMod and the common toolchains, by signature |
| 2. Extract | Turn standard formats into standard files | `disc --extract` (tracks from block 0, or header to tailer; audio as WAV), `film` (Cinepak films, their audio as WAV), `cvid`, `pixels` (RGB16) | CRY pixels, the object processor's bitmap objects, JagMod/Protracker music, the common sound formats |
| 3. Map code | What does the code do, where? | `m68k` (recursive descent with the hardware and CD BIOS named; `--cart`, `--boot`), `risc` (GPU and DSP), `hw` (the register map and the CD BIOS jump table) | finding RISC modules in a binary, and the 68000 code that copies them; Ghidra scripts |
| 4. Translate | Turn Jaguar code into C | - | nothing planned: the first port reimplements |
| 5. Runtime | What an engine links | `runtime/` (C99): `jk_film` (the container, films found by structure, audio resolved), `jk_cvid` (Cinepak to RGB24 and to the Jaguar's rounded RGB16); `bigpemu/` (the config, `jk_ramdump.c`, `findfb`, `strip`, `fakeram`) | a PCM mixer for the film audio; Red Book audio from the image |

## Principles

* Pure Python 3.8+, no dependencies, except `m68k` (capstone) and `bigpemu/`
  (numpy). The runtime is C99 with no dependencies.
* Every claim is checked on a real disc before it goes in.
* Game knowledge stays out. Where games differ - audio signed or unsigned,
  where an audio block's offset points - it is a parameter or it is worked
  out from the data, never a constant.
* Every change is checked on every port before it goes in.

## Checks behind each module

| Module | Checked by |
|---|---|
| `disc` | Highlander, Baldies and Battle Morph as `.jcd` against `.cue`: all 30 tracks equal byte for byte - data from lead-in to tailer, audio whole at the one-sample shift `wav()` corrects. Myst and Iron Soldier 2 read as `.cue` (10 and 20 tracks). The header, the tailer and the `ATRI` runs on 42 of 42 data tracks; the boot image's load address and length on all five; Highlander's boot image equal to the port's own extraction (which starts 56 bytes into it) |
| `cart` | 76 cartridge images: `$04040404` at $400 on all, the entry at $802000 on 75 and at $82F240 on Battle Sphere Gold |
| `hw` | the 68000 code reached from the entries of the 5 CD boot images and 76 cartridges: 1,818 of 1,908 uses of a hardware register name one (95%); what is left is below |
| `m68k` | the same 81 programs traced from their entries (the census above); Myst's boot image opens with `G_END`, `D_END`, `VI` and a read of `JOYBUTS`, as a Jaguar program should. The tracer is the one Highlander's port mapped its resident binary with |
| `risc` | the disassembler Highlander's port read its GPU modules with (COMBAT.GAS, the script VM, the Cinepak player); `--header` at $36E90 of its resident binary gives a module of 3,288 bytes loading at $F031D0, the GPU RAM address its MOVEIs then use |
| `film` | 233 films on four games - Highlander 36, Battle Morph 14, Baldies 6, Myst 177 - with every chunk's sync pad, sample table and 15,117 audio blocks accounted for, and no chunk with a byte no sample covers. Highlander's 36 block numbers, Python and C, are the 36 its scripts name (docs/11-script-vm.md 11.7) |
| `cvid` | every frame of those 233 films, 79,672 of them, with no decoder error - Highlander 13,922, Battle Morph 6,502, Baldies 2,436, Myst 56,812 (sizes from 84x106 to 320x288, some not multiples of four, some frames padded) |
| `runtime/` | `jkfilm` over the same tracks: the same film, frame and whole-picture counts as the Python, no errors. One Highlander frame and one Battle Morph frame byte for byte the same PPM in C and in Python, and the Highlander one the same as the port's own engine produces |
| `pixels` | R5 B5 G6 and rounding: Highlander's frames against the Jaguar's own RAM under BigPEmu - truncation half a step low on every channel, rounding at a mean error of zero, on six frames of six |
| `bigpemu` | `findfb` finds a frame `fakeram` planted, at the exact address, plain (320x288) and phrase-interleaved (320x240); on three of Highlander's real dumps it picks the same base and stride as the port's own tool ($0C8008, $0ADC00, $0BF410, all stride 1280). `jk_ramdump.c` is Highlander's `hl_cine.c` with the names changed and not yet re-run under BigPEmu |

## Known gaps

* **`hw` does not name everything the code touches.** 25 addresses reached by
  the 81 programs have no name: GPU RAM seen through other windows ($F04000,
  $F0B000-$F0B6F0, $F05000, $F06000), DSP RAM past its 8 KB ($F1D000,
  $F1D600, $F1E000), $F0229C past the blitter's last documented register,
  and a handful used by one or two programs ($F00140, $F000F0, $F00024,
  $F10032, $F10034, $F16000, $F01800). None is guessed at here.
* **The CD unit's own registers** (Butch, at $DFFF00) are not named: no
  program here touches them outside the CD BIOS, so nothing has checked them.
* **CRY** is not decoded. Highlander's pictures are all RGB16.
* **ADSC** is carried, not read. Its second long behaves like a clock divider
  and its first like a stereo flag (see `film.py`), on three discs.
* **Film audio's format is not in the container.** Signed on three games,
  unsigned on one; `--wav` asks rather than guesses. The rate, 22,252 Hz for
  every film measured so far, is measured rather than read.
* **`m68k`** follows what it can prove: `jmp (a0)` and jump tables stop it,
  and a cartridge's first $2000 bytes are data. `--entry` and `--linear` are
  the way past both.
* **`risc --header`** reads a destination and a size in front of a module,
  which is Highlander's convention. Another game will copy its modules
  another way.
* **The `.jcd`** stops at a track's tailer, so the few bytes some tracks carry
  past it (on 4 of 42, 674 to 4,510 of them, high entropy) exist only in the
  `.cue`. Nothing reads them; `--same-as` reports them rather than failing.

## History

jaguarkit was drawn out of pc-highlander's own tools after its session 14:
`disc` from `tools/jcd/jcdinfo.py`, `m68k` and `hw` from `tools/m68k/`,
`risc` from `tools/gpu/disgpu.py`, `film` and `cvid` from `tools/cinepak/`,
`runtime/` from the engine's `src/media/`, and `bigpemu/` from `tools/emu/`.
Each was then run against the other discs, and what they disagreed with
became the kit's parameters: the cue reader, the tailer, films found without
a tag, signed and unsigned audio, audio offsets that are starts or ends,
pictures that are not multiples of four, and a film's block being its pad's.

Highlander's own tools and engine still use their own copies; moving them
onto the kit is the next step on the port's side.

## Licence

MIT - see [LICENSE](LICENSE). jaguarkit contains no game data and no Atari
code; it reads and replaces, it does not include. The BigPEmu probe is
compiled by BigPEmu and uses its script API; BigPEmu itself is not included.
