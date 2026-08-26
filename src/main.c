/* hlview - the phase 3 viewer.
 *
 * It shows one of the 672 pre-rendered backdrops, or its Z-buffer, and
 * composites a model extracted from the disc into it, depth-tested against
 * that Z-buffer.  Everything it reads is a track as jcdinfo extracts it, plus
 * assets/manifest.json for the names.
 *
 *   hlview --scene CA_CAM03
 *   hlview --scene CA_CAM06 --object CA_WINE
 *   hlview --model boot:6 --spin
 *   hlview --scene CA_CAM03 --depth --shot z.ppm --no-window
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "util/io.h"
#include "util/json.h"
#include "game/scene.h"
#include "game/model.h"
#include "game/anim.h"
#include "game/control.h"
#include "game/actor.h"
#include "game/set.h"
#include "game/sheet.h"
#include "game/ai.h"
#include "game/act.h"
#include "script/vm.h"
#include "r3d/r3d.h"
#include "platform/window.h"

typedef struct {
    const char *tracks;
    const char *manifest;
    const char *scene;
    const char *model;
    const char *object;
    const char *shot;
    int   shot_at;
    const char *pad;
    int   no_window, depth, spin, wire, shade, scale, frames;
    int   mesh, no_ground;
    int   character, anim, frame, play, walk, events, drive;
    int   have_pos, pos[3];
    int   have_face, face[3];
    R3dCull cull;
    int   list_scenes, list_models, list_objects, list_chars, list_sheets;
    int   alone;
} Args;

static char path_pict[512], path_boot[512], path_t5[512], path_set[512];

static void paths(const char *dir)
{
    snprintf(path_pict, sizeof path_pict, "%s/track04_pict.bin", dir);
    snprintf(path_boot, sizeof path_boot, "%s/track02_00004000.bin", dir);
    snprintf(path_t5,   sizeof path_t5,   "%s/track05_data.bin", dir);
    snprintf(path_set,  sizeof path_set,  "%s/track03_data.bin", dir);
}

/* ---- the manifest ------------------------------------------------- */

typedef struct {
    Json j;
    int  scenes, world, sets;
} Index;

static int index_open(Index *ix, const char *path)
{
    Blob b = io_read(path);
    if (!b.data) {
        fprintf(stderr, "cannot read %s\n", path);
        return 0;
    }
    int ok = json_parse(&ix->j, (const char *)b.data, b.size);
    io_free(&b);
    if (!ok) {
        fprintf(stderr, "%s is not valid JSON\n", path);
        return 0;
    }
    ix->scenes = json_member(&ix->j, ix->j.root, "scenes");
    ix->world  = json_member(&ix->j, ix->j.root, "world");
    ix->sets   = json_member(&ix->j, ix->j.root, "sets");
    return 1;
}

/* A scene by name (CA_CAM03) or by "#n". */
static int index_scene(const Index *ix, const char *name)
{
    if (name[0] == '#')
        return atoi(name + 1);
    int n = json_count(&ix->j, ix->scenes);
    for (int i = 0, e = json_at(&ix->j, ix->scenes, 0); i < n;
         i++, e = ix->j.v[e].next) {
        const char *s = json_strf(&ix->j, e, "name", "");
        if (strcmp(s, name) == 0)
            return (int)json_numf(&ix->j, e, "scene", -1);
    }
    return -1;
}

static const char *index_scene_name(const Index *ix, int scene)
{
    int n = json_count(&ix->j, ix->scenes);
    for (int i = 0, e = json_at(&ix->j, ix->scenes, 0); i < n;
         i++, e = ix->j.v[e].next)
        if ((int)json_numf(&ix->j, e, "scene", -1) == scene)
            return json_strf(&ix->j, e, "name", "?");
    return "?";
}

/* A world record by index ("#5"), by July name ("WORLD_CA_WINE"), or by the
 * short form the viewer prints, "<SET>_<TAIL>" as in CA_WINE. */
static int index_object(const Index *ix, const char *name, int *pos, int *face)
{
    int n = json_count(&ix->j, ix->world);
    for (int i = 0, e = json_at(&ix->j, ix->world, 0); i < n;
         i++, e = ix->j.v[e].next) {
        const char *jn = json_strf(&ix->j, e, "july_name", NULL);
        const char *set = json_strf(&ix->j, e, "set", NULL);
        int idx = (int)json_numf(&ix->j, e, "index", -1);
        char shortname[96] = "";
        if (jn && set && strncmp(jn, "WORLD_", 6) == 0) {
            const char *tail = jn + 6;
            size_t sl = strlen(set);
            if (strncmp(tail, set, sl) == 0 && tail[sl] == '_')
                snprintf(shortname, sizeof shortname, "%s", tail);
        }
        int hit = (name[0] == '#' && atoi(name + 1) == idx) ||
                  (jn && strcmp(jn, name) == 0) ||
                  (shortname[0] && strcmp(shortname, name) == 0);
        if (!hit)
            continue;
        pos[0] = (int)json_numf(&ix->j, e, "x", 0);
        pos[1] = (int)json_numf(&ix->j, e, "y", 0);
        pos[2] = (int)json_numf(&ix->j, e, "z", 0);
        int f = json_member(&ix->j, e, "face");
        for (int k = 0; k < 3; k++)
            face[k] = (int)json_num(&ix->j, json_at(&ix->j, f, k), 0);
        return idx;
    }
    return -1;
}

/* ---- output ------------------------------------------------------- */

static int write_ppm(const char *path, const uint16_t *fb)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    fprintf(f, "P6\n%d %d\n255\n", R3D_W, R3D_H);
    for (int i = 0; i < R3D_W * R3D_H; i++) {
        uint32_t c = scene_rgb(fb[i]);
        fputc((c >> 16) & 0xFF, f);
        fputc((c >> 8) & 0xFF, f);
        fputc(c & 0xFF, f);
    }
    fclose(f);
    return 1;
}

/* The Z half as a picture: the whole range stretched over the grey ramp, the
 * check docs/07-scene-format.md 7.4 used - it has to read as a surface. */
static void depth_to_fb(const Scene *s, uint16_t *fb)
{
    int lo = 65535, hi = 0;
    for (int i = 0; i < SCENE_PIXELS; i++) {
        if (s->depth[i] < lo) lo = s->depth[i];
        if (s->depth[i] > hi) hi = s->depth[i];
    }
    int range = hi > lo ? hi - lo : 1;
    for (int i = 0; i < SCENE_PIXELS; i++) {
        int v = (s->depth[i] - lo) * 31 / range;
        fb[i] = (uint16_t)((v << 11) | (v << 6) | (v * 63 / 31));
    }
}

/* ---- models ------------------------------------------------------- */

typedef struct {
    Blob   file;
    Model *list;
    int    count;
    int    from_boot;
} ModelSet;

static int models_open(ModelSet *ms, const char *spec, int *index)
{
    const char *colon = strchr(spec, ':');
    char src[32];
    size_t n = colon ? (size_t)(colon - spec) : strlen(spec);
    if (n >= sizeof src)
        n = sizeof src - 1;
    memcpy(src, spec, n);
    src[n] = 0;
    *index = colon ? atoi(colon + 1) : 0;

    const char *path;
    int min_verts;
    if (strcmp(src, "boot") == 0 || strcmp(src, "items") == 0) {
        path = path_boot;
        ms->from_boot = 1;
        min_verts = 3;
    } else if (strcmp(src, "track5") == 0 || strcmp(src, "t5") == 0) {
        path = path_t5;
        ms->from_boot = 0;
        /* Every one of them: a character's feet are a single vertex apiece,
         * and dropping them would break the chain that hangs on them. */
        min_verts = 1;
    } else {
        fprintf(stderr, "unknown model source '%s' - try boot:N or track5:N\n", src);
        return 0;
    }
    ms->file = io_read(path);
    if (!ms->file.data) {
        fprintf(stderr, "cannot read %s\n", path);
        return 0;
    }
    ms->count = model_scan(ms->file.data, ms->file.size, min_verts, &ms->list);
    return ms->count > 0;
}

/* ---- the cast ------------------------------------------------------ */

/* Track 5 in the shape the engine wants it: the model bundles, and for each
 * one the animations stored between it and the next.  What links a sheet to
 * one of these bundles is the sheet's own file record - block offset into
 * track 5, which is where the bundle begins (sheet.h, and
 * docs/12-world-and-sheets.md 12.7).  The track's layout - a bundle and then
 * its animations, one slot each - is what makes the animations follow. */
#define CAST_MAX 32

typedef struct {
    ModelSet ms;
    Blob     file;
    Anim    *anim;
    int      nanims;
    int      ncast;
    Bundle   bundle[CAST_MAX];
    int      first_anim[CAST_MAX], nanim[CAST_MAX];
} Cast;

static int cast_open(Cast *c)
{
    memset(c, 0, sizeof *c);
    if (!models_open(&c->ms, "track5", &(int){0}))
        return 0;
    c->file = io_read(path_t5);
    if (!c->file.data)
        return 0;
    c->nanims = anim_scan(c->file.data, c->file.size, &c->anim);

    for (int n = 0; n < CAST_MAX; n++) {
        int first = bundle_at(c->ms.list, c->ms.count, n);
        if (first < 0)
            break;
        if (!bundle_build(&c->bundle[c->ncast], c->ms.list, c->ms.count, first))
            break;
        c->ncast++;
    }
    /* The animations between one bundle and the next belong to it. */
    for (int i = 0; i < c->ncast; i++) {
        long from = c->bundle[i].piece[0]->offset;
        long to   = i + 1 < c->ncast ? c->bundle[i + 1].piece[0]->offset : 0x7FFFFFFF;
        c->first_anim[i] = -1;
        for (int k = 0; k < c->nanims; k++)
            if (c->anim[k].offset > from && c->anim[k].offset < to) {
                if (c->first_anim[i] < 0)
                    c->first_anim[i] = k;
                c->nanim[i]++;
            }
    }
    return c->ncast > 0;
}

static void cast_free(Cast *c)
{
    if (c->ms.list)
        model_free_all(c->ms.list, c->ms.count);
    io_free(&c->ms.file);
    free(c->anim);
    io_free(&c->file);
}

/* ---- the companion ------------------------------------------------- */

