/* set - one environment: its camera views, its doorways, its floor and its
 * event list, read off track 3.
 *
 * Format: docs/10-set-track.md.  A set is one 56-block slot, a seven-long
 * header of offsets and then the tables it points at.
 *
 * The floor is a 2D mesh - vertices are (x, z) pairs and the height lives on
 * the triangle - which is the whole reason the game can do fixed cameras and
 * stairs with no 3D collision at all.  The height word is the world y of the
 * floor at 1:1: fitting D1's storeys against the surface its backdrops
 * actually draw gives a slope of 0.96 over heights from 1 to 2473
 * (docs/13-viewer.md 13.6).
 */
#ifndef HL_SET_H
#define HL_SET_H

#include <stdint.h>

#define SET_SLOT   (56 * 2352)
#define SET_COUNT  48

typedef struct { int32_t x, z; } SetVert;

typedef struct {
    int16_t height;             /* world y of this piece of floor */
    uint16_t vert[3];           /* a word on the disc: CNY01 has 348
                                   vertices, D1 300 and NEOSW 293, so a
                                   byte silently folds their meshes  */
    int16_t adj[3];             /* neighbour across edge k = (vert[k], vert[k+1]),
                                   -1 for a wall.  The links are symmetric. */
} SetTri;

typedef struct {
    uint16_t id;                /* group * 64 + camera */
    uint32_t block;             /* CD block on the PICT track */
    int      scene;             /* block / 110, the slot index */
} SetScene;

/* A doorway.  The id is the view you were looking at when you *left*, which
 * belongs to the set you came from - no entry on the disc is keyed on its own
 * set's group, and for all 123 with a real id the departing set has a SCENE
 * event, fired from that very view, that cuts into this one.  $FFFF is the
 * arrival used when the view you left is not listed.
 *
 * The flags word is the arrival pose: the top byte is the facing on the
 * 256-step circle and bit 0 says which character, 0 for the player and 1 for
 * the companion.  All 153 entries are exactly `facing * 256 + (0 or 1)`, 44
 * ids carry both halves of the pair, and the .MAP format says the same thing
 * in words - two `START` blocks, `QUENTIN` and `RAMIREZ`, sharing one `FROM`,
 * each with an `ORIENTATION` in degrees.  315 degrees is 224 steps, and 224
 * is one of the twenty facings the disc uses.  docs/10-set-track.md 10.3. */
typedef struct {
    uint16_t id;                /* the view you left, not the one you arrive in */
    uint16_t flags;             /* facing * 256 + character                     */
    int32_t  x, z;
} SetEntry;

#define ENTRY_FACING(e)    ((int)((e)->flags >> 8))
#define ENTRY_COMPANION(e) ((e)->flags & 1)

typedef struct {
    uint16_t scene;             /* $FFFF = any view in this set */
    uint16_t type;
    int32_t  x, z, height, radius2;
    uint16_t status, condition, cdata[3], data[3];
} SetEvent;

typedef struct {
    int       index;
    uint32_t  hinum, lonum, script_off;
    int       nscenes, nentries, nevents, nverts, ntris;
    SetScene *scene;
    SetEntry *entry;
    SetEvent *event;
    SetVert  *vert;
    SetTri   *tri;
    uint8_t  *script;           /* the bytecode at script_off, to the end of
                                   the slot; NULL when this set has none    */
    int       script_len;
} Set;

int  set_load(Set *s, const char *track3, int index);
void set_free(Set *s);

/* The set that owns a scene id, or -1.  A set lists views borrowed from its
 * neighbours too, so ownership is by group: id / 64. */
int  set_of_scene(const char *track3, uint16_t scene_id);

/* Which triangle covers (x, z).  set_find_tri searches outwards from `from`
 * over the adjacency, the way FINDTRI.GAS does - a list, not a walk down one
 * path, which the source is explicit about and which matters: the greedy
 * version fails on 258 of the disc's 5,342 triangles.  It is what movement
 * should use.  set_locate scans the whole mesh and is how you get a first
 * triangle.  Both return -1 when the point is off the mesh. */
int  set_locate(const Set *s, int32_t x, int32_t z);
int  set_contains(const Set *s, int tri, int32_t x, int32_t z);
int  set_find_tri(const Set *s, int32_t x, int32_t z, int from);

/* The same search, reporting how it went: `steps` comes back as the number of
 * triangles examined before the answer, or -1 if it gave up and the whole mesh
 * was scanned.  Movement wants the first; a check wants to know how often it
 * got the second, since a search that always falls back proves nothing. */
int  set_walk(const Set *s, int32_t x, int32_t z, int from, int *steps);

/* Follow the adjacency `hops` times from `tri`, staying on the mesh.  Only
 * useful for tests: it manufactures a start that is genuinely connected to
 * where the walk has to arrive, which is the situation movement is always in. */
int  set_step_away(const Set *s, int tri, int hops);

/* The floor height at (x, z), or 0 with a -1 return if it is off the mesh. */
int  set_ground(const Set *s, int32_t x, int32_t z, int from, int32_t *height);

#endif
