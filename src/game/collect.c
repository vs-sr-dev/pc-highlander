#include "collect.h"

#include "game.h"

#include <string.h>

/* ---- the world record, as bytes ------------------------------------- */

static uint8_t *ws_rec(Game *g, int i)
{
    return i >= 0 && i < WS_COUNT ? g->vm.ws + i * WS_REC : NULL;
}

static int ws_get8(Game *g, int i, int off)
{
    const uint8_t *p = ws_rec(g, i);
    return p ? p[off] : 0;
}

static void ws_put8(Game *g, int i, int off, int v)
{
    uint8_t *p = ws_rec(g, i);
    if (p)
        p[off] = (uint8_t)v;
}

static uint32_t ws_get32(Game *g, int i, int off)
{
    const uint8_t *p = ws_rec(g, i);
    if (!p)
        return 0;
    return ((uint32_t)p[off] << 24) | ((uint32_t)p[off + 1] << 16) |
           ((uint32_t)p[off + 2] << 8) | p[off + 3];
}

static void ws_put32(Game *g, int i, int off, uint32_t v)
{
    uint8_t *p = ws_rec(g, i);
    if (!p)
        return;
    p[off]     = (uint8_t)(v >> 24);
    p[off + 1] = (uint8_t)(v >> 16);
    p[off + 2] = (uint8_t)(v >> 8);
    p[off + 3] = (uint8_t)v;
}

static void ws_put16(Game *g, int i, int off, int v)
{
    uint8_t *p = ws_rec(g, i);
    if (!p)
        return;
    p[off]     = (uint8_t)(v >> 8);
    p[off + 1] = (uint8_t)v;
}

/* Who owns record `i`, as a record index, or -1 for nobody. */
static int ws_parent(Game *g, int i)
{
    uint32_t a = ws_get32(g, i, WS_PARENT);
    return a ? ws_owner_of(a) : -1;
}

static int game_bit(const Game *g, int bit)
{
    return (g->vm.gamestate[(bit >> 5) & 31] >> (bit & 31)) & 1;
}

static int player_world(const Game *g)
{
    return g->act.player >= 0 && g->act.player < g->act.n
         ? g->act.a[g->act.player].world : -1;
}

/* ---- the state ------------------------------------------------------ */

void collect_init(Collect *c)
{
    memset(c, 0, sizeof *c);
    c->screen    = COLLECT_CLOSED;
    c->pickup    = -1;
    c->inhand    = -1;
    c->tinhand   = -1;
    c->usingw    = -1;
    c->animsheet = -1;
    c->bank      = 0;
    c->dist      = -600;
}

int collect_modal(const Collect *c)
{
    return c->screen != COLLECT_CLOSED;
}

/* The bank the player's animation numbers land in, pushed onto his own table
 * entry.  Everything that changes what is in his hand goes through here. */
static void collect_hand(Game *g)
{
    if (g->act.player < 0 || g->act.player >= g->act.n)
        return;
    Act *a = &g->act.a[g->act.player];
    a->bank = g->collect.bank;
    if (a->bank + 28 > a->nanims)
        a->bank = 0;                /* a bundle that does not carry that bank */
}

/* ---- taking one out of the world ------------------------------------ */

/* `acceptobj`.  Two stores and a demolition: the record's parent becomes the
 * new owner, and the body it had stops existing - the model comes out of the
 * draw list, the entry comes out of the active character table, and
 * `WSTRegistered` goes off so that nothing puts it back.
 *
 * Nothing is remembered about where it was.  That is what makes the pair of
 * operations clean: an object in a pocket is a world record and nothing else,
 * and dropping it builds a body from scratch. */
int collect_accept(Game *g, int world, int owner)
{
    if (world < 0 || world >= WS_COUNT)
        return 0;
    ws_put32(g, world, WS_PARENT, ws_addr_of(owner));

    int slot = act_of_world(&g->act, world);
    if (slot >= 0)
        act_free(&g->act, slot);
    ws_put8(g, world, WS_FLAGS, ws_get8(g, world, WS_FLAGS) & ~1);  /* WSTRegistered */
    g->collect.taken++;
    return 1;
}

/* ---- putting one back ----------------------------------------------- */

/* `dropit` and `createchar`.  The record takes the *player's* set and x and z
 * - "LETS JUST TRY DROPPING IT AT OUR FEET FIRST", says the file - keeps its
 * own three facing bytes, gets `WSTRegistered`, and is handed to the spawner
 * for a body.
 *
 * The height is the player's `citHeight` plus 8 rather than the player's y:
 * `createchar` reads the floor the player is standing on out of his character
 * instance entry, and the commented-out line beside it shows the author trying
 * the object's own radius there first and settling on a constant. */