/* A second character, walking about under his own joypad.
 *
 * Nothing below this struct treats him as a special case.  AICTRL.GAS runs two
 * master loops over every active character: the first fills in `actJoypad` -
 * from the hardware for the player, from `ComputerControl` for everyone else -
 * and the second turns that joypad into an animation.  So the only difference
 * between Quentin and Ramirez, in the whole of this file, is which of the two
 * writes the pad. */
typedef struct {
    int           sheet;        /* which character sheet he wears           */
    int           cast;         /* the bundle that sheet names              */
    const Bundle *bundle;
    const Anim   *anim;
    Actor         actor;
    Control       ctl;
    Ai            ai;
    int           placed;       /* 1 from his own entry, 2 beside the
                                   player, 0 not on this set's floor        */
} Companion;

/* Who follows you, worked out rather than chosen.  The sheet chain is in the
 * same order as the 1995 `SHEET.S`; four sheets in a row carry
 * `cshBehaviour = aiFollowPlayer` where SHEET.S has RAMIREZ, FAVEB, MANGUA and
 * ARAKA; and the first of the four names track 5 block 1176 through its one
 * file record, which is where bundle 9 begins - fifteen pieces and the four
 * animations `BO_LOGICS_2A` asks for. */
static int companion_open(Companion *co, Sheets *sh, Cast *cast)
{
    memset(co, 0, sizeof *co);
    long off[CAST_MAX];
    for (int i = 0; i < cast->ncast && i < CAST_MAX; i++)
        off[i] = cast->bundle[i].piece[0]->offset;
    sheets_bundles(sh, off, cast->ncast);

    co->sheet = sheets_by_behaviour(sh, AI_FOLLOW_PLAYER);
    if (co->sheet < 0)
        return 0;
    co->cast = sh->sheet[co->sheet].bundle;
    if (co->cast < 0 || co->cast >= cast->ncast)
        return 0;
    co->bundle = &cast->bundle[co->cast];
    control_init(&co->ctl);
    ai_init(&co->ai, sh->sheet[co->sheet].behaviour);
    return 1;
}

/* Where he stands when a set opens.  The init table carries him and always
 * has: a doorway is two entries sharing an id, one with bit 0 of the flags
 * clear and one with it set, and 44 of the disc's ids have both halves
 * (docs/10-set-track.md 10.3).  `from_id` is the view being left, which is
 * what those entries are keyed on. */
static int companion_place(Companion *co, const Set *s, uint16_t from_id,
                           const Actor *player)
{
    const SetEntry *door = NULL, *any = NULL;
    for (int k = 0; k < s->nentries; k++) {
        const SetEntry *e = &s->entry[k];
        if (!ENTRY_COMPANION(e))
            continue;
        if (e->id == from_id)            door = e;
        else if (e->id == 0xFFFF && !any) any  = e;
    }
    if (!door) door = any;
    co->placed = 0;
    if (door && actor_place(&co->actor, s, door->x, door->z, ENTRY_FACING(door)))
        co->placed = 1;
    else if (player) {
        /* No entry of his own - a third of the disc's views do not list one.
         * Stand him one follow_range behind the player, facing the same way,
         * which is where following would have left him; if that is off the
         * mesh, on the player himself.  This fallback is ours: the original
         * never needs it, because a script put him somewhere before the set
         * was ever entered. */
        double k = 2.0 * 3.14159265358979323846 / 256.0;
        int32_t bx = player->x - (int32_t)lrint(AI_FOLLOW_RANGE * sin(player->facing * k));
        int32_t bz = player->z - (int32_t)lrint(AI_FOLLOW_RANGE * cos(player->facing * k));
        if (actor_place(&co->actor, s, bx, bz, player->facing) ||
            actor_place(&co->actor, s, player->x, player->z, player->facing))
            co->placed = 2;
    }
    if (co->placed) {
        co->anim = NULL;
        control_init(&co->ctl);
        ai_init(&co->ai, co->ai.behaviour);
    }
    return co->placed;
}

/* One frame of him: `ComputerControl`, and then the same `ActionCode` the
 * player goes through.  `PlayerControl` is deliberately not run - the
 * three-frame carry and the double-tap window belong to the hardware pad, and
 * an AI that wants to run sets JOY_DOUBLE itself, which is exactly what
 * AIAttackCode does. */
static void companion_step(Companion *co, const Set *s, const Actor *player,
                           const Cast *cast)
{
    if (!co->bundle || !co->placed)
        return;

    co->ctl.pad = ai_control(&co->ai, &co->actor, player, NULL);

    int fps     = co->anim && co->anim->fps ? co->anim->fps : 20;
    int frate   = co->anim && co->anim->fps ? 256 / co->anim->fps : 12;
    int playing = co->anim && co->actor.frame + 1 < co->anim->frames;
    control_action(&co->ctl, &control_follower, playing);
    if (co->ctl.restarted) {
        int k = co->ctl.anim;
        if (k >= 0 && k < cast->nanim[co->cast]) {
            const Anim *pick = &cast->anim[cast->first_anim[co->cast] + k];
            if (pick != co->anim) {
                co->anim = pick;
                fps   = pick->fps ? pick->fps : 20;
                frate = pick->fps ? 256 / pick->fps : 12;
            }
            co->actor.frame = co->anim->frames - 1;
        }
    }
    co->actor.facing = (uint8_t)(co->actor.facing + control_turn(&co->ctl, fps));
    if (co->anim)
        actor_step(&co->actor, s, co->anim, frate);
}

/* ---- the doorways, checked ----------------------------------------- */

/* A doorway is two halves that have to agree.  One half is a SCENE event that
 * cuts to a view another set owns; the other is that set's init entry, keyed
 * on the view being left rather than the one being arrived at - no entry on
 * the disc is keyed on its own group.  So walk every SCENE event on the disc
 * that leaves its set, and ask the set on the other side where it would put
 * the character.  Two things can go wrong and both are counted: the arriving
 * set may list no entry for that view and no default, and the entry it does
 * list may be off its own collision mesh, which would leave the character
 * nowhere.  docs/10-set-track.md 10.3. */
static int check_doors(void)
{
    Set cache[SET_COUNT];
    int  have[SET_COUNT];
    for (int i = 0; i < SET_COUNT; i++)
        have[i] = set_load(&cache[i], path_set, i);

    int entries = 0, on_mesh = 0, faces = 0;
    for (int i = 0; i < SET_COUNT; i++) {
        if (!have[i]) continue;
        for (int k = 0; k < cache[i].nentries; k++) {
            const SetEntry *e = &cache[i].entry[k];
            entries++;
            /* Every one of the 153 is exactly facing * 256 + (0 or 1): the
             * facing fills the top byte, so the only bits left over are bit 0,
             * which says whose arrival this is. */
            if ((e->flags & 0xFE) == 0)
                faces++;
            if (cache[i].ntris && set_locate(&cache[i], e->x, e->z) >= 0)
                on_mesh++;
        }
    }

    int leaves = 0, found = 0, defaulted = 0, missing = 0, off = 0;
    for (int i = 0; i < SET_COUNT; i++) {
        if (!have[i]) continue;
        for (int v = 0; v < cache[i].nevents; v++) {
            const SetEvent *ev = &cache[i].event[v];
            if (ev->type != 0) continue;                /* EVENT_TYPE_SCENE */
            int ti = set_of_scene(path_set, ev->data[0]);
            if (ti < 0 || ti == i || !have[ti]) continue;
            leaves++;
            /* The view being left: the event names it when it is not $FFFF,
             * and otherwise it is whichever of this set's views the player
             * happened to be in, so try them all and take the first that the
             * far side lists. */
            const SetEntry *door = NULL, *any = NULL;
            for (int k = 0; k < cache[ti].nentries; k++) {
                const SetEntry *e = &cache[ti].entry[k];
                if (ENTRY_COMPANION(e)) continue;
                if (e->id == 0xFFFF) { if (!any) any = e; continue; }
                if (ev->scene != 0xFFFF) {
                    if (e->id == ev->scene) door = e;
                } else {
                    for (int j = 0; j < cache[i].nscenes && !door; j++)
                        if (cache[i].scene[j].id == e->id) door = e;
                }
            }
            if (door)      found++;
            else if (any) { defaulted++; door = any; }
            else           missing++;
            if (door && cache[ti].ntris &&
                set_locate(&cache[ti], door->x, door->z) < 0)
                off++;
        }
    }

    printf("init entries: %d over %d sets\n", entries, SET_COUNT);
    printf("  %d of %d have flags = facing * 256 + character\n", faces, entries);
    printf("  %d of %d stand on their own set's collision mesh\n",
           on_mesh, entries);
    printf("doorways: %d SCENE events cut into another set\n", leaves);
    printf("  %d find an entry keyed on the view being left,"
           " %d fall back to the set's default, %d find neither\n",
           found, defaulted, missing);
    printf("  %d of the arrivals are off the arriving set's mesh\n", off);

    for (int i = 0; i < SET_COUNT; i++)
        if (have[i]) set_free(&cache[i]);
    return missing == 0 && off == 0 && faces == entries;
}

/* ---- the floor, checked -------------------------------------------- */

/* set_find_tri searches the adjacency the way FINDTRI.GAS does; set_locate
 * scans every triangle.  They must agree, so run the search against the scan
 * for every triangle of every set, from two kinds of start: an arbitrary one,
 * and one five triangles away, which is the only case movement is ever in.
 * Report how often the search gave up into a full scan as well - a search that
 * always gave up would "agree" with the scan every time and prove nothing. */
