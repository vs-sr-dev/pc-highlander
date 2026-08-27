#include "act.h"

#include "sheet.h"

#include <string.h>

void act_init(ActTable *t)
{
    memset(t, 0, sizeof *t);
    t->player   = -1;
    t->instpick = -1;
    for (int i = 0; i < ACT_MAX; i++) {
        t->a[i].world = -1;
        t->a[i].sheet = -1;
        t->a[i].cast  = -1;
    }
}

int act_add(ActTable *t, int world, int sheet, int cast,
            const Anim *anims, int nanims, const LogicTable *logic)
{
    /* The original's `.actloop` walks the table for a zero entry and takes it,
     * so a slot a dropped object or a departed set gave back is the next one
     * used.  `t->n` stays the high-water mark, because that is what every
     * loop over the table counts to. */
    int i = -1;
    for (int k = 0; k < t->n; k++)
        if (!(t->a[k].flags & ACT_CREATED)) {
            i = k;
            break;
        }
    if (i < 0) {
        if (t->n >= ACT_MAX)
            return -1;
        i = t->n++;
    }
    Act *a = &t->a[i];
    memset(a, 0, sizeof *a);
    a->world  = world;
    a->sheet  = sheet;
    a->cast   = cast;
    a->anims  = anims;
    a->nanims = nanims;
    a->logic  = logic;
    a->flags  = ACT_CREATED;
    a->actor.tri = -1;
    control_init(&a->ctl);
    ai_init(&a->ai, AI_NOP);
    return i;
}

void act_free(ActTable *t, int i)
{
    if (i < 0 || i >= t->n)
        return;
    memset(&t->a[i], 0, sizeof t->a[i]);
    t->a[i].world = -1;
    t->a[i].sheet = -1;
    t->a[i].cast  = -1;
    t->a[i].actor.tri = -1;
    if (t->player == i)
        t->player = -1;
}

int act_of_world(const ActTable *t, int world)
{
    if (world < 0)
        return -1;
    for (int i = 0; i < t->n; i++)
        if ((t->a[i].flags & ACT_CREATED) && t->a[i].world == world)
            return i;
    return -1;
}

/* Is the animation he is on still running?  The original keeps this in
 * `citStance`'s FSAPlay bit, maintained by the animation player; here the
 * animation player is actor_step and the frame it left behind says the same
 * thing.  Everything that reads FSAPlay in AICTRL.GAS reads this. */
static int act_playing(const Act *a)
{
    return a->anim && a->actor.frame + 1 < a->anim->frames;
}

/* ---- the two handlers that come before everything else --------------- */

/* `HitControl`.  A character who has just been struck does not get a joypad:
 * he gets *this* joypad, and the logic table turns it into a reaction.  The
 * two knockback bits go in beside it, so which way he was hit from picks which
 * reaction, and the whole of `citStance` above the stance itself is cleared -
 * which is what takes FSAHit back off again, one frame later. */
static void hit_control(Act *a, int alive)
{
    uint32_t joy = PAD(JOY_HIT);
    if (!alive)
        joy |= PAD(JOY_KILL);
    joy |= (uint32_t)(a->knock & 3) << JOY_KNOCK1;
    a->ctl.pad = joy;
    a->ctl.stance = STANCE(a->ctl.stance);
}

/* `DeadControl`.  Nothing is pressed, and once the animation that killed him
 * has run out his radius goes to zero - which is what takes a body out of the
 * collision entirely, since COMBAT.GAS drops any pair where either radius is
 * zero.  The original's own test is the same one: it reads the radius, and a
 * radius already zero means "dead, instead of dying" and there is nothing left
 * to do.  So the whole of the rest of the routine happens exactly once, on
 * that frame, and the rest of it is the drop - every world record whose parent
 * is him becomes a body standing where he fell.  `died` is that one frame;
 * game.c does the dropping, because it is the half that needs the world. */
static void dead_control(Act *a, uint8_t *ws)
{
    a->ctl.pad = 0;
    if (act_playing(a) || !ws || a->world < 0)
        return;
    uint8_t *p = ws + a->world * WS_REC + WS_RADIUS;
    if (p[0] || p[1]) {
        p[0] = p[1] = 0;
        a->actor.radius = 0;
        a->died = 1;
    }
}

/* ActionCode, for one record: choose the animation from the joypad and the
 * stance, turn, and let the animation's own root motion do the moving.  The
 * player and an AI character go through exactly this, which is the point. */
