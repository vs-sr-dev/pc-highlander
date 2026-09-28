# 17 — jaguarkit

[`jaguarkit/`](../jaguarkit/) is the part of this port that is not about
Highlander: the Jaguar CD disc, the 68000 and the Jaguar RISC, the register
map, Atari's Cinepak container and a decoder for it, and BigPEmu as an oracle.
It is the same idea as wiikit, saturnkit and ps2kit in the other ports, and
it lives here the way ps2kit lives inside pc-extermination: until a second
Jaguar game is ported, one game does not make a platform. When one is, it
comes out with its history (`git subtree split --prefix=jaguarkit`) and
becomes a submodule.

Its own [README](../jaguarkit/README.md) is the reference: what each module
does, what a Jaguar CD looks like, and what every claim was checked on.

## Where it came from

Every module started as one of this port's tools, and was then run on the
other Jaguar CD discs at hand — Baldies, Battle Morph, Iron Soldier 2 and
Myst — and on 76 cartridges. Where the other discs disagreed, the kit took
the general form and Highlander became one case of it:

| This port assumed | The other discs say | In the kit |
|---|---|---|
| a `.jcd` image | Redump `.cue`/`.bin` too; every track equal to the `.jcd`'s, byte for byte, on three discs | `disc` reads both |
| a data track is a header and then data | it also **ends** with `ATARI APPROVED DATA TAILER ATRI` and 16 `ATRI`, on 42 of 42 tracks | `Track.tailer`; `--payload` stops there |
| films are found by the `'1111'` sync tag | the tag is per disc, even per film (`CPK0`, `CPK1`…) | films found by structure |
| film audio is signed 8-bit | unsigned on Battle Morph; signed on Baldies and Myst | `--pcm s8`/`u8` |
| an audio block's offset is its end | it is its start in Battle Morph's films with 16,384-byte blocks | resolved from the samples tiling the chunk |
| pictures are 320x240 | 320x288, and in Myst sizes like 92x134 that are coded rounded up to 4 | the coded size, cropped |
| a film's block is where its header is | it is where its sync pad starts — film 7's pad is in block 16,782 and its header in 16,783, and the script says 16,782 | `Film.block` |

The last one was caught by checking the kit's film list against the table in
[11-script-vm.md](11-script-vm.md) 11.7: 35 of 36 agreed before, 36 of 36
after.

## What it says about this port

Three things turned up in this port's own material on the way:

* **`track02_00004000.bin` starts 56 bytes into the boot image.** The boot
  track's payload is a load address ($4000), a length (60,024) and the code;
  `jcdinfo.py` strips 96 bytes after the lead-in as it does on every track,
  treating the first 64 bytes of the payload as a sync pad the boot track does
  not have. `python -m jaguarkit.disc IMAGE --boot` gives the image as the CD
  BIOS loads it.
* **`jcdinfo.py --extract` reads past the end of every track** into the next
  one's lead-in, because it reads `blocks x 2352` from the payload rather than
  from the track's start. Nothing here reads that far, so nothing is wrong.
* **The GPU module example is 8 bytes off.** The README and
  [08-code-and-gpu.md](08-code-and-gpu.md) give `--off 0x36e98 --header`; the
  module's header is at `0x36e90` (a module of 3,288 bytes loading at
  $F031D0).

The port's tools and engine still use their own copies. Moving them onto the
kit — `tools/jcd`, `tools/m68k`, `tools/gpu`, `tools/cinepak`, `tools/emu`
and `src/media` — is the next step, and `--check-film` is what says it held.
