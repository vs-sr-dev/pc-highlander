#include "combat.h"

#include "sheet.h"

#include <stdlib.h>
#include <string.h>

CombatStats combat_stats;

/* COMBAT.GAS works in distances rather than in squared distances - it has a
 * sixteen-step square root inline, added by the `shooting_fix` that made the
 * spread angle depend on how far away the target is.  So do this. */
static int32_t isqrt32(int32_t v)
{
    if (v <= 0)
        return 0;
    uint32_t x = (uint32_t)v, root = 0, rem = 0;
    for (int i = 0; i < 16; i++) {
        rem = (rem << 2) | (x >> 30);
        x <<= 2;
        uint32_t trial = (root << 2) + 1;
        root <<= 1;
        if (trial <= rem) {
            rem -= trial;
            root |= 1;
        }
    }
    return (int32_t)root;
}

/* The four values a character's current hit frame carries.  `HITFRAME` is
 * zero unless the animation player stepped onto a frame with an `animHit`,
 * and the original reads frame zero in that case - which comes to the same
 * thing for every animation whose frame zero carries nothing. */
typedef struct { int hit, range, dir, spread; } Blow;

static Blow blow_of(const Act *a)
{
    Blow b = { 0, 0, 0, 0 };
    if (!a->anim || !a->actor.hitframe)
        return b;
    AnimFrame f;
    anim_frame(a->anim, a->actor.hitframe, &f);
    b.hit    = f.hit;
    b.range  = f.range;
    b.dir    = f.dir[0];
    b.spread = f.spread[0];
    return b;
}

static inline int ws_byte(const uint8_t *ws, int rec, int off)
{
    return rec >= 0 && rec < WS_COUNT ? ws[rec * WS_REC + off] : 0;
}

static inline void ws_put(uint8_t *ws, int rec, int off, int v)
{
    if (rec >= 0 && rec < WS_COUNT)
        ws[rec * WS_REC + off] = (uint8_t)v;
}

static inline int ws_word(const uint8_t *ws, int rec, int off)
{
    if (rec < 0 || rec >= WS_COUNT)
        return 0;
    const uint8_t *p = ws + rec * WS_REC + off;
    return (p[0] << 8) | p[1];
}

/* The angle from b to a, on the 256-step circle.  ai.c's arctan is the
 * engine's own and is already checked over 3,600 directions; COMBAT.GAS has
 * its own copy of the same table lookup, so this is one routine rather than
 * two. */
static uint8_t bearing(int32_t dx, int32_t dz)
{
    return ai_angle(dx, dz);
}

/* Is the blow pointing at him?  `face` is the swinger's facing, `dir` the
 * animation's own offset from it, and `angle` the direction from the target
 * to the swinger - so the swinger has to be looking the other way, which is
 * the $80 the original adds for the first of the pair and not for the second.
 */
static int inside_arc(int face, int dir, int flip, int angle, int spread)
{
    int d = (int8_t)(face + (flip ? 0x80 : 0) + dir - angle);
    if (d < 0)
        d = -d;
    return d <= spread;
}

