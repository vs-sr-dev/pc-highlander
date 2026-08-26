# 14 — The character: fifteen pieces, one pose, and the floor under it

Status: **the assembly is settled and checked.** `hlview --char N` loads one of
track 5's fifteen-piece bundles, chains it through its origin points, poses it
at a frame of one of its animations, stands it on a set's collision mesh, walks
it about, and cuts the camera when it crosses a `SCENE` event.

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

## 14.7 What the character makes urgent: the ground gap

A 69-unit wine bottle sitting a little low was a curiosity
([13-viewer.md](13-viewer.md) 13.6). A 414-unit character standing in the same
place is buried to the hips, and in one set to the neck, so the gap between the
collision plane and the floor the backdrops draw now has to be settled.

It is measured, per set, by inverting the Z-buffer
([tools/scene/backproj.py](../tools/scene/backproj.py) `--ground`). For the
sets whose mesh is a single level, so that the number means one thing:

| set | gap | set | gap | set | gap |
|---|---:|---|---:|---|---:|
| `PRI` | 348 | `TENT6` | 76 | `DUN2` | 17 |
| `CA` | 155 | `TENT3` | 54 | `C3` | 15 |
| `REST` | 112 | `C2` | 56 | `DUN1` | 12 |
| `TRAIN` | 102 | `SHANR2` | 56 | `D3` | 7 |
| `CN4` | 96 | `TA` | 72 | `CN5` | 3 |
| `TENT5` | 77 | | | `SHANR3` | −50 |

It is **not a constant**, so it is not a datum the engine adds. It does not
track the camera's height (r = −0.39) or either unexplained field of the camera
footer (r = +0.15, −0.41). And it is not a scale error: `D1`, which is built in
storeys, still fits `floor = 0.962 * height + 36.7` over heights from 1 to
2,473.

So the collision plane really does sit under the drawn ground by a per-set
amount, and where that amount is large the character stands in it. What the
engine did about it is the open question this session hands on. Three places
worth looking: the 24 unexplained bytes after each set header
([10-set-track.md](10-set-track.md) 10.1), the light list that has never been
located, and the possibility that the original simply lived with it and the sets
where it is worst are ones the player never stands in.

## 14.8 Using it

```
hlview --list-chars                                  the fourteen bundles
hlview --char 0 --anim 10 --play                     Quentin, walking on the spot
hlview --scene DUN1_CAM00 --char 0 --anim 10 --walk  arrows turn and walk
hlview --scene DUN1_CAM04 --char 0 --anim 10 --play --events --frames 400
hlview --check-char                                  the pose, checked
```

`--frame N` holds one frame, `--pos X,Y,Z` and `--face E,A,T` place it, and
`--frames N --no-window` runs N frames headless, which is how the walk above
was recorded.
