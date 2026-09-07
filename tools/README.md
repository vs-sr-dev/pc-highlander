Extractors and utilities. Every tool is standalone Python 3; `scenex.py` needs
numpy and Pillow, the disassemblers need capstone.

```
manifest.py          one JSON tying all of the above together
jcd/jcdinfo.py       read a .jcd disc image: list, hex-dump, extract tracks
scene/scenex.py      backdrops and Z-buffers off the PICT track, as PNG
set/setx.py          the set track: scenes, doorways, collision, events
world/worldx.py      the world-state table and the character sheets
script/scriptx.py    the script VM: disassemble the set scripts and MAINSCRIPT
model/modelx.py      polygon models: list, OBJ export, quick PNG preview
anim/animx.py        character animations: list, JSON export
text/textx.py        the localised item text, English / French / German
cinepak/filmls.py    inventory of the films on the FMV track
cinepak/filmwav.py   the films' interleaved audio, as .wav
cinepak/filmdec.py   the films' video: a cvid decoder, and a frame as .ppm
wave/wavex.py        the WAVE sound-effect bundles on tracks 5 and 6, as .wav
m68k/dis68k.py       recursive-descent 68000 disassembler (capstone)
m68k/jagsyms.py      Jaguar hardware registers and the Jaguar CD BIOS jump table
gpu/disgpu.py        Jaguar RISC (GPU / DSP) disassembler
emu/hl_cine.c        a BigPEmu script module: dumps Jaguar RAM while a film runs
emu/findfb.py        finds the framebuffer inside such a dump, untold
emu/cmpfb.py         which frame it is, and how it differs from our decoder's
emu/strip.py         a tall strip of memory as an image, to see where a picture starts
emu/fakeram.py       plants a known picture in noise, to check findfb.py
emu/setup_cfg.py     points BigPEmu at the disc and switches the probe on
```

`emu/` is the frame comparator, and it is C plus Python rather than Python
alone: the Jaguar end is compiled by the emulator itself. See
[emu/README.md](emu/README.md).

Run any of them with `--help`. The formats they implement are documented in
`docs/`; each tool's module docstring records the specific findings it relies on.
