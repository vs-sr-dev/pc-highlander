#include "control.h"

#include <string.h>

void control_init(Control *c)
{
    memset(c, 0, sizeof *c);
    c->stance = ST_STAND;
    c->anim   = -1;
    c->action = ~0u;            /* nothing has been chosen yet, so the first
                                   frame always counts as a change */
}

/* ---- PlayerControl -------------------------------------------------- */

uint32_t control_pad(Control *c, uint32_t raw)
{
    /* `slowtowalk`, the global byte the scripts set to make Quentin walk where
     * he would otherwise jog.  It rewrites the stance in place - 4 into 3 -
     * and clears FSAPlay so the new animation starts rather than waiting for
     * the old one to finish. */
    if (c->slowtowalk && STANCE(c->stance) == ST_JOG)
        c->stance = (uint8_t)((c->stance & ~7u & ~FSA_PLAY) | ST_WALK);

    uint32_t pad = raw;
    int32_t  old = c->count;

    /* The count is bumped every frame and reset whenever forward is held, so
     * it is the number of frames since forward was last down. */
    c->count = (raw & PAD(JOY_UP)) ? 0 : c->count + 1;
    if (c->count > 0x7FFF)
        c->count = 0x7FFF;      /* the sat16 */

    if (old < 4) {
        /* Held on through a gap of up to three frames.  The original does this
         * in a branch delay slot, so it happens whether or not the double-tap
         * test below is reached. */
        pad |= PAD(JOY_UP);
        if (old != 0 && (raw & PAD(JOY_UP)))
            pad |= PAD(JOY_DOUBLE);
    }

    c->pad = pad;
    return pad;
}

/* ---- ActionCode ----------------------------------------------------- */

void control_action(Control *c, const LogicTable *t, int playing)
{
    const LogicTuple *tu = t->tuple;
    int i = 0;
    while (i < t->count && (c->pad & tu[i].mask) != tu[i].mask)
        i++;
    if (i >= t->count)
        i = t->count - 1;       /* the mask-zero tuple, which always matches */

    uint32_t action = c->pad & tu[i].mask;
    const LogicEntry *e = &tu[i].e[STANCE(c->stance)];

    /* Restart when the masked pad has changed, when the last animation has
     * run out, or when this entry names a different one. */
    c->restarted = (action != c->action) || !playing || e->anim != c->anim;
    c->action    = action;
    if (c->restarted)
        c->anim = e->anim;
    c->stance = e->stance;
}

int control_turn(const Control *c, int fps)
{
    if (c->stance & FSA_TURN)
        return 0;               /* the animation is doing the turning */
    int step = fps > 0 ? (4 * 20) / fps : 4;
    if (c->pad & PAD(JOY_LEFT))
        return  step;
    if (c->pad & PAD(JOY_RIGHT))
        return -step;
    return 0;
}

/* ---- the table ------------------------------------------------------ */

/* Quentin's thirty animations are three loads of fourteen, fourteen and two
 * from BO_ANIM_QUENTIN_HAND1..3 (SHEET.S), and which of them is which is read
 * off their own root motion rather than guessed:
 *
 *    6   24 frames, the root barely moves          stand
 *   10   18 frames, +411 along z, 22.8 a frame     walk forward
 *   11   16 frames, +623, 38.9 a frame             run
 *   12   17 frames, -280, 16.5 a frame             walk backward
 *   15   15 frames, +62 of facing, 4.1 a frame     turn left on the spot
 *   16   14 frames, -61 of facing, 4.4 a frame     turn right
 *
 * Animation 10 is the one session 8's walk already used, which fixes the sign:
 * +z is forward.  The two turn animations turn at the same four steps a frame
 * the joypad rotate uses, which is what FSATurn relies on.
 */
#define A_STAND      6
#define A_WALK      10
#define A_RUN       11
#define A_BACK      12
#define A_TURN_L    15
#define A_TURN_R    16

/* Reading a row: given this much of the pad held, and given the stance down
 * the row, play that animation and take that stance.  Turning from a stand
 * plays the turn animation and hands the rotate to it (FSATurn); turning while
 * moving does not, so holding a direction while walking steers instead. */
