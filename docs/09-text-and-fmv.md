# 09 — Localised text and the Cinepak films

---

## 9.1 The text block

The retail build ships **English, French and German** in one block of the
resident binary, at address `$19760`, 6,603 bytes:

* `~` separates records,
* `|` is a line break inside a record.

The block is three consecutive lists — 64 English records, then 65 French, then
64 German. The French list is one longer because one string carries a stray `~`
where a `|` was meant: `UN CLE TROUVEE DANS LES~CABANES DES CHASSEURS` splits
into two records. It does not corrupt the game, because strings are not indexed
positionally.

### Pointer triples

Each item is addressed by **three consecutive longs — English, French, German** —
and those triples sit in the data area next to the code that uses them rather
than in one table. Sixty of them are recoverable by scanning for three longs in
a row that all point into the block in language order:

```
$00B164 -> $019760 "BARE HANDS"   $019F3F "MAINS NUES"   $01A83A "MIT BLOSSEN HÄNDEN"
$00B174 -> $01976B "YOUR FISTS - A BARELY|ADEQUATE WEAPON"  ...
```

Four records have no triple — `KEY`, `BRASS MOGONDAN KEY.|FOR MOGONDAN DOORS?`,
`MAP`, `HUNTER'S ORDERS`. They are presumably reached by an index computed at
runtime; the positional dump gets them.

### The accent encoding

Accented capitals are stored as **lowercase letters**, because the game font
puts them where the lowercase glyphs would be. Three are attested in the shipped
text:

| code | glyph | evidence |
|---|---|---|
| `b` | Ä | `HbNDEN` = HÄNDEN, `JbGER` = JÄGER |
| `d` | Ö | `HdCHSTER` = HÖCHSTER, `dFFNEN` = ÖFFNEN |
| `f` | Ü | `FfR` = FÜR, `SCHLfSSEL` = SCHLÜSSEL |

No other lowercase code occurs anywhere in the block, and the **French text
carries no accents at all** — `CLE`, `EPEE`, `TROUVEE` are all written bare.
Any port should reproduce that or fix it deliberately.

Other string areas in the binary, not yet broken out: `$B167` (menu, German
`IN GEBRAUCH | OPTION BEENDET`), `$24C4B`, `$24F72`, and `$26FA0`, which holds
`HIGHLANDER ONE`, `SETTINGS`, `GAME 1` and the disc-error messages.

```
python tools/text/textx.py TRACK2 --format tsv     # the pointer triples
python tools/text/textx.py TRACK2 --all            # every record, positionally
```

---

## 9.2 The films

Track 7 holds **36 Cinepak films**, all `cvid` 320x240, filling 100,100 of the
track's 102,127 blocks — 98% of it.

They are packed back to back and are **not block aligned**. That is fine because
the player seeks to a block and then scans forward for the sync tag: the code at
`$86C6` looks for the long `'1111'`, which is the track's own content tag and
the `sync_header equ '1111'` of `CINEPAK.S`.

```
'FILM'  size  0  0
'FDSC'  20    'cvid'  height  width          always 240, 320
'CTAB'  size  <8 bytes>  then 16 per chunk:  offset, size, timestamp, tag
```

Chunk offsets are relative to the film start; the tag is a 4-byte value that
increments per chunk (`$20202020`, `$21212121`, ...). Every chunk opens with its
own 64-byte sync pad — the tag repeated 16 times, the same convention the CD
tracks themselves use — followed by:

```
'STAB'  size  rate  count   then 16 per sample: offset, size, timestamp, type
```

So **the films carry interleaved audio**, and that answers where the speech
went. The July build addressed 88 spoken lines as Red Book audio on a dedicated
track (`BO_CDAUDIO_LINE005` .. `LINE100`); the retail disc has no such track. The
sampled-audio tracks cannot be hiding it either — track 6 holds 38 `WAVE`
bundles and track 5 another 36, about 98 seconds in total at 22 kHz 8-bit, which
is a sound-effect budget, not a dialogue budget. The FMV, by contrast, is
roughly 100 MB of chunks with a sample table in every one.

The boot sequence plays **film 34**, at block 94,340 (`$17084`, the constant
written to the film-offset variable `$44BC` at `$5212`). It is one of the two
largest on the disc: 92 chunks, 15.2 MB.

```
python tools/cinepak/filmls.py TRACK7 --tsv
python tools/cinepak/filmls.py TRACK7 --chunks 34
```

### Still open: which film is which

The 20 names in July's `DATA.INC` are alphabetical and the block offsets all
moved, so the names cannot be mapped by position. The workable route is through
the set data: track 3 carries each set's event list (`EventOffset` in the set
header), `EVENT_TYPE_CINEPAK` events name a film by block offset, and July's
`CDLINK.INC` still names the sets. Matching a film to the set that triggers it
would name most of them by context.