int collect_drop(Game *g, int world)
{
    if (world < 0 || world >= WS_COUNT || g->act.player < 0)
        return 0;
    const Act *p = &g->act.a[g->act.player];

    ws_put16(g, world, WS_SET, ws_get8(g, p->world, WS_SET) << 8 |
                               ws_get8(g, p->world, WS_SET + 1));
    ws_put32(g, world, WS_PARENT, 0);
    ws_put32(g, world, WS_XPOS, (uint32_t)p->actor.x);
    ws_put32(g, world, WS_ZPOS, (uint32_t)p->actor.z);
    ws_put32(g, world, WS_YPOS, (uint32_t)(p->actor.ground + 8));
    ws_put8(g, world, WS_FLAGS, ws_get8(g, world, WS_FLAGS) | 1);

    if (g->collect.inhand == world) {
        g->collect.inhand    = -1;
        g->collect.animsheet = -1;
        g->collect.bank      = 0;
        collect_hand(g);
    }
    if (g->collect.tinhand == world)
        g->collect.tinhand = -1;

    int slot = -1;
    if (g->spawn) {
        int sheet = sheets_of_addr(&g->sheets, ws_get32(g, world, WS_SHEET));
        slot = g->spawn(g->spawn_ud, g, world, sheet,
                        sheet >= 0 ? g->sheets.sheet[sheet].behaviour : 0);
        if (slot >= 0 && g->have_set) {
            Act *a = &g->act.a[slot];
            if (!actor_place(&a->actor, &g->set, p->actor.x, p->actor.z,
                             ws_get8(g, world, WS_ZFACE)))
                actor_place(&a->actor, &g->set, p->actor.x, p->actor.z, 0);
            a->actor.radius = (int16_t)((ws_get8(g, world, WS_RADIUS) << 8) |
                                        ws_get8(g, world, WS_RADIUS + 1));
            /* `createchar` sets `PICKUP` on the entry it builds, and this is
             * the one line that says why: he is standing on the thing he has
             * just put down, and without it PPCOLL would offer it straight
             * back to him on the next frame.  He has to walk away from it
             * first.  `DeadControl` builds its entries with the flags word
             * zeroed instead, so what a dead man drops *is* offered at once -
             * which is the difference you would want between the two. */
            a->picked = 1;
            vm_register(&g->vm, world);
        }
    }
    g->collect.dropped++;
    return slot >= 0 || !g->spawn;
}

/* `DeadControl`'s drop.  The original walks the whole world table once, on the
 * frame the dying character's radius goes to zero, and for every record whose
 * parent is him: clears the parent, sets `WSTRegistered`, copies **his** set
 * and his three coordinates onto it, and builds it an entry with `aiNop`.
 *
 * That is the same move as dropping something, aimed at somebody else's feet,
 * and it is how most of the game's items reach the floor: 49 of the 99 start
 * in a pocket, and every one of those pockets belongs to somebody you fight. */
int collect_drop_all(Game *g, int world)
{
    if (world < 0)
        return 0;
    int slot = act_of_world(&g->act, world);
    const Act *dead = slot >= 0 ? &g->act.a[slot] : NULL;
    int n = 0;

    for (int i = 0; i < WS_COUNT; i++) {
        if (i == world || !ws_get32(g, i, WS_SHEET))
            continue;
        if (ws_parent(g, i) != world)
            continue;

        ws_put32(g, i, WS_PARENT, 0);
        ws_put8(g, i, WS_FLAGS, ws_get8(g, i, WS_FLAGS) | 1);
        ws_put16(g, i, WS_SET, (ws_get8(g, world, WS_SET) << 8) |
                                ws_get8(g, world, WS_SET + 1));
        for (int k = 0; k < 12; k++)
            ws_put8(g, i, WS_XPOS + k, ws_get8(g, world, WS_XPOS + k));

        if (g->collect.inhand == i) {
            g->collect.inhand    = -1;
            g->collect.animsheet = -1;
            g->collect.bank      = 0;
            collect_hand(g);
        }
        if (g->spawn) {
            int sheet = sheets_of_addr(&g->sheets, ws_get32(g, i, WS_SHEET));
            int made  = g->spawn(g->spawn_ud, g, i, sheet, AI_NOP);
            if (made >= 0 && g->have_set && dead) {
                Act *a = &g->act.a[made];
                if (!actor_place(&a->actor, &g->set, dead->actor.x,
                                 dead->actor.z, ws_get8(g, i, WS_ZFACE)))
                    actor_place(&a->actor, &g->set, dead->actor.x,
                                dead->actor.z, 0);
                a->actor.radius = (int16_t)((ws_get8(g, i, WS_RADIUS) << 8) |
                                            ws_get8(g, i, WS_RADIUS + 1));
                vm_register(&g->vm, i);
            }
        }
        n++;
    }
    g->collect.from_dead += n;
    return n;
}

