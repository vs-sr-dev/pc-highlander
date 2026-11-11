# Session 15 — Where the port stands, and the Jaguar half moved out

No engine code changed this session. It did two things: a sweep of what the
port still owes before it is a game someone can play from the title screen
to the ending, and jaguarkit, the Jaguar half of this port put where the next
Jaguar port can use it.

→ [17-jaguarkit.md](../17-jaguarkit.md), [jaguarkit/README.md](../../jaguarkit/README.md)

---

## 1. The sweep

The build is clean and all eight checks pass as session 14 left them:
13,922 film frames with no error, 586,632 script commands with none past the
dispatch table, eleven duels out of eleven ending with somebody dead.

What is done is phases 0 to 4, phase 5's criterion (a projectile apart), and
the first half of phase 6: the script machine, the loop, and the films. What
stands between that and a playable game is, in the order it blocks:

* **The way in.** There is no path from power-on to the first playable frame.
  The game starts from `--scene ... --drive`; `reset` prints a line; the boot
  film, the language screen, the main menu and the intro are all on the disc
  and all read, and none of them is chained.
* **Sound.** There is no audio device at all. The films' speech is decoded
  and verified, the `WAVE` bundles are extracted, and `want_sample` is thrown
  away every frame.
* **What the player sees besides the world.** No life bar, no text (the
  inventory shows no item names), no pause, no saves, keyboard only.
* **The game has never been played through.** `--check-script` never executes
  36 of the 83 opcodes — `chase`, `attack`, `animate`, `pickup`, `charchange`
  and `waitforit` among them — because nothing in a 1,200-frame run with
  nobody at the pad reaches them. Only a playthrough does.
* **The known gaps**, unchanged: the projectile, `misc[0]` and the logic
  tables, the 15th AI command, the knockback codes unverified on the machine.

At the pace of the last fourteen sessions, six to nine more, the playthrough
being the part nobody can size in advance.

## 2. jaguarkit

What in this port is Jaguar rather than Highlander — the disc, the 68000 and
RISC disassemblers, the register map, the Cinepak films in Python and C,
BigPEmu as an oracle — is now `jaguarkit/`, and was checked on the four other
Jaguar CD discs at hand and on 76 cartridges. Seven of this port's
assumptions turned out to be Highlander's case of something more general; the
table is in [17-jaguarkit.md](../17-jaguarkit.md). The one that touches this
port's own data: **a film's block is where its sync pad starts**, which is
why film 7 is 16,782 in 11.7's table and its header is in 16,783.

Three small things about this port's material were found on the way and are
written down there rather than fixed: `track02_00004000.bin` starts 56 bytes
into the boot image, `jcdinfo.py` reads past every track's end, and the GPU
module example is at `0x36e90`, not `0x36e98`.

It lives here until a second Jaguar port splits it out.

## Still open

Everything in session 14's list, plus: the tools and the engine have not yet
moved onto the kit.

## TODO for session 16

The order is the one the sweep gives: what stops the game being played
first, what stops it being checked after.

### 1. The way in — phase 6's own criterion

"The main menu works and the intro plays." Everything it needs is read:

* the boot film is film 34, played by the 68000 rather than by a script
  (9.2);
* set 30 is the language screen and writes 0, 1 or 2 to `$4E14`; set 29 is
  the main menu, whose first item releases the player into the world and
  whose third plays film 5, the credits (11.6);
* the menu set's `CINEPAK` event plays film 9, the intro;
* `reset` has to be a real restart — the world table, the active character
  table and `MAINSCRIPT` back to their power-on state — because the death
  film, `SHANR1`'s fall and `PRI`'s ending all end in one.

`$4E14` then has to reach the text (three languages are already extracted),
and `$4E16`, which `PRI` writes before the ending, has to be found a reader.

**Done when** a check runs the whole path with a scripted pad — power on,
boot film, language, menu, intro, first playable frame — and then dies on
purpose and comes back to the menu, with no command-line set or position.

### 2. Sound — phase 7, carried for the fifth session

An SDL audio stream and a mixer, and three sources into it:

* **the films' speech**, signed 8-bit mono at 22,252 Hz, locked to the
  film's own clock, which already runs the video;
* **the `WAVE` effects**, for `want_sample` and for the four `sound*` entries
  on every character sheet;
* **track 1**, 2:19 of Red Book in session 1. Nothing on the disc plays it —
  no script uses `redbook` — so first listen to it
  (`python -m jaguarkit.disc IMAGE --extract DIR` writes it as a WAV) and
  find out whether it is title music or the CD player's warning.

### 3. The port onto the kit

Mechanical, and a good first hour of any session: `tools/jcd`, `tools/m68k`,
`tools/gpu`, `tools/cinepak` and `tools/emu` replaced by `jaguarkit`, and
`src/media/film.c` and `cinepak.c` by `jaguarkit/runtime`. `--check-film`
must print what it prints now, film for film. Fix the three things
17-jaguarkit.md lists while there.

### 4. What the player sees

The life bar, the item names on the inventory screen, a pause, and a
gamepad. `MAIN.S`'s `update` runs `BITMAP` and `TEXTS` after `ENGINE`, which
is where to start reading for the first two. Saves come after, once `reset`
has said what state there is to keep.

### 5. The probe on the game, not the film — session 14's first item

Still the way to settle the knockback codes, the combat timing and the logic
tables behind `misc[0]`: find the world table in a RAM dump by its shape (197
records of a known layout) and read the structures around it.
`jaguarkit/bigpemu/jk_ramdump.c` takes the dumps; BigPEmu is run by hand.

### 6. Then the playthrough

With 1 to 4 in, play it from the menu to the ending, record the pad, and
make the recording a check. Everything the 36 unexecuted opcodes do gets
exercised there first.

### Smaller

* **The projectile** for `aiShootPlayer`, whose 1,500-unit reach is applied
  as an ordinary blow.
* **The last 20% of the picture**: carry Y, U and V to the RGB16 reduction
  instead of clamping to eight bits first.
* **Films 15, 28 and 33**, named by looking at them; **world records 143 and
  144**; **the 15th AI command**; **the low byte of `cshBehaviour`**.
