The extractors for Highlander's own formats. Every tool is standalone Python 3;
`scenex.py` needs numpy and Pillow.

What is Jaguar rather than Highlander — the disc, the 68000 and RISC
disassemblers, the register map, the Cinepak films, BigPEmu — is
[jaguarkit](../jaguarkit/README.md), and these tools read the tracks it
extracts:

```
python -m jaguarkit.disc <image.jcd> --extract assets/kit_tracks
```

```
manifest.py          one JSON tying all of the below together
track.py             a kit track from ORIGIN, where every offset in docs/ begins
scene/scenex.py      backdrops and Z-buffers off the PICT track, as PNG
scene/backproj.py    a backdrop's Z-buffer, back into world coordinates
set/setx.py          the set track: scenes, doorways, collision, events
world/worldx.py      the world-state table and the character sheets
script/scriptx.py    the script VM: disassemble the set scripts and MAINSCRIPT
model/modelx.py      polygon models: list, OBJ export, quick PNG preview
anim/animx.py        character animations: list, JSON export
text/textx.py        the localised item text, English / French / German
wave/wavex.py        the WAVE sound-effect bundles on tracks 5 and 6, as .wav
emu/cmpfb.py         a film frame the Jaguar decoded against ours
```

**Offsets.** A kit track is written whole, from its block 0. Every offset in
`docs/` and in the manifest counts from 160 bytes in, past the lead-in, the
Atari header and the sync pad, and `track.py` reads from there. The kit's own
tools read the file whole, so a docs offset is +160 there. For example, the
resident binary is `--off 0x126a0` to `jaguarkit.m68k`, where 08 gives
`$12600`.

Run any of them with `--help`. The formats they implement are documented in
`docs/`; each tool's module docstring records the specific findings it relies on.
