/* combat - COMBAT.GAS's `PPCOLL`, the pass that decides who hit whom.
 *
 * It is one GPU module in the original, run once a frame between the animation
 * player and the 3D engine (`MAIN.S`: `ANIMCODE`, then `PPCOLL`, then
 * `ENGINE`), and it does two jobs in one walk over the active characters:
 * combat, and keeping two bodies out of each other.
 *
 * **Combat comes out of the animation data.** Every animation frame carries
 * `animHit` - positive is an attack, negative is a defence - with `animRange`
 * for the reach and `animDirAz` and `animSprAz` for the direction the blow
 * covers and how wide that arc is.  The animation player leaves the frame to
 * read in `HITFRAME`, so a swing lands on the frame the animator drew it on.
 * Nothing here is a table of moves; the moves are the animations.
 *
 * The resolution, for a pair within reach and inside the arc:
 *
 *   - a positive hit adds to the *other* character's damage;
 *   - a negative hit adds to your *own* damage, which is how a defence
 *     subtracts from what is coming at you;
 *   - what is left, if it is one or more, comes off `wstLife` and sets
 *     `FSAHit`, along with two bits saying which way you were knocked;
 *   - if a blow connected but the defence swallowed it, that is a **parry**,
 *     and the defender gets `FSAShield` instead.
 *
 * Then, separately, any two bodies whose circles overlap add their `wstStr` to
 * each other's `citCollision`, and anybody left with a non-zero one is put
 * back where he started the frame - the moveback the animation player wrote.
 * The original's own comments describe a second level, where the combined
 * strength pushing on you decides whether you are shoved back rather than just
 * stopped; it is designed there and not implemented, so it is not here either.
 *
 * Two rules keep it from being a free-for-all, both dated in COMBAT.GAS's own
 * history: **exactly one of the pair must be the player** (01/05/95, "prevent
 * Hunters killing each other"), and neither may be carrying `FSAShield`.
 */
#ifndef HL_COMBAT_H
#define HL_COMBAT_H

#include <stdint.h>

#include "act.h"

/* What the last combat_frame did, for the log line and for --check-combat. */
typedef struct {
    long pairs;                 /* pairs compared                          */
    long swings;                /* frames where somebody's blow was live   */
    long landed;                /* blows that reached and were inside the arc */
    long hits;                  /* blows that took life off                */
    long parries;
    long deaths;
    long bumps;                 /* pairs whose circles overlapped          */
    long moved_back;            /* characters undone by one                */
    long damage;
} CombatStats;

extern CombatStats combat_stats;

/* One frame of PPCOLL.  `ws` is the live world-state table - the 32-byte
 * records of LOGICS.INC, which is where the radius, the strength and the life
 * points are, and where the life points have to be written back so that a
 * script can see them.  Runs after everybody has moved. */
void combat_frame(ActTable *t, uint8_t *ws);

#endif
