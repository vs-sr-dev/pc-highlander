# 15 — Combat

> `COMBAT.GAS` (Robert C. Dibley, 1995, "loosely based on work by Andrew M.
> Harris"), `PCOL.TXT`, `AICTRL.GAS`'s `AIAttackCode`, and the animation data
> itself. The port is [src/game/combat.c](../src/game/combat.c), and
> `hlview --check-combat` is what says it holds.

---

## 15.1 Combat is in the animation data

There is no table of moves anywhere in this game. Every animation frame carries
four fields that only combat reads ([03-data-formats.md](03-data-formats.md)
§3.4):

```
animHit    .b   positive is an attack, negative is a defence
animRange  .w   how far it reaches
animDirAz  .b   which way it points, offset from the character's facing
animSprAz  .b   how wide the arc is
```

So "which of these animations is a sword swing" is a question the disc answers,
and `hlview --list-attacks` asks it. **87 of the 285 animations on track 5 carry
a hit value** — 188 attack frames and 231 defence frames — and they are not
scattered:

```
bundle  0  anim  19..25  attack    anim 26, 27  defence
bundle  1  anim  19..25  attack    anim 26, 27  defence   plus 13
bundle  2  anim  19..25  attack    anim 26, 27  defence   plus 13
...
```

Every bundle that carries a full bank puts its attacks at **19 to 25** and its
guards at **26 and 27**. That is the first half of §15.2.

## 15.2 The animation numbering is the format's, not Quentin's

The joypad table this port drives every character with was read off Quentin's
thirty animations alone in session 8 — 6 is the stand, 10 the walk, 11 the run,
15 and 16 the turns — and every other character on the disc is then driven
through the same numbers. That is only sound if the numbering is a convention
of the format rather than a fact about Quentin.

It is, and combat is what proves it, because combat needs seven more roles and
the data names them. Two readings suffice: how far the root travels, and **how
high the pose still is when the animation ends** — every frame records the
highest point of the body it describes, so an animation that starts at 414 and
ends at 82 has put the character on the floor.

| # | what the data says | role |
|---:|---|---|
| 0 | 612 back, 232 down, ends 111 high | falls backwards |
| 1 | 189 left, turns 144, ends 104 high | spins and falls |
| 2 | 343 back, 74 right, ends 83 high | falls back-right |
| 3 | 332 back, 136 left, ends 82 high | falls back-left |
| 4 | 416 back, ends 424 high | knocked back, still up |
| 5 | 325 forward, ends 406 high | knocked forward |
| 8 | nothing moves, 80 high throughout | lying there |
| 19–25 | positive `animHit` | the seven attacks |
| 26–27 | negative `animHit` | the two guards |

`hlview --check-combat` asserts all fourteen of those in **every bundle with a
full bank — six of them — and finds no departures.** Nothing in that test knows
which character it is looking at.

Four falls and two staggers is one of each per knockback quadrant plus the two
`STRUCDEF.INC` names outright:

```
; the knockback bits work as follows :
; 00 - back
; 01 -
; 10 - forward
; 11 -
```

Animation 0 goes straight backwards and animation 1 turns half a circle, so 0
is the back one and 1 is the front one. The two side codes are unnamed there
and ours here.

## 15.3 Quentin's 114 animations are 30 plus three weapons

His own sheet declares **30**. The other 84 are three banks of 28, and there
are exactly three sheets on the disc with one model and 28 animations — **22,
23 and 24**, which are weapons. `30 + 28 + 28 + 28 = 114`, and the combat
animations land where that arithmetic says: attack and defence values at
19..27, then again at **49..57, 77..85 and 105..113** — the same 19..27 at
offsets 0, 30, 58 and 86.

That is `AICTRL.GAS`'s `.weapon_action` made visible:

```
	movei	#animsheet,reg16		; use player weapon character sheet
	cmp	reg0,reg9
	jr	NE,.default_action		; jump if computer
	load	(reg16),reg16
	cmpq	#0,reg16
	jr	EQ,.default_action		; jump if bare handed
.weapon_action:
	moveq	#cshAnimOff-cshNext,reg20	; using object sheet
```

The player's animation number is looked up in the sheet of whatever he is
holding before falling back to his own. **Picking up a sword does not change
the table; it changes which bank the table's numbers land in.** What the four
banks hit for says what they are:

| bank | offset | peak `animHit` | reach | arc |
|---:|---:|---:|---:|---:|
| his hands | 0 | 2 | 250 | 21 |
| 1 | 30 | 30 | 250–375 | 21 |
| 2 | 58 | 127 | 625 | 91 |
| 3 | 86 | 80 | 1000–1500 | 5–10 |

Bank 3 has a reach of four metres and an arc of five degrees, which is a thing
you shoot with rather than swing — and exactly one character sheet on the disc
reads `aiShootPlayer`. `hlview --drive --weapon N` puts one in his hands.

## 15.4 `PPCOLL`, in order

One GPU module, run once a frame between the animation player and the 3D
engine — `MAIN.S` queues `ANIMCODE`, then `PPCOLL`, then `ENGINE` — doing two
jobs in one walk over the pairs of active characters.

**Who may fight whom.** Two rules, both dated in COMBAT.GAS's own history:
**exactly one of the pair must be the player** (01/05/95, "modified to prevent
Hunters killing each other") and **neither may be carrying `FSAShield`**.
A zero `wstRadius` is uncollidable and drops the pair; a `WSTCollectable` skips
combat entirely and goes to the pickup path.

Note what the first rule does *not* say. It stops two hunters hurting each
other; it says nothing about the player and his companion, who are one of each
and therefore a duel. Walk into `DUN1` swinging with Ramirez behind you and you
will kill him in eight frames, and that is the rule as written rather than a
fault in the port.

**The blow.** Each character's current hit frame is `HITFRAME`, which the
animation player leaves behind — the first frame carrying an `animHit` that
this game frame stepped over, and zero otherwise. From it come the hit value,
the reach and the arc. A blow reaches if

```
distance <= the other's radius + animRange
|facing + animDirAz - bearing| <= animSprAz + 40 * their radius / distance
```

with 180 degrees added for the first of the pair, because the bearing is
measured one way and both of them are tested against it. That last term is the
`shooting_fix` of 18/05/95: a body subtends a wider angle the closer it is, so
**your spread is widened by the other man's size**, not your own.

**What it comes to.** A positive hit adds to the *other* one's damage; a
negative hit adds to your *own*, which is how a guard subtracts from what is
coming at you. Then:

* damage of one or more comes off `wstLife`, saturating at zero, and sets
  `FSAHit` on the victim along with two bits saying which way he was struck;
* if a blow connected but the defence swallowed it — and the defender was not
  swinging himself — that is a **parry**, and he gets `FSAShield` instead.

**And the bodies.** Any two circles that overlap add their `wstStr` to each
other's `citCollision`, and anybody left with a non-zero one is put back where
he started the frame, triangle included — the moveback the animation player
writes as it goes. `PCOL.TXT` designs a second level, where the combined
strength pushing on you decides whether you are shoved back rather than merely
stopped; `COMBAT.GAS` never implements it, and neither does this.

## 15.5 `HitControl`, and the joypad you do not press

`ControlCode` tests five things in order, and the order is the whole design:

```
FSAHit set        -> HitControl      being hit interrupts anything, your own swing included
FSALock set       -> nothing         an animation nobody may disturb
life == 0         -> DeadControl
ACTControlled     -> ComputerControl a script driving the player
not the player    -> ComputerControl
                  -> PlayerControl
```

`HitControl` does not read the pad. It *writes* one — `JOY_HIT`, plus
`JOY_KILL` if the life points have reached zero, plus the two knockback bits —
and clears everything above the stance in `citStance`, which is what takes
`FSAHit` off again one frame later. The logic table then turns that joypad into
a reaction exactly as it turns any other joypad into a walk. **Being knocked
over is a button press nobody pressed.**

`DeadControl` presses nothing, and once the animation that killed him has run
out it sets `wstRadius` to zero — which takes the body out of the collision
entirely, since a zero radius drops every pair. (It goes on from there to drop
whatever he was carrying into the world as new characters. That is inventory,
and it is not ported.)

## 15.6 `actStatus` is three states, and its clock is a hash

`AIAttackCode` closes to two metres at a run and then fights, and how it fights
is `actStatus` — 0 attack, 1 defend, 2 pause — advanced by `btst` on **reg1**.

It is easy to read reg1 as the frame counter, because that is what
`ControlCode` loads into it at the top of the loop. It is not, and this is the
one thing in the file that will silently mislead a port:

```
AIRandomCode:
	imultn	reg1,reg1
	imacn	reg17,reg18
	imacn	reg19,reg20
	resmac	reg1
	xor	reg17,reg1
	rorq	#8,reg1
	...
```

`AIRandomCode` **overwrites reg1** with that frame count squared, mixed with
both characters' coordinates and rolled a byte at a time, and every `btst` in
`AIAttackCode` reads the result. The rhythm of a fight is a hash of where the
two of them are standing: not periodic, and not random either.

The defending state is the good part. It reads **the opponent's own
`actJoypad`** — found by searching the character table for the record whose
`actWorld` matches the target — and, if he is pressing a fire button and not
already blocking, presses *the same buttons plus down*. A block is a reply to a
particular swing rather than a stance you stand in.

## 15.7 Two things the source settles on the way

**The Jaguar GPU has one delay slot**, and COMBAT.GAS proves it from its own
arithmetic rather than from a manual:

```
	cmpq	#0,ptemp3		; check for sign of attack
	jr	pl,player2
	add	ptemp3,att2		; use as attack value
	add	ptemp3,att1		; setup defence value
	jr	player2
	sub	ptemp3,att2		; remove attack value if not needed
```

Read with one delay slot, a positive hit adds to `att2` and jumps; a negative
one adds to `att2`, then to `att1`, then takes it back off `att2` — leaving the
defence on `att1` alone, which is what the comments say happens. Read with two,
the second `add` would execute on both paths and the comments would be wrong.

**And a hit value on frame zero is a standing guard.** `ANIM.GAS` records the
hit frame as "zero means none", so a hit value on frame 0 can never be reached
by that route — except that `PPCOLL` reads frame `HITFRAME` whatever it is, so
frame 0's values apply *whenever nothing else is happening*. Nine animations
on track 5 carry one, eight of them negative, and one of the eight is animation
13 in every hunter bundle: four frames, defence throughout. It is a guard you are
holding while you are doing nothing else.

## 15.8 What is checked

```
build/hlview --check-combat
```

```
animation roles: 6 bundles with a full bank, 0 departures from the convention
hunter: sheet 2, bundle 1 with 28 animations, world records 3 and 4
duels: 11 fought one on one, 11 ended with somebody dead, 617 frames each
  the hunter died 4 of the 5 times the player had a weapon and 1 of the 6 he
  had his hands
  0 life values that went up, which is what says no life byte wrapped
  two hunters within reach of each other for 1185 frames of 1,200, and 0
  points of damage between them
```

Three things, none of them "it looked like a fight". The **fourteen animation
roles**, in every bundle, which is what licenses driving all of them through
one table. **Eleven duels in a real set at eleven different ranges and angles,
every one of them ending with somebody dead**, no life value ever rising — a
byte that wrapped would show up there and nowhere else — and the weapon
deciding the outcome, which is the game's own economy: bare hands hit for 2 and
a hunter hits for 20. And **two hunters standing inside each other's reach for
1,185 frames out of 1,200 without taking a single point off each other**, which
is COMBAT.GAS's rule of 1/05/95 and the one thing here that a bug would quietly
undo.

In the game rather than in the harness:

```
build/hlview --scene DUN1_CAM04 --char 0 --drive --fight --weapon 1

hunter: world 3, sheet 2, attack player, bundle 1 with 28 animations
weapon 1: animations 30..57 of the bundle
frame   17: hunter is hit, life 40
frame   39: hunter is hit, life 10
frame   48: hunter is killed, life 0
```

and with `--weapon` left off he loses, because a man hitting for 2 against one
hitting for 20 loses. That is phase 5's success criterion.

## 15.9 What the table cannot see, and why it needs no lock

The logic tables are the five CD files nothing on the retail disc loads
([14-characters.md](14-characters.md) 14.9), so the one this port drives
everybody with is ours. Combat put a real constraint on its shape, and the
constraint is worth writing down because it says something about the missing
file.

The AI reissues its joypad every frame while it is attacking, and the button it
picks comes out of `AIRandomCode` — so on half the frames it presses `FIRE_C`
and on the other half `FIRE_B` and `FIRE_C` together. A table that told those
two apart would restart the swing every other frame, and the blow would never
reach the frame the animator drew it on. This one cannot tell them apart: the
search wants every bit of the mask held and `actAction` records the pad
**masked**, so both pads match the row whose mask is `FIRE_C` alone and both
leave the same value behind.

That is why `AIAttackCode` needs no `FSALock` around a swing, and it is
probably why `BO_LOGICS_1` was built the same way. Locking it instead — which
this port tried first — freezes a character out of `ControlCode` altogether, so
a swing that starts in range and drifts out of it can never close again, and
three characters end up standing in a field swinging at nothing.

What is still ours, and stated as such: which reaction each of the four
knockback codes picks, the two extra stances (`ST_HIT`, `ST_DEAD`), and the
choice of which attack each fire button reaches.

## 15.10 Still open

* **Inventory.** `DeadControl` drops what a character was carrying into the
  world as fresh characters, and `PPCOLL`'s `COLLECTABLE` path is the pickup
  that puts it back. Neither is ported; `instpick` and the `PICKUP` flag are
  read in COMBAT.GAS and have nowhere to go here yet.
* **`AIShootPerson` and `AIShootPlayer`** are ported as far as the attack
  machine, but nothing carries a projectile: bank 3's reach of 1,000 units is
  applied as an ordinary blow.
* **The combat sounds**, which COMBAT.GAS plays through `WaveInterface` from
  four entries on the character sheet — `soundKIA`, `soundHIT`, `soundATT`,
  `soundPAR` ([06-jcd-format.md](06-jcd-format.md)). Phase 7.
* **Level-2 knockback**, designed in `PCOL.TXT` and never implemented.
