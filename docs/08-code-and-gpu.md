# 08 — The retail binary: boot chain, memory map, GPU modules

Everything here is read out of track 2 of the retail disc with
[tools/m68k/dis68k.py](../tools/m68k/dis68k.py) and
[tools/gpu/disgpu.py](../tools/gpu/disgpu.py). It is the first look inside the
shipped code rather than the July source, and it is what unlocked the scene
format.

---

## 8.1 Track 2 is two programs, not one

The track's 4-character content tag is not text: it is `00 00 40 00`, the
**load address**. The CD BIOS drops the track at `$4000` and jumps there.

```
file offset   memory     contents
0x00000       $4000      boot loader, about 300 bytes of code then its data
0x125C0       -          64 bytes: the tag "CODE" repeated 16 times
0x12600       $5000      the game, 196,608 bytes
0x445C0       -          "ATARI APPROVED DATA TAILER ATRI"
```

The loader is short and its job is exactly what you would expect:

```
$4012  lea     $12978,a0          ; GPU module table
$4018  move.l  #$F03000,d0
$401E  add.l   8(a0),d0           ; + the module's offset in GPU RAM
$4024  jsr     CD_initm           ; hand the BIOS somewhere to put its own code
...
$4068  move.w  #$1F00,INT1
$40AA  move.l  #'CODE',d1
$40B2  movea.l #$5000,a0
$40B8  movea.l #$35000,a1
$40BE  jsr     CD_read            ; pull the game in behind the loader
$40C8  jsr     CD_ptr             ; spin until the pointer reaches $35000
$4116  move.l  #1,$F030EC         ; poke a flag into GPU RAM
$4124  movea.l #$5000,a0
$412A  jmp     (a0)
```

`$5000` then starts with textbook init — clear the registers, `movea.l
#$1FFFFC,a7` (top of the 2 MB DRAM), set `OLP`, `VMODE = $6C7` — which is how
the load address was confirmed independently of the tag.

The CD BIOS entry points are the standard Jaguar CD jump table in DRAM at
`$3000` (`CD_read` = `$303C`, `CD_ptr` = `$304E`, and so on), which is precisely
why a CD boot binary is linked at `$4000`: it is the first free long above the
table. [tools/m68k/jagsyms.py](../tools/m68k/jagsyms.py) carries the whole list.

### Memory map as the retail build uses it

```
$0000-$2FFF   vectors, BIOS workspace
$3000-$3071   CD BIOS jump table
$4000-$4FFF   game variables  (accessed as absolute-short: $4238, $42F0, $4554 ...)
$5000-$34FFF  game code and data
$30610        the scene XOR key, 8 KB
$41D00        ...                          object / draw buffers
$C8000        Cinepak screen buffers
$116000       Cinepak film header window
$118000       CD read buffer
$1FF800       object list
$1FFFFC       stack top
```

## 8.2 How the game addresses the disc

Two small routines do all of it:

```
sub_007E9E   data type -> track start MSF
             walks the BIOS table of contents at $2C00 (8 bytes per track) to
             the first entry flagged as data, then indexes by the type
sub_007EF4   MSF -> block:  ((min * 60) + sec) * 75 + frame
sub_007F16   block -> MSF
```

So the "data type" really is an index relative to the first data track, exactly
as `GetTrack` in `CDCONTRO.GAS` describes: type *n* is CD track *n* + 2. Verified
against the one call site that names a type literally — the Cinepak player asks
for type 5, which is track 7.

Reads go through `CD_read` with `d0` = absolute block, `a0`/`a1` = buffer bounds
(`$118000`..`$1F8000`), and a poll loop on `CD_ptr`.

## 8.3 The GPU: one resident core, twenty overlays

`$F03000` holds a 4 KB kernel loaded by the 68000 through the blitter. Its entry
point is at `$F03050`:

```
f03050  movei  #$00F031D0, r31     ; stack pointer, growing down through the core
f03056  movei  #$00F03EF4, r0
f0305c  jump   t, (r0)
```

The dispatcher at `$F03EF4` is a mailbox loop:

```
f03ef4  movei  #$00F03FFC, r2      ; the mailbox
f03f00  load   (r2), r1            ; a module header address, or 0
        jr     ne, run
        load   ($F03FE4), r3       ; else: a queued request
        ...
run:    wait for the blitter, then blit  (r1)=dest, 4(r1)=size, payload at 8(r1)
        jump   t, (r2)             ; r2 = the destination, i.e. run what arrived
```

So **the 68000 posts a module to the GPU by writing the address of a module
header into `$F03FFC`**; the GPU pulls the code into itself with the blitter and
jumps to it. The idiom on the 68000 side is:

