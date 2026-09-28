"""jaguarkit - game-agnostic building blocks for Atari Jaguar reverse engineering and ports.

Each module handles one thing the Jaguar, its CD unit or Atari's libraries
impose on every game, independent of any particular title:

    disc     Jaguar CD images (.jcd, Redump .cue/.bin): sessions, tracks, the
             Atari data header, the boot image, extraction, audio as WAV
    cart     cartridge images (.j64/.rom): the header, the entry point
    hw       the address map: TOM, JERRY, the blitter, the OP, the CD BIOS
             jump table
    m68k     recursive-descent 68000 disassembler with the hardware named
             (needs capstone)
    risc     the Jaguar RISC (GPU and DSP) disassembler
    film     Atari's Cinepak container on Jaguar CD: FILM/FDSC/ADSC/CTAB/STAB,
             frames and interleaved audio
    cvid     a Cinepak (cvid) decoder, and the Jaguar's RGB16
    pixels   the Jaguar's RGB16 (R5 B5 G6) to and from RGB24, and PPM
    bigpemu/ BigPEmu as an oracle: its config, a RAM-dump probe module, and
             finding a framebuffer in a dump (numpy)
    runtime/ C99 for the engine side: the FILM container and the cvid decoder

Everything except m68k (capstone) and bigpemu (numpy) is pure Python 3.8+
with no dependencies.  Game-specific knowledge belongs in the game's own
tools/, not here.
"""