static void act_action(Act *a, const Set *s)
{
    int fps     = a->anim && a->anim->fps ? a->anim->fps : 20;
    int frate   = a->anim && a->anim->fps ? 256 / a->anim->fps : 12;
    int playing = act_playing(a);

    /* A locked character is skipped by ActionCode and by nothing else: the
     * animation player is its own module in the original and runs for
     * everybody, which is exactly what a lock is for - the animation goes on
     * to its end without the table being asked again. */
    if (a->ctl.stance & FSA_LOCK) {
        if (a->anim)
            actor_step(&a->actor, s, a->anim, frate);
        return;
    }

    control_action(&a->ctl, a->logic, playing);
    if (a->ctl.restarted) {
        /* The table names an animation; which one that *is* depends on what
         * he is holding.  `.weapon_action` looks the number up in `animsheet`
         * before his own sheet, and since the player carries every bank in
         * one bundle that comes to adding an offset. */
        int k = a->ctl.anim + a->bank;
        if (k >= 0 && k < a->nanims) {
            const Anim *pick = &a->anims[k];
            if (pick != a->anim) {
                a->anim = pick;
                fps   = pick->fps ? pick->fps : 20;
                frate = pick->fps ? 256 / pick->fps : 12;
            }
            /* Start it again from the top: the next step wraps to frame zero,
             * which is also where the accumulated lift resets. */
            a->actor.frame = a->anim->frames - 1;
        }
    }
    a->actor.facing = (uint8_t)(a->actor.facing + control_turn(&a->ctl, fps));
    if (a->anim)
        actor_step(&a->actor, s, a->anim, frate);
}

void act_frame(ActTable *t, const Set *s, uint32_t rawpad, uint8_t *ws)
{
    t->frame++;

    const Act   *pl     = t->player >= 0 && t->player < t->n
                          ? &t->a[t->player] : NULL;
    const Actor *player = pl ? &pl->actor : NULL;

    /* ControlCode: the joypad for everybody, before anybody moves.
     *
     * The order of the tests is the original's and it matters.  Being hit
     * comes first, so a blow interrupts whatever you were doing - including
     * your own swing.  Then a locked animation, which nothing may disturb.
     * Then being dead.  Then ACTControlled, which wins over being the player,
     * so a script can drive him through the AI commands and hand him back by
     * clearing the bit.  Only then does the pad get a say. */
    for (int i = 0; i < t->n; i++) {
        Act *a = &t->a[i];
        if (!(a->flags & ACT_CREATED))
            continue;
        int life = ws && a->world >= 0 ? ws[a->world * WS_REC + WS_LIFE] : 1;

        if (a->ctl.stance & FSA_HIT) {
            hit_control(a, life != 0);
            continue;
        }
        if (a->ctl.stance & FSA_LOCK) {
            /* A locked character is skipped by both loops in the original,
             * and nothing in AICTRL.GAS ever unlocks him: the script's own
             * `animate` sets the bit and `waitanim` polls ANIMEND, so the end
             * of the animation is what the lock is waiting for.  This port
             * takes that literally and clears it there. */
            if (!act_playing(a))
                a->ctl.stance &= (uint8_t)~FSA_LOCK;
            continue;
        }
        if (life == 0) {
            dead_control(a, ws);
            continue;
        }
        if (i == t->player && !(a->flags & ACT_CONTROLLED)) {
            control_pad(&a->ctl, rawpad);
            continue;
        }

        /* ComputerControl.  The Person commands name another world entry
         * through AIData1; resolving it here keeps ai.c free of the table. */
        int p = act_of_world(t, a->ai.data1);
        const Act *person = p >= 0 ? &t->a[p] : NULL;
        const Act *aim = ai_person_command(a->ai.command) ? person : pl;

        AiWorld w;
        memset(&w, 0, sizeof w);
        w.player      = player;
        w.person      = person ? &person->actor : NULL;
        w.target_pad  = aim ? aim->ctl.pad : 0;
        w.target_face = aim ? aim->actor.facing : 0;
        w.prev_pad    = a->ctl.pad;
        w.stance      = (uint8_t)(a->ctl.stance | (act_playing(a) ? FSA_PLAY : 0));
        w.status      = &a->status;
        a->ctl.pad = ai_control(&a->ai, &a->actor, &w);
    }

    /* ActionCode: and only now does anybody move. */
    for (int i = 0; i < t->n; i++) {
        Act *a = &t->a[i];
        if (!(a->flags & ACT_CREATED) || a->actor.tri < 0)
            continue;
        act_action(a, s);
    }
}