/* ---- using one ------------------------------------------------------ */

/* `douse`.  The `wstUsage` byte is a set of independent switches and the
 * routine tests all of them in turn, so one record can do several things.
 * Five of the six implemented bits are here; bit 5 is marked "not
 * implemented" in 1995 and stays that way.
 *
 * Bit 1 is the interesting one, because it does not act at all - it puts a
 * process on the script machine.  `UseItem` is MAINSCRIPT's fourth block, the
 * one nothing in any script spawns, and the engine starts it with the two
 * `wstDword` bytes in r0 and r1: the animation to play and the life points to
 * add.  r2 says whether to restore what was in the hand afterwards, which is
 * bit 3's business.  vm_start is that door, and it was written for this. */
static void douse(Game *g, int world)
{
    Collect *c = &g->collect;
    c->usingw = world;
    if (c->pickup >= 0)
        collect_accept(g, c->pickup, player_world(g));

    int use = ws_get8(g, world, WS_SANITY);      /* wstUsage */
    if (!use)
        return;
    int d0 = ws_get8(g, world, WS_PERSON);       /* wstDword, high byte */
    int d1 = ws_get8(g, world, WS_STR);          /* and low             */

    if (use & USE_IDENTITY) {
        /* The record changes what it *is*: the dword is an offset from the
         * sheet chain's head to the new sheet, and the machine is told through
         * the same event a script's `charchange` posts. */
        g->vm.scriptscene = (uint16_t)((d0 << 8) | d1);
        g->vm.scriptblock = (uint32_t)world;
        g->vm.scriptevent = (uint16_t)(EV_CHARCHANGE + 1);
    }
    if (use & USE_ANIM)
        vm_start(&g->vm, VM_MAIN, 0x10, d0, d1, (use & USE_TEMPHAND) ? 1 : 0);
    if (use & USE_INHAND) {
        c->inhand    = world;
        c->animsheet = -1;                       /* "del animsheet, animsheet = 0" */
        c->bank      = 0;
        collect_hand(g);
    }
    if (use & USE_TEMPHAND)
        c->tinhand = world;
    if (use & USE_BAREHAND) {
        c->animsheet = -1;
        c->bank      = 0;
        collect_hand(g);
    }
    c->used++;
}

/* `chooseit`, and `wpn` behind it.
 *
 * The dummy object - `currws` of zero, which is what the ends of the search
 * leave behind - is the bare hand, and choosing it is the way to put a weapon
 * away.  A record with `WSTWeapon` goes down the weapon path whatever `use`
 * says; anything else is used only if the button was C.  That last test is one
 * line of the original and it is the whole difference between the two buttons:
 * OPTION arrives with the flag clear and skips the `CALL_SUB`. */
void collect_choose(Game *g, int world, int use)
{
    Collect *c = &g->collect;

    if (world >= 0 && world == c->inhand)
        return;                                  /* "same as what we already got!" */

    if (world < 0) {                             /* the bare hand */
        c->animsheet = -1;
        c->bank      = 0;
        c->inhand    = -1;
        c->chosen++;
        collect_hand(g);
        if (c->pickup >= 0)
            collect_accept(g, c->pickup, player_world(g));
        return;
    }

    if (!(ws_get8(g, world, WS_FLAGS) & WST_WEAPON)) {
        if (use)
            douse(g, world);
        return;
    }

    /* `wpn`: point animsheet at the weapon's own sheet and load its
     * animations.  What that comes to in this port is which bank of the
     * player's bundle his table's numbers land in - the arithmetic
     * `--weapon` was doing by hand (docs/15-combat.md 15.3). */
    int sheet = sheets_of_addr(&g->sheets, ws_get32(g, world, WS_SHEET));
    c->animsheet = sheet;
    c->bank      = sheet >= 0 ? sheets_anim_bank(&g->sheets, sheet) : 0;
    c->inhand    = world;
    c->chosen++;
    collect_hand(g);

    if (c->pickup >= 0)
        collect_accept(g, c->pickup, player_world(g));
}

/* ---- the screen ----------------------------------------------------- */