static int check_mesh(void)
{
    int total = 0, wrong = 0, unreachable = 0;
    int fell_back = 0, walked_ok = 0; long walk_steps = 0;
    int connected = 0, connected_wrong = 0, connected_fell_back = 0;
    long connected_steps = 0;
    for (int i = 0; i < SET_COUNT; i++) {
        Set s;
        if (!set_load(&s, path_set, i) || s.ntris == 0)
            continue;
        int bad = 0;
        for (int t = 0; t < s.ntris; t++) {
            /* the centroid, which is inside triangle t by construction */
            const SetTri *tr = &s.tri[t];
            int64_t cx = 0, cz = 0;
            for (int k = 0; k < 3; k++) {
                cx += s.vert[tr->vert[k]].x;
                cz += s.vert[tr->vert[k]].z;
            }
            int32_t x = (int32_t)(cx / 3), z = (int32_t)(cz / 3);
            int from = (t * 7 + 3) % s.ntris;       /* start somewhere else */
            int steps = -1;
            int walked = set_walk(&s, x, z, from, &steps);
            if (steps < 0) fell_back++; else { walk_steps += steps; walked_ok++; }
            int scanned = set_locate(&s, x, z);
            total++;
            if (scanned < 0)
                unreachable++;
            else if (!set_contains(&s, walked, x, z)) {
                /* The mesh overlaps itself here and there, so the walk landing
                 * on a different index than the scan is fine; landing on a
                 * triangle that does not cover the point is not. */
                wrong++;
                bad++;
                if (bad <= 2)
                    printf("  set %2d tri %3d: centroid (%d,%d) -> walk %d, scan %d\n",
                           i, t, x, z, walked, scanned);
            }

            /* The case movement is actually in: the character was five
             * triangles away a moment ago, so the start is connected to the
             * destination and the walk must not need the scan at all. */
            int near_start = set_step_away(&s, t, 5);
            int near_steps = -1;
            int near = set_walk(&s, x, z, near_start, &near_steps);
            connected++;
            if (near_steps < 0)
                connected_fell_back++;
            else
                connected_steps += near_steps;
            if (!set_contains(&s, near, x, z))
                connected_wrong++;
        }
        if (bad)
            printf("  set %2d: %d of %d centroids disagree\n", i, bad, s.ntris);
        set_free(&s);
    }
    printf("mesh walk vs scan: %d triangles, %d disagreements, "
           "%d centroids the scan itself could not place\n",
           total, wrong, unreachable);
    printf("  from an arbitrary start: %d found across the adjacency, %.1f "
           "triangles examined on average, %d gave up into a full scan\n",
           walked_ok, walked_ok ? (double)walk_steps / walked_ok : 0.0, fell_back);
    printf("  from five triangles away, which is where movement always starts: "
           "%d searches, %d wrong, %d gave up, %.1f triangles examined on average\n",
           connected, connected_wrong, connected_fell_back,
           connected > connected_fell_back
               ? (double)connected_steps / (connected - connected_fell_back) : 0.0);
    return wrong == 0 && connected_wrong == 0;
}

/* ---- the events ---------------------------------------------------- */

/* EVENT.GAS walks the set's event list every frame and fires everything the
 * player is standing inside.  An event is a circle on the floor plan tagged
 * with the view it belongs to - $FFFF for any view in the set - and a SCENE
 * event carries the id of the view to cut to in the first of its three data
 * words.  62 of CA's 66 events are of that type: the camera cuts are data.
 *
 * Three details that are the original's and not obvious: the height word, when
 * it is not zero, is a ceiling and not a floor, so an event only fires from at
 * or below it; a radius of zero means "always colliding"; and a SCENE event
 * fires even when the status word already says the player is inside it, which
 * no other type does - "scene changes dont give a **** if we are already
 * colliding".  Only the first cut of a frame is taken.
 *
 * Returns the scene id to cut to, or -1. */
static int events_scan(Set *s, uint16_t scene_id, const Actor *act)
{
    int cut = -1;
    for (int i = 0; i < s->nevents; i++) {
        SetEvent *e = &s->event[i];
        if (e->scene != 0xFFFF && e->scene != scene_id)
            continue;
        int colliding = 1;
        if (e->height != 0 && act->y > e->height)
            colliding = 0;
        if (colliding && e->radius2 != 0) {
            int64_t dx = (int64_t)e->x - act->x, dz = (int64_t)e->z - act->z;
            colliding = dx * dx + dz * dz < labs((long)e->radius2);
        }
        if (!colliding) {
            e->status &= (uint16_t)~0x3FFF;     /* the engine clears the count */
            continue;
        }
        int was_in = e->status & 1;
        e->status |= 1;
        if (e->type != 0 && was_in)
            continue;                           /* everything else is an edge */
        if (e->type == 0 && cut < 0 && e->data[0] != scene_id)
            cut = e->data[0];
    }
    return cut;
}

/* ---- the character, checked ---------------------------------------- */

/* Assembling fifteen pieces at an animation frame is where the model format,
 * the animation format and the skeleton first have to agree, and the data
 * carries its own answer: every frame records the highest and lowest point of
 * the pose it describes, in `animHigh` and `animLow`, measured from the floor.
 *
 * So pose the bundle, measure it, and compare.  The root sits HEIGHTSTART above
 * the floor and every frame's own y move shifts it further, so the pose's own
 * extent about its root should come out at
 *
 *     low - HEIGHTSTART - (the y moves so far)  ..  high - HEIGHTSTART - same
 *
 * Nothing in that is circular: the angles come from the frame, the joints from
 * the models, and high and low are two more words of the same frame that the
 * assembly never reads. */
static int check_char(void)
{
    Cast c;
    if (!cast_open(&c)) {
        fprintf(stderr, "no character bundles on track 5\n");
        return 0;
    }

    long total = 0, frames = 0, bad = 0;
    int worst = 0, skipped = 0;
    long hist[8] = { 0 };
    for (int b = 0; b < c.ncast; b++) {
        const Bundle *B = &c.bundle[b];
        /* Fifteen pieces is what a character is - cshModelNum is 15 for every
         * full one (docs/12-world-and-sheets.md 12.3) and the origin numbers
         * lay out the same fifteen, 128 to 141.  The bundles that are not
         * characters are machinery, and their animations do not fill high and
         * low in: the grinder's two wheels swing a thousand units either side
         * of their axle while its frames alternate between 965 and 0.  There
         * is nothing there to check against, so they are reported and not
         * counted. */
        int character = B->npieces == 15;
        long bt = 0, bn = 0;
        int bworst = 0;
        for (int k = 0; k < c.nanim[b]; k++) {
            const Anim *an = &c.anim[c.first_anim[b] + k];
            if (an->pieces != B->npieces) {
                skipped++;
                continue;
            }
            int32_t lift = 0;
            for (int f = 0; f < an->frames; f++) {
                AnimFrame fr;
                anim_frame(an, f, &fr);
                lift += fr.move[1];
                ActorPose pose[ACTOR_MAX_PIECES];
                int32_t root[3] = { 0, 0, 0 }, lo, hi;
                actor_pose(B, fr.angle, 0, root, pose);
                actor_extent(B, pose, &lo, &hi);
                int e1 = abs(lo - (fr.low  - an->height_start - lift));
                int e2 = abs(hi - (fr.high - an->height_start - lift));
                for (int i = 0; i < 2; i++) {
                    int e = i ? e2 : e1;
                    bt += e;
                    if (e > bworst) bworst = e;
                    if (!character)
                        continue;
                    total += e;
                    hist[e < 2 ? 0 : e < 4 ? 1 : e < 8 ? 2 : e < 16 ? 3 :
                         e < 32 ? 4 : e < 64 ? 5 : e < 128 ? 6 : 7]++;
                    if (e > worst) worst = e;
                    if (e >= 32) bad++;
                }
                bn += 2;
                if (character)
                    frames++;
            }
        }
        if (bn)
            printf("  %-9s %2d  %2d pieces  %3d animations  %5ld frames  "
                   "mean %.2f, worst %d\n", character ? "character" : "machine",
                   b, B->npieces, c.nanim[b], bn / 2, (double)bt / bn, bworst);
    }
    long n = frames * 2;
    printf("character pose vs the frame's own high and low: "
           "%ld frames, %ld measurements\n", frames, n);
    printf("  mean error %.2f units, worst %d, %ld over 32 (%.2f%%)\n",
           n ? (double)total / n : 0.0, worst, bad, n ? 100.0 * bad / n : 0.0);
    printf("  within  2: %ld    4: %ld    8: %ld   16: %ld   32: %ld   "
           "64: %ld  128: %ld  more: %ld\n",
           hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7]);
    if (skipped)
        printf("  %d animations skipped: their piece count is not the bundle's\n",
               skipped);
    cast_free(&c);
    /* The residual is the 256-step circle and the s1.14 sine table, compounded
     * over a chain of up to four joints.  Anything past 32 units would not be
     * rounding. */
    return n > 0 && bad == 0;
}

/* ---- the companion, checked ---------------------------------------- */

/* Two things have to be right before a follower is worth looking at, and both
 * can be checked without a window.
 *
 * The first is the bearing.  `AIRotateCode` finds the direction to the target
 * through a 257-entry arctan table and three sign tests, and the sign tests
 * are where a port goes wrong: get one quadrant swapped and the character
 * walks away from you in a quarter of the world and towards you in the rest.
 * So sweep the whole circle and compare with the arctangent the table is an
 * approximation of.  The table's own resolution is a quarter of a step, so
 * anything past one step is a fault rather than rounding.
 *
 * The second is that he stays on the floor.  `AIFollowCode` walks straight at
 * the player - there is no path finding anywhere in the original - so he can
 * and will be stopped by a wall.  What must never happen is that he leaves the
 * collision mesh, because everything downstream, the ground height included,
 * is indexed by the triangle he is on.  So run every set that carries a
 * companion entry: put the two of them down where the doorway says, walk the
 * player forward until he is stopped, let him stand, and watch. */
