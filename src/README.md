Engine code, phase 3 onwards. Empty for now.

The first thing to live here is the viewer: an SDL3 window that shows one of the
672 backdrops with its Z-buffer and composites an extracted model into it,
depth-tested. The brief, with the four facts worth carrying over and the order
to build them in, is in
[docs/sessions/session-05.md](../docs/sessions/session-05.md); the projection and
the camera footer are in [docs/05-roadmap.md](../docs/05-roadmap.md) and
[docs/07-scene-format.md](../docs/07-scene-format.md) §7.5.

Everything it reads is produced by `tools/`, and `tools/manifest.py` ties it
together into one JSON.
