/* collect - COLLECT.GAS, which is the inventory: what you are carrying, what
 * is in your hand, and the screen that lets you change either.
 *
 * It is one GPU module like the rest, and `MAIN.S` runs it **last** - after
 * `ANIMCODE`, `PPCOLL`, `ENGINE`, `BITMAP` and `TEXTS`, as the final thing in
 * the frame.  That position is the design: when it opens a screen it spins its
 * own loop, drawing its own picture and reading the pad itself, and the game
 * behind it does not advance until a button ends it.  `collect_modal` is that,
 * and `game_frame` honours it.
 *
 * **There is no inventory list.**  A world record's `wstParent` is who owns
 * it, and carrying something *is* that field pointing at you.  So the screen's
 * left and right are a search through the 256 world records for the next one
 * whose parent is the player, the ends of the table are the bare hand rather
 * than a wrap, and giving an object away is one store.  The disc ships with
 * **49 of the 99 collectables already owned**, all of them by characters
 * rather than by Quentin - which is what makes `DeadControl`'s drop
 * (`collect_drop_all`) the way most of them enter the game.
 *
 * The way in is `instpick`, which `PPCOLL` writes (combat.h): the one
 * collectable the player has this frame stepped inside the radius of.  From
 * there COLLECT.GAS's own comment block is the specification, and the five
 * buttons are its five lines:
 *
 *     left/right - change 'current' by searching thru ws for next/last "owned"
 *     Reject pickup OR Drop 'current' and end - A
 *     Accept any pickup object and end - B
 *     Choose 'current' as inhand, accept any pickup and end - C
 *
 * with OPTION doing what C does but without *using* what it chose, and `#` or
 * `*` leaving.  When the screen was opened by walking onto something the four
 * directions are dead: a pickup is take it or leave it.
 *
 * Taking one is `wstParent` plus a despawn - the body comes out of the active
 * character table and `WSTRegistered` goes off.  Dropping one is the reverse
 * and a little more: `dropit` copies the *player's* set and position onto the
 * record and `createchar` builds it a fresh entry, which is the same code path
 * `DeadControl` takes for every object a dying character was carrying.  An
 * item therefore has exactly two states, in the world or in a pocket, and the
 * world half is rebuilt from the world record every time.
 *
 * And `chooseit` is what makes a weapon a weapon.  A record with `WSTWeapon`
 * set points `animsheet` at its own character sheet, and from then on the
 * player's animation numbers are looked up there before his own - which is
 * `AICTRL.GAS`'s `.weapon_action`, and the thing `--weapon` was faking.
 * Anything without the flag is *used* instead (`douse`), and what using means
 * is the `wstUsage` byte, whose bits COLLECT.GAS lists in a comment.
 *
 * docs/16-inventory.md.
 */
#ifndef HL_COLLECT_H
#define HL_COLLECT_H

#include <stdint.h>

struct Game;

/* Which screen is up, if any. */
#define COLLECT_CLOSED  0
#define COLLECT_PICKUP  1       /* opened by walking onto something         */
#define COLLECT_OPTION  2       /* opened by pressing OPTION                */

/* The first two world-state bits in the game, and they are both this file's:
 * `WSB.INC` includes `ROBSWSB.INC` before anything else and it declares
 * exactly these two.  `COLLECT_PREVENT` is "when set you can't pick anything
 * up using option button" and `SCRIPT_PICKUP` is "new one to let me pick
 * things up automatically" - the second one paired with the script command
 * `pickup`, which names a record and puts it straight into the hand with no
 * screen at all. */
#define WSB_COLLECT_PREVENT 0
#define WSB_SCRIPT_PICKUP   1

/* `wstUsage` - which is `wstSanity` on anything you can pick up, since a loaf
 * of bread has no sanity - and its bits, from COLLECT.GAS's own list:
 *
 *     0 - identity change item (dword = offset from cs of charsht)
 *     1 - anim script use item (dword = anim num, +hp)
 *     2 - item put in hand now
 *     3 - put item in temp - restore 'weapon' item after anim
 *     4 - change to barehand weapon (+animsheet) now
 *     5 - special script use item      * not implemented *
 *     6 - x4 size
 *     7
 *
 * Bit 7 the comment leaves blank, and the code says what it is: bit 6 halves
 * the distance the inventory screen holds the model at and bit 7 quarters it
 * again, so the two together are how close to bring a small thing.  **88 of
 * the 99 collectables set bit 7** and 23 set bit 6, which is the right shape
 * for "most of what you carry is small".
 *
 * `wstDword` is `wstPerson` and the byte after it, which on a person are the
 * personality and the strength.  An item is never in a fight - PPCOLL drops
 * the pair before it reads either - so the two bytes are free, and on the 63
 * food items they hold the animation to play and the life points to add. */
#define USE_IDENTITY    (1u << 0)
#define USE_ANIM        (1u << 1)
#define USE_INHAND      (1u << 2)
#define USE_TEMPHAND    (1u << 3)
#define USE_BAREHAND    (1u << 4)
#define USE_SCRIPT      (1u << 5)
#define USE_NEAR        (1u << 6)
#define USE_NEARER      (1u << 7)

typedef struct {
    int  screen;                /* COLLECT_*                                */
    int  pickup;                /* the record under his feet, or -1         */
    int  inhand;                /* the record in his hand, or -1            */
    int  tinhand;               /* the one a `douse` swapped in, or -1      */
    int  usingw;                /* what `douse` last used, or -1            */
    int  animsheet;             /* the sheet his animations come from, -1
                                   for his own.  AICTRL.GAS's `.weapon_action` */
    int  bank;                  /* where that sheet's animations begin in his
                                   bundle: the thing --weapon used to set    */

    /* The screen itself, which is one model on a turntable.  `dist` is
     * COLLECT.GAS's own -600 shifted right by the two `wstUsage` bits. */
    int32_t dist;
    int     spin;

    /* Counters, so a run can be read and checked. */
    long taken, dropped, chosen, used, offered, from_dead;
} Collect;

void collect_init(Collect *c);

/* Is the game frozen behind a screen? */
int  collect_modal(const Collect *c);

/* COLLECT.GAS, run where MAIN.S runs it: the last thing in the frame.
 * `shot` is the pad's rising edges, which is what `pad_shot` holds. */
void collect_frame(struct Game *g, uint32_t now, uint32_t shot);

/* `DeadControl`'s second half: everything `world` was carrying stops being
 * carried and becomes a body standing where he fell.  Returns how many.  The
 * original runs the whole 512-record table for this, once, on the frame the
 * radius goes to zero. */
int  collect_drop_all(struct Game *g, int world);

/* Puts `world` into the player's hands as if he had chosen it off the screen:
 * a weapon points `animsheet` at its sheet, anything else is used.  This is
 * `chooseit`, and it is also the whole of the `pickup` script command once
 * `WSB_SCRIPT_PICKUP` has routed it here.  `use` is the difference between C
 * and OPTION - both choose, only C uses. */
void collect_choose(struct Game *g, int world, int use);

/* The two halves of the world/pocket move, exposed because the checks drive
 * them directly and because `giveobj` needs the first. */
int  collect_accept(struct Game *g, int world, int owner);
int  collect_drop(struct Game *g, int world);

#endif
