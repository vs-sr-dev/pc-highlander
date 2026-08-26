#include "game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MAINSCRIPT is not on a data track: it sits in the retail binary right behind
 * the script VM's own GPU module and runs to the next module header.
 * docs/11-script-vm.md 11.1. */
#define MAIN_MEM  0x2FEB8
#define MAIN_END  0x30130
#define MAIN_FILE (MAIN_MEM - 0x5000 + 0x12600)
#define WS_FILE   (0x15458 - 0x5000 + 0x12600)

int game_open(Game *g, const char *track3, const char *track2)
{
    memset(g, 0, sizeof *g);
    snprintf(g->track3, sizeof g->track3, "%s", track3);
    g->want_scene  = -1;
    g->want_sample = -1;
    act_init(&g->act);

    if (!sheets_load(&g->sheets, track2))
        return 0;
    g->boot = io_read(track2);
    if (!g->boot.data) {
        sheets_free(&g->sheets);
        return 0;
    }
    vm_init(&g->vm, g->boot.data + WS_FILE, g->boot.data + MAIN_FILE,
            MAIN_END - MAIN_MEM);
    g->vm.act = &g->act;
    return 1;
}

void game_close(Game *g)
{
    if (g->have_set)
        set_free(&g->set);
    io_free(&g->boot);
    sheets_free(&g->sheets);
}

/* ---- arriving ------------------------------------------------------- */

/* One half of a doorway: the arrival this set lists for the view being left.
 * Every id that has one has it twice, once for each character, and 41 of the
 * disc's 129 cross-set cuts fall back on the `$FFFF` default instead.
 * docs/10-set-track.md 10.3. */
static const SetEntry *entry_for(const Set *s, uint16_t from_id, int companion)
{
    const SetEntry *door = NULL, *any = NULL;
    for (int k = 0; k < s->nentries; k++) {
        const SetEntry *e = &s->entry[k];
        if (ENTRY_COMPANION(e) != (companion ? 1 : 0))
            continue;
        if (e->id == from_id)            door = e;
        else if (e->id == 0xFFFF && !any) any  = e;
    }
    return door ? door : any;
}

/* Nowhere in the init table names him.  A third of the disc's views list no
 * arrival at all, and on the way into a set for the first time there is no view
 * being left either, so this is the fallback - and it is ours: the original
 * never needs one, because a script has always put a character somewhere
 * before the set is entered.
 *
 * The player takes any arrival point the set has, since those are places the
 * game itself puts people; failing that the middle of the first triangle.  The
 * companion stands one follow_range behind the player, which is where
 * following would have left him. */
static void place_spare(Game *g, Act *a, int companion)
{
    const Actor *p = g->act.player >= 0 ? &g->act.a[g->act.player].actor : NULL;

    if (companion && p) {
        double k = 2.0 * 3.14159265358979323846 / 256.0;
        int32_t bx = p->x - (int32_t)(AI_FOLLOW_RANGE * sin(p->facing * k));
        int32_t bz = p->z - (int32_t)(AI_FOLLOW_RANGE * cos(p->facing * k));
        if (actor_place(&a->actor, &g->set, bx, bz, p->facing))
            return;
        if (actor_place(&a->actor, &g->set, p->x, p->z, p->facing))
            return;
    }
    if (a->actor.tri >= 0 &&
        actor_place(&a->actor, &g->set, a->actor.x, a->actor.z, a->actor.facing))
        return;
    for (int k = 0; k < g->set.nentries; k++)
        if (!ENTRY_COMPANION(&g->set.entry[k]) &&
            actor_place(&a->actor, &g->set, g->set.entry[k].x,
                        g->set.entry[k].z, ENTRY_FACING(&g->set.entry[k])))
            return;
    if (g->set.ntris > 0) {
        const SetTri *tr = &g->set.tri[0];
        int64_t cx = 0, cz = 0;
        for (int k = 0; k < 3; k++) {
            cx += g->set.vert[tr->vert[k]].x;
            cz += g->set.vert[tr->vert[k]].z;
        }
        actor_place(&a->actor, &g->set, (int32_t)(cx / 3), (int32_t)(cz / 3),
                    a->actor.facing);
    }
}

