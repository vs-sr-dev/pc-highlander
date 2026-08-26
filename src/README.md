Engine code, phase 3 onwards.

```
main.c        hlview: the viewer, and for now the whole front end
game/         scene.c  a backdrop slot: XOR key, colour, depth, camera footer
              model.c  the SKELSKIN polyhedron format, read off the disc
              set.c    one environment: views, doorways, events, and the floor
                       mesh, with the triangle search FINDTRI.GAS describes
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
build/hlview --help
```

It reads the tracks as `tools/jcd/jcdinfo.py --extract` leaves them, in
`assets/tracks/`, and `assets/manifest.json` for the names — nothing is
pre-converted, the scene decode and the model parse are the engine's own.

What the viewer settled, and what it left open, is
[docs/13-viewer.md](../docs/13-viewer.md). The formats it reads are
[07-scene-format.md](../docs/07-scene-format.md) and
[03-data-formats.md](../docs/03-data-formats.md) 3.3.