/* `nextobj` and `lastobj`.  Step one record in the given direction from the
 * one on show - or from either end of the table if that is the dummy - and
 * take the first whose parent is the player.  Running off the end is not a
 * wrap: the original loads the *offset* `wstParent` as the address and
 * subtracts it straight back off, which leaves zero, which is the dummy.  So
 * the list is bare hands, then what you have, then bare hands again. */
static int obj_search(Game *g, int dir)
{
    int player = player_world(g);
    int i = g->vm.currws;

    if (i < 0)
        i = dir > 0 ? 0 : WS_COUNT - 1;
    else
        i += dir;

    for (; i >= 0 && i < WS_COUNT; i += dir)
        if (ws_get32(g, i, WS_SHEET) && ws_parent(g, i) == player)
            return i;
    return -1;
}

/* How far back the viewer holds the model: -600, halved by `wstUsage` bit 6
 * and quartered again by bit 7. */
static int32_t view_dist(Game *g, int world)
{
    int32_t d = -600;
    if (world < 0)
        return d;
    int u = ws_get8(g, world, WS_SANITY);
    if (u & USE_NEAR)
        d >>= 1;
    if (u & USE_NEARER)
        d >>= 2;
    return d;
}

static void screen_open(Game *g, int which)
{
    Collect *c = &g->collect;
    c->screen = which;
    c->spin   = 0;
    /* `obj_select`: what is on show is the pickup if there is one, and
     * whatever is in the hand otherwise. */
    g->vm.currws = c->pickup >= 0 ? c->pickup : c->inhand;
    c->dist = view_dist(g, g->vm.currws);
}

static void screen_close(Game *g)
{
    Collect *c = &g->collect;
    c->screen      = COLLECT_CLOSED;
    c->pickup      = -1;
    g->act.instpick = -1;
}

/* One turn of `objloop1`.  The four directions only mean anything when the
 * screen was opened by pressing OPTION: with a pickup on show the code jumps
 * straight past them to the buttons, which is take it or leave it. */
static void screen_step(Game *g, uint32_t now, uint32_t shot)
{
    Collect *c = &g->collect;
    (void)now;

    c->spin += 2;
    c->dist  = view_dist(g, g->vm.currws);

    if (c->pickup < 0) {
        if (shot & (PAD(JOY_UP) | PAD(JOY_LEFT))) {
            g->vm.currws = obj_search(g, -1);
            return;
        }
        if (shot & (PAD(JOY_DOWN) | PAD(JOY_RIGHT))) {
            g->vm.currws = obj_search(g, +1);
            return;
        }
    }

    if (shot & PAD(FIRE_A)) {
        /* Reject the pickup, or drop what is on show.  With a pickup up it is
         * the first: `dropobj` returns without doing anything at all, and the
         * screen closing with `pickup` cleared is the rejection. */
        if (c->pickup < 0 && g->vm.currws >= 0) {
            int w = g->vm.currws;
            g->vm.currws = -1;
            collect_drop(g, w);
        }
        screen_close(g);
        return;
    }
    if (shot & PAD(FIRE_B)) {
        if (c->pickup >= 0)
            collect_accept(g, c->pickup, player_world(g));
        screen_close(g);
        return;
    }
    if (shot & PAD(FIRE_C)) {
        collect_choose(g, g->vm.currws, 1);
        screen_close(g);
        return;
    }
    if (shot & PAD(PAD_OPTION)) {
        collect_choose(g, g->vm.currws, 0);
        screen_close(g);
        return;
    }
    if (shot & (PAD(PAD_STAR) | PAD(PAD_HASH)))
        screen_close(g);
}

/* ---- the module ----------------------------------------------------- */

void collect_frame(Game *g, uint32_t now, uint32_t shot)
{
    Collect *c = &g->collect;

    if (c->screen != COLLECT_CLOSED) {
        screen_step(g, now, shot);
        return;
    }

    /* The script's own pickup, which skips the screen entirely: `pickup #N`
     * has put the record in `currws`, and this bit says take it. */
    if (game_bit(g, WSB_SCRIPT_PICKUP)) {
        collect_choose(g, g->vm.currws, 1);
        g->act.instpick = -1;
        c->pickup = -1;
        return;
    }

    c->usingw = -1;
    c->pickup = g->act.instpick >= 0 && g->act.instpick < g->act.n
              ? g->act.a[g->act.instpick].world : -1;

    if (c->pickup >= 0) {
        c->offered++;
        screen_open(g, COLLECT_PICKUP);
        return;
    }
    if (!game_bit(g, WSB_COLLECT_PREVENT) && (shot & PAD(PAD_OPTION))) {
        screen_open(g, COLLECT_OPTION);
        return;
    }

    g->act.instpick = -1;
}