int game_enter(Game *g, uint16_t scene_id, int set_hint)
{
    uint16_t from_id = g->scene;
    g->entered_set = 0;

    int si = set_hint;
    if (si < 0 || si >= SET_COUNT)
        si = set_of_scene(g->track3, scene_id);
    if (si < 0)
        return 0;

    if (!g->have_set || si != g->set.index) {
        Set next;
        if (!set_load(&next, g->track3, si))
            return 0;
        if (g->have_set)
            set_free(&g->set);
        g->set = next;
        g->have_set = 1;
        g->entered_set = 1;
        g->vm.set = &g->set;

        /* A new set is a new script.  Changing it is what drops the old set's
         * processes and starts this one's; the resident ones carry on. */
        g->vm.curset = (uint16_t)si;
        vm_set_script(&g->vm, si, g->set.script, g->set.script_len);

        /* The two sets do not share an origin, so nobody is carried across:
         * everybody is put down again from the init table.  The player goes
         * first, because where the companion ends up may depend on him. */
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < g->act.n; i++) {
                Act *a = &g->act.a[i];
                int companion = i != g->act.player;
                if (!(a->flags & ACT_CREATED) || companion != pass)
                    continue;
                const SetEntry *e = entry_for(&g->set, from_id, companion);
                if (e && actor_place(&a->actor, &g->set, e->x, e->z,
                                     ENTRY_FACING(e)))
                    continue;
                place_spare(g, a, companion);
            }
        }
        for (int i = 0; i < g->act.n; i++)
            if (g->act.a[i].world >= 0)
                vm_register(&g->vm, g->act.a[i].world);
    }

    g->scene    = scene_id;
    g->vm.scene = scene_id;
    g->last_cut_from = from_id;
    return 1;
}

/* ---- the set's own events ------------------------------------------- */

/* A collision circle on the floor with a type and a condition.  The engine
 * clears the count when you are outside one and fires on the edge going in,
 * except for type 0 - the camera cut - which fires while you are inside it.
 *
 * `eventmode` is the mask a script can close: `eventmask $0000` turns the whole
 * lot off for the length of a cutscene and `eventmask $FFFF` puts them back,
 * which is what MAINSCRIPT's item handler does around an animation. */
static int events_scan(Game *g, const Actor *act)
{
    int cut = -1;
    for (int i = 0; i < g->set.nevents; i++) {
        SetEvent *e = &g->set.event[i];
        if (e->scene != 0xFFFF && e->scene != g->scene)
            continue;
        if (!((g->vm.eventmask >> (e->type & 31)) & 1))
            continue;
        int colliding = 1;
        if (e->height != 0 && act->y > e->height)
            colliding = 0;
        if (colliding && e->radius2 != 0) {
            int64_t dx = (int64_t)e->x - act->x, dz = (int64_t)e->z - act->z;
            colliding = dx * dx + dz * dz < labs((long)e->radius2);
        }
        if (!colliding) {
            e->status &= (uint16_t)~0x3FFF;
            continue;
        }
        int was_in = e->status & 1;
        e->status |= 1;
        if (e->type != 0 && was_in)
            continue;                           /* everything else is an edge */
        if (e->type == 0 && cut < 0 && e->data[0] != g->scene)
            cut = e->data[0];
    }
    return cut;
}

/* ---- the frame ------------------------------------------------------ */

void game_frame(Game *g, uint32_t rawpad)
{
    g->frame++;
    if (!g->have_set)
        return;

    /* 1. The script machine. */
    g->vm.pad = rawpad;
    vm_frame(&g->vm);

    /* 2. What it posted.  The machine will not run another command until the
     * event is cleared, which is how a script waits for a film to finish or a
     * camera to move.  Everything that needs a screen or the disc is handed
     * out here and cleared, so a script never stalls on something this port
     * cannot do yet. */
    if (g->vm.scriptevent) {
        switch (g->vm.scriptevent - 1) {
        case EV_SCENE:
            g->want_scene = g->vm.scriptscene;
            break;
        case EV_CINEPAK:
            g->want_film = g->vm.scriptblock;
            break;
        case EV_CDAUDIO:
            g->want_redbook = g->vm.scriptblock;
            g->vm.redbook_done = 1;
            break;
        case EV_SOUND:
            g->want_sample = g->vm.scriptscene;
            break;
        default:
            break;
        }
        g->vm.scriptevent = 0;
    }
    if (g->vm.gameover) {
        g->gameover = 1;
        g->vm.gameover = 0;
    }

    /* 3. Everybody's frame: the joypad for all of them, and only then the
     * animation that moves them. */
    act_frame(&g->act, &g->set, rawpad);

    /* 4. The floor's own event lines.  Only the player crosses them - the
     * companion walking over a doorway does not take you through it. */
    if (g->act.player >= 0) {
        int cut = events_scan(g, &g->act.a[g->act.player].actor);
        if (cut >= 0 && g->want_scene < 0)
            g->want_scene = cut;
    }
}
