Engine code, phase 3 onwards.

```
main.c        hlview: the viewer, and for now the whole front end
game/         scene.c  a backdrop slot: XOR key, colour, depth, camera footer
              model.c  the SKELSKIN polyhedron format, read off the disc
              anim.c   the animation records: root motion and three angles a piece
              actor.c  a character: fifteen pieces chained through their origin
                       points, posed by a frame, walked over the floor the way
                       COLLIDE.GAS does it
              set.c    one environment: views, doorways, events, and the floor
                       mesh, with the triangle search FINDTRI.GAS describes
              control.c AICTRL.GAS's ControlCode/ActionCode: the joypad, and
                       the animation it chooses - the double tap, the stance
                       and the turn
              ai.c     AICTRL.GAS's other half: the joypad a character who is
                       not the player presses.  The arctan bearing, and the
                       face / goto / follow codes behind the fourteen commands
              sheet.c  the world table and the character sheets, out of the
                       resident binary: who a character is, which bundle he
                       wears, and what he does when nobody drives him
              act.c    the active character table: one record per character in
                       the world, and AICTRL.GAS's two master loops over it
              combat.c COMBAT.GAS's PPCOLL: who hit whom, read out of the
                       animation frame the blow was drawn on, and who is
                       standing in whom
              game.c   the loop: the script machine, the events it posts, the
                       two master loops, and the event lines on the floor -
                       everything one game frame is, in the order it runs
script/       vm.c     SCRIPT.GAS's interpreter: 83 opcodes, the process
                       table, and the world the commands reach into
media/        film.c   the FILM container on track 7: seek to a block, find
                       the sync, walk the chunks, hand out frames and speech
              cinepak.c the cvid decoder - codebooks, 4x4 blocks, V1 and V4,
                       inter and intra frames - into 24-bit and then RGB16
r3d/          r3d.c    projection, matrices, scanline fill, Z-buffer
platform/     window.c the only file that knows SDL exists
util/         io.c     whole-file reads and big-endian accessors
              json.c   a small DOM reader for assets/manifest.json
```

`make` from the repository root builds `build/hlview`. It needs SDL3 through
pkg-config; on Windows that means an MSYS2 mingw64 shell with
`mingw-w64-x86_64-sdl3` installed.

```
build/hlview --scene CA_CAM03                      a backdrop
build/hlview --scene CA_CAM03 --depth              its Z-buffer as grey
build/hlview --model boot:6 --spin                 the wine bottle, turning
build/hlview --scene TENT6_CAM01 --model boot:6 --object '#190'
build/hlview --scene DUN1_CAM00 --mesh             the collision mesh, over the art
build/hlview --check-mesh                          the triangle search, checked
build/hlview --char 0 --anim 10 --play             Quentin, walking on the spot
build/hlview --scene DUN1_CAM00 --char 0 --anim 10 --walk --events
build/hlview --scene DUN1_CAM04 --char 0 --drive   the arrows drive him
build/hlview --scene DUN1_CAM04 --char 0 --drive --pad 'up:40,-:2,up:60'
build/hlview --check-char                          the pose, checked
build/hlview --check-doors                         the doorways, checked
build/hlview --list-sheets                         the sheets, and what each wears
build/hlview --scene DUN1_CAM00 --char 0 --drive   ...and Ramirez follows you
build/hlview --check-follow                        the bearing, and the follow
build/hlview --check-script                        every script on the disc, run
build/hlview --film 19                             a film, at its own 12 fps
build/hlview --film 19 --shot-at 30 --shot f.ppm --no-window
build/hlview --check-film                          every frame of all 36, decoded
build/hlview --scene DUN1_CAM04 --char 0 --drive --fight --weapon 1
build/hlview --list-attacks                        which animations carry a blow
build/hlview --check-combat                        the duel, checked
build/hlview --scene SHANR1_CAM00 --char 0 --drive  ...and its script plays one
build/hlview --help
```

It reads the tracks as `tools/jcd/jcdinfo.py --extract` leaves them, in
`assets/tracks/`, and `assets/manifest.json` for the names — nothing is
pre-converted, the scene decode and the model parse are the engine's own.

What the viewer settled, and what it left open, is
[docs/13-viewer.md](../docs/13-viewer.md) and, for the characters,
[docs/14-characters.md](../docs/14-characters.md). The formats it reads are
[07-scene-format.md](../docs/07-scene-format.md) and
[03-data-formats.md](../docs/03-data-formats.md) 3.3 and 3.4.
