# Session 8 — A character on the floor, walking, and the camera cutting

The three things session 7 left for this one, in its order:

1. a character assembled from its fifteen pieces through the origin points,
2. movement over the collision mesh,
3. the camera cuts on the set's `SCENE` events.

All three are in, and `hlview --scene DUN1_CAM04 --char 0 --anim 10 --play
--events` walks Quentin north through DUN1 and cuts to `DUN1_CAM07` and then
`DUN1_CAM08` — which is the roadmap's success criterion for phase 4.

Session 7 predicted that making the model format, the animation format and the
character sheet agree with each other would correct one of the three. It
corrected something else instead: the rasteriser.

---

## 1. The origin points, and the one word that made them a skeleton

Session 7 found the numbers — the torso publishes 128, 135, 138, 141 — but not
where the numbers live. They are in the **fourth word of the origin point**, the
one a drawn vertex spends on a homogeneous 1. `3DENGINE.GAS` reads it, and if it
is 2 or more takes a different branch: transform the point like any other vertex,
then write the result into a table indexed by that number with bit 7 cleared. A
model whose own header names an origin reads its position straight back out of
that table instead of being placed from the world.

So **positions chain and orientations do not**. Each piece's matrix comes from
its own three angle bytes and is concatenated only with the view; `ANIM.GAS`
writes the frame's bytes straight into each instance and never accumulates.
That is why the pieces laid out with every angle zero are a 431-unit scarecrow
rather than a person: the stored pose is not a pose.

→ [14-characters.md](../14-characters.md) 14.1, 14.2

## 2. The check the data supplies for free

Every animation frame carries `animHigh` and `animLow`, the highest and lowest
point of the pose, measured from the floor. The assembly never reads them. So
pose the fifteen pieces, measure the result, and compare — remembering that the
root sits `HEIGHTSTART` above the floor and each frame's `animYmove` shifts it
further.

```
character pose vs the frame's own high and low: 6591 frames, 13182 measurements
  mean error 2.48 units, worst 27, 0 over 32 (0.00%)
  within  2: 4966    4: 4883    8: 3121   16: 178   32: 34   64: 0  128: 0
```

Two units on a figure 414 units tall is the 256-step circle and the s1.14 sine
table compounded over four joints, and nothing else. `hlview --check-char`.

It is also **discriminating**, which matters more than the number: fitting each
of the seventeen animation groups against each of the fourteen bundles gives a
clean diagonal, every group's own bundle at about 1 unit and the next best four
to a hundred times worse. That settles, from the data rather than from the
layout, which animations belong to which body — the link
[12-world-and-sheets.md](../12-world-and-sheets.md) 12.7 lists as open. It also
says **Quentin's body carries four banks of animations**, 114 in all, where his
sheet declares 30.

## 3. What it corrected: nothing on the disc has a facet normal

Drawing fifteen pieces instead of one made the backface cull impossible to miss:
Quentin came out with holes, keeping 254 pixels of a 327-pixel silhouette.

`3DENGINE.GAS` culls by transforming the stored facet normal and dropping the
facet when it points away. The screen-space signed area — which is what our
rasteriser used, marked in the code as ours rather than the game's — is only its
*fallback*, for facets flagged as having no normal.

**All 6,821 facets on the disc have a normal of (0, 0, 0).** 5,548 on track 5,
1,273 in the binary, and the surviving `.INC` sources too. So the engine's test
always passes, nothing is ever culled, and the Z-buffer does the work.

The models then prove the point by disagreeing with each other about winding —
the wine bottle keeps its whole silhouette culled one way, Quentin the other —
which is exactly what [03-data-formats.md](../03-data-formats.md) 3.3 predicts:
the item models were exported with two axes swapped, and swapping two axes is a
mirror.

`hlview` no longer culls. The bottle's three recorded silhouettes move by two
pixels each; the 41 the tent pole hides do not move at all.

→ [13-viewer.md](../13-viewer.md) 13.4

## 4. Walking, and being stopped

`src/game/actor.c` is `ANIM.GAS` and the `COLLIDE.GAS` it includes.

* The root's height is `wstYpos + HEIGHTSTART + the accumulated animYmove`, and
  `wstYpos` chases the triangle's height with `SMOOTH.TXT`'s asymmetric rule —
  a fall that accelerates, a rise at a fixed rate that grows only when the step
  is taller than it.
