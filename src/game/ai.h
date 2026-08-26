/* ai - AICTRL.GAS's other half: the joypad a character who is not the player
 * presses.
 *
 * The engine's master loop runs twice over the active characters.  The first
 * pass, `ControlCode`, fills in `actJoypad` for each of them - through
 * `PlayerControl` for the one the pad is driving (control.c) and through
 * `ComputerControl` for everybody else.  The second pass, `ActionCode`, turns
 * that joypad into an animation, and it does not care which pass wrote it.
 * So an AI character is not a special case anywhere below this file: he
 * presses buttons, and the same table lookup, the same turn and the same root
 * motion move him.
 *
 * `ComputerControl` is a jump through `ControlCodeTable`, indexed by
 * `actAICommand` - the fourteen commands LOGICS.INC numbers.  Each is built
 * from three pieces: something that says where the target is (a position, a
 * person, or the player), `AIRotateCode`, which turns towards it, and one of
 * `AIFaceCode` / `AIGotoCode` / `AIFollowCode` / `AIAttackCode`, which decides
 * whether to walk as well.
 *
 * Where a character's command comes from is his sheet: `cshBehaviour`
 * (sheet.h), which `AIDefault` copies into `actAICommand`.  Ramirez's sheet
 * says aiFollowPlayer, and that one word is the whole of "the companion
 * follows you".
 */
#ifndef HL_AI_H
#define HL_AI_H

#include <stdint.h>

#include "actor.h"

/* LOGICS.INC's AI command numbers, which are also the order of
 * `ControlCodeTable`. */
#define AI_NOP              0
#define AI_GOTO_POSITION    1
#define AI_GOTO_PERSON      2
#define AI_FACE_POSITION    3
#define AI_FACE_PERSON      4
#define AI_ATTACK_PERSON    5
#define AI_ATTACK_PLAYER    6
#define AI_FACE_PLAYER      7
#define AI_GOTO_PLAYER      8
#define AI_FOLLOW_PLAYER    9
#define AI_SHOOT_PERSON    10
#define AI_SHOOT_PLAYER    11
#define AI_DEFAULT         12
#define AI_FOLLOW_PERSON   13

/* The ranges, in the units the source writes them in - `1000/4` per metre,
 * which puts Quentin's 414-unit height at 1.66 m. */
#define AI_POSITION_RANGE  (1000/4*1/4)         /*  0.25 m */
#define AI_PERSON_RANGE    (1000/4*5/4)         /*  1.25 m */
#define AI_FOLLOW_RANGE    (1000/4*5/2)         /*  2.50 m */
#define AI_SENTRY_RANGE    (1000/4*20)          /* 20.00 m */
#define AI_MELEE_RANGE     (1000/4*2)           /*  2.00 m */
#define AI_MISSILE_RANGE   (1000/4*5)           /*  5.00 m */

typedef struct {
    int      command;           /* actAICommand                             */
    int32_t  data1, data2;      /* actAIData1 and 2: a position for the
                                   Position commands; for the Person ones
                                   the first is which character             */
    int      behaviour;         /* the sheet's cshBehaviour, which aiDefault
                                   loads back into the command              */
    uint32_t seed;              /* reg1, the running random word            */
    int32_t  dist2;             /* how far the target was, last frame       */
} Ai;

void ai_init(Ai *ai, int behaviour);

/* AIRotateCode's arctan: the facing, on the 256-step circle, that looks from
 * the origin towards (dx, dz).  Facing 0 is +z and 64 is +x, which is the
 * convention the walk already uses. */
uint8_t ai_angle(int32_t dx, int32_t dz);

/* Everything `ComputerControl` can see besides the character himself.  Most
 * of it is for the attack machine: closing to melee range needs only the
 * target's position, but *fighting* needs what he is pressing this frame, how
 * he is standing, and the engine's frame counter, which is where the original
 * gets its rhythm from instead of a random number. */
typedef struct {
    const Actor *player;        /* who the Player commands aim at, or NULL  */
    const Actor *person;        /* the target of the Person commands        */
    uint32_t     target_pad;    /* that target's own joypad, this frame     */
    uint32_t     prev_pad;      /* what this character pressed last frame   */
    uint8_t      stance;        /* citStance: FSAPlay and FSATurn gate it   */
    uint8_t      target_face;   /* the target's own facing                  */
    uint16_t    *status;        /* actStatus, which the machine advances    */
} AiWorld;

/* `ComputerControl`: the joypad this character presses this frame. */
uint32_t ai_control(Ai *ai, const Actor *self, const AiWorld *w);

/* Does this command aim at `person` rather than at the player?  The caller
 * has the character table and resolves the target; this says which one. */
int  ai_person_command(int command);

const char *ai_command_name(int command);

#endif
