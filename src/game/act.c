#include "act.h"

#include <string.h>

void act_init(ActTable *t)
{
    memset(t, 0, sizeof *t);
    t->player = -1;
    for (int i = 0; i < ACT_MAX; i++) {
        t->a[i].world = -1;
        t->a[i].sheet = -1;
        t->a[i].cast  = -1;
    }
}

int act_add(ActTable *t, int world, int sheet, int cast,
            const Anim *anims, int nanims, const LogicTable *logic)
{
    if (t->n >= ACT_MAX)
        return -1;
    Act *a = &t->a[t->n];
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
    return t->n++;
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

/* ActionCode, for one record: choose the animation from the joypad and the
 * stance, turn, and let the animation's own root motion do the moving.  The
 * player and an AI character go through exactly this, which is the point. */
static void act_action(Act *a, const Set *s)
{
    int fps     = a->anim && a->anim->fps ? a->anim->fps : 20;
    int frate   = a->anim && a->anim->fps ? 256 / a->anim->fps : 12;
    int playing = a->anim && a->actor.frame + 1 < a->anim->frames;

    control_action(&a->ctl, a->logic, playing);
    if (a->ctl.restarted) {
        int k = a->ctl.anim;
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

void act_frame(ActTable *t, const Set *s, uint32_t rawpad)
{
    const Actor *player = t->player >= 0 && t->player < t->n
                          ? &t->a[t->player].actor : NULL;

    /* ControlCode: the joypad for everybody, before anybody moves.
     *
     * Note which test comes first.  ACTControlled wins over being the player,
     * so a script can drive the player through the AI commands and hand him
     * back by clearing the bit.  HitControl and DeadControl sit ahead of both
     * in the original and are combat, so they are not here yet. */
    for (int i = 0; i < t->n; i++) {
        Act *a = &t->a[i];
        if (!(a->flags & ACT_CREATED))
            continue;
        if (i == t->player && !(a->flags & ACT_CONTROLLED)) {
            control_pad(&a->ctl, rawpad);
        } else {
            /* The Person commands name another world entry through AIData1;
             * resolving it here keeps ai.c free of the table. */
            int p = act_of_world(t, a->ai.data1);
            const Actor *person = p >= 0 ? &t->a[p].actor : NULL;
            a->ctl.pad = ai_control(&a->ai, &a->actor, player, person);
        }
    }

    /* ActionCode: and only now does anybody move. */
    for (int i = 0; i < t->n; i++) {
        Act *a = &t->a[i];
        if (!(a->flags & ACT_CREATED) || a->actor.tri < 0)
            continue;
        act_action(a, s);
    }
}
