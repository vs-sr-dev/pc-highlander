# tools/emu — the frame comparator's two ends

Four sessions listed a frame comparator first and skipped it as "blocked on an
emulator". It is not blocked. This is the plumbing.

The Jaguar end is a **BigPEmu script module**, compiled by the emulator itself
(the developer build recompiles `Scripts/*.c` on startup); the PC end is two
Python tools that take what it wrote and find the picture in it.

Nothing here is committed with a disc or an emulator in it: `BigPEmuDEV/` is
ignored, like `Highlander/` and `assets/`.

## Setting it up

BigPEmu will not run a Jaguar CD without a **CD BIOS image** — its own UI says
so, `STR_SETTING_CDBIOS` and *"Audio discs will not be loaded unless a CD BIOS
is provided"*, and every track of a Jaguar CD `.cue` is marked `TRACK AUDIO`.
The cartridge BIOS is not it. Set the CD BIOS under *System*, then:

* **the disc must be the `.cue`.** Four boots of the same game settled it:
  `.jcd` is not loaded at all, and only the cue/bin set gets the 68000 running
  in RAM.
* **the cartridge slot must be empty** — the ReadMe warns against a cart and a
  disc together, and a CD needs no cart.
* **the script module must be switched on**, under *System → Script Modules*.

`setup_cfg.py` does the last two and points the disc, by editing BigPEmu's own
JSON config in `%APPDATA%\BigPEmu`. It keeps a backup beside it:

```
python tools/emu/setup_cfg.py --disc "...\Highlander (USA).cue" --script hl_cine
python tools/emu/setup_cfg.py --restore
```

BigPEmu is not headless and will not start a disc on its own, so the run itself
is by hand: launch it, *Run with Images*, and leave it.

## `hl_cine.c` — the probe

Copy it into `BigPEmuDEV/Scripts/` (and make `Scripts/hlcine/`, which is where
it writes; the script VM's filesystem is rooted at `Scripts`). It takes the
Jaguar's **whole 2 MB** every four seconds, thirty times, and writes a
heartbeat line once a second so a run can be watched from a shell.

It dumps everything because the addresses are not to be trusted. `CINEPAK.INC`
fixes the film player's screen at `$000C0000` and its `'FILM'` header at
`$0010E000`, and on the retail disc that is wrong: with the boot film visibly
on screen, `$0010E000` holds `12441245`, which is two RGB16 pixels. The source
dump is a July 1995 build and the release moved its memory.

## `findfb.py` — where the picture is

Finds the framebuffer in a dump without being told, by the one property a
framebuffer has that nothing else in RAM does: **its rows repeat**. It scores
every 8-byte-aligned window by the mean absolute difference between each byte
and the byte one row below, which is a single cumulative sum over the whole
dump, and tries both strides — 640 for a plain buffer and 1280 for
`CINEPAK.S`'s phrase-interleaved double buffer (`screen_gap 2`).

```
python tools/emu/findfb.py BigPEmuDEV/Scripts/hlcine/ram7.bin --ppm shot.ppm
```

## `cmpfb.py` — the answer

Takes the picture out of a dump, works out *which* frame of *which* film it is,
and compares it with ours. Nothing in the dump says any of that, so each step
is a search: the horizontal roll comes from the column where the wrap seam is,
the frame from a colour histogram (which does not care about a vertical offset
that is still unknown), and the vertical offset from trying every row against
the frame the histogram picked.

```
python tools/emu/cmpfb.py BigPEmuDEV/Scripts/hlcine/ram16.bin --film 34

on the pixels: frame 721 at row 26 ($0C8200), mean |RGB16 word| 515.9

ours truncated: 19.299% of pixels identical to the Jaguar's
    R       51.027%    46.132%     2.841%      -0.462       95.660%
ours rounded: 84.208% of pixels identical to the Jaguar's
    R       90.988%     4.314%     4.698%      +0.003       95.986%
```

It prints both reductions on purpose. The finding is not "rounding looks
better" but "truncating puts every channel half a step low and rounding puts
the mean error at zero", and a tool that showed only the answer would not be
showing that. See [09-text-and-fmv.md](../09-text-and-fmv.md) 9.5.

## `strip.py` — when the geometry is still unknown

Draws far more rows than a picture has, from before a candidate base, so the
top edge is something to look at rather than something to infer. It is how the
black band above the picture turned out to be cleared buffer and how the
horizontal wrap was spotted at all.

## `fakeram.py` — and how that is checked

A detector that is told nothing has to be checked against a dump whose answer
is known. This plants one of our own decoded frames in 2 MB of noise, in either
layout, and `findfb.py` has to come back with the address it was planted at:

```
build/hlview --film 34 --shot-at 200 --shot build/emu/f34_200.ppm --no-window
python tools/emu/fakeram.py build/emu/f34_200.ppm build/emu/fake.bin \
    --base 0xC0000 --interleave
python tools/emu/findfb.py build/emu/fake.bin --ppm build/emu/found.ppm
```

`winner: $0C0000, stride 1280`, and the picture that comes back is the picture
that went in.

## `probe_matrix.sh`

The four boots that ruled out the `.jcd`: cue and jcd, with and without the
cartridge boot ROM, each judged on what the probe's heartbeat says the 68000
was doing rather than on what the window looked like.
