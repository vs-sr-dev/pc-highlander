# 14 — The character: fifteen pieces, one pose, and the floor under it

Status: **the assembly is settled and checked, and the pad now drives it.**
`hlview --char N` loads one of track 5's fifteen-piece bundles, chains it through
its origin points, poses it at a frame of one of its animations, stands it on a
set's collision mesh, and walks it about. The joypad picks the animation and the
animation's own root motion does the moving, the way `AICTRL.GAS` has it (14.8);
a `SCENE` event cuts the camera, and one that names a view another set owns
carries him through the doorway into that set (14.9).

This is the first thing in the port where the model format, the animation format
and the skeleton all have to agree with each other, so it needed a test that
none of the three could fake. The animation data provides one: every frame
records the highest and lowest point of the pose it describes, in two words the
assembly never reads.

---

## 14.1 The origin points are the skeleton, and the fourth word names them

A model's header carries two bytes nobody had needed until now
([03-data-formats.md](03-data-formats.md) 3.3):

```
+2  .b  origin number       0 for a root, 128..255 for a piece
+3  .b  number of origins   points that follow the vertices and are not drawn
```

The origin points sit in the vertex list after the drawn vertices, in the same
four-word form. A drawn vertex spends its fourth word on a homogeneous 1; an
origin point spends it on **its own number**. Quentin's torso publishes four:

```
(   0,   0,  -1) 128      the pelvis
(  38,  86,   8) 135      the left shoulder
( -40,  89,   9) 138      the right shoulder
(   0, 115,  21) 141      the neck
```

and the pieces that follow declare themselves anchored at those numbers, in
turn publishing their own. All fourteen full-size characters on the disc carry
the same fifteen pieces in the same order and the same skeleton:

```
      torso  0
       |-- 128  pelvis -- 129 -- 130 -- 131      left leg, thigh, shin, foot
       |          `----- 132 -- 133 -- 134      right leg
       |-- 135  ---- 136 ---- 137                left arm, upper, fore, hand
       |-- 138  ---- 139 ---- 140 -- (144)       right arm; 144 is what the
       `-- 141  head                              hand hands on to, the weapon
```

`3DENGINE.GAS` says what to do with them, in the loop that transforms a model's
vertices. The fourth word decides which branch a vertex takes:

```
	load	(ptr2),source2	;Z|1 or Z|O if an origin
	...
	cmpq	#2,temp3
	jump	MI,(ptr1)	; less than 2 - an ordinary vertex
nvtx:	bclr	#7,temp3	;128-255 made into 0..127
	movei	#origsave,ptr1	; the saved-origin table
	shlq	#3,temp3
	add	temp3,ptr1
	storew	result1,(ptr1)	; x, y, z of the transformed origin
```

So an **origin point is transformed exactly like a vertex** — through the
piece's own matrix, plus the piece's own position — and the result is written
into a table indexed by the origin number. Earlier in the same routine, a model
whose origin number is not zero skips the usual "object position less view
position" path entirely and reads its position straight out of that table.

Two consequences, and the second is the one that matters:

* **Positions chain.** A piece sits where its parent's origin point ended up,
  which means the parent's rotation carries it.
* **Orientations do not.** Each instance's matrix is loaded from its own
  rotation words and concatenated only with the view. `ANIM.GAS` writes the
  frame's three angle bytes straight into each piece's instance and never
  accumulates. A piece's angles are absolute in the character's own frame.

That is why the models look wrong laid out flat: with every angle zero the
fifteen pieces splay into a 431-unit scarecrow. The rest pose is not a pose.

## 14.2 The three angles, and which axis each one turns

`ANIM.GAS` writes them in the order the frame stores them, and adds the
character's facing to the third:

```
rotlp:	loadb	(frameptr),temp1	; instance +24
	storew	temp1,(idp)
	loadb	(frameptr),temp1	; instance +26
	storew	temp1,(idp+2)
	loadb	(frameptr),temp1	; instance +28
	add	facing,temp1
	storew	temp1,(idp+4)
```

`FORMMAT.GAS` reads the same three words back as `xaxis`, `zaxis`, `yaxis`, in
that order, and builds its matrix from them. So

| frame byte | instance word | axis | name |
|---|---|---|---|
| 0 | +24 | x | elevation |
| 1 | +26 | z | twist |
| 2 | +28 | y | azimuth, **plus the character's facing** |

