/* act - the active character table, LOGICS.INC's `actRecord`.
 *
 * One record per character who is in the world right now: which world-state
 * entry he is, which sheet he wears, and the three things that move him - the
 * joypad (control.h), the AI that may be pressing it (ai.h), and the instance
 * that is standing on the floor (actor.h).
 *
 * This table is what `AICTRL.GAS`'s two master loops walk, and it is why they
 * can be written once rather than once per kind of character:
 *
 *   ControlCode  fills in actJoypad for every record - from the hardware if
 *                this is the player and nothing has taken him over, from
 *                ComputerControl otherwise;
 *   ActionCode   turns each actJoypad into an animation and steps it.
 *
 * `ACTControlled` is the bit that decides which, and it is deliberately not
 * "is he the player": a script that says `freeze` or `chase` sets it, and one
 * that says `release` clears it, so the same bit takes the pad away from the
 * player during a cutscene and gives it back afterwards.
 */
#ifndef HL_ACT_H
#define HL_ACT_H

#include <stdint.h>

#include "actor.h"
#include "control.h"
#include "ai.h"
#include "set.h"

#define ACT_MAX 32

/* actFlags, from LOGICS.INC. */
#define ACT_CREATED     (1u << 0)
#define ACT_CONTROLLED  (1u << 1)
/* Ours, and only ours: this record was put here by `ParseWST` reading the world
 * table, so `ParseACT` and `SCNLOGIC` may take it away again.  The original has
 * no such bit because it has no other way in - every character it ever has
 * comes off the world table.  A harness that stands somebody in a set by hand
 * has, and the two must not be confused: world record 1, Ramirez, ships
 * `WSTDeactivated` and would be culled the moment he was looked at. */
#define ACT_FROM_WORLD  (1u << 8)

typedef struct {
    int         world;          /* the world record he is, -1 if free       */
    int         sheet;          /* his character sheet, -1 if none          */
    int         cast;           /* his model bundle, -1 if none             */
    uint16_t    flags;          /* actFlags                                 */
    uint16_t    status;         /* actStatus: the AI's attack machine, 0
                                   attack, 1 defend, 2 pause                */
    uint8_t     knock;          /* citFlags KNOCKBACK1/2: which way the last
                                   blow came from - 0 back, 2 forward       */
    uint8_t     died;           /* set on the one frame `DeadControl` takes
                                   his radius away, which is where it drops
                                   what he was carrying (collect.h)          */
    uint8_t     picked;         /* citFlags PICKUP: he is standing inside a
                                   collectable's circle right now.  It is set
                                   on the frame he arrives and cleared the
                                   frame he leaves, and it is what stops one
                                   item offering itself twice over            */
    Control     ctl;            /* actJoypad, actAction, actCount, citStance */
    Ai          ai;             /* actAICommand and actAIData1..4           */
    Actor       actor;          /* the CIT: where he is and what he does    */
    const Anim *anim;           /* citAnimate: the one playing, or NULL     */
    const Anim *anims;          /* his bundle's whole bank                  */
    int         nanims;
    int         bank;           /* AICTRL.GAS's `.weapon_action`: the offset
                                   into that bank the logic table's numbers
                                   land at.  Zero is his own animations; a
                                   weapon in his hand moves it to that
                                   weapon's sheet's block of 28, and the
                                   table itself does not change (collect.h) */
    const LogicTable *logic;    /* which joypad table drives him            */
} Act;

typedef struct {
    Act  a[ACT_MAX];
    int  n;
    int  player;                /* the slot the pad drives, -1 if none      */
    long frame;                 /* framecount, which is where the AI's
                                   attack machine gets its rhythm from      */
    int  instpick;              /* COMBAT.GAS's `instpick`: the one
                                   collectable the player has just come
                                   within reach of, as an act slot, or -1.
                                   Set by the pickup half of PPCOLL, read
                                   and cleared by COLLECT (collect.h)        */
} ActTable;

void act_init(ActTable *t);

/* Takes a slot.  Returns its index, or -1 when the table is full.  The record
 * is created but not placed: actor_place puts him on a floor. */
int  act_add(ActTable *t, int world, int sheet, int cast,
             const Anim *anims, int nanims, const LogicTable *logic);

/* Gives a slot back.  `ParseACT` does this to everybody who does not belong to
 * the set being entered, and `acceptobj` to an object that has just gone into
 * a pocket: the record is cleared and the slot is reused by the next act_add,
 * which is the original's own search for a zero entry.  Records after it do
 * not move, so an index held across this call still names the same character. */
void act_free(ActTable *t, int i);

/* The slot holding a given world record, or -1.  This is `select`'s search:
 * the script names a world-state entry and gets back the character table
 * record that is currently being that entry. */
int  act_of_world(const ActTable *t, int world);

/* One game frame over the whole table: ControlCode and then ActionCode, in
 * that order and each complete before the other starts, which is what lets an
 * AI aim at where the player has already got to this frame.  `rawpad` is the
 * hardware pad, in control.h's bit numbering, and `ws` is the live world-state
 * table - where the life points are, which is what decides whether a character
 * gets a joypad at all.  Pass NULL and nobody is ever dead. */
void act_frame(ActTable *t, const Set *s, uint32_t rawpad, uint8_t *ws);

#endif
