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

## The port onto the kit

**The engine's films are the kit's** (session 16). `src/media/film.c` and
`cinepak.c` are gone, and the engine builds `jaguarkit/runtime/jk_film.c`
and `jk_cvid.c` instead. The calls change name and nothing else. All eight
checks print what they printed before, byte for byte.

That needed **track 7 as the kit extracts it**. A film's block is where its
sync pad starts, counted from the track's block 0, which is where
`jaguarkit.disc --extract` begins a file. `jcdinfo.py` begins it 160 bytes
later: 96 bytes of header, then 64 it takes for a sync pad. Read from
`jcdinfo.py`'s file, the kit's rule puts films 10 and 35 one block early:
38,405 and 100,862, where the scripts say 38,406 and 100,863. Read from the
kit's own file, all 36 agree. 

**Then every track moved.** Taking the other tracks from the kit as they
were broke six checks, since `mesh`, `doors`, `script`, `follow`, `combat` and
`inventory` read tracks 2, 3 and 5 at offsets measured in `jcdinfo.py`'s
files. But the difference is the same on every data track: the kit's file is
`jcdinfo.py`'s with **160 bytes in front**. Those bytes are 64 of `ATRI`
lead-in, the 32-byte `ATARI APPROVED DATA HEADER ATRI!`, and a 64-byte sync
pad. On track 2, which has no pad, they are the boot image's first 64 bytes,
which is where 56 bytes into the boot image came from. Past them the two
files are equal, byte for byte, to the end of the kit's.

So the engine does not move its offsets; it moves its origin.
`io_read_track` and `io_read_track_at` (`src/util/io.h`) read a kit track
from `TRACK_ORIGIN` = 160, and every offset the engine and
`assets/manifest.json` carry stays what it was. The films are the
exception: the kit's runtime reads track 7 whole, by block. `hlview` reads
every track from `--tracks DIR`, default `assets/kit_tracks`, from
`python -m jaguarkit.disc IMAGE --extract assets/kit_tracks`. All eight checks
print what they printed on `jcdinfo.py`'s files. So do three backdrops on
track 4 (the first, the last and `CA_CAM03`), which no check reads.

**And then the Python side.** The extractors for Highlander's own formats stay
in `tools/`, and read a kit track through `tools/track.py`, from the same
`ORIGIN`. It refuses a file without the kit's lead-in and header, rather
than read it 160 bytes off. Each one, run old on `jcdinfo.py`'s files and new
on the kit's, writes the same thing. That covers the sets, the world, both
animation tracks, both model sources, the text, both wave tracks, the 27
scripts and MAINSCRIPT, twenty backdrops from either end of track 4, and the
back-projection: 150 files and every line of output. `manifest.py` builds the
same manifest, except for its `disc` section, which records the kit's file
names and sizes. Its films now come from `jaguarkit.film` on the whole of
track 7, and all 36 agree. `scriptx.py` takes its film numbers from the
manifest, since `films.tsv` came from a tool that is gone.

What the kit replaced is gone: `tools/jcd` (`jaguarkit.disc`), `tools/m68k`
(`jaguarkit.m68k`, `jaguarkit.hw`), `tools/gpu` (`jaguarkit.risc`),
`tools/cinepak` (`jaguarkit.film`, `jaguarkit.cvid`), and the BigPEmu half of
`tools/emu` (`jaguarkit/bigpemu`). `tools/emu/cmpfb.py` stays, because which
frame of which film is on screen is Highlander's question. It decodes with
the kit now, and on session 14's `ram12.bin` it prints what it printed
before, line for line. The GPU module offset is corrected where the commands
are given: its header is at `$36E90`.