The facing landing on the y rotation is the check: facing is a compass bearing
and y is the world's up axis.

## 14.3 The check: every frame's own high and low

The animation frame carries `animHigh` and `animLow`, "highest point of the
character" and "lowest point", measured from the floor. The root sits
`HEIGHTSTART` above the floor and each frame's `animYmove` shifts it further, so
a correctly assembled pose must span

```
low  - HEIGHTSTART - (the y moves so far)   ..   high - HEIGHTSTART - same
```

about its own root. Nothing in that is circular: the angles come from the
frame, the joints from the models, and high and low are two more words of the
same frame that the assembly never touches.

`hlview --check-char` runs it over every frame of every animation of every
bundle:

```
character pose vs the frame's own high and low: 6591 frames, 13182 measurements
  mean error 2.48 units, worst 27, 0 over 32 (0.00%)
  within  2: 4966    4: 4883    8: 3121   16: 178   32: 34   64: 0  128: 0
```

Two units of error on a figure 414 units tall, over a chain of up to four
joints, is the 256-step circle and the s1.14 sine table and nothing else.

**It is a discriminating test, not a vacuous one.** Posing an animation against
the wrong bundle fails it: fitting each of the seventeen animation groups
against each of the fourteen bundles gives a clean diagonal, every group's own
bundle scoring about 1 unit and the next best four to a hundred times worse.

## 14.4 Which animations go with which bundle

Nothing on the disc statically links a character sheet to a model bundle
([12-world-and-sheets.md](12-world-and-sheets.md) 12.7). The track's layout
does, and the fit of 14.3 confirms it independently: **a bundle is followed by
its own animations**, and the next bundle starts the next character.

```
bundle  0  0x000004  15 pieces  114 animations     Quentin - four banks, in
                                                   four slots of the track
bundle  1  0x101404  15 pieces   28 animations
bundle  2  0x141904  15 pieces   28
bundle  3  0x181e04  15 pieces   28
bundle  4  0x1c2304  15 pieces   28                identical geometry to 3
bundle  5  0x202804  15 pieces   28
bundle  6..10        15 pieces    4 each
bundle 11..13        15 pieces    3 each
bundle 14  0x344104   3 pieces    1                not a character - see below
```

That is 1 + 5 + 8 = fourteen bodies, against `DATA.INC`'s one sheet with 30
animations, five with 28 and eight with 3 or 4 — the same shape from a third
side, and `cshModelNum` is 15 for every one of them. The one thing the layout
says and the sheets do not is that **Quentin's body carries four animation
banks**, not one: the three extra 28-animation groups in slots 1 to 3 fit his
skeleton uniquely and no other. Weapon-specific sets are the obvious guess and
the sheet's `cshAnimOff` is where it would be selected.

**Bundle 14 is machinery, not a character.** Three pieces: a root that publishes
two joints 24 units apart, and two identical 42-vertex discs a thousand units
across that its one animation counter-rotates a step at a time. It is the
grinder. Its animation's `high` and `low` do not describe it — they alternate
between 965 and 0 while the pose barely moves — so `--check-char` reports it and
does not count it.

## 14.5 Standing, walking, and being stopped

`src/game/actor.c` follows `ANIM.GAS` and the `COLLIDE.GAS` it includes.

**The root's height** is `wstYpos + HEIGHTSTART + the accumulated animYmove`,
and `wstYpos` chases `citHeight`, the height word of the triangle underfoot. It
does not jump: `SMOOTH.TXT` wants a fall that accelerates and a rise at a fixed
rate that only grows when the step is taller than it, so that stairs taken
quickly still work.

**Movement is a swept circle against edges.** `COLLIDE.GAS` puts the current
triangle on a list, and for each triangle on the list tests each of its three
edges against the circle of `wstRadius` swept from where the character is to
where it wants to be. An edge the circle reaches is either passable — and its
triangle joins the list — or it is not, and there are two ways to be
impassable:

* **no neighbour**, which is a wall; or
* **a neighbour more than 255 units higher.** The engine subtracts, saturates
  away the negative, shifts right by eight and treats anything left as a wall.
  "converts anything up to 1 metre to 0", says the comment, which prices the
  unit at about four millimetres.

