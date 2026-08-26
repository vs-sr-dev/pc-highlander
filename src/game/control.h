/* control - AICTRL.GAS: the joypad, and the animation it chooses.
 *
 * The original never moves the player.  It reads the pad, picks an animation
 * from a table, and the animation's own root motion does the walking - which
 * is why `--walk` and `--play` were two different things in the viewer and why
 * wiring them together is what turns it into a game.  Two loops of AICTRL.GAS
 * do it, and both are reproduced here.
 *
 * `PlayerControl` turns the hardware pad into `actJoypad`.  Two things happen
 * on the way, and neither is obvious from the pad alone:
 *
 *   - `actCount` counts the frames since JOY_UP was last held, and while that
 *     count is 1, 2 or 3 the code *forces JOY_UP back on*.  Letting go of
 *     forward for under a fifth of a second does not stop the walk.
 *   - if the pad is pressed again inside that window, JOY_DOUBLE is set as
 *     well.  A double tap is how you run, and it is the pad handler rather
 *     than the animation table that detects it.
 *
 * `ActionCode` then walks a *logic table*: a list of tuples, each headed by a
 * joypad mask, the first tuple all of whose bits are held winning.  The
 * matched tuple holds eight entries and the low three bits of `citStance`
 * choose between them, so what a button does depends on what the character is
 * already doing.  Each entry names an animation and the stance to move to.
 * The animation restarts when the masked pad changes, when the previous one
 * has finished, or when the entry names a different one.
 *
 * Turning is separate and additive: JOY_LEFT and JOY_RIGHT turn `citFacing` by
 * `4 * 20 / framerate` steps of the 256-step circle - four a frame at the
 * animations' own 20 fps - unless the new stance has FSATurn, which means the
 * animation is turning by itself.  Quentin's turn-on-the-spot animation turns
 * 62 steps over 15 frames, which is that same four a frame: the two rates are
 * the same rate, and that is what lets a character hand the turn to the
 * animation without the speed changing.
 *
 * What is the original's and what is ours.  The mechanism above, the stance
 * bits, the rotate rate, the double-tap window and `slowtowalk` are
 * AICTRL.GAS's.  The *table* is ours: the real ones are five CD files named in
 * DATA.INC (`BO_LOGICS_1` "full", `2A` "stand & walk", `2B` "stand & walk - no
 * turn anims", `3A` "stand", `3B` "stand - no turn anims") loaded through the
 * character sheet's file list, and where the retail disc put them is not
 * known.  docs/14-characters.md 14.9.
 */
#ifndef HL_CONTROL_H
#define HL_CONTROL_H

#include <stdint.h>

/* The pad.  Bits 24 and up are the original's own numbers, from LOGICS.INC;
 * the seven hardware bits below them are ours, because the Jaguar include that
 * numbered them is not in the source dump. */
#define JOY_UP      0
#define JOY_DOWN    1
#define JOY_LEFT    2
#define JOY_RIGHT   3
#define FIRE_A      4
#define FIRE_B      5
#define FIRE_C      6
#define JOY_DOUBLE  24          /* LOGICS.INC */
#define JOY_KNOCK1  26
#define JOY_KNOCK2  27
#define JOY_HIT     30
#define JOY_KILL    31

#define PAD(b) (1u << (b))

/* citStance: the low three bits are the stance, the top five are flags.
 * FSATurn..FSAHit are LOGICS.INC's numbers. */
#define FSA_TURN    (1u << 3)
#define FSA_PLAY    (1u << 4)
#define FSA_LOCK    (1u << 5)
#define FSA_SHIELD  (1u << 6)
#define FSA_HIT     (1u << 7)
#define STANCE(s)   ((s) & 7)

/* The eight stances.  Only two of the eight numbers survive in the source:
 * PlayerControl's `slowtowalk` block names 4 as the jog and 3 as the walk when
 * it rewrites one into the other.  The rest are ours. */
#define ST_STAND    0
#define ST_TURN     1
#define ST_BACK     2
#define ST_WALK     3
#define ST_JOG      4
#define ST_HIT      5           /* ours: reeling from a blow                */
#define ST_DEAD     6           /* ours: on the floor                       */
#define ST_COUNT    8

/* One entry of a logic tuple: an animation, and the stance to go to. */
typedef struct { int16_t anim; uint8_t stance; } LogicEntry;

/* One tuple: the mask that has to be held, and one entry per stance. */
typedef struct { uint32_t mask; LogicEntry e[8]; } LogicTuple;

/* A logic table is a tuple list ending in one whose mask is zero, which is
 * what makes the search terminate - the original does not bound it either. */
typedef struct { const LogicTuple *tuple; int count; } LogicTable;

typedef struct {
    uint32_t pad;               /* actJoypad, after PlayerControl        */
    uint32_t action;            /* actAction: the masked pad the running
                                   animation was chosen for              */
    int32_t  count;             /* actCount: frames since JOY_UP was held */
    uint8_t  stance;            /* citStance                             */
    int      anim;              /* citAnimate, as an index into the
                                   bundle's animation list, -1 for none  */
    int      restarted;         /* set on the frame the animation changed */
    int      slowtowalk;        /* the global byte of the same name      */
} Control;

void control_init(Control *c);

/* PlayerControl.  `raw` is this frame's hardware pad; the result is stored in
 * c->pad and returned. */
uint32_t control_pad(Control *c, uint32_t raw);

/* ActionCode.  Picks the animation and the stance from `t`, and reports
 * through c->restarted whether the animation has to start again.  `playing` is
 * whether the animation last chosen has frames left to run: the original keeps
 * it in citStance's FSAPlay bit and the caller is the only one who knows. */
void control_action(Control *c, const LogicTable *t, int playing);

/* The turn.  Returns the change to add to the facing - `4 * 20 / fps` steps
 * per frame in the direction held, or zero when nothing is held, when the
 * stance says the animation is turning, or when a wall is being run along and
 * this is not the player. */
int  control_turn(const Control *c, int fps);

/* The table this port drives Quentin with, and what each animation in it is.
 * `control_name` is for the log line, so a run can be read. */
extern const LogicTable control_quentin;

/* And the one it drives a follower with: four animations rather than thirty,
 * which is what a sheet that loads BO_LOGICS_2A - "stand & walk" - has to
 * work from. */
extern const LogicTable control_follower;
const char *control_stance_name(uint8_t stance);

#endif
