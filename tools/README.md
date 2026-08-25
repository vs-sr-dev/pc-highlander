Extractors and utilities. Every tool is standalone Python 3; `scenex.py` needs
numpy and Pillow, the disassemblers need capstone.

```
jcd/jcdinfo.py       read a .jcd disc image: list, hex-dump, extract tracks
scene/scenex.py      backdrops and Z-buffers off the PICT track, as PNG
text/textx.py        the localised item text, English / French / German
cinepak/filmls.py    inventory of the films on the FMV track
m68k/dis68k.py       recursive-descent 68000 disassembler (capstone)
m68k/jagsyms.py      Jaguar hardware registers and the Jaguar CD BIOS jump table
gpu/disgpu.py        Jaguar RISC (GPU / DSP) disassembler
```

Run any of them with `--help`. The formats they implement are documented in
`docs/`; each tool's module docstring records the specific findings it relies on.