The floor the character ends up on is the **highest** any reached triangle
offers, which is what carries it up a step rather than through it.

**Hitting a wall abandons the whole move** — not a slide, not a projection — and
turns the character four steps of the 256-step circle towards running along the
wall, keeping the same sense for as long as the collision lasts. That is the
original's entire response, and walking into a palisade in `DUN1` reproduces it:
a few frames of stop-and-turn and then a clean run along the line.

`FINDTRI.GAS` runs last, to make sure the triangle really does contain the point
the move ended on ([13-viewer.md](13-viewer.md) 13.8).

## 14.6 The camera cuts

62 of `CA`'s 66 events are `SCENE` events and 77 of `DUN1`'s 80 are, so the cuts
are data and the set loader already reads them. `EVENT.GAS` walks the list every
frame:

* an event applies to the current view, or to any view in the set when its
  scene word is `$FFFF` — which is what all 80 of `DUN1`'s are;
* the height word, when it is not zero, is a **ceiling**: the event fires from
  at or below it;
* a radius of zero means always colliding;
* the status word's bit 0 remembers that the player is already inside, and
  every type but `SCENE` is edge-triggered on it. `SCENE` fires anyway —
  "scene changes dont give a **** if we are already colliding" — and only the
  first cut of a frame is taken.

The target is the first of the event's three data words, a scene id, and the
set's own scene table turns that into a slot on the picture track.

Walking north through `DUN1` from `(-5574, 3172)`:

```
$ hlview --scene DUN1_CAM04 --char 0 --anim 10 --play --events \
         --pos -5574,0,3172 --frames 400
cut to DUN1_CAM07 (id 1287) at (-5795,5761) on triangle 186
cut to DUN1_CAM08 (id 1288) at (-5798,5785) on triangle 186
```

which is the roadmap's success criterion for the phase.

**The circles are small.** Over the disc's 1,203 `SCENE` events the radius runs
from 4 to 107 units, median 27, against a character 414 units tall who covers
21 units in a frame of the walk. They are thresholds to step over rather than
regions to be in, and a walk that passes 76 units to one side of one, as one of
ours did, simply misses it. Whether the retail exporter turned the `.MAP`
format's event *line segments* ([03-data-formats.md](03-data-formats.md) 3.6)
into single circles or into rows of them is worth settling.

## 14.7 The ground gap, and what it turned out to be

Session 8 measured a gap between the collision plane and the floor the backdrops
draw, per set, and read off a table that ran from -50 in `SHANR3` to +348 in
`PRI`, with `CA` at 155 - which put a 414-unit character in `CA` in the ground to
the hips and in `PRI` to the neck. That table was the wrong measurement, and
correcting it is most of what this question needed.

**It is not a per-set number, and the first sign is inside a single set.**
Running the same estimator per camera rather than pooling all of a set's cameras
gives, for `DUN1` alone, figures from -259 to +283. Cameras that share a
position give byte-identical answers, so the spread is not noise in the
reconstruction; it is the estimator answering a different question for each
camera.

**What it was measuring.** The estimator took the modal reconstructed height
over every pixel whose plan position falls inside a collision triangle. A
triangle's footprint in plan does not contain only floor: it contains the wall
standing on its edge, the crate in the middle of it, and the parts of it the
camera cannot see. Measured on the same triangle from two cameras, that estimate
disagrees with itself by a **median of 300 units** - more than the gap it was
being used to report.

**Measured properly.** Nothing is below the floor, so the floor is the *lowest
flat surface* a camera sees in a given patch of plan, and where two cameras can
both see it they agree. Re-run that way over 100-unit cells:

```
43 sets: gap median -5, mean -7, sd 61, range -105 .. +136
  PRI 348 -> 45      CA 155 -> 89      REST 112 -> -72
  DUN1  12 -> 13     SHANR2 56 -> 48   SHANR3 -50 -> -57
```

The sets that barely move are the ones session 8's number was already right
about; the ones that move are the ones with things standing on the floor.

**And then measured a fourth way, without any of that machinery.** `hlview`
already counts how many pixels of a model survive the backdrop's own Z-buffer.
Raise a model a step at a time and the height at which the backdrop stops hiding
any of it *is* the drawn floor, measured by the engine, with no reconstruction,
no projection inverse and no assumption about what is floor. In `PRI`, at the
doorway, the 69-unit wine bottle standing at collision height 1 keeps 2 of its
23 pixels, and is completely clear by y = 60. Not 348.

