#include "ai.h"
#include "control.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- the arctan table ----------------------------------------------- */

/* JOY.S carries a 257-byte `arctan_table` that AIRotateCode and COMBAT.GAS
 * both index with `(|num| << 8) / den`.  It is not an approximation of
 * anything: every one of its 257 entries is exactly
 *
 *     round(atan(i / 256) * 512 / pi)
 *
 * so it is built here rather than transcribed, and the equality is the check.
 * Its unit is a quarter of a step of the game's 256-step circle - entry 256 is
 * $80, which is 128 quarters, which is the 32 steps of 45 degrees the code
 * gets after its `sharq #2`. */
#define AI_PI 3.14159265358979323846

static uint8_t arctan[257];
static int     arctan_built;

static void build_arctan(void)
{
    for (int i = 0; i <= 256; i++)
        arctan[i] = (uint8_t)lrint(atan(i / 256.0) * 512.0 / AI_PI);
    arctan_built = 1;
}

uint8_t ai_angle(int32_t dx, int32_t dz)
{
    if (!arctan_built)
        build_arctan();

    /* AIRotateCode, in its own order.  `num` and `den` start as the two
     * differences; the larger of them has to be the denominator, so the
     * quadrant is picked first and the pair swapped into it. */
    int32_t num = dx, den = dz;
    int     quad = 0;

    if (labs((long)dz) < labs((long)dx)) {      /* cmp |dx|,|dz|: swap        */
        int32_t t = num;
        num = -den;
        den = t;
        quad |= 64;
    }
    if (den < 0) {
        num = -num;
        den = -den;
        quad |= 128;
    }
    if (den == 0)
        return (uint8_t)quad;                   /* nowhere to look: the two
                                                   points are the same        */

    int32_t i = (labs((long)num) << 8) / den;
    if (i > 256)
        i = 256;
    int a = arctan[i] >> 2;
    if (num < 0)
        a = -a;
    return (uint8_t)(quad + a);
}

/* AIRotateCode's other half: which way to turn to face `want`.  Under two
 * steps out and it does not bother, which is what keeps a follower from
 * juddering left and right on the spot. */
static uint32_t ai_rotate(uint32_t pad, uint8_t facing, uint8_t want)
{
    int diff = (int8_t)(facing - want);
    if (diff > -2 && diff < 2)
        return pad;
    return pad | (diff < 0 ? PAD(JOY_LEFT) : PAD(JOY_RIGHT));
}

/* AIRandomCode: the seed is stirred with the four coordinates every frame.
 * `imultn`/`imacn` are the Jaguar's 16-bit multiply-accumulate, so the
 * products are of the low halves. */
static void ai_random(Ai *ai, int32_t tx, int32_t tz, int32_t sx, int32_t sz)
{
    int32_t r = (int32_t)ai->seed;
    r = (int16_t)r * (int16_t)r + (int16_t)tx * (int16_t)tz
                                + (int16_t)sx * (int16_t)sz;
    uint32_t s = (uint32_t)r;
    const int32_t v[4] = { tx, tz, sx, sz };
    for (int i = 0; i < 4; i++) {
        s ^= (uint32_t)v[i];
        s = (s >> 8) | (s << 24);
    }
    ai->seed = s;
}

/* ---- the commands --------------------------------------------------- */

void ai_init(Ai *ai, int behaviour)
{
    memset(ai, 0, sizeof *ai);
    ai->behaviour = behaviour;
    ai->command   = behaviour;
    ai->seed      = 1;
    ai->dist2     = 0;
}

/* Where the command is aimed.  Returns 0 when it names a target that is not
 * there, which is the one thing the original cannot happen upon: its tables
 * are filled in by the script that issues the command. */
static int ai_target(const Ai *ai, const Actor *player, const Actor *person,
                     int32_t *tx, int32_t *tz)
{
    switch (ai->command) {
    case AI_GOTO_POSITION:
    case AI_FACE_POSITION:
        *tx = ai->data1;
        *tz = ai->data2;
        return 1;
    case AI_GOTO_PERSON:
    case AI_FACE_PERSON:
    case AI_ATTACK_PERSON:
    case AI_SHOOT_PERSON:
    case AI_FOLLOW_PERSON:
        if (!person)
            return 0;
        *tx = person->x;
        *tz = person->z;
        return 1;
    default:
        if (!player)
            return 0;
        *tx = player->x;
        *tz = player->z;
        return 1;
    }
}