```
lea     $F03FFC,a0
move.l  #<module>,(a0)
bsr     sub_007C68          ; spin on G_CTRL until the GPU goes idle
```

A module header is the same two longs the 68000's own loader uses
(`sub_007BA8`): destination address, then size, then the payload.

```
+0  .l  destination   $F03000 for the core, $F031D0 for every overlay
+4  .l  size in bytes
+8  ..  payload
```

Words just below the mailbox are the parameter block: `$F03FE8`, `$F03FEC`,
`$F03FF0`, `$F03FF4`, `$F03FF8` all receive pointers or values from the 68000
before a module is posted.

### The module table

Twenty-one modules live in the resident binary. The core owns `$F03000`-`$F031D0`
plus the top of GPU RAM; every overlay loads at `$F031D0`.

| header | dest | size | posted from | what it is |
|---|---|---:|---|---|
| `$27568` | `$F031D0` | 104 | `$7034` | |
| `$275D8` | `$F031D0` | 46 | `$551C`, `$676C`, `$766E`, `$30B2C`, `$31D7C` | |
| `$27610` | `$F03000` | 4096 | (blitted by the 68000 at startup) | **the core** |
| `$28618` | `$F031D0` | 3356 | | |
| `$287F8` | `$F031D0` | 2960 | | |
| `$29390` | `$F031D0` | 400 | `$569C`, `$30CAC` | |
| `$29528` | `$F031D0` | 864 | | |
| `$29890` | `$F031D0` | 3288 | `$7FE0` | **the scene decoder** |
| `$2A570` | `$F031D0` | 3352 | `$5484`, `$580A`, `$8D0C` | |
| `$2B290` | `$F031D0` | 496 | `$53C4`, `$53FC`, `$5582`, `$55B2`, `$8CC4` | |
| `$2B488` | `$F031D0` | 328 | `$556A` | |
| `$2B5D8` | `$F031D0` | 3292 | `$560C` | |
| `$2C8F0` | `$F031D0` | 800 | `$5414`, `$55CA` | |
| `$2CC18` | `$F031D0` | 2896 | `$67D4` | |
| `$2D770` | `$F031D0` | 1480 | `$5626` | |
| `$2DD40` | `$F031D0` | 2880 | `$572E` | |
| `$2E888` | `$F031D0` | 1944 | | |
| `$2F028` | `$F031D0` | 736 | `$570E`, `$6C7C`, `$799E`, `$8352` | |
| `$2F310` | `$F031D0` | 2976 | `$545A`, `$8CF2` | **the script VM** |
| `$30130` | `$F031D0` | 536 | | |
| `$30350` | `$F031D0` | 48 | `$8428` | |

A DSP module is loaded separately from a three-long parameter block at `$2C2C0`,
destination `$F1B000`.

**How the scene decoder was found:** it is the only module whose `movei`
immediates include `$3E800` (256,000) and `$1F400` (128,000). Posting it from
`$7FE0` — inside the CD service routine — is the confirmation.

**How the script VM was found:** `SCRIPT.GAS` ends with `condtable`, five longs
reading `0, $11, $22, $44, $05`, and that sequence occurs exactly once in the
binary, at `$2FD8C`. Its 70-long dispatch table follows immediately, and the
module holding both is `$2F310`. See [11-script-vm.md](11-script-vm.md).

## 8.4 The tools

```
python tools/m68k/dis68k.py FILE --base 0x5000 --off 0x12600 --len 0x30000 \
                                --entry 0x5000 --out code.asm
python tools/gpu/disgpu.py FILE --off 0x36e98 --header --out module.gas
```

`dis68k` is a recursive-descent 68000 disassembler on capstone: it follows
branches and calls, labels every referenced address, prints the gaps as data,
and substitutes names for the Jaguar hardware registers and the CD BIOS entries.
`--linear` adds a plain sweep of whatever the recursion did not reach.

`disgpu` covers the Jaguar RISC used by both the GPU and the DSP. The encoding
is fixed 16-bit — `opcode[15:10] | reg1[9:5] | reg2[4:0]` — with only `MOVEI`
carrying a 32-bit immediate behind it, low half first, so a linear sweep from a
word-aligned start is exact. `--header` reads the destination and size longs of
a module header and disassembles the payload at its real load address.

Two encoding traps worth recording, both found by cross-checking a GPU routine
against the 68000 routine that does the same job:

* `SHLQ` stores **32 - n**; `SHRQ`, `SHARQ` and `RORQ` store **n**. The pair
  `shrq #3 / shlq #3` (encoded `3` and `29`) rounding a pointer down to a phrase
  is the giveaway.
* For `JR` the condition sits in the **destination** field and the signed word
  offset in the source field, with the target at `pc + 2 + off * 2`.
