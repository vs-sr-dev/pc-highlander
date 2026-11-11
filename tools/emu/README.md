# tools/emu — the frame comparator

Does our Cinepak decoder put the same pixels on screen as the Jaguar's? The
Jaguar end is BigPEmu, dumping RAM while a film plays. The PC end finds the
picture in a dump, works out which frame it is, and compares it with ours.

Everything that is Jaguar rather than Highlander moved to
[jaguarkit/bigpemu](../../jaguarkit/README.md) in session 16. That covers the
config editor, the RAM-dump script module (`jk_ramdump.c`, which was
`hl_cine.c`), `findfb`, `strip` and `fakeram`. What stays here is the one
step that knows which game it is looking at.

Nothing here is committed with a disc or an emulator in it: `BigPEmuDEV/` is
ignored, like `Highlander/` and `assets/`.

## Taking the dumps

BigPEmu runs a Jaguar CD only with a CD BIOS image set under *System*, the
disc as the `.cue` (it does not load the `.jcd`), and the cartridge slot
empty. It is not headless, so the run is by hand:

```
python -m jaguarkit.bigpemu.config --disc "...\Highlander (USA).cue" --script jk_ramdump
```

then copy `jaguarkit/bigpemu/jk_ramdump.c` into `BigPEmuDEV/Scripts/`,
launch BigPEmu, *Run with Images*, and leave it while a film plays.

The whole 2 MB is dumped because the addresses are not to be trusted.
`CINEPAK.INC` fixes the player's screen at `$000C0000` and its `'FILM'`
header at `$0010E000`, and on the retail disc that is wrong. With the boot
film on screen, `$0010E000` holds two RGB16 pixels. The source dump is a July
1995 build, and the release moved its memory.

## `cmpfb.py` — the answer

```
python -m jaguarkit.bigpemu.findfb BigPEmuDEV/Scripts/hlcine/ram12.bin --ppm shot.ppm
python tools/emu/cmpfb.py BigPEmuDEV/Scripts/hlcine/ram12.bin --film 34
```

Nothing in a dump says which frame of which film is on screen, so each step
is a search. The horizontal roll comes from the column where the wrap seam
is. The frame comes from a colour histogram, which does not care about a
vertical offset that is still unknown. The vertical offset comes from trying
every row against the frame the histogram picked. It reads track 7 as the kit
extracts it, and decodes with `jaguarkit.cvid`.

It prints both reductions, truncated and rounded, on purpose. The finding is
not "rounding looks better" but "truncating puts every channel half a step
low and rounding puts the mean error at zero", and a tool that showed only
the answer would not be showing that. See
[09-text-and-fmv.md](../../docs/09-text-and-fmv.md) 9.5. On `ram12.bin`,
film 34, rounding gives red 79.7% exact and 98.3% within one step, the same
before and after the move to the kit.