uint32_t ai_control(Ai *ai, const Actor *self, const Actor *player,
                    const Actor *person)
{
    if (ai->command == AI_DEFAULT) {
        /* AIDefault does not act: it copies the sheet's own behaviour into
         * the command and presses nothing this frame. */
        ai->command = ai->behaviour;
        return 0;
    }
    if (ai->command == AI_NOP)
        return 0;

    int32_t tx = 0, tz = 0;
    if (!ai_target(ai, player, person, &tx, &tz))
        return 0;

    int32_t dx = tx - self->x, dz = tz - self->z;
    int64_t d2 = (int64_t)dx * dx + (int64_t)dz * dz;
    ai->dist2 = d2 > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)d2;

    if (ai->command == AI_ATTACK_PERSON || ai->command == AI_ATTACK_PLAYER ||
        ai->command == AI_SHOOT_PERSON  || ai->command == AI_SHOOT_PLAYER)
        ai_random(ai, tx, tz, self->x, self->z);

    uint32_t pad = ai_rotate(0, self->facing, ai_angle(dx, dz));

    switch (ai->command) {
    case AI_FACE_POSITION:
    case AI_FACE_PERSON:
    case AI_FACE_PLAYER:
        /* AIFaceCode: turn, and nothing else. */
        break;

    case AI_GOTO_POSITION:
    case AI_GOTO_PERSON:
    case AI_GOTO_PLAYER: {
        /* AIGotoCode: walk until inside the range, then press back once and
         * drop to aiNop.  The range is the command's own - a quarter of a
         * metre for a position, a metre and a quarter for a person. */
        int32_t r = ai->command == AI_GOTO_POSITION ? AI_POSITION_RANGE
                                                    : AI_PERSON_RANGE;
        if (ai->dist2 > r * r)
            pad |= PAD(JOY_UP);
        else {
            pad |= PAD(JOY_DOWN);
            ai->command = AI_NOP;
        }
        break;
    }

    case AI_FOLLOW_PLAYER:
    case AI_FOLLOW_PERSON:
        /* AIFollowCode, which is AIGotoCode without the arriving: two and a
         * half metres back, and it keeps the command, so it starts walking
         * again the moment you do. */
        if (ai->dist2 > AI_FOLLOW_RANGE * AI_FOLLOW_RANGE)
            pad |= PAD(JOY_UP);
        break;

    default:
        /* AIAttackCode and AIShootCode, as far as this port goes: out past
         * the sentry range they do nothing at all, and inside it they close
         * to melee range at a run.  The swing itself is combat - actStatus,
         * the opponent's own joypad and the hit frames - and belongs with
         * phase 5, so it is not pretended at here. */
        if (ai->dist2 > AI_SENTRY_RANGE * AI_SENTRY_RANGE)
            return 0;
        if (ai->dist2 > AI_MELEE_RANGE * AI_MELEE_RANGE) {
            pad |= PAD(JOY_UP);
            if (!(ai->seed & 1))
                pad |= PAD(JOY_DOUBLE);
        }
        break;
    }
    return pad;
}

const char *ai_command_name(int command)
{
    switch (command) {
    case AI_NOP:            return "nop";
    case AI_GOTO_POSITION:  return "goto position";
    case AI_GOTO_PERSON:    return "goto person";
    case AI_FACE_POSITION:  return "face position";
    case AI_FACE_PERSON:    return "face person";
    case AI_ATTACK_PERSON:  return "attack person";
    case AI_ATTACK_PLAYER:  return "attack player";
    case AI_FACE_PLAYER:    return "face player";
    case AI_GOTO_PLAYER:    return "goto player";
    case AI_FOLLOW_PLAYER:  return "follow player";
    case AI_SHOOT_PERSON:   return "shoot person";
    case AI_SHOOT_PLAYER:   return "shoot player";
    case AI_DEFAULT:        return "default";
    case AI_FOLLOW_PERSON:  return "follow person";
    default:                return "?";
    }
}