The probe is an **upper bound**, because it asks for every pixel to be clear and
a crate standing in front raises it. So the figure to read is the lowest it
gives across several places in a set, and running it over triangle centroids
answers the question by not agreeing with itself:

```
DUN1   10   20   30   30   60   330        CA   20   50  110  190
PRI    59
```

Three sets, three ways, against session 8's table:

| set | session 8 | re-measured | engine probe, lowest of several places |
|---|---:|---:|---:|
| `DUN1` | 12 | 13 | 10 |
| `CA` | 155 | 89 | 20 |
| `PRI` | 348 | 45 | 59 |

`DUN1`, which session 8 got right, all three agree on. The two it got wrong come
down by a factor of three to seven, and the two new methods bracket rather than
match - which is the honest state of it, because they are measuring the floor
under different square metres of the same room.

**So the gap is relief in the art, and it is local.** It is not a constant, not a
per-set datum, and not something the engine subtracted, because - §10.7 - there
is nothing in the pipeline it could have been derived from. The map editor
calibrates the horizontal plane and only the horizontal plane: one `SCALE`
segment, one `ORIGIN` vertex, and a ground height that is an integer somebody
typed into a `HEIGHT` field by eye. The third of session 8's three candidates is
the right one: **the original lived with it.**

What it costs is worth stating in pixels rather than units, because that is what
the player saw. `PRI_CAM00` looks at that doorway from about 2,200 units away,
where the projection puts 59 units of error at 6.6 pixels and a 414-unit
character at 46 pixels tall. Ankle deep, on a 320x200 screen.

**Two things ruled out on the way.** The 24 bytes after the set header are a
load list and not a datum - §10.1, where they are now decoded as far as their
shape. And the projection constants are not the cause: fitting them from
camera-to-camera agreement alone, over 220 distinct cameras in seven sets, leaves
`XSCALE` and the principal column exactly where the viewer has them and `YSCALE`
flat from 236 to 261. The principal *row* appears on that criterion to want 90
rather than 99, which would be worth a great deal if it held - but it does not.
A collision triangle is flat by construction, so the surface drawn over it has to
reconstruct level, and minimising that tilt puts the row at 104, on the other
side of 99. Two independent criteria disagreeing means the first one was
measuring its own flatness filter. The constants stand.

## 14.8 The player, driven

The original never moves the player. It reads the pad, picks an animation from a
table, and lets that animation's own root motion do the walking - which is why
the viewer had `--play` and `--walk` as two separate things and why joining them
is what turns it into a game. Two loops of `AICTRL.GAS` do the whole of it, and
`src/game/control.c` is both.

**`PlayerControl` makes `actJoypad` out of the hardware pad**, and two things
happen on the way that the pad alone does not say.

* `actCount` counts frames since JOY_UP was last held, and while that count is
  1, 2 or 3 the code **forces JOY_UP back on** - in a branch delay slot, so it
  happens whether or not the test after it is reached. Letting go of forward for
  under a fifth of a second does not stop the walk.
* Press again inside that window and JOY_DOUBLE is set as well. **The double tap
  is detected by the pad handler, not by the animation table**, and it is how
  you run.
* `slowtowalk`, a global byte the scripts set, rewrites the stance in place: 4
  into 3, jog into walk, clearing FSAPlay so the change takes at once. Those two
  numbers are the only stance values the source names outright, and they are
  what fixes the rest of ours.

**`ActionCode` then walks a logic table.** It is a list of tuples, each headed by
a joypad mask, and the first tuple all of whose bits are held wins - so the list
is a priority order and the last mask is zero, which is what makes the unbounded
search terminate. The matched tuple holds eight entries and the low three bits of
`citStance` choose between them, so **what a button does depends on what the
character is already doing**: forward from a stand is a walk, forward from a jog
stays a jog, and a direction held while walking steers where from a stand it
plays the turn on the spot. Each entry names an animation and the stance to move
to, and the animation restarts when the masked pad changes, when the last one has
run out, or when the entry names a different one.

