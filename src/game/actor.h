/* actor - a character: fifteen pieces chained through their origin points,
 * posed by an animation frame, standing on and walking over a set's floor.
 *
 * Three files of the original meet here and have to agree with each other.
 * SKELSKIN's model format supplies the pieces and the joints between them
 * (model.h), the animation record supplies three angles per piece per frame
 * (anim.h), and 3DENGINE.GAS supplies the rule that turns the two into a pose:
 *
 *   - a piece's *position* is the origin point its parent publishes, carried
 *     through the parent's own rotation and added to the parent's position;
 *   - a piece's *rotation* is its three animation angles and nothing else.
 *     Orientations do not chain.  The engine loads each instance's matrix
 *     independently and only ever concatenates it with the view.
 *
 * That is checked rather than assumed: every animation frame also records the
 * highest and lowest point of the pose it describes, and assembling the disc's
 * 6,655 character frames this way reproduces both to a median of 1.7 units.
 * `hlview --check-char` is that test.  docs/14-characters.md.
 *
 * Movement is COLLIDE.GAS: the swept collision circle is tested against the
 * edges of the triangles it can reach, an edge with no neighbour - or with a
 * neighbour more than 255 units higher - stops it dead, and the floor the
 * character ends up on is the highest one the circle touched.
 */
#ifndef HL_ACTOR_H
#define HL_ACTOR_H

#include <stdint.h>

#include "model.h"
#include "anim.h"
#include "set.h"

#define ACTOR_MAX_PIECES 32

/* One character's model bundle: the pieces as they lie on track 5, in the
 * order the animation angles are given, with the skeleton resolved. */
typedef struct {
    int          npieces;
    const Model *piece[ACTOR_MAX_PIECES];
    int          parent[ACTOR_MAX_PIECES];  /* the piece that publishes our
                                               origin, or -1 for the root  */
    int          joint[ACTOR_MAX_PIECES];   /* which of its origin points  */
} Bundle;

/* The world placement of one piece: what r3d_place wants. */
typedef struct {
    int32_t rot[9];                         /* row major, s1.14            */
    int32_t pos[3];
} ActorPose;

typedef struct {
    int32_t x, z;               /* wstXpos, wstZpos                        */
    int32_t y;                  /* wstYpos: the foot height, which chases  */
    int32_t ground;             /* citHeight: the triangle underfoot       */
    int32_t gravity;            /* citGravity                              */
    int     tri;                /* citTriangle                             */
    uint8_t facing;             /* citFacing, on the 256-step circle       */
    int16_t radius;             /* wstRadius                               */
    int     collided, colturn;  /* citFlags COLLIDE and COLTURN            */
    int16_t speed;              /* citSpeed: carried because the scripts
                                   write it, not yet read by the movement  */
    int32_t lift;               /* the animation's own accumulated y move  */
    int     frame;              /* the frame last shown                    */

    /* What COMBAT.GAS reads back out of the animation player.  `hitframe` is
     * `HITFRAME`: the first frame carrying an `animHit` that this game frame
     * stepped over, or zero - the combat code reads the hit value, the reach
     * and the arc out of that frame, so an attack lands on the frame the
     * animator drew it on and not on the frame the game happened to sample.
     * The rest is the moveback: what this frame's movement was, negated, and
     * the triangle it started on, so that a collision can be undone whole. */
    int     hitframe;
    int32_t mbx, mby, mbz;      /* citXmoveback, citYmoveback, citZmoveback */
    int     mbtri;              /* citTmoveback                            */
    int32_t collision;          /* citCollision: what is pushing on him    */
} Actor;

/* Collects the bundle whose first piece is list[first] - a model that hangs on
 * no origin and publishes some - and every piece after it that hangs on an
 * origin the bundle has already named.  Returns the number of pieces. */
int  bundle_build(Bundle *b, const Model *list, int count, int first);

/* The index in `list` of the n'th bundle, or -1.  A bundle starts at a model
 * with origin 0 that publishes at least one. */
int  bundle_at(const Model *list, int count, int n);

/* Poses the bundle.  `angle` is three bytes per piece in bundle order, or NULL
 * for every angle zero; `facing` is added to the third of each, which is what
 * ANIM.GAS does.  `root` is where the root piece goes.  `out` takes npieces
 * entries. */
void actor_pose(const Bundle *b, const int8_t *angle, int facing,
                const int32_t root[3], ActorPose *out);

/* The extent of a posed bundle in y, which is what the animation's own `high`
 * and `low` words record - measured from the root, so add HEIGHTSTART to
 * compare. */
void actor_extent(const Bundle *b, const ActorPose *pose, int32_t *lo, int32_t *hi);

/* Puts the actor on the mesh at (x, z), or returns 0 if that is off it. */
int  actor_place(Actor *a, const Set *s, int32_t x, int32_t z, int facing);

/* COLLIDE.GAS.  Tries to move by (dx, dz).  Returns 1 if it moved, 0 if it hit
 * something - in which case the move is abandoned whole and the facing turns
 * by four steps towards sliding along what it hit, which is the original's
 * entire response to a wall. */
int  actor_move(Actor *a, const Set *s, int32_t dx, int32_t dz);

/* The height chase of SMOOTH.TXT: `y` falls under gravity towards `ground` and
 * rises at an eighth of the gap.  `frate` is 256 / frames-per-second, the
 * scale ANIM.GAS works in. */
void actor_settle(Actor *a, int frate);

/* One animation frame: settle, turn, move the root through the collision, and
 * accumulate the frame's own y move.  Advances a->frame. */
void actor_step(Actor *a, const Set *s, const Anim *an, int frate);

/* Where the root of the model goes: the foot height, plus the animation's
 * HEIGHTSTART, plus everything its y moves have added since it started. */
void actor_root(const Actor *a, const Anim *an, int32_t out[3]);

#endif