static int check_follow(void)
{
    /* 1: the bearing. */
    int worst = 0;
    long over = 0, n = 0;
    for (int i = 0; i < 3600; i++) {
        double t = i * (2.0 * 3.14159265358979323846 / 3600.0);
        int32_t dx = (int32_t)lrint(4000.0 * sin(t));
        int32_t dz = (int32_t)lrint(4000.0 * cos(t));
        int want = (int)lrint(i * (256.0 / 3600.0)) & 255;
        int got  = ai_angle(dx, dz);
        int e = (got - want) & 255;
        if (e > 128) e -= 256;
        if (e < 0) e = -e;
        if (e > worst) worst = e;
        if (e > 1) over++;
        n++;
    }
    printf("bearing: %ld directions, worst %d step%s off, %ld past one\n",
           n, worst, worst == 1 ? "" : "s", over);

    /* 2: the follow, over the disc. */
    Cast cast;
    if (!cast_open(&cast)) {
        fprintf(stderr, "no character bundles on track 5\n");
        return 0;
    }
    Sheets sh;
    Companion co;
    if (!sheets_load(&sh, path_boot) || !companion_open(&co, &sh, &cast)) {
        fprintf(stderr, "no aiFollowPlayer sheet with a bundle\n");
        cast_free(&cast);
        return 0;
    }
    printf("companion: sheet %d, behaviour %s, track 5 block %d, "
           "bundle %d with %d animations\n", co.sheet,
           ai_command_name(sh.sheet[co.sheet].behaviour),
           sh.sheet[co.sheet].block, co.cast, cast.nanim[co.cast]);

    const Anim *walk = &cast.anim[cast.first_anim[0] + 10];   /* the player's */
    int sets = 0, offmesh = 0, arrived = 0, tried = 0;
    long far_total = 0;
    for (int i = 0; i < SET_COUNT; i++) {
        Set set;
        if (!set_load(&set, path_set, i))
            continue;
        sets++;

        /* A doorway both of them arrive through. */
        const SetEntry *me = NULL, *him = NULL;
        for (int k = 0; k < set.nentries && !him; k++) {
            if (ENTRY_COMPANION(&set.entry[k]))
                continue;
            for (int j = 0; j < set.nentries; j++)
                if (ENTRY_COMPANION(&set.entry[j]) &&
                    set.entry[j].id == set.entry[k].id) {
                    me = &set.entry[k];
                    him = &set.entry[j];
                    break;
                }
        }
        if (!him) { set_free(&set); continue; }
        tried++;

        Actor player;
        Control ctl;
        control_init(&ctl);
        if (!actor_place(&player, &set, me->x, me->z, ENTRY_FACING(me)) ||
            !companion_place(&co, &set, him->id, &player)) {
            printf("  set %2d: the entry pair for view %d is off the mesh\n",
                   i, me->id);
            set_free(&set);
            continue;
        }
        const Anim *an = walk;
        int off = 0, farthest = 0;
        for (int f = 0; f < 500; f++) {
            /* 300 frames of forward, then let go and stand. */
            control_pad(&ctl, f < 300 ? PAD(JOY_UP) : 0);
            int playing = player.frame + 1 < an->frames;
            control_action(&ctl, &control_quentin, playing);
            if (ctl.restarted && ctl.anim >= 0 && ctl.anim < cast.nanim[0]) {
                const Anim *pick = &cast.anim[cast.first_anim[0] + ctl.anim];
                if (pick != an) an = pick;
                player.frame = an->frames - 1;
            }
            player.facing = (uint8_t)(player.facing +
                                      control_turn(&ctl, an->fps ? an->fps : 20));
            actor_step(&player, &set, an, an->fps ? 256 / an->fps : 12);

            companion_step(&co, &set, &player, &cast);
            if (co.actor.tri < 0)
                off++;
            int64_t dx = co.actor.x - player.x, dz = co.actor.z - player.z;
            int d = (int)lrint(sqrt((double)(dx * dx + dz * dz)));
            if (d > farthest) farthest = d;
        }
        int64_t dx = co.actor.x - player.x, dz = co.actor.z - player.z;
        int end = (int)lrint(sqrt((double)(dx * dx + dz * dz)));
        int ok = end <= AI_FOLLOW_RANGE;
        arrived += ok;
        far_total += farthest;
        if (off) offmesh++;
        if (off || !ok)
            printf("  set %2d: %3d frames off the mesh, farthest %5d, "
                   "ended %5d%s\n", i, off, farthest, end,
                   ok ? "" : "  - did not close up");
        set_free(&set);
    }
    printf("follow: %d sets, %d with a companion entry, %d never off the mesh,"
           " %d closed to within %d\n", sets, tried, tried - offmesh, arrived,
           AI_FOLLOW_RANGE);
    printf("  mean farthest %ld over the 500 frames\n",
           tried ? far_total / tried : 0);
    sheets_free(&sh);
    cast_free(&cast);
    /* The bearing is exact or it is broken; leaving the mesh is never allowed.
     * Being left behind by a wall is not a failure - the original has no path
     * finding either - so it is reported and not counted. */
    return worst <= 1 && offmesh == 0 && tried > 0;
}

/* ---- the scripts, running ------------------------------------------- */

/* MAINSCRIPT is not on a data track: it sits in the retail binary right behind
 * the script VM's own GPU module and runs to the next module header.
 * docs/11-script-vm.md 11.1. */
#define MAIN_MEM  0x2FEB8
#define MAIN_END  0x30130
#define MAIN_FILE (MAIN_MEM - 0x5000 + 0x12600)

/* Every script on the disc, run.
 *
 * The disassembler settled that all 1,173 commands *decode*; whether they
 * *execute* is a different question, and the difference is where a port of a
 * machine like this goes wrong - a mis-decoded operand class, a branch
 * condition read the wrong way round, a handler that never yields.  So load
 * each of the twenty-seven set scripts in turn beside the resident one, run
 * them for a few hundred game frames against a real set and a real character,
 * and count.
 *
 * Three things must come out at zero.  No command past the dispatch table -
 * the original abandons the process on one, so a decoder that drifts by a byte
 * shows up as scripts quietly dying.  No process hitting the command budget -
 * that is a loop with no `quit` in it, which on the real machine hangs the
 * GPU.  And no script left running off the end of its own slot. */
static int check_script(void)
{
    Blob boot = io_read(path_boot);
    if (!boot.data) {
        fprintf(stderr, "cannot read %s\n", path_boot);
        return 0;
    }
    Sheets sh;
    if (!sheets_load(&sh, path_boot)) {
        fprintf(stderr, "no world table in %s\n", path_boot);
        io_free(&boot);
        return 0;
    }
    /* The world table as bytes, which is what the machine addresses. */
    const uint8_t *world = boot.data + (0x15458 - 0x5000 + 0x12600);
    const uint8_t *main  = boot.data + MAIN_FILE;

    long total = 0, unknown = 0, unmapped = 0, overrun = 0, films = 0, cuts = 0,
         padding = 0, quiet_ops = 0, stray = 0;
    long seen[VM_LASTOP];
    memset(seen, 0, sizeof seen);
    int scripts = 0, sets = 0;

    for (int i = 0; i < SET_COUNT; i++) {
        Set set;
        if (!set_load(&set, path_set, i))
            continue;
        sets++;

        /* Somewhere to stand, and somebody to be.  A script that says
         * `select #0` wants an active character behind world record 0. */
        ActTable acts;
        act_init(&acts);
        int p = act_add(&acts, 0, 1, 0, NULL, 0, &control_quentin);
        acts.player = p;
        if (set.nentries > 0)
            actor_place(&acts.a[p].actor, &set, set.entry[0].x, set.entry[0].z,
                        ENTRY_FACING(&set.entry[0]));

        Vm vm;
        vm_init(&vm, world, main, MAIN_END - MAIN_MEM);
        vm_register(&vm, 0);
        vm.act = &acts;
        vm.set = &set;
        vm.curset = (uint16_t)i;
        vm.scene = set.nscenes ? set.scene[0].id : 0;
        vm_set_script(&vm, i, set.script, set.script_len);

        /* Two passes.  The first is the set as you would walk into it, and
         * most scripts spend it sitting on a game bit that nothing has set -
         * which is correct, and reaches about a third of the machine.  The
         * second stirs the world underneath them: game bits go up and down, a
         * different world entry is "used" every few frames, and one pad key at
         * a time is held.  It is not a playthrough, but it drives the scripts
         * down the branches a playthrough would, and what it is really
         * checking is that no handler falls over when it gets there. */
        uint32_t rnd = 0x1234567u + (uint32_t)i * 2654435761u;
        for (int pass = 0; pass < 2; pass++) {
            for (int f = 0; f < 600; f++) {
                if (pass) {
                    rnd = rnd * 1103515245u + 12345u;
                    int b = (int)((rnd >> 16) & 63);
                    if (rnd & 0x100)
                        vm.gamestate[b >> 5] |=  1u << (b & 31);
                    else
                        vm.gamestate[b >> 5] &= ~(1u << (b & 31));
                    vm.used = (int)((rnd >> 8) & 0xFF);
                    vm.pad  = 1u << ((rnd >> 24) & 31);
                    for (int g = 0; g < 3; g++)
                        vm.gvar[g] = (int32_t)((rnd >> (g * 3)) & 3);
                    /* MAINSCRIPT's fourth block is the "item used" handler,
                     * and nothing in any script spawns it: the engine starts
                     * it with r0, r1 and r2 already set when the player uses
                     * something.  So does this, every 200 frames. */
                    if (f % 200 == 100)
                        vm_start(&vm, VM_MAIN, 0x10,
                                 ((f / 200) & 1) ? 0 : 1, 5,
                                 (f / 200) & 1);
                }
                vm_frame(&vm);
                /* The host's half of the handshake: the machine posts an event
                 * and waits for it to be cleared.  Here that is instant, which
                 * is what lets a script get past a film rather than sitting on
                 * it for ever. */
                if (vm.scriptevent) {
                    if (vm.scriptevent == EV_CINEPAK + 1)  films++;
                    if (vm.scriptevent == EV_SCENE + 1) {
                        /* A `camera` names a view by scene id, and a set
                         * lists every view it can cut to - its own and the
                         * ones it borrows.  If the id the script asks for is
                         * not in that list, either the operand is being
                         * decoded wrong or the set table is. */
                        cuts++;
                        int known = 0;
                        for (int k = 0; k < set.nscenes; k++)
                            if (set.scene[k].id == vm.scriptscene)
                                known = 1;
                        if (!known) {
                            stray++;
                            printf("  set %2d: camera to scene %d, which this "
                                   "set does not list\n", i, vm.scriptscene);
                        }
                        vm.scene = vm.scriptscene;
                    }
                    vm.scriptevent = 0;
                    vm.redbook_done = 1;
                }
                if (vm.gameover)
                    vm.gameover = 0;    /* `reset` restarts the machine; here
                                           it is noted and cleared           */
            }
            if (pass == 0)
                quiet_ops += 0;         /* the split is reported below       */
        }

        if (set.script_len)
            scripts++;
        total    += vm.executed;
        unknown  += vm.unknown_op;
        unmapped += vm.unmapped;
        overrun  += vm.overrun;
        padding  += vm.padding;
        for (int k = 0; k < VM_LASTOP; k++)
            seen[k] += vm.opcount[k];
        if (vm.unknown_op || vm.overrun)
            printf("  set %2d: %ld commands, %ld past the table, %ld overran\n",
                   i, vm.executed, vm.unknown_op, vm.overrun);
        else if (set.script_len)
            printf("  set %2d: %6ld commands, %d process%s still alive\n", i,
                   vm.executed, vm_active(&vm), vm_active(&vm) == 1 ? "" : "es");
        set_free(&set);
    }

    int used = 0;
    for (int k = 0; k < VM_LASTOP; k++)
        if (seen[k])
            used++;
    printf("scripts: %d sets, %d of them with one, plus MAINSCRIPT in every "
           "run\n", sets, scripts);
    printf("  %ld commands executed, %d of the %d opcodes exercised\n",
           total, used, VM_LASTOP);
    printf("  %ld past the dispatch table, %ld processes overran, "
           "%ld fields this port has no home for\n", unknown, overrun, unmapped);
    printf("  %ld processes ran into their slot's padding and stopped\n", padding);
    printf("  %ld camera cuts asked for, %ld of them to a view the set does "
           "not list, and %ld films\n", cuts, stray, films);

    printf("  never executed:");
    for (int k = 0; k < VM_LASTOP; k++)
        if (!seen[k])
            printf(" %s", vm_opname(k));
    putchar('\n');

    sheets_free(&sh);
    io_free(&boot);
    return unknown == 0 && overrun == 0 && stray == 0 && total > 0;
}