static const LogicTuple quentin[] = {
  { PAD(JOY_DOUBLE) | PAD(JOY_UP), {           /* a double tap: run          */
      [ST_STAND] = { A_RUN,  ST_JOG  }, [ST_TURN] = { A_RUN,  ST_JOG  },
      [ST_BACK]  = { A_RUN,  ST_JOG  }, [ST_WALK] = { A_RUN,  ST_JOG  },
      [ST_JOG]   = { A_RUN,  ST_JOG  }, }},
  { PAD(JOY_UP), {                             /* forward                    */
      [ST_STAND] = { A_WALK, ST_WALK }, [ST_TURN] = { A_WALK, ST_WALK },
      [ST_BACK]  = { A_WALK, ST_WALK }, [ST_WALK] = { A_WALK, ST_WALK },
      [ST_JOG]   = { A_RUN,  ST_JOG  }, }},
  { PAD(JOY_DOWN), {                           /* back                       */
      [ST_STAND] = { A_BACK, ST_BACK }, [ST_TURN] = { A_BACK, ST_BACK },
      [ST_BACK]  = { A_BACK, ST_BACK }, [ST_WALK] = { A_BACK, ST_BACK },
      [ST_JOG]   = { A_BACK, ST_BACK }, }},
  { PAD(JOY_LEFT), {                           /* turn left                  */
      [ST_STAND] = { A_TURN_L, ST_TURN | FSA_TURN },
      [ST_TURN]  = { A_TURN_L, ST_TURN | FSA_TURN },
      [ST_BACK]  = { A_BACK,   ST_BACK }, [ST_WALK] = { A_WALK, ST_WALK },
      [ST_JOG]   = { A_RUN,    ST_JOG  }, }},
  { PAD(JOY_RIGHT), {                          /* turn right                 */
      [ST_STAND] = { A_TURN_R, ST_TURN | FSA_TURN },
      [ST_TURN]  = { A_TURN_R, ST_TURN | FSA_TURN },
      [ST_BACK]  = { A_BACK,   ST_BACK }, [ST_WALK] = { A_WALK, ST_WALK },
      [ST_JOG]   = { A_RUN,    ST_JOG  }, }},
  { 0, {                                       /* nothing held: stand        */
      [ST_STAND] = { A_STAND, ST_STAND }, [ST_TURN] = { A_STAND, ST_STAND },
      [ST_BACK]  = { A_STAND, ST_STAND }, [ST_WALK] = { A_STAND, ST_STAND },
      [ST_JOG]   = { A_STAND, ST_STAND }, }},
};

const LogicTable control_quentin = { quentin, (int)(sizeof quentin / sizeof *quentin) };

/* ---- the companion's table ------------------------------------------ */

/* Ramirez's sheet loads `BO_LOGICS_2A`, which DATA.INC annotates "stand &
 * walk", and his bundle carries exactly four animations - which, read off
 * their own root motion the way Quentin's thirty were, are
 *
 *    0    5 frames, nothing moves                  stand
 *    1   25 frames, +381 along z, 15.2 a frame     walk
 *    2   15 frames, +51 of facing, 3.4 a frame     turn left
 *    3   15 frames, -56 of facing, 3.7 a frame     turn right
 *
 * so the table below is the same shape as Quentin's with the rows that need a
 * run or a walk backward taken out, because he has neither.  Everything else -
 * the stances, FSATurn on the turn, the fall through to standing - is the same
 * mechanism.  Like Quentin's, the table itself is ours: the real `2A` is one
 * of the five CD files nothing on the retail disc loads (docs/14-characters.md
 * 14.9).
 */
#define B_STAND      0
#define B_WALK       1
#define B_TURN_L     2
#define B_TURN_R     3

static const LogicTuple follower[] = {
  { PAD(JOY_UP), {                             /* forward                    */
      [ST_STAND] = { B_WALK, ST_WALK }, [ST_TURN] = { B_WALK, ST_WALK },
      [ST_BACK]  = { B_WALK, ST_WALK }, [ST_WALK] = { B_WALK, ST_WALK },
      [ST_JOG]   = { B_WALK, ST_WALK }, }},
  { PAD(JOY_LEFT), {                           /* turn left                  */
      [ST_STAND] = { B_TURN_L, ST_TURN | FSA_TURN },
      [ST_TURN]  = { B_TURN_L, ST_TURN | FSA_TURN },
      [ST_BACK]  = { B_WALK,   ST_WALK }, [ST_WALK] = { B_WALK, ST_WALK },
      [ST_JOG]   = { B_WALK,   ST_WALK }, }},
  { PAD(JOY_RIGHT), {                          /* turn right                 */
      [ST_STAND] = { B_TURN_R, ST_TURN | FSA_TURN },
      [ST_TURN]  = { B_TURN_R, ST_TURN | FSA_TURN },
      [ST_BACK]  = { B_WALK,   ST_WALK }, [ST_WALK] = { B_WALK, ST_WALK },
      [ST_JOG]   = { B_WALK,   ST_WALK }, }},
  { 0, {                                       /* nothing held: stand        */
      [ST_STAND] = { B_STAND, ST_STAND }, [ST_TURN] = { B_STAND, ST_STAND },
      [ST_BACK]  = { B_STAND, ST_STAND }, [ST_WALK] = { B_STAND, ST_STAND },
      [ST_JOG]   = { B_STAND, ST_STAND }, }},
};

const LogicTable control_follower = { follower, (int)(sizeof follower / sizeof *follower) };

const char *control_stance_name(uint8_t stance)
{
    switch (STANCE(stance)) {
    case ST_STAND: return "stand";
    case ST_TURN:  return "turn";
    case ST_BACK:  return "back";
    case ST_WALK:  return "walk";
    case ST_JOG:   return "jog";
    default:       return "?";
    }
}