void combat_frame(ActTable *t, uint8_t *ws)
{
    memset(&combat_stats, 0, sizeof combat_stats);
    if (!ws)
        return;

    for (int i = 0; i < t->n; i++)
        t->a[i].actor.collision = 0;

    for (int i = 0; i < t->n; i++) {
        Act *A = &t->a[i];
        if (!(A->flags & ACT_CREATED) || A->world < 0)
            continue;
        int rad1 = ws_word(ws, A->world, WS_RADIUS);
        if (rad1 == 0)
            continue;               /* a zero radius is uncollidable, and the
                                       original drops the whole outer entry  */

        for (int j = i + 1; j < t->n; j++) {
            Act *B = &t->a[j];
            if (!(B->flags & ACT_CREATED) || B->world < 0)
                continue;
            int rad2 = ws_word(ws, B->world, WS_RADIUS);
            if (rad2 == 0)
                continue;
            combat_stats.pairs++;

            /* Exactly one of them has to be the player, and neither may be
             * carrying a shield.  Both rules are dated in COMBAT.GAS: the
             * first stops the hunters killing each other, the second stops a
             * parry from turning straight into a second exchange. */
            int duel = (i == t->player) != (j == t->player);
            if ((A->ctl.stance | B->ctl.stance) & FSA_SHIELD)
                duel = 0;

            /* Is exactly one of them a thing rather than a person?  The
             * original combines the two flag bytes with `xor` and tests the
             * one bit, which says "one of them and not both" in a single
             * instruction and is why two items in a heap are still just two
             * bodies to each other. */
            int coll = ((ws_byte(ws, A->world, WS_FLAGS) & WST_COLLECTABLE) != 0)
                     ^ ((ws_byte(ws, B->world, WS_FLAGS) & WST_COLLECTABLE) != 0);
            int item = coll
                     ? (ws_byte(ws, A->world, WS_FLAGS) & WST_COLLECTABLE ? i : j)
                     : -1;

            int32_t dx = A->actor.x - B->actor.x;
            int32_t dz = A->actor.z - B->actor.z;
            int32_t dist = isqrt32(dx * dx + dz * dz);
            int angle = bearing(dx, dz);

            /* Two bits saying which way each of them was struck from: the
             * quadrant of the bearing, taken 45 degrees round so that "behind"
             * is a quadrant and not a boundary.  STRUCDEF.INC names two of the
             * four, 00 back and 10 forward. */
            int knock1 = ((angle + 0xA0 - A->actor.facing) & 0xFF) >> 6;
            int knock2 = ((angle + 0x20 - B->actor.facing) & 0xFF) >> 6;

            int att1 = 0, att2 = 0;     /* damage owed to each of them       */
            int land1 = 0, land2 = 0;   /* what actually reached him         */

            if (duel && !coll) {
                Blow b1 = blow_of(A), b2 = blow_of(B);
                if (b1.hit || b2.hit)
                    combat_stats.swings++;

                /* A body subtends a wider angle the closer it is, so the
                 * spread each of them has to cover is widened by the *other*
                 * one's radius over the distance between them. */
                int extra1 = dist > 0 ? 40 * rad2 / dist : 0;
                int extra2 = dist > 0 ? 40 * rad1 / dist : 0;

                if (b1.hit && dist <= rad2 + b1.range &&
                    inside_arc(A->actor.facing, b1.dir, 1, angle,
                               b1.spread + extra1)) {
                    land1 = b1.hit;
                    if (b1.hit > 0) att2 += b1.hit;
                    else            att1 += b1.hit;
                    combat_stats.landed++;
                }
                if (b2.hit && dist <= rad1 + b2.range &&
                    inside_arc(B->actor.facing, b2.dir, 0, angle,
                               b2.spread + extra2)) {
                    land2 = b2.hit;
                    if (b2.hit > 0) att1 += b2.hit;
                    else            att2 += b2.hit;
                    combat_stats.landed++;
                }
            }

            /* And what that comes to.  The original does B first, then A. */
            struct { Act *who; int dmg, other_dmg, reached, knock; } side[2] = {
                { B, att2, att1, land1, knock2 },
                { A, att1, att2, land2, knock1 },
            };
            for (int k = 0; duel && !coll && k < 2; k++) {
                Act *w = side[k].who;
                if (side[k].dmg >= 1) {
                    int life = ws_byte(ws, w->world, WS_LIFE) - side[k].dmg;
                    if (life < 0)
                        life = 0;
                    ws_put(ws, w->world, WS_LIFE, life);
                    w->ctl.stance |= FSA_HIT;
                    w->knock = (uint8_t)side[k].knock;
                    combat_stats.hits++;
                    combat_stats.damage += side[k].dmg;
                    if (life == 0)
                        combat_stats.deaths++;
                } else if (side[k].other_dmg < 1 && side[k].reached >= 1) {
                    /* Nothing got through, but something was aimed at him and
                     * he was not swinging himself: that is a parry. */
                    w->ctl.stance |= FSA_SHIELD;
                    combat_stats.parries++;
                }
            }

            /* The pickup, which stands where the physical collision would.
             * An item and a person never push on each other: whichever way
             * this goes, the pair is finished here. */
            if (coll) {
                if (!duel)
                    continue;               /* only the player picks things up */
                if (dist >= rad1 + rad2) {
                    t->a[item].picked = 0;  /* `.miss`: he has stepped out of it */
                    continue;
                }
                if (t->instpick < 0 && !t->a[item].picked) {
                    t->a[item].picked = 1;
                    t->instpick = item;
                    combat_stats.offered++;
                }
                continue;
            }

            /* The bodies themselves.  Each one's strength pushes on the other,
             * so what a character carries is the sum of what is against him. */
            if (dist <= rad1 + rad2) {
                A->actor.collision += ws_byte(ws, B->world, WS_STR);
                B->actor.collision += ws_byte(ws, A->world, WS_STR);
                combat_stats.bumps++;
            }
        }
    }

    /* The resolution, which is one line of design and no arithmetic: anybody
     * with something pushing on him goes back to where he started the frame,
     * triangle included. */
    for (int i = 0; i < t->n; i++) {
        Act *a = &t->a[i];
        if (!(a->flags & ACT_CREATED) || a->actor.collision == 0)
            continue;
        a->actor.x   += a->actor.mbx;
        a->actor.y   += a->actor.mby;
        a->actor.z   += a->actor.mbz;
        if (a->actor.mbtri >= 0)
            a->actor.tri = a->actor.mbtri;
        a->actor.mbx = a->actor.mby = a->actor.mbz = 0;
        combat_stats.moved_back++;

        /* The world state carries the same position, and a script reads it
         * back - the original moves both, and so does this. */
        if (a->world >= 0 && a->world < WS_COUNT) {
            uint8_t *p = ws + a->world * WS_REC;
            int32_t x = a->actor.x, z = a->actor.z;
            p[WS_XPOS] = (uint8_t)(x >> 24); p[WS_XPOS + 1] = (uint8_t)(x >> 16);
            p[WS_XPOS + 2] = (uint8_t)(x >> 8); p[WS_XPOS + 3] = (uint8_t)x;
            p[WS_ZPOS] = (uint8_t)(z >> 24); p[WS_ZPOS + 1] = (uint8_t)(z >> 16);
            p[WS_ZPOS + 2] = (uint8_t)(z >> 8); p[WS_ZPOS + 3] = (uint8_t)z;
        }
    }
}