/* ---- drawing a character ------------------------------------------- */

static void draw_actor(R3dTarget *t, const Bundle *b, const ActorPose *pose,
                       const SceneCam *cam, const R3dOpts *o,
                       int *facets, int *tested, int *drawn)
{
    *facets = *tested = *drawn = 0;
    for (int i = 0; i < b->npieces; i++) {
        R3dXform x;
        r3d_place(&x, cam, pose[i].rot, pose[i].pos);
        r3d_draw_model(t, b->piece[i], &x, o);
        *facets += r3d_stats.facets;
        *tested += r3d_stats.tested;
        *drawn  += r3d_stats.drawn;
    }
}

/* ---- main --------------------------------------------------------- */

static void usage(void)
{
    puts(
"hlview - the Highlander engine, phases 3 and 4\n"
"\n"
"  --tracks DIR      extracted tracks (default assets/tracks)\n"
"  --manifest FILE   default assets/manifest.json\n"
"  --scene NAME      a scene by name (CA_CAM03) or #index\n"
"  --depth           show the scene's Z half as grey instead of the picture\n"
"  --mesh            draw the set's collision mesh over the scene\n"
"  --no-ground       leave the object at y = 0 instead of on the floor\n"
"  --model SRC:N     boot:N for the 19 item models, track5:N for the 220\n"
"  --object NAME     place the model at a world record (CA_WINE, #5)\n"
"  --pos X,Y,Z       place it explicitly instead\n"
"  --face E,A,T      the three rotations, on a 256-step circle\n"
"  --spin            turn the model about its azimuth\n"
"  --wire            draw facet edges instead of filling\n"
"  --flat            do not shade the facets\n"
"  --cull MODE       none (default), back or front\n"
"  --shot FILE.ppm   write the first frame out\n"
"  --shot-at N      write it at frame N instead\n"
"  --no-window       render without opening a window\n"
"  --frames N        stop after N frames\n"
"  --scale N         window magnification (default 3)\n"
"  --char N          one of track 5's fifteen-piece character bundles\n"
"  --anim N | --frame N   which animation, and which frame of it\n"
"  --play | --walk   run the animation; walk it over the set's floor\n"
"  --drive           the joypad picks the animation and the root motion\n"
"                    moves him, the way AICTRL.GAS has it\n"
"  --pad SPEC        a scripted pad for --drive, keys:frames separated\n"
"                    by commas - up:40,-:2,up:60 is a double tap\n"
"  --events          fire the set's events: the camera cuts, and the doors\n"
"  --list-scenes | --list-models | --list-objects | --list-chars\n"
"  --alone           leave the companion out of --drive\n"
"  --list-sheets     the character sheets, and what each one wears\n"
"  --check-mesh | --check-char | --check-doors | --check-follow\n");
}

static int parse_triple(const char *s, int *out)
{
    return sscanf(s, "%d,%d,%d", &out[0], &out[1], &out[2]) == 3;
}


/* ---- a scripted joypad --------------------------------------------- */

/* --pad holds keys down for a given number of frames, so a run can be
 * reproduced without a window: "up:40,-:2,up:60" walks, lets go for two
 * frames and presses again, which is the double tap PlayerControl is looking
 * for.  The names are control.h's bits. */
typedef struct { uint32_t pad; int frames; } PadStep;

static PadStep pad_script[32];
static int     pad_steps;

static int parse_pad(const char *spec)
{
    pad_steps = 0;
    while (*spec && pad_steps < 32) {
        uint32_t bits = 0;
        while (*spec && *spec != ':' && *spec != ',') {
            static const struct { const char *n; int b; } key[] = {
                { "up", JOY_UP }, { "down", JOY_DOWN }, { "left", JOY_LEFT },
                { "right", JOY_RIGHT }, { "a", FIRE_A }, { "b", FIRE_B },
                { "c", FIRE_C }, { NULL, 0 }
            };
            int n = 0;
            while (spec[n] && spec[n] != '+' && spec[n] != ':' && spec[n] != ',')
                n++;
            for (int k = 0; key[k].n; k++)
                if ((int)strlen(key[k].n) == n && !strncmp(spec, key[k].n, n))
                    bits |= 1u << key[k].b;
            spec += n;
            if (*spec == '+') spec++;
        }
        int frames = 1;
        if (*spec == ':') frames = atoi(++spec);
        while (*spec && *spec != ',') spec++;
        if (*spec == ',') spec++;
        pad_script[pad_steps].pad = bits;
        pad_script[pad_steps].frames = frames > 0 ? frames : 1;
        pad_steps++;
    }
    return pad_steps;
}

static uint32_t pad_at(int frame)
{
    for (int i = 0; i < pad_steps; i++) {
        if (frame < pad_script[i].frames)
            return pad_script[i].pad;
        frame -= pad_script[i].frames;
    }
    return pad_steps ? pad_script[pad_steps - 1].pad : 0;
}