* Movement is a swept circle of `wstRadius` tested against the edges of the
  triangles it can reach. An edge with **no neighbour** stops it, and so does a
  neighbour **more than 255 units higher** — the engine shifts the rise right by
  eight and treats the remainder as a wall, which prices the unit at about four
  millimetres.
* The floor is the **highest** any reached triangle offers, which is what
  carries a character up a step instead of through it.
* A wall **abandons the whole move** and turns four steps of the 256-step
  circle toward running along it, keeping the sense until the collision ends.
  Walking into DUN1's palisade reproduces it: a few frames of stop-and-turn,
  then a clean run along the line.

## 5. The camera cuts

62 of `CA`'s 66 events are `SCENE` events and 77 of `DUN1`'s 80 are. Three
details of `EVENT.GAS` had to be right: the height word is a **ceiling** rather
than a floor, a radius of zero means always colliding, and a `SCENE` event fires
even when the status word already says the player is inside — "scene changes
dont give a **** if we are already colliding" — where every other type is edge
triggered.

**The circles are small**: over 1,203 `SCENE` events the radius runs 4 to 107
units, median 27, against a character 414 units tall who covers 21 units in a
frame of the walk. They are thresholds to cross, not regions to be in, and one
of our walks missed one by passing 76 units to the side of it. Whether the
retail exporter turned the `.MAP` format's event *line segments* into one circle
each or into rows of them is worth an hour.

## 6. Two things the disc gave up on the way

**The camera footer names its set.** The long at +32, which session 3 recorded
as the constant 672 from a single sample, is a multiple of 56 — the set slot
size on track 3 — and dividing by 56 gives the set index. For **all 672 scenes**
the set it names is one whose scene table lists that scene id, and every one of
the 48 sets is named by at least one view. It is a stronger link than the group
vote of [10-set-track.md](../10-set-track.md) 10.6, and where the two disagree,
on `DUN1_CAM20` and `MENU_CAM00`, it agrees with what session 5 had to read the
scripts to establish. `set_of_scene` is now the fallback.

**The Makefile had no header dependencies**, so changing `model.h` relinked
objects compiled against the old struct. It cost an hour of hunting a
"renderer bug" that was a stale `r3d.o`. Fixed with `-MMD -MP`.

---

## Still open

**The ground gap is now the port's problem, not a curiosity.** Session 7 read
the residual between the collision plane and the drawn floor as relief in the
art — 12 units in DUN1, 70 in TENT6 — and concluded a 69-unit bottle would sit
a little low. Measured across the disc it runs from **−50 in `SHANR3` to +348 in
`PRI`**, with `CA` at 155, and a 414-unit character standing in `CA` is buried
to the hips.

It is not a constant, so it is not a datum the engine adds; it does not track
the camera's height (r = −0.39) or either unexplained field of the camera footer
(r = +0.15, −0.41); and it is not a scale error, since `D1`'s storeys still fit
`floor = 0.962 * height + 36.7` from 1 to 2,473. Three places left to look: the
24 unexplained bytes after each set header, the light list nobody has located,
and the possibility that the original lived with it.

→ [14-characters.md](../14-characters.md) 14.7

Also still open, unchanged: **`ZMODELT`**'s sense; the non-uniform `face`
elevation on the items; track 9, the three unreferenced films, the 125 unnamed
world records, the seventeen unnamed GPU modules, the `SLP` payload,
`cshBehaviour`, `gvar[0..2]`.

## TODO for session 9

1. **The ground gap.** It blocks anything that looks right on screen, and it is
   the one measurement above that has no explanation. Start with the 24 bytes
   after the set header and the `.MAP` files' `SCALE` and `ORIGIN` blocks, which
   are the only surviving description of how a set was built.
2. **The player, driven.** `--walk` turns and walks, but the animation and the
   input are separate: the game picks the animation from the joypad state
   (`actJoypad`, `citStance`, the `slowtowalk` byte) and the animation's own root
   motion does the moving. Wiring that up is what makes it a game rather than a
   viewer, and `AICTRL.GAS` is the reference.
3. **The doorways.** `--events` cuts within a set. Crossing into another set
   means loading it, placing the character at the init entry the arriving scene
   id names (10.3), and carrying on — which is the last piece of "walk around
   the world".

Still worth an hour whenever there is one: **the frame comparator** — render a
scene, run the same scene under an emulator, diff. It only gets harder to
retrofit, and the ground gap is exactly the kind of question it would answer in
one run.