**Turning is separate and additive.** JOY_LEFT and JOY_RIGHT turn the facing by
`4 * 20 / framerate` steps of the 256-step circle - four a frame at the
animations' own 20 fps - unless the new stance carries FSATurn, which says the
animation is turning by itself. That the two rates are the same rate is not an
assumption: Quentin's turn-on-the-spot animation turns 62 steps over 15 frames,
which is 4.1 a frame. A character can hand the turn to the animation and the
speed does not change.

**Which animation is which, from the data.** The table needs to name Quentin's
thirty animations and `SHEET.S` says where they come from - three loads of 14,
14 and 2 from `BO_ANIM_QUENTIN_HAND1..3`, with the sword and gun banks living on
the weapon's own sheet, which is what `.weapon_action` reaches for. Which one is
the walk is read off their root motion rather than guessed:

```
  6   24 frames, the root barely moves               stand
 10   18 frames, +411 along z, 22.8 units a frame    walk forward
 11   16 frames, +623, 38.9 a frame                  run
 12   17 frames, -280, 16.5 a frame                  walk backward
 15   15 frames, +62 of facing, 4.1 a frame          turn left on the spot
 16   14 frames, -61 of facing, 4.4 a frame          turn right
```

Animation 10 is the one session 8's walk already used, which settles the sign:
+z is forward.

**What is the original's and what is ours.** The mechanism, the stance bits, the
rotate rate, the double-tap window and `slowtowalk` are `AICTRL.GAS`'s. The
*table* is ours, and it is marked as ours in the code, because the real ones are
five CD files that `DATA.INC` names - `BO_LOGICS_1` "full", `2A` "stand & walk",
`2B` "stand & walk - no turn anims", `3A` "stand", `3B` "stand - no turn anims" -
at block offsets $0, $10, $20, $30 and $40 of the data type `VIDSTUFF.INC` calls
`CHARDATA` and annotates "jakes joypad logics". Where the retail disc put them is
not known; scanning every track for the structure `ActionCode` reads finds only a
clamp table in the binary. That is the one piece of this section still missing.

```
frame    0: walk  animation 10
frame   42: jog   animation 11   (double tap)
frame  106: stand animation  6
frame  132: turn  animation 15
frame  162: walk  animation 10
```

That is `hlview --scene DUN1_CAM04 --char 0 --drive --pad 'up:40,-:2,up:60,-:30,left:30,up:40'`,
and the frame numbers are the mechanism: the release at frame 102 does not reach
`stand` until 106, three frames of grace later, and the two-frame release at 40
comes back as a jog rather than a walk.

## 14.9 Through the door

`--events` cut the camera inside a set. Crossing into another one is the same
event doing more work, and §10.3 is the half that had to be settled first: the
arriving set's init table is keyed on **the view you are leaving**, and its flags
word is `facing * 256 + character`. Both hold for all 153 entries on the disc.

So the engine, on a `SCENE` event whose target view another set owns, loads that
set, looks up the entry keyed on the view being left, falls back to the set's
`$FFFF` default, and stands the character there facing the way the entry says.
The two sets do not share an origin, so he has to be put down again rather than
carried across - which is exactly what the init table exists to say.

```
cut to CNY08_CAM12 (id 780) at (-4304,3903) on triangle 38
  through the door into set 8: arriving at (6099,1290) facing 213, from view 832
```

`hlview --check-doors` runs it over the disc rather than over one walk: 153 of
153 entries decode and stand on their own set's mesh, and all 129 `SCENE` events
that leave their set find an arrival - 88 keyed, 41 by the default, none off the
far mesh.

## 14.10 Using it

```
hlview --list-chars                                  the fourteen bundles
hlview --char 0 --anim 10 --play                     Quentin, walking on the spot
hlview --scene DUN1_CAM00 --char 0 --anim 10 --walk  arrows turn and walk
hlview --scene DUN1_CAM04 --char 0 --anim 10 --play --events --frames 400
hlview --check-char                                  the pose, checked
hlview --scene DUN1_CAM04 --char 0 --drive           the pad drives him (14.8)
hlview --scene CNY09_CAM00 --char 0 --drive --pad 'up:250' --pos '-2574,0,3398' --face '0,192,0'
hlview --check-doors                                 the doorways, checked
```

`--frame N` holds one frame, `--pos X,Y,Z` and `--face E,A,T` place it, and
`--frames N --no-window` runs N frames headless, which is how the walk above
was recorded.