int main(int argc, char **argv)
{
    Args a = { "assets/tracks", "assets/manifest.json", NULL, NULL, NULL, NULL, 0, NULL,
               0, 0, 0, 0, 1, 3, 0, 0, 0,
               -1, 0, -1, 0, 0, 0, 0,
               0, {0,0,0}, 0, {0,0,0},
               R3D_CULL_NONE, 0, 0, 0, 0, 0,
               0 };

    for (int i = 1; i < argc; i++) {
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if      (!strcmp(argv[i], "--tracks")   && v) a.tracks = argv[++i];
        else if (!strcmp(argv[i], "--manifest") && v) a.manifest = argv[++i];
        else if (!strcmp(argv[i], "--scene")    && v) a.scene = argv[++i];
        else if (!strcmp(argv[i], "--model")    && v) a.model = argv[++i];
        else if (!strcmp(argv[i], "--object")   && v) a.object = argv[++i];
        else if (!strcmp(argv[i], "--shot")     && v) a.shot = argv[++i];
        else if (!strcmp(argv[i], "--shot-at") && v) a.shot_at = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scale")    && v) a.scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames")   && v) a.frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pos")      && v) a.have_pos = parse_triple(argv[++i], a.pos);
        else if (!strcmp(argv[i], "--face")     && v) a.have_face = parse_triple(argv[++i], a.face);
        else if (!strcmp(argv[i], "--cull")     && v) {
            const char *m = argv[++i];
            a.cull = !strcmp(m, "back")  ? R3D_CULL_BACK :
                     !strcmp(m, "front") ? R3D_CULL_FRONT : R3D_CULL_NONE;
        }
        else if (!strcmp(argv[i], "--char")     && v) a.character = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--anim")     && v) a.anim = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frame")    && v) a.frame = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--play"))      a.play = 1;
        else if (!strcmp(argv[i], "--walk"))      a.walk = 1;
        else if (!strcmp(argv[i], "--drive"))     a.drive = 1;
        else if (!strcmp(argv[i], "--pad")      && v) a.pad = argv[++i];
        else if (!strcmp(argv[i], "--events"))    a.events = 1;
        else if (!strcmp(argv[i], "--depth"))     a.depth = 1;
        else if (!strcmp(argv[i], "--mesh"))      a.mesh = 1;
        else if (!strcmp(argv[i], "--no-ground")) a.no_ground = 1;
        else if (!strcmp(argv[i], "--spin"))      a.spin = 1;
        else if (!strcmp(argv[i], "--wire"))      a.wire = 1;
        else if (!strcmp(argv[i], "--flat"))      a.shade = 0;
        else if (!strcmp(argv[i], "--no-window")) a.no_window = 1;
        else if (!strcmp(argv[i], "--list-scenes"))  a.list_scenes = 1;
        else if (!strcmp(argv[i], "--list-models"))  a.list_models = 1;
        else if (!strcmp(argv[i], "--list-objects")) a.list_objects = 1;
        else if (!strcmp(argv[i], "--list-chars"))   a.list_chars = 1;
        else if (!strcmp(argv[i], "--list-sheets"))  a.list_sheets = 1;
        else if (!strcmp(argv[i], "--alone"))        a.alone = 1;
        else if (!strcmp(argv[i], "--check-mesh")) { paths(a.tracks); return !check_mesh(); }
        else if (!strcmp(argv[i], "--check-char")) { paths(a.tracks); return !check_char(); }
        else if (!strcmp(argv[i], "--check-doors")) { paths(a.tracks); return !check_doors(); }
        else if (!strcmp(argv[i], "--check-follow")) { paths(a.tracks); return !check_follow(); }
        else if (!strcmp(argv[i], "--check-script")) { paths(a.tracks); return !check_script(); }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
        else { usage(); return argv[i][0] == '-' ? 1 : 0; }
    }
    paths(a.tracks);
    if (a.pad) parse_pad(a.pad);

    Index ix;
    if (!index_open(&ix, a.manifest))
        return 1;

    if (a.list_scenes) {
        int n = json_count(&ix.j, ix.scenes);
        for (int i = 0, e = json_at(&ix.j, ix.scenes, 0); i < n; i++, e = ix.j.v[e].next)
            printf("%3d  %s\n", (int)json_numf(&ix.j, e, "scene", -1),
                   json_strf(&ix.j, e, "name", "?"));
        return 0;
    }
    if (a.list_objects) {
        int n = json_count(&ix.j, ix.world);
        for (int i = 0, e = json_at(&ix.j, ix.world, 0); i < n; i++, e = ix.j.v[e].next) {
            if (!json_num(&ix.j, json_member(&ix.j, e, "used"), 0))
                continue;
            int f = json_member(&ix.j, e, "face");
            printf("%3d  %-8s r=%-4d (%7d,%7d) face %3d,%3d,%3d  %s\n",
                   (int)json_numf(&ix.j, e, "index", -1),
                   json_strf(&ix.j, e, "set", "-"),
                   (int)json_numf(&ix.j, e, "radius", 0),
                   (int)json_numf(&ix.j, e, "x", 0), (int)json_numf(&ix.j, e, "z", 0),
                   (int)json_num(&ix.j, json_at(&ix.j, f, 0), 0),
                   (int)json_num(&ix.j, json_at(&ix.j, f, 1), 0),
                   (int)json_num(&ix.j, json_at(&ix.j, f, 2), 0),
                   json_strf(&ix.j, e, "july_name", ""));
        }
        return 0;
    }

    /* The cast: track 5's fifteen-piece character bundles. */
    Cast cast;
    int have_cast = 0;
    if (a.character >= 0 || a.list_chars || a.list_sheets) {
        if (!cast_open(&cast)) {
            fprintf(stderr, "no character bundles on track 5\n");
            return 1;
        }
        have_cast = 1;
    }
    if (a.list_chars) {
        for (int i = 0; i < cast.ncast; i++) {
            const Bundle *b = &cast.bundle[i];
            int verts = 0, faces = 0;
            for (int k = 0; k < b->npieces; k++) {
                verts += b->piece[k]->nverts;
                faces += b->piece[k]->nfacets;
            }
            printf("%3d  0x%08lx  %2d pieces  %4d verts  %4d facets  "
                   "%3d animations\n", i, b->piece[0]->offset, b->npieces,
                   verts, faces, cast.nanim[i]);
        }
        cast_free(&cast);
        return 0;
    }

    /* The sheets: what a character is, as against what he looks like.  The
     * behaviour column is the whole of docs/12-world-and-sheets.md 12.8's open
     * question - it is one of AICTRL.GAS's fourteen AI commands, in the high
     * byte of a word whose low byte is something else again. */
    if (a.list_sheets) {
        Sheets sh;
        if (!sheets_load(&sh, path_boot)) {
            fprintf(stderr, "cannot read the sheets from %s\n", path_boot);
            return 1;
        }
        long off[CAST_MAX];
        for (int i = 0; i < cast.ncast && i < CAST_MAX; i++)
            off[i] = cast.bundle[i].piece[0]->offset;
        sheets_bundles(&sh, off, cast.ncast);
        int used[CSH_MAX] = { 0 };
        for (int i = 0; i < sh.nworld; i++) {
            int k = sheets_of_addr(&sh, sh.world[i].sheet);
            if (k >= 0) used[k]++;
        }
        printf("%d character sheets, %d world records\n", sh.nsheets, sh.nworld);
        printf("  #  addr    models anims  block  bundle  uses  behaviour\n");
        for (int i = 0; i < sh.nsheets; i++) {
            const SheetRec *r = &sh.sheet[i];
            printf("%3d $%05X  %3d %5d  %5d  %6d  %4d  %s", i, r->addr,
                   r->models, r->anims, r->block, r->bundle, used[i],
                   ai_command_name(r->behaviour));
            if (r->behaviour > AI_FOLLOW_PERSON)
                printf(" %d", r->behaviour);   /* past the fourteen the 1995
                                                  LOGICS.INC numbers */
            if (r->property)
                printf("   [+%d]", r->property);
            if (r->model0)
                printf("   model $%05X in the binary", r->model0);
            putchar('\n');
        }
        sheets_free(&sh);
        cast_free(&cast);
        return 0;
    }

    /* The model, if there is one. */
    ModelSet ms = { { NULL, 0 }, NULL, 0, 0 };
    Model *model = NULL;
    int model_index = 0;
    if (a.model || a.list_models) {
        if (!models_open(&ms, a.model ? a.model : "boot", &model_index)) {
            fprintf(stderr, "no models found\n");
            return 1;
        }
        if (a.list_models) {
            for (int i = 0; i < ms.count; i++)
                printf("%3d  $%06X  %4d verts  %4d facets\n", i, ms.list[i].base,
                       ms.list[i].nverts, ms.list[i].nfacets);
            return 0;
        }
        if (model_index < 0 || model_index >= ms.count) {
            fprintf(stderr, "model %d out of range (0..%d)\n", model_index, ms.count - 1);
            return 1;
        }
        model = &ms.list[model_index];
        printf("model %s:%d  $%06X  %d verts  %d facets\n",
               ms.from_boot ? "boot" : "track5", model_index, model->base,
               model->nverts, model->nfacets);
    }

    /* Where to put it.  A world record supplies both the position and the
     * three rotations - and it is the world table that says the item models
     * are stored on their side: every WINE record carries elevation 192. */
    int pos[3] = { 0, 0, 0 };
    int face[3] = { 0, 0, 0 };
    if (model)
        face[0] = ms.from_boot ? 192 : 0;
    if (a.object) {
        int idx = index_object(&ix, a.object, pos, face);
        if (idx < 0) {
            fprintf(stderr, "no world record '%s'\n", a.object);
            return 1;
        }
        printf("object %d at (%d,%d,%d) face %d,%d,%d\n",
               idx, pos[0], pos[1], pos[2], face[0], face[1], face[2]);
    }
    if (a.have_pos)  memcpy(pos, a.pos, sizeof pos);
    if (a.have_face) memcpy(face, a.face, sizeof face);

    /* The scene, if there is one. */
    static Scene scene;
    static uint8_t key[8192];
    int scene_index = -1;
    int have_scene = 0;
    if (a.scene) {
        Blob boot = io_read(path_boot);
        if (!boot.data) {
            fprintf(stderr, "cannot read %s\n", path_boot);
            return 1;
        }
        int ok = scene_key(boot.data, boot.size, key);
        io_free(&boot);
        if (!ok) {
            fprintf(stderr, "no CODE tag in the boot track - wrong file?\n");
            return 1;
        }
        scene_index = index_scene(&ix, a.scene);
        if (scene_index < 0 || !scene_load(&scene, path_pict, scene_index, key)) {
            fprintf(stderr, "cannot load scene '%s'\n", a.scene);
            return 1;
        }
        have_scene = 1;
        printf("scene %d %s  id %d  camera (%d,%d,%d)\n", scene_index,
               index_scene_name(&ix, scene_index), scene.cam.id,
               scene.cam.pos[0], scene.cam.pos[1], scene.cam.pos[2]);
    }

    /* The set the view belongs to: its floor is what puts an object at the
     * right height, and its mesh is the overlay --mesh draws. */
    static Set set;
    int have_set = 0;
    if (have_scene) {
        /* The footer names it outright: the long at +32 is the set's own block
         * on track 3.  set_of_scene is the fallback, and the slower and weaker
         * of the two - it has to vote on which group a set owns. */
        int si = scene_set(&scene.cam);
        if (si < 0 || si >= SET_COUNT)
            si = set_of_scene(path_set, scene.cam.id);
        if (si >= 0 && set_load(&set, path_set, si)) {
            have_set = 1;
            printf("set %d  %d views  %d doorways  %d events  "
                   "%d floor triangles over %d vertices\n",
                   si, set.nscenes, set.nentries, set.nevents,
                   set.ntris, set.nverts);
        } else {
            fprintf(stderr, "no set owns scene id %d\n", scene.cam.id);
        }
    }

    /* Put the object on the floor.  A world record's Ypos is zero in all 197
     * of them: the height a character stands at is GROUNDHEIGHT, the height
     * word of the collision triangle it is on, so y here reads as a height
     * above the floor rather than an absolute. */
    if (have_set && !a.no_ground) {
        int32_t h = 0;
        int tri = set_ground(&set, pos[0], pos[2], -1, &h);
        if (tri >= 0) {
            printf("floor: triangle %d, height %d%s\n", tri, h,
                   pos[1] ? " (plus the y given)" : "");
            pos[1] += h;
        } else {
            printf("floor: (%d,%d) is off the collision mesh\n", pos[0], pos[2]);
        }
    }

    /* The character, if there is one: which bundle, which of its animations,
     * and where on the floor it stands. */
    Bundle *bundle = NULL;
    const Anim *anim = NULL;
    Actor actor;
    memset(&actor, 0, sizeof actor);
    if (have_cast) {
        if (a.character >= cast.ncast) {
            fprintf(stderr, "character %d out of range (0..%d)\n",
                    a.character, cast.ncast - 1);
            return 1;
        }
        bundle = &cast.bundle[a.character];
        if (cast.nanim[a.character] > 0) {
            int k = a.anim;
            if (k < 0 || k >= cast.nanim[a.character]) {
                fprintf(stderr, "animation %d out of range (0..%d)\n",
                        k, cast.nanim[a.character] - 1);
                return 1;
            }
            anim = &cast.anim[cast.first_anim[a.character] + k];
        }
        printf("character %d  %d pieces  %d animations", a.character,
               bundle->npieces, cast.nanim[a.character]);
        if (anim)
            printf("  playing %d: %d frames at %d fps, root %d above the floor",
                   a.anim, anim->frames, anim->fps, anim->height_start);
        putchar('\n');
        if (anim && anim->pieces != bundle->npieces)
            fprintf(stderr, "warning: animation moves %d pieces, bundle has %d\n",
                    anim->pieces, bundle->npieces);

        /* Somewhere to stand.  An explicit position wins; otherwise the set's
         * first doorway, which is a place the game itself puts people. */
        int32_t sx = pos[0], sz = pos[2];
        if (!a.have_pos && have_set) {
            /* The first arrival point that is not the companion's - half the
             * disc's doorways carry two, and the one with bit 0 set is his. */
            const SetEntry *mine = NULL;
            for (int k = 0; k < set.nentries && !mine; k++)
                if (!ENTRY_COMPANION(&set.entry[k]))
                    mine = &set.entry[k];
            if (mine) {
                sx = mine->x;
                sz = mine->z;
            } else if (set.ntris > 0) {
                const SetTri *tr = &set.tri[0];
                int64_t cx = 0, cz = 0;
                for (int k = 0; k < 3; k++) {
                    cx += set.vert[tr->vert[k]].x;
                    cz += set.vert[tr->vert[k]].z;
                }
                sx = (int32_t)(cx / 3);
                sz = (int32_t)(cz / 3);
            }
        }
        if (have_set) {
            if (!actor_place(&actor, &set, sx, sz, face[1]))
                fprintf(stderr, "(%d,%d) is off the collision mesh\n", sx, sz);
            else
                printf("standing at (%d,%d) on triangle %d, floor %d\n",
                       actor.x, actor.z, actor.tri, actor.ground);
        } else {
            actor.x = sx;
            actor.z = sz;
            actor.tri = -1;
            actor.facing = (uint8_t)face[1];
            actor.radius = 100;
        }
        actor.frame = a.frame > 0 ? a.frame : 0;
        if (anim) {
            /* The root's height is HEIGHTSTART plus the y moves up to the
             * frame being shown, which is what the engine's own accumulation
             * comes to when it reaches that frame. */
            for (int f = 0; f <= actor.frame && f < anim->frames; f++) {
                AnimFrame fr;
                anim_frame(anim, f, &fr);
                actor.lift += fr.move[1];
            }
        }
    }

    /* The companion.  Nothing has to be asked for: the character sheets say
     * who follows the player, the init table says where he arrives, and the
     * two together are enough to put a second person in the world.  --alone
     * leaves him out. */
    Sheets sheets;
    Companion co;
    int have_co = 0;
    if (have_cast && have_set && a.drive && !a.alone) {
        if (!sheets_load(&sheets, path_boot))
            fprintf(stderr, "no character sheets in %s - going alone\n", path_boot);
        else if (!companion_open(&co, &sheets, &cast))
            fprintf(stderr, "no aiFollowPlayer sheet with a bundle - going alone\n");
        else {
            have_co = 1;
            companion_place(&co, &set, have_scene ? scene.cam.id : 0xFFFF, &actor);
            printf("companion: sheet %d, %s, bundle %d with %d animations\n",
                   co.sheet, ai_command_name(co.ai.behaviour), co.cast,
                   cast.nanim[co.cast]);
            if (co.placed)
                printf("  standing at (%d,%d) on triangle %d facing %d%s\n",
                       co.actor.x, co.actor.z, co.actor.tri, co.actor.facing,
                       co.placed == 2 ? "  (no entry of his own: beside you)" : "");
            else
                printf("  this set puts him nowhere on its floor\n");
        }
    }

    /* Without a scene the viewer is a turntable: the camera sits at the origin
     * looking down -z and the model is pushed back far enough to fit. */
    SceneCam solo;
    int centre[3] = { 0, 0, 0 };        /* solo mode: the model's own middle */
    if (!have_scene) {
        memset(&solo, 0, sizeof solo);
        for (int i = 0; i < 3; i++)
            solo.m[i * 3 + i] = 16384;
        if (model && !a.have_pos) {
            /* Frame it: the models are built about a joint or a base, not
             * about their middle, so centre on the bounding box and push it
             * back until the box fits the 200-line window. */
            int lo[3] = { 32767, 32767, 32767 }, hi[3] = { -32768, -32768, -32768 };
            for (int i = 0; i < model->nverts; i++)
                for (int k = 0; k < 3; k++) {
                    if (model->vert[i][k] < lo[k]) lo[k] = model->vert[i][k];
                    if (model->vert[i][k] > hi[k]) hi[k] = model->vert[i][k];
                }
            int r = 1;
            for (int k = 0; k < 3; k++) {
                if ((hi[k] - lo[k]) / 2 > r) r = (hi[k] - lo[k]) / 2;
                centre[k] = (lo[k] + hi[k]) / 2;
            }
            pos[0] = pos[1] = 0;
            pos[2] = -(int)(r * 3.4);
        }
        if (bundle && !a.have_pos) {
            /* Same for a character, but its size has to be measured from the
             * pose rather than from any one piece. */
            AnimFrame fr;
            const int8_t *ang = NULL;
            if (anim) { anim_frame(anim, actor.frame, &fr); ang = fr.angle; }
            ActorPose pose[ACTOR_MAX_PIECES];
            int32_t origin[3] = { 0, 0, 0 }, lo, hi;
            actor_pose(bundle, ang, actor.facing, origin, pose);
            actor_extent(bundle, pose, &lo, &hi);
            actor.x = 0;
            actor.y = -(lo + hi) / 2 - (anim ? anim->height_start : 0) - actor.lift;
            actor.z = -(int32_t)((hi - lo) * 1.7);
        }
    }

    static R3dTarget target;
    R3dOpts opts = { a.cull, a.shade, a.wire };
    int spin = 0, show_depth = a.depth, spinning = a.spin, shots = 0;
    int last_tri = actor.tri, last_ground = actor.ground, last_collided = 0;
    Input in = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    /* The player, driven: AICTRL.GAS's own state, one record of it. */
    Control ctl;
    control_init(&ctl);
    int last_stance = 0xFF, last_anim = -2;

    int windowed = !a.no_window;
    if (windowed && !window_open("Highlander viewer", a.scale > 0 ? a.scale : 3))
        windowed = 0;               /* window_open has said why; carry on
                                       headless so --shot still works */

    for (int frame = 0; !in.quit; frame++) {
        if (have_scene && !show_depth)
            r3d_backdrop(&target, &scene);
        else if (have_scene)
            { depth_to_fb(&scene, target.colour);
              for (int i = 0; i < R3D_W * R3D_H; i++) target.depth[i] = scene_z(scene.depth[i]); }
        else
            r3d_clear(&target, 0x18C6, 1 << 20);

        if (a.mesh && have_set && have_scene) {
            /* The floor, drawn where the game thinks it is.  Not depth-tested:
             * the collision plane sits a little *under* the ground the backdrop
             * draws - 12 units in DUN1, about 70 in TENT6 - so testing it
             * against the backdrop hides the whole mesh.  That is the measured
             * offset of docs/13-viewer.md 13.6, not a bug in the overlay. */
            int32_t id[9];
            R3dXform x;
            int32_t origin[3] = { 0, 0, 0 };
            r3d_identity(id);
            r3d_place(&x, &scene.cam, id, origin);
            for (int i = 0; i < set.ntris; i++) {
                const SetTri *tr = &set.tri[i];
                int32_t v[3][3];
                for (int k = 0; k < 3; k++) {
                    int32_t w[3] = { set.vert[tr->vert[k]].x, tr->height,
                                     set.vert[tr->vert[k]].z };
                    r3d_to_view(&x, w, v[k]);
                }
                for (int k = 0; k < 3; k++)
                    r3d_line(&target, v[k], v[(k + 1) % 3],
                             tr->adj[k] < 0 ? 0xF800 : 0x003F, 0);   /* wall red, open edge green */
            }
        }

        if (bundle) {
            int32_t root[3];
            actor_root(&actor, anim, root);
            AnimFrame fr;
            const int8_t *ang = NULL;
            if (anim) { anim_frame(anim, actor.frame, &fr); ang = fr.angle; }
            ActorPose pose[ACTOR_MAX_PIECES];
            actor_pose(bundle, ang, actor.facing, root, pose);
            int fa, te, dr;
            draw_actor(&target, bundle, pose, have_scene ? &scene.cam : &solo,
                       &opts, &fa, &te, &dr);
            if (frame == 0) {
                int32_t lo, hi;
                actor_extent(bundle, pose, &lo, &hi);
                printf("pose: frame %d, root at (%d,%d,%d), facing %d\n",
                       actor.frame, root[0], root[1], root[2], actor.facing);
                if (anim)
                    printf("      %d..%d above the floor; the frame says "
                           "%d..%d\n", lo - actor.y, hi - actor.y,
                           fr.low, fr.high);
                printf("drew %d/%d pixels of %d facets over %d pieces\n",
                       dr, te, fa, bundle->npieces);
                if (have_scene) {
                    /* The same counted criterion the wine bottle got: draw it
                     * again against an empty Z-buffer and see what the scenery
                     * took away. */
                    static R3dTarget alone;
                    int f2, t2, d2;
                    r3d_clear(&alone, 0, 0x7FFFFFFF);
                    draw_actor(&alone, bundle, pose, &scene.cam, &opts, &f2, &t2, &d2);
                    int silhouette = 0, visible = 0;
                    for (int i = 0; i < R3D_W * R3D_H; i++) {
                        if (alone.depth[i] != 0x7FFFFFFF)
                            silhouette++;
                        if (target.depth[i] < scene_z(scene.depth[i]))
                            visible++;
                    }
                    printf("silhouette %d px, %d visible, %d hidden by the "
                           "scene\n", silhouette, visible, silhouette - visible);
                }
            }
        }

        /* And the companion, through exactly the same three calls. */
        if (have_co && co.placed && co.bundle) {
            int32_t root[3];
            actor_root(&co.actor, co.anim, root);
            AnimFrame fr;
            const int8_t *ang = NULL;
            if (co.anim) { anim_frame(co.anim, co.actor.frame, &fr); ang = fr.angle; }
            ActorPose pose[ACTOR_MAX_PIECES];
            actor_pose(co.bundle, ang, co.actor.facing, root, pose);
            int fa, te, dr;
            draw_actor(&target, co.bundle, pose, have_scene ? &scene.cam : &solo,
                       &opts, &fa, &te, &dr);
        }

        if (model) {
            int32_t rot[9], objpos[3] = { pos[0], pos[1], pos[2] };
            r3d_face_matrix(face[0], (face[1] + spin) & 255, face[2], rot);
            if (!have_scene)            /* keep the turntable centred as it turns */
                for (int i = 0; i < 3; i++) {
                    int64_t c = 0;
                    for (int k = 0; k < 3; k++)
                        c += (int64_t)rot[i * 3 + k] * centre[k];
                    objpos[i] -= (int32_t)(c >> 14);
                }
            R3dXform x;
            r3d_place(&x, have_scene ? &scene.cam : &solo, rot, objpos);
            r3d_draw_model(&target, model, &x, &opts);
            if (frame == 0 && have_scene) {
                /* Where the object's own origin lands, and what the backdrop
                 * has at that pixel - the two numbers the depth test compares. */
                int sx, sy;
                if (r3d_project(x.pos, &sx, &sy) &&
                    sx >= 0 && sx < R3D_W && sy >= 0 && sy < R3D_H)
                    printf("origin at (%d,%d)  |z| %d  backdrop %d  -> %s\n",
                           sx, sy, x.pos[2] < 0 ? -x.pos[2] : x.pos[2],
                           scene_z(scene.depth[sy * R3D_W + sx]),
                           (x.pos[2] < 0 ? -x.pos[2] : x.pos[2]) <
                           scene_z(scene.depth[sy * R3D_W + sx]) ? "in front" : "behind");
                else
                    printf("origin off screen\n");
            }
            if (frame == 0)
                printf("drew %d/%d pixels of %d facets, depth %d..%d\n",
                       r3d_stats.drawn, r3d_stats.tested, r3d_stats.facets,
                       r3d_stats.znear, r3d_stats.zfar);

            /* The success criterion, counted rather than eyeballed: draw the
             * model again against an empty Z-buffer to get its silhouette,
             * then see how much of it survived the backdrop's. */
            if (frame == 0 && have_scene) {
                static R3dTarget alone;
                r3d_clear(&alone, 0, 0x7FFFFFFF);
                r3d_draw_model(&alone, model, &x, &opts);
                int silhouette = 0, visible = 0;
                for (int i = 0; i < R3D_W * R3D_H; i++) {
                    if (alone.depth[i] != 0x7FFFFFFF)
                        silhouette++;
                    if (target.depth[i] < scene_z(scene.depth[i]))
                        visible++;
                }
                printf("silhouette %d px, %d visible, %d hidden by the scene\n",
                       silhouette, visible, silhouette - visible);
            }
        }

        if (a.shot && shots == 0 && frame >= a.shot_at) {
            if (!write_ppm(a.shot, target.colour))
                fprintf(stderr, "cannot write %s\n", a.shot);
            else
                printf("wrote %s\n", a.shot);
            shots++;
        }

        if (windowed) {
            window_present(target.colour);
            window_poll(&in);
            if (in.toggle_depth && have_scene) show_depth = !show_depth;
            if (in.toggle_spin) spinning = !spinning;
            if (in.shot) {
                char name[64];
                snprintf(name, sizeof name, "shot%03d.ppm", shots++);
                write_ppm(name, target.colour);
                printf("wrote %s\n", name);
            }
            if ((in.next || in.prev) && have_scene) {
                int n = scene_index + (in.next ? 1 : -1);
                if (n >= 0 && scene_load(&scene, path_pict, n, key)) {
                    scene_index = n;
                    printf("scene %d %s\n", n, index_scene_name(&ix, n));
                }
            }
            if (!bundle) {
                pos[0] += in.dx * 20;
                pos[2] += in.dz * 20;
            }
        }

        /* The character's own turn.  ANIM.GAS works in a frame rate of
         * 256 / fps, so at the animations' own 20 fps that is 12. */
        if (bundle) {
            int frate = anim && anim->fps ? 256 / anim->fps : 12;
            if (a.drive && have_set) {
                /* AICTRL.GAS, in its own order: read the pad, choose the
                 * animation from it and the stance, turn, and only then let
                 * the animation move the character.  Nothing here moves him
                 * directly - the root motion does all of it. */
                int fps = anim && anim->fps ? anim->fps : 20;
                control_pad(&ctl, a.pad ? pad_at(frame) : in.pad);
                int playing = anim && actor.frame + 1 < anim->frames;
                control_action(&ctl, &control_quentin, playing);
                if (ctl.restarted) {
                    int k = ctl.anim;
                    if (k >= 0 && k < cast.nanim[a.character]) {
                        const Anim *pick =
                            &cast.anim[cast.first_anim[a.character] + k];
                        if (pick != anim) {
                            anim = pick;
                            frate = anim->fps ? 256 / anim->fps : 12;
                        }
                        /* Start it again from the top: the next step wraps to
                         * frame zero, which is also where the lift resets. */
                        actor.frame = anim->frames - 1;
                    }
                }
                actor.facing = (uint8_t)(actor.facing + control_turn(&ctl, fps));
                if (anim)
                    actor_step(&actor, &set, anim, frate);
                if (ctl.stance != last_stance || ctl.anim != last_anim) {
                    printf("frame %4d: %-5s animation %2d%s\n", frame,
                           control_stance_name(ctl.stance), ctl.anim,
                           (ctl.pad & PAD(JOY_DOUBLE)) ? "   (double tap)" : "");
                    last_stance = ctl.stance;
                    last_anim   = ctl.anim;
                }
                /* His frame, after the player's: ControlCode runs down the
                 * whole active list before ActionCode starts, so the pad he
                 * presses is aimed at where the player has already got to. */
                if (have_co) {
                    uint8_t was = co.ctl.stance;
                    companion_step(&co, &set, &actor, &cast);
                    if (STANCE(co.ctl.stance) != STANCE(was)) {
                        int d = (int)lrint(sqrt((double)co.ai.dist2));
                        printf("frame %4d: companion %-5s animation %d, "
                               "%d behind you\n", frame,
                               control_stance_name(co.ctl.stance),
                               co.ctl.anim, d);
                    }
                }
            } else if (a.walk)
                actor.facing = (uint8_t)(actor.facing + in.dx * 3);
            if (!a.drive && a.play && anim && have_set) {
                actor_step(&actor, &set, anim, frate);
            } else if (!a.drive && a.play && anim) {
                AnimFrame fr;
                actor.frame = (actor.frame + 1) % anim->frames;
                if (actor.frame == 0)
                    actor.lift = 0;
                anim_frame(anim, actor.frame, &fr);
                actor.lift += fr.move[1];
                actor.facing = (uint8_t)(actor.facing + fr.turn);
            } else if (!a.drive && a.walk && have_set) {
                if (in.dz) {
                    /* Facing 0 looks down +z: the same rotation ANIM.GAS puts
                     * a frame's own root motion through. */
                    double k = 2.0 * 3.14159265358979323846 / 256.0;
                    int32_t sp = -in.dz * 24;
                    int32_t mx = (int32_t)lrint(sp * sin(actor.facing * k));
                    int32_t mz = (int32_t)lrint(sp * cos(actor.facing * k));
                    actor_move(&actor, &set, mx, mz);
                }
                actor_settle(&actor, frate);
            }

            /* Say when the floor underfoot changes - which triangle, how
             * high, and whether a wall stopped the move.  That is the whole of
             * what movement over the mesh has to get right. */
            if ((a.walk || a.play || a.drive) && have_set &&
                (actor.tri != last_tri || actor.ground != last_ground ||
                 actor.collided != last_collided)) {
                printf("frame %4d: (%6d,%6d) triangle %3d floor %5d y %5d"
                       " facing %3d%s\n", frame, actor.x, actor.z, actor.tri,
                       actor.ground, actor.y, actor.facing,
                       actor.collided ? "  - stopped by a wall" : "");
                last_tri = actor.tri;
                last_ground = actor.ground;
                last_collided = actor.collided;
            }

            if ((a.events || a.drive) && have_set && have_scene) {
                int id = events_scan(&set, scene.cam.id, &actor);
                uint16_t from_id = scene.cam.id;   /* the view being left */
                if (id >= 0) {
                    int target_scene = -1;
                    for (int i = 0; i < set.nscenes; i++)
                        if (set.scene[i].id == id)
                            target_scene = set.scene[i].scene;
                    if (target_scene >= 0 &&
                        scene_load(&scene, path_pict, target_scene, key)) {
                        scene_index = target_scene;
                        printf("cut to %s (id %d) at (%d,%d) on triangle %d\n",
                               index_scene_name(&ix, scene_index), id,
                               actor.x, actor.z, actor.tri);
                        /* A doorway is a cut to a view another set owns: its
                         * floor and its events are the ones that matter from
                         * now on, and the two sets do not share an origin, so
                         * the character has to be put down again rather than
                         * carried across.  Where he lands is 10.3's init
                         * table, keyed on the view he is leaving. */
                        int si = set_of_scene(path_set, (uint16_t)id);
                        if (si < 0) {
                            si = scene_set(&scene.cam);
                            if (si >= SET_COUNT) si = -1;
                        }
                        Set next;
                        if (si >= 0 && si != set.index &&
                            set_load(&next, path_set, si)) {
                            set_free(&set);
                            set = next;
                            const SetEntry *door = NULL, *any = NULL;
                            for (int k = 0; k < set.nentries; k++) {
                                const SetEntry *e = &set.entry[k];
                                if (ENTRY_COMPANION(e))
                                    continue;       /* that one is Ramirez */
                                if (e->id == from_id)            door = e;
                                else if (e->id == 0xFFFF && !any) any  = e;
                            }
                            if (!door) door = any;
                            if (door) {
                                if (actor_place(&actor, &set, door->x, door->z,
                                                ENTRY_FACING(door)))
                                    printf("  through the door into set %d:"
                                           " arriving at (%d,%d) facing %d,"
                                           " from view %d%s\n", si,
                                           actor.x, actor.z, actor.facing,
                                           from_id,
                                           door->id == 0xFFFF ?
                                             " (the default entry)" : "");
                                else
                                    printf("  into set %d, but the entry for"
                                           " view %d is off its mesh\n", si,
                                           from_id);
                                last_tri = actor.tri;
                                last_ground = actor.ground;
                            } else {
                                actor_place(&actor, &set, actor.x, actor.z,
                                            actor.facing);
                                printf("  into set %d, which lists no entry"
                                       " for view %d\n", si, from_id);
                            }
                            /* And the other half of the doorway.  He is put
                             * down from the same table, keyed on the same
                             * view - the entry with bit 0 of its flags set. */
                            if (have_co) {
                                companion_place(&co, &set, from_id, &actor);
                                if (co.placed == 1)
                                    printf("  the companion arrives at (%d,%d)"
                                           " facing %d\n", co.actor.x,
                                           co.actor.z, co.actor.facing);
                                else if (co.placed == 2)
                                    printf("  the companion follows you in:"
                                           " this view lists no arrival for"
                                           " him\n");
                                else
                                    printf("  the companion is left behind:"
                                           " nowhere on this floor\n");
                            }
                        }
                    } else if (target_scene < 0) {
                        printf("cut to id %d, which this set does not list\n", id);
                    }
                }
            }
        }
        if (spinning)
            spin = (spin + 2) & 255;
        if (a.frames && frame + 1 >= a.frames)
            break;
        if (!windowed && !a.frames)
            break;      /* --frames N runs headless, which is how walking and
                           the camera cuts get exercised without a window */
    }

    if (windowed)
        window_close();
    if (have_set)
        set_free(&set);
    if (have_cast)
        cast_free(&cast);
    if (ms.list)
        model_free_all(ms.list, ms.count);
    io_free(&ms.file);
    json_free(&ix.j);
    return 0;
}
