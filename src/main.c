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
#include "game/game.h"
#include "game/combat.h"
#include "media/film.h"
#include "media/cinepak.h"
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
    int   film;
    int   fight;
    int   weapon;
} Args;

static char path_pict[512], path_boot[512], path_t5[512], path_set[512];
static char path_film[512];

static void paths(const char *dir)
{
    snprintf(path_pict, sizeof path_pict, "%s/track04_pict.bin", dir);
    snprintf(path_boot, sizeof path_boot, "%s/track02_00004000.bin", dir);
    snprintf(path_t5,   sizeof path_t5,   "%s/track05_data.bin", dir);
    snprintf(path_set,  sizeof path_set,  "%s/track03_data.bin", dir);
    snprintf(path_film, sizeof path_film, "%s/track07_1111.bin", dir);
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

/* The films are decoded to 24-bit and only packed down to the framebuffer's
 * R5 B5 G6 to be shown, so what goes out here is what the decoder produced -
 * which is what makes it comparable, byte for byte, with filmdec.py. */
static int write_ppm_rgb(const char *path, const uint8_t *rgb, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    fwrite(rgb, 3, (size_t)w * h, f);
    fclose(f);
    return 1;
}

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

/* ---- who follows you ------------------------------------------------ */

/* Worked out rather than chosen.  The sheet chain is in the same order as the
 * 1995 `SHEET.S`; four sheets in a row carry `cshBehaviour = aiFollowPlayer`
 * where SHEET.S has RAMIREZ, FAVEB, MANGUA and ARAKA; and the first of the
 * four names track 5 block 1176 through its one file record, which is where
 * bundle 9 begins - fifteen pieces and the four animations `BO_LOGICS_2A`
 * asks for.  Returns the sheet index, and its bundle through `bundle`. */
static int follower_sheet(Sheets *sh, Cast *cast, int *bundle)
{
    long off[CAST_MAX];
    for (int i = 0; i < cast->ncast && i < CAST_MAX; i++)
        off[i] = cast->bundle[i].piece[0]->offset;
    sheets_bundles(sh, off, cast->ncast);

    int k = sheets_by_behaviour(sh, AI_FOLLOW_PLAYER);
    if (k < 0)
        return -1;
    *bundle = sh->sheet[k].bundle;
    return *bundle >= 0 && *bundle < cast->ncast ? k : -1;
}

/* Puts one character into the active character table with his own bundle, his
 * own animations and the joypad table that suits them. */
static int cast_join(ActTable *t, Cast *cast, int world, int sheet, int b,
                     const LogicTable *logic, int behaviour)
{
    int i = act_add(t, world, sheet, b,
                    cast->first_anim[b] >= 0 ? &cast->anim[cast->first_anim[b]]
                                             : NULL,
                    cast->nanim[b], logic);
    if (i >= 0)
        ai_init(&t->a[i].ai, behaviour);
    return i;
}

/* Where a weapon's animations begin inside the player's bundle.
 *
 * Quentin's bundle carries **114** animations and his own sheet declares
 * **30**.  The other 84 are three banks of 28, and there are exactly three
 * sheets on the disc with one model and 28 animations - sheets 22, 23 and 24,
 * the weapons.  30 + 28 + 28 + 28 = 114, and the combat animations land where
 * that arithmetic says they should: `hlview --list-attacks` finds attack and
 * defence values at 19..27, then again at 49..57, 77..85 and 105..113, which
 * is the same 19..27 at offsets 0, 30, 58 and 86.
 *
 * That is `AICTRL.GAS`'s `.weapon_action`: the player's animation number is
 * looked up in the sheet of whatever he is holding before falling back to his
 * own, so picking up a sword does not change the table, it changes which bank
 * the same table's numbers land in.  Bank 0 is bare hands, and its attacks do
 * 2 points; bank 1 does 30, bank 2 does 127 and bank 3 is the one with a reach
 * of 1,000 and an arc of five degrees, which is a thing you shoot with. */
static int weapon_bank(const Sheets *sh, int n)
{
    if (n <= 0)
        return 0;
    int off = sh->nsheets > 1 ? sh->sheet[1].anims : 0;
    int seen = 0;
    for (int i = 0; i < sh->nsheets; i++) {
        if (sh->sheet[i].models != 1 || sh->sheet[i].anims == 0)
            continue;
        if (++seen == n)
            return off;
        off += sh->sheet[i].anims;
    }
    return -1;
}

/* The first world record wearing a given sheet, which is how a character who
 * is only known by his sheet gets a body: the radius, the strength and the
 * life points all live on the world record, and combat reads all three. */
static int world_of_sheet(const Sheets *sh, int sheet)
{
    if (sheet < 0)
        return -1;
    for (int w = 2; w < sh->nworld; w++) {
        if (!sh->world[w].sheet)
            continue;
        if (sheets_of_addr(sh, sh->world[w].sheet) == sheet)
            return w;
    }
    return -1;
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
static int events_only(Set *s, uint16_t scene_id, const Actor *act)
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
    int follow_bundle = -1;
    int follow = -1;
    if (!sheets_load(&sh, path_boot) ||
        (follow = follower_sheet(&sh, &cast, &follow_bundle)) < 0) {
        fprintf(stderr, "no aiFollowPlayer sheet with a bundle\n");
        cast_free(&cast);
        return 0;
    }
    printf("companion: sheet %d, behaviour %s, track 5 block %d, "
           "bundle %d with %d animations\n", follow,
           ai_command_name(sh.sheet[follow].behaviour),
           sh.sheet[follow].block, follow_bundle, cast.nanim[follow_bundle]);

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

        ActTable acts;
        act_init(&acts);
        int ip = cast_join(&acts, &cast, WORLD_PLAYER, 1, 0,
                           &control_quentin, AI_NOP);
        int ic = cast_join(&acts, &cast, WORLD_COMPANION, follow, follow_bundle,
                           &control_follower, sh.sheet[follow].behaviour);
        acts.player = ip;
        if (ip < 0 || ic < 0 ||
            !actor_place(&acts.a[ip].actor, &set, me->x, me->z, ENTRY_FACING(me)) ||
            !actor_place(&acts.a[ic].actor, &set, him->x, him->z, ENTRY_FACING(him))) {
            printf("  set %2d: the entry pair for view %d is off the mesh\n",
                   i, me->id);
            set_free(&set);
            continue;
        }
        const Actor *player = &acts.a[ip].actor, *co = &acts.a[ic].actor;

        int off = 0, farthest = 0;
        for (int f = 0; f < 500; f++) {
            /* 300 frames of forward, then let go and stand.  One call: the
             * table is what AICTRL.GAS's two loops walk, and the player and
             * the follower are two records in it. */
            act_frame(&acts, &set, f < 300 ? PAD(JOY_UP) : 0, NULL);
            if (co->tri < 0)
                off++;
            int64_t dx = co->x - player->x, dz = co->z - player->z;
            int d = (int)lrint(sqrt((double)(dx * dx + dz * dz)));
            if (d > farthest) farthest = d;
        }
        int64_t dx = co->x - player->x, dz = co->z - player->z;
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

/* ---- the films ------------------------------------------------------ */

/* Track 7 is 36 Cinepak films back to back, and a script names one by the CD
 * block it starts in.  What the viewer wants instead is "film 19", so the
 * track is walked once for the inventory - which is also the check that the
 * container reads the same way twice, since assets/films.tsv came out of
 * filmls.py months before any of this. */
#define FILM_MAX 64

static int film_inventory(uint32_t *blocks)
{
    int n = film_scan(path_film, blocks, FILM_MAX);
    if (n <= 0)
        fprintf(stderr, "no films in %s: %s\n", path_film, film_error());
    return n;
}

/* The film's own clock.  A sample's timestamp counts the film's ticks and its
 * duration says how long the frame is held: 50 at a rate of 600, 2 at 24, and
 * 2 and 3 alternating at 30 - which is 12 fps every time.  So the wait is the
 * timestamp turned into milliseconds, not a constant. */
static int play_film_block(const Args *a, uint32_t block, int windowed,
                           long stop)
{
    Film fm;
    if (!film_open(&fm, path_film, block)) {
        fprintf(stderr, "%s\n", film_error());
        return 0;
    }

    static Cinepak cv;
    if (!cinepak_open(&cv, fm.width, fm.height)) {
        fprintf(stderr, "the film at block %u is %dx%d, which is not a "
                "Cinepak picture\n", block, fm.width, fm.height);
        film_close(&fm);
        return 0;
    }
    uint16_t *fb = malloc((size_t)fm.width * fm.height * sizeof *fb);
    if (!fb) {
        cinepak_close(&cv);
        film_close(&fm);
        return 0;
    }

    printf("the film at block %u: %s %dx%d, %d chunks, %u ticks at a rate of "
           "%u = %.1f seconds\n", block, fm.codec, fm.width, fm.height,
           fm.nchunks, fm.ticks, fm.rate,
           fm.rate ? (double)fm.ticks / fm.rate : 0.0);

    /* A film is 320x240 and the game draws 320x200, so the window changes
     * shape for it - which is what the original did too, in video mode. */
    if (windowed && !window_resize(fm.width, fm.height))
        windowed = 0;

    long frames = 0, errors = 0, audio = 0;
    int quit = 0, shot = 0;
    uint64_t t0 = windowed ? window_ms() : 0;
    Input in = { 0 };           /* window_poll latches quit, so it is kept  */

    for (int c = 0; c < fm.nchunks && !quit; c++) {
        if (!film_chunk(&fm, c)) {
            fprintf(stderr, "%s\n", film_error());
            errors++;
            break;
        }
        for (int i = 0; i < fm.nsamples && !quit; i++) {
            const FilmSample *s = &fm.sample[i];
            if (film_audio(s)) {
                audio += s->size;       /* phase 7's, and already verified   */
                continue;
            }
            int e = cinepak_frame(&cv, film_data(&fm, s), s->size);
            if (e != CVID_OK) {
                printf("  frame %ld: %s\n", frames, cinepak_why(e));
                errors++;
                continue;
            }
            if (a->shot && !shot && frames >= a->shot_at) {
                if (write_ppm_rgb(a->shot, cv.rgb, cv.w, cv.h))
                    printf("wrote %s: frame %ld\n", a->shot, frames);
                else
                    fprintf(stderr, "cannot write %s\n", a->shot);
                shot = 1;
                if (!windowed)
                    quit = 1;           /* nothing else to do without a window */
            }
            if (windowed) {
                /* Wait for the frame's own moment, then show it. */
                uint64_t due = t0 + (uint64_t)film_ticks(s) * 1000 / fm.rate;
                uint64_t now = window_ms();
                if (due > now && due - now < 2000)
                    window_sleep((uint32_t)(due - now));
                cinepak_rgb16(&cv, fb);
                window_present(fb);
                window_poll(&in);
                if (in.quit)
                    quit = 1;
            }
            frames++;
            if (stop > 0 && frames >= stop)
                quit = 1;
        }
    }

    uint64_t took = windowed ? window_ms() - t0 : 0;
    printf("%ld frames, %ld of them whole pictures, %ld bytes of speech",
           cv.frames, cv.keyframes, audio);
    if (windowed && took)
        printf(", played in %.1f s at %.1f fps", took / 1000.0,
               frames * 1000.0 / (double)took);
    printf("\n");

    free(fb);
    cinepak_close(&cv);
    film_close(&fm);
    return errors == 0;
}

/* `--film N`: the same film, named by where it sits on the track rather than
 * by the block a script would give, and in a window of its own. */
static int play_film(const Args *a, int which)
{
    uint32_t blocks[FILM_MAX];
    int nfilms = film_inventory(blocks);
    if (nfilms <= 0)
        return 0;
    if (which < 0 || which >= nfilms) {
        fprintf(stderr, "film %d: the track has %d\n", which, nfilms);
        return 0;
    }
    printf("film %d is ", which);

    int windowed = !a->no_window;
    if (windowed && !window_open_size("Highlander film", 320, 240,
                                      a->scale > 0 ? a->scale : 2))
        windowed = 0;
    int ok = play_film_block(a, blocks[which], windowed, a->frames);
    if (windowed)
        window_close();
    return ok;
}

/* Every frame of all 36 films, decoded.
 *
 * Three things have to come out at zero.  No decoder error - a Cinepak stream
 * that is being read a byte out of step runs out of vectors before it runs out
 * of picture, so drift shows up here rather than as a smear.  No chunk whose
 * sample table does not account for its bytes - the container is ours and this
 * is what says it is read whole.  And no frame whose timestamp disagrees with
 * what the picture turned out to be: bit 31 is clear on exactly the frames a
 * player may start at, which is a whole picture or the one after an audio
 * block, and that can only be checked with a decoder in hand.
 *
 * The frame counts are for reading against filmdec.py's --frames, which walks
 * the same container in Python. */
static int check_film(void)
{
    uint32_t blocks[FILM_MAX];
    int nfilms = film_inventory(blocks);
    if (nfilms <= 0)
        return 0;

    static Cinepak cv;
    long frames = 0, keys = 0, errors = 0, unaccounted = 0, resume = 0;
    long long audio = 0;
    double running = 0.0;

    for (int n = 0; n < nfilms; n++) {
        Film fm;
        if (!film_open(&fm, path_film, blocks[n])) {
            printf("film %2d: %s\n", n, film_error());
            errors++;
            continue;
        }
        if (strcmp(fm.codec, "cvid") != 0) {
            printf("film %2d: codec %s\n", n, fm.codec);
            errors++;
        }
        if (!cinepak_open(&cv, fm.width, fm.height)) {
            printf("film %2d: %dx%d\n", n, fm.width, fm.height);
            errors++;
            film_close(&fm);
            continue;
        }

        long fbad = 0, fslack = 0, fresume = 0, fblocks = 0;
        long long fdur = 0;
        int prev_audio = 0;

        for (int c = 0; c < fm.nchunks; c++) {
            if (!film_chunk(&fm, c)) {
                printf("  film %2d: %s\n", n, film_error());
                fbad++;
                continue;
            }
            if (film_unaccounted(&fm) != 0) {
                printf("  film %2d chunk %d: %u bytes no sample accounts for\n",
                       n, c, film_unaccounted(&fm));
                fslack++;
            }
            for (int i = 0; i < fm.nsamples; i++) {
                const FilmSample *s = &fm.sample[i];
                if (film_audio(s)) {
                    audio += s->size;
                    fblocks++;
                    prev_audio = 1;
                    continue;
                }
                int e = cinepak_frame(&cv, film_data(&fm, s), s->size);
                if (e != CVID_OK) {
                    printf("  film %2d chunk %d frame %ld: %s\n", n, c,
                           cv.frames, cinepak_why(e));
                    fbad++;
                } else if (film_resume(s) != (cv.keyframe || prev_audio)) {
                    fresume++;
                }
                fdur += s->dur;
                prev_audio = 0;
            }
        }

        double secs = fm.rate ? (double)fdur / fm.rate : 0.0;
        printf("film %2d  block %6d  %3d chunks  %5ld frames  %4ld whole"
               "  %4ld audio  %6.1f s  %4.1f fps%s\n", n, (int)blocks[n],
               fm.nchunks, cv.frames, cv.keyframes, fblocks, secs,
               secs > 0 ? cv.frames / secs : 0.0,
               fbad || fslack || fresume ? "  <-" : "");

        frames      += cv.frames;
        keys        += cv.keyframes;
        running     += secs;
        errors      += fbad;
        unaccounted += fslack;
        resume      += fresume;
        cinepak_close(&cv);
        film_close(&fm);
    }

    printf("films: %d of them, %ld frames decoded, %ld whole pictures\n",
           nfilms, frames, keys);
    printf("  %ld decoder errors, %ld chunks their sample table does not "
           "account for\n", errors, unaccounted);
    printf("  %ld frames whose timestamp disagrees with the picture\n", resume);
    printf("  %.0f seconds of picture at %.1f fps, and %lld bytes of speech, "
           "which is %.0f seconds at 22,252 Hz\n", running,
           running > 0 ? frames / running : 0.0, audio, audio / 22252.0);
    return errors == 0 && unaccounted == 0 && resume == 0 && frames > 0;
}

/* ---- what the animations say about combat --------------------------- */

/* Combat is not a table somewhere: it is in the animation data.  Every frame
 * carries `animHit` - positive is an attack, negative is a defence - with
 * `animRange` for the reach and `animDirAz` / `animSprAz` for the direction it
 * covers and how wide that arc is, and COMBAT.GAS reads them straight out of
 * the frame the character is on.  So which of a bundle's animations are swings
 * is a question the disc answers.  This prints the answer. */
static int list_attacks(void)
{
    Cast c;
    if (!cast_open(&c)) {
        fprintf(stderr, "no character bundles on track 5\n");
        return 0;
    }

    long attacks = 0, defences = 0, anims = 0, hitanims = 0;

    for (int b = 0; b < c.ncast; b++) {
        if (c.first_anim[b] < 0)
            continue;
        printf("bundle %2d, %d animations\n", b, c.nanim[b]);
        for (int i = 0; i < c.nanim[b]; i++) {
            const Anim *a = &c.anim[c.first_anim[b] + i];
            anims++;
            int nhit = 0, ndef = 0, first = -1;
            int peak = 0, reach = 0, dir = 0, spread = 0;
            for (int f = 0; f < a->frames; f++) {
                AnimFrame fr;
                anim_frame(a, f, &fr);
                if (!fr.hit)
                    continue;
                if (first < 0) {
                    first = f;
                    reach = fr.range;
                    dir = fr.dir[0];
                    spread = fr.spread[0];
                }
                if (fr.hit > 0) { nhit++; attacks++; }
                else            { ndef++; defences++; }
                if (fr.hit > peak || -fr.hit > peak)
                    peak = fr.hit > 0 ? fr.hit : -fr.hit;
            }
            if (first < 0)
                continue;
            hitanims++;
            printf("  anim %3d  %2d frames  %s on frame %d of %d"
                   "  %d attack %d defence  peak %d  reach %d"
                   "  direction %d spread %d\n",
                   i, a->frames, nhit ? "attack" : "defence", first,
                   a->frames, nhit, ndef, peak, reach, dir, spread);
        }
    }
    printf("%ld animations, %ld of them carrying a hit value: "
           "%ld attack frames and %ld defence frames\n",
           anims, hitanims, attacks, defences);
    return 1;
}

/* Every animation of one bundle, read off its own root motion the way
 * session 8 read Quentin's thirty: how far the root travels, how far it
 * turns, and what combat values its frames carry.  That is how an animation
 * gets a name here - nothing on the disc names them. */
static int list_anims(int which)
{
    Cast c;
    if (!cast_open(&c)) {
        fprintf(stderr, "no character bundles on track 5\n");
        return 0;
    }
    if (which < 0 || which >= c.ncast || c.first_anim[which] < 0) {
        fprintf(stderr, "bundle %d: the track has %d\n", which, c.ncast);
        return 0;
    }
    printf("bundle %d, %d animations\n", which, c.nanim[which]);
    printf("  #  frames    dx    dy    dz   turn  per frame  high0 highN  hit\n");
    for (int i = 0; i < c.nanim[which]; i++) {
        const Anim *a = &c.anim[c.first_anim[which] + i];
        long dx = 0, dy = 0, dz = 0, turn = 0;
        int hit = 0, def = 0;
        for (int f = 0; f < a->frames; f++) {
            AnimFrame fr;
            anim_frame(a, f, &fr);
            dx += fr.move[0];
            dy += fr.move[1];
            dz += fr.move[2];
            turn += fr.turn;
            if (fr.hit > 0) hit++;
            else if (fr.hit < 0) def++;
        }
        AnimFrame f0, fn;
        anim_frame(a, 0, &f0);
        anim_frame(a, a->frames - 1, &fn);
        printf("%3d  %6d %5ld %5ld %5ld %6ld %10.1f %6d %5d", i, a->frames,
               dx, dy, dz, turn,
               a->frames ? (double)dz / a->frames : 0.0, f0.high, fn.high);
        if (hit || def)
            printf("  %d attack %d defence", hit, def);
        printf("\n");
    }
    return 1;
}

/* Three readings of one animation, for the check below: how high the pose is
 * at its first or last frame, how far the root travels along z, and the hit
 * value the frames carry. */
static int c_nanim(const Cast *c, int b)
{
    return b >= 0 && b < c->ncast && c->first_anim[b] >= 0 ? c->nanim[b] : 0;
}

static const Anim *c_anim(const Cast *c, int b, int k)
{
    return k < c_nanim(c, b) ? &c->anim[c->first_anim[b] + k] : NULL;
}

static int anim_high(const Cast *c, int b, int k, int last)
{
    const Anim *a = c_anim(c, b, k);
    if (!a)
        return 0;
    AnimFrame f;
    anim_frame(a, last ? a->frames - 1 : 0, &f);
    return f.high;
}

static long anim_move(const Cast *c, int b, int k)
{
    const Anim *a = c_anim(c, b, k);
    long dz = 0;
    if (!a)
        return 0;
    for (int i = 0; i < a->frames; i++) {
        AnimFrame f;
        anim_frame(a, i, &f);
        dz += f.move[2];
    }
    return dz;
}

/* Positive if the animation attacks, negative if it defends, zero if neither. */
static int anim_hit(const Cast *c, int b, int k)
{
    const Anim *a = c_anim(c, b, k);
    if (!a)
        return 0;
    for (int i = 0; i < a->frames; i++) {
        AnimFrame f;
        anim_frame(a, i, &f);
        if (f.hit)
            return f.hit;
    }
    return 0;
}

static int32_t dist_between(const Actor *a, const Actor *b)
{
    double dx = a->x - b->x, dz = a->z - b->z;
    return (int32_t)lrint(sqrt(dx * dx + dz * dz));
}

/* ---- the combat, checked -------------------------------------------- */

/* Two things have to hold, and neither of them is "it looked like a fight".
 *
 * **The animation numbering is a convention shared by every bundle.** The
 * joypad table this port drives everybody with was read off Quentin's thirty
 * animations alone - 6 is the stand, 10 the walk, 15 and 16 the turns - and
 * every other character on the disc is then driven through the same numbers.
 * That is only sound if the numbering is the format's rather than Quentin's,
 * so ask the data: in every bundle that carries a full bank, 0 to 3 must end
 * with the body on the floor, 4 and 5 must stay upright and travel backwards
 * and forwards, 8 must be a pose already on the floor, 19 to 25 must carry a
 * positive `animHit` and 26 and 27 a negative one.  Nothing in this test knows
 * which character it is looking at.
 *
 * **A duel resolves, and only between the player and somebody else.** Put the
 * player, a hunter and a second hunter down in a real set at a range of real
 * distances, hold the attack button, and run it out.  What must come out of it
 * is that somebody dies; that no life value ever rises or goes below zero; and
 * that the two hunters never take a point off each other, which is
 * COMBAT.GAS's own rule of 1/05/95 and the one thing about this pass that a
 * bug would quietly undo. */
static int check_combat(void)
{
    Cast cast;
    if (!cast_open(&cast)) {
        fprintf(stderr, "no character bundles on track 5\n");
        return 0;
    }
    Sheets sh;
    if (!sheets_load(&sh, path_boot)) {
        fprintf(stderr, "no world table in %s\n", path_boot);
        return 0;
    }
    long off[CAST_MAX];
    for (int i = 0; i < cast.ncast && i < CAST_MAX; i++)
        off[i] = cast.bundle[i].piece[0]->offset;
    sheets_bundles(&sh, off, cast.ncast);

    /* 1: the convention. */
    int banks = 0, wrong = 0;
    for (int b = 0; b < cast.ncast; b++) {
        if (c_nanim(&cast, b) < 28)
            continue;
        banks++;
        int bad = 0;
        for (int k = 0; k < 4; k++)
            if (anim_high(&cast, b, k, 0) < 350 || anim_high(&cast, b, k, 1) > 200)
                bad++;                          /* the four that end down    */
        if (anim_high(&cast, b, 4, 1) < 350 || anim_move(&cast, b, 4) > -100)
            bad++;                              /* knocked back, upright     */
        if (anim_high(&cast, b, 5, 1) < 350 || anim_move(&cast, b, 5) < 100)
            bad++;                              /* knocked forward, upright  */
        if (anim_high(&cast, b, 8, 0) > 200)
            bad++;                              /* already lying there       */
        for (int k = 19; k <= 25; k++)
            if (anim_hit(&cast, b, k) <= 0)
                bad++;
        for (int k = 26; k <= 27; k++)
            if (anim_hit(&cast, b, k) >= 0)
                bad++;
        if (bad) {
            printf("  bundle %2d: %d of the fourteen roles do not hold\n",
                   b, bad);
            wrong += bad;
        }
    }
    printf("animation roles: %d bundles with a full bank, %d departures from "
           "the convention\n", banks, wrong);

    /* 2: the duel, one on one. */
    int hs = sheets_by_behaviour(&sh, AI_ATTACK_PLAYER);
    int hb = hs >= 0 ? sh.sheet[hs].bundle : -1;
    int hw = world_of_sheet(&sh, hs);
    int hw2 = -1;
    for (int w = hw + 1; w < sh.nworld && hw >= 0; w++)
        if (sh.world[w].sheet && sheets_of_addr(&sh, sh.world[w].sheet) == hs) {
            hw2 = w;
            break;
        }
    if (hb < 0 || hw < 0 || hw2 < 0) {
        fprintf(stderr, "no aiAttackPlayer sheet with a bundle and two world "
                        "records\n");
        return 0;
    }
    printf("hunter: sheet %d, bundle %d with %d animations, world records %d "
           "and %d\n", hs, hb, c_nanim(&cast, hb), hw, hw2);

    Blob boot = io_read(path_boot);
    const uint8_t *world = boot.data
                         ? boot.data + (0x15458 - 0x5000 + 0x12600) : NULL;
    static uint8_t ws[WS_COUNT * WS_REC];

    long duels = 0, resolved = 0, rose = 0, armed_wins = 0, bare_wins = 0;
    long total_damage = 0, frames_to_kill = 0;

    for (int trial = 0; trial < 16; trial++) {
        Set set;
        if (!set_load(&set, path_set, 19))          /* DUN1: room to move   */
            break;
        ActTable acts;
        act_init(&acts);
        int ip = cast_join(&acts, &cast, WORLD_PLAYER, 1, 0,
                           &control_quentin, AI_NOP);
        acts.player = ip;
        /* Armed on half the trials, which is what decides who wins. */
        int bank = (trial & 1) ? weapon_bank(&sh, 1) : 0;
        if (bank > 0 && bank + 28 <= c_nanim(&cast, 0)) {
            acts.a[ip].anims  += bank;
            acts.a[ip].nanims -= bank;
        }
        int ih = cast_join(&acts, &cast, hw, hs, hb, &control_quentin,
                           AI_ATTACK_PLAYER);

        const SetEntry *e0 = &set.entry[set.nentries ? trial % set.nentries : 0];
        int32_t px = e0->x, pz = e0->z;
        if (ip < 0 || ih < 0 || !actor_place(&acts.a[ip].actor, &set, px, pz, 0)) {
            set_free(&set);
            continue;
        }
        double k = 2.0 * 3.14159265358979323846 / 256.0;
        int a1 = (trial * 37) & 0xFF;
        int ok = 0;
        for (int try = 0; try < 6 && !ok; try++) {
            int32_t r = 400 + try * 120;
            ok = actor_place(&acts.a[ih].actor, &set,
                             px + (int32_t)lrint(r * sin(a1 * k)),
                             pz + (int32_t)lrint(r * cos(a1 * k)),
                             (a1 + 128) & 0xFF);
        }
        if (!ok) {
            set_free(&set);
            continue;
        }
        duels++;

        if (world)
            memcpy(ws, world, sizeof ws);
        int rec[2] = { WORLD_PLAYER, hw };
        int life[2] = { ws[rec[0] * WS_REC + WS_LIFE],
                        ws[rec[1] * WS_REC + WS_LIFE] };
        int dead = -1, f;

        for (f = 0; f < 20000 && dead < 0; f++) {
            act_frame(&acts, &set, PAD(FIRE_A), ws);
            combat_frame(&acts, ws);
            total_damage += combat_stats.damage;
            for (int q = 0; q < 2; q++) {
                int now = ws[rec[q] * WS_REC + WS_LIFE];
                if (now > life[q])
                    rose++;
                life[q] = now;
                if (now == 0)
                    dead = q;
            }
        }
        if (dead >= 0) {
            resolved++;
            frames_to_kill += f;
            if (dead == 1) {
                if (bank) armed_wins++;
                else      bare_wins++;
            }
        } else {
            printf("  trial %2d, %s: a stand-off - life %d against %d after "
                   "20,000 frames\n", trial, bank ? "armed" : "bare",
                   life[0], life[1]);
        }
        set_free(&set);
    }

    printf("duels: %ld fought one on one, %ld ended with somebody dead, "
           "%.0f frames each\n", duels, resolved,
           resolved ? (double)frames_to_kill / resolved : 0.0);
    printf("  the hunter died %ld of the %ld times the player had a weapon "
           "and %ld of the %ld he had his hands\n", armed_wins, duels / 2,
           bare_wins, duels - duels / 2);
    printf("  %ld life values that went up, which is what says no life byte "
           "wrapped\n", rose);

    /* 3: and they must not fight each other.  Two hunters together, the
     * player a long way off: COMBAT.GAS drops any pair that is not one of
     * each, and this is the only thing in the pass that would quietly undo
     * it. */
    long crossfire = 0, together = 0;
    {
        Set set;
        if (set_load(&set, path_set, 19)) {
            ActTable acts;
            act_init(&acts);
            int ip = cast_join(&acts, &cast, WORLD_PLAYER, 1, 0,
                               &control_quentin, AI_NOP);
            acts.player = ip;
            int i1 = cast_join(&acts, &cast, hw,  hs, hb, &control_quentin,
                               AI_ATTACK_PLAYER);
            int i2 = cast_join(&acts, &cast, hw2, hs, hb, &control_quentin,
                               AI_ATTACK_PLAYER);
            const SetEntry *e0 = &set.entry[0];
            if (world)
                memcpy(ws, world, sizeof ws);
            /* Take the player out of it the way the engine itself does - a
             * zero radius, which COMBAT.GAS reads as uncollidable and drops
             * every pair he is in.  What is left is the two hunters. */
            ws[WORLD_PLAYER * WS_REC + WS_RADIUS] = 0;
            ws[WORLD_PLAYER * WS_REC + WS_RADIUS + 1] = 0;
            if (actor_place(&acts.a[ip].actor, &set, e0->x, e0->z, 0) &&
                actor_place(&acts.a[i1].actor, &set, e0->x + 150, e0->z, 64) &&
                actor_place(&acts.a[i2].actor, &set, e0->x - 150, e0->z, 192)) {
                int l1 = ws[hw * WS_REC + WS_LIFE];
                int l2 = ws[hw2 * WS_REC + WS_LIFE];
                for (int f = 0; f < 1200; f++) {
                    act_frame(&acts, &set, PAD(FIRE_A), ws);
                    combat_frame(&acts, ws);
                    if (dist_between(&acts.a[i1].actor,
                                     &acts.a[i2].actor) < 600)
                        together++;
                }
                crossfire = (l1 - ws[hw * WS_REC + WS_LIFE]) +
                            (l2 - ws[hw2 * WS_REC + WS_LIFE]);
                /* Whatever the player did to them is his; what matters is
                 * that the two of them, standing on top of each other for
                 * most of the run, never took a point off each other. */
            }
            set_free(&set);
        }
    }
    printf("  two hunters within reach of each other for %ld frames of "
           "1,200, and %ld points of damage between them\n", together,
           crossfire);

    io_free(&boot);

    return wrong == 0 && duels > 0 && resolved == duels && rose == 0 &&
           armed_wins > bare_wins && together > 0 && crossfire == 0;
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
"  --fight           put a hunter in the set, two metres in front\n"
"  --weapon N        1, 2 or 3: which bank of the bundle he swings\n"
"  --list-sheets     the character sheets, and what each one wears\n"
"  --list-anims N    one bundle's animations, by their own root motion\n"
"  --list-attacks    which animations carry a blow, and how hard\n"
"  --film N          play one of the 36 Cinepak films, at its own 12 fps\n"
"                    with --shot F.ppm --shot-at N it writes frame N out\n"
"  --check-mesh | --check-char | --check-doors | --check-follow\n"
"  --check-script | --check-film | --check-combat\n");
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
               0, -1, 0, 0 };

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
        else if (!strcmp(argv[i], "--list-attacks")) { paths(a.tracks); return !list_attacks(); }
        else if (!strcmp(argv[i], "--list-anims") && v) { paths(a.tracks); return !list_anims(atoi(argv[++i])); }
        else if (!strcmp(argv[i], "--alone"))        a.alone = 1;
        else if (!strcmp(argv[i], "--fight"))        a.fight = 1;
        else if (!strcmp(argv[i], "--weapon") && v)  a.weapon = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--film")     && v) a.film = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--check-mesh")) { paths(a.tracks); return !check_mesh(); }
        else if (!strcmp(argv[i], "--check-char")) { paths(a.tracks); return !check_char(); }
        else if (!strcmp(argv[i], "--check-doors")) { paths(a.tracks); return !check_doors(); }
        else if (!strcmp(argv[i], "--check-follow")) { paths(a.tracks); return !check_follow(); }
        else if (!strcmp(argv[i], "--check-script")) { paths(a.tracks); return !check_script(); }
        else if (!strcmp(argv[i], "--check-film")) { paths(a.tracks); return !check_film(); }
        else if (!strcmp(argv[i], "--check-combat")) { paths(a.tracks); return !check_combat(); }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
        else { usage(); return argv[i][0] == '-' ? 1 : 0; }
    }
    paths(a.tracks);
    if (a.pad) parse_pad(a.pad);

    /* A film is its own thing: it has no scene, no set and no character, and
     * it runs on the film's clock rather than on the game's. */
    if (a.film >= 0)
        return !play_film(&a, a.film);

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

    /* The game, if we are playing rather than looking.  Everything --drive
     * needs now lives behind one call a frame: the script machine, the two
     * master loops over the active characters, the set's event lines and the
     * doorways.  What stays here is what needs a screen - loading a backdrop
     * and drawing. */
    Game game;
    int have_game = 0, hunter = -1;
    if (a.drive && have_cast && have_set && have_scene) {
        if (!game_open(&game, path_set, path_boot)) {
            fprintf(stderr, "cannot open the world tables from %s\n", path_boot);
            return 1;
        }
        int fb = -1, fs = follower_sheet(&game.sheets, &cast, &fb);
        int ip = cast_join(&game.act, &cast, WORLD_PLAYER, 1, a.character,
                           &control_quentin, AI_NOP);
        game.act.player = ip;
        printf("player: world %d, bundle %d with %d animations\n",
               WORLD_PLAYER, a.character, cast.nanim[a.character]);

        /* What he is holding, which is an offset into his own bundle and
         * nothing else - the table's animation numbers do not change. */
        int bank = weapon_bank(&game.sheets, a.weapon);
        if (a.weapon > 0 && ip >= 0 && bank >= 0 &&
            bank + 28 <= cast.nanim[a.character]) {
            game.act.a[ip].anims  += bank;
            game.act.a[ip].nanims -= bank;
            printf("weapon %d: animations %d..%d of the bundle\n", a.weapon,
                   bank, bank + 27);
        } else if (a.weapon > 0) {
            fprintf(stderr, "no weapon bank %d in this bundle\n", a.weapon);
        }
        if (fs >= 0 && !a.alone) {
            cast_join(&game.act, &cast, WORLD_COMPANION, fs, fb,
                      &control_follower, game.sheets.sheet[fs].behaviour);
            printf("companion: world %d, sheet %d, %s, bundle %d with %d "
                   "animations\n", WORLD_COMPANION, fs,
                   ai_command_name(game.sheets.sheet[fs].behaviour), fb,
                   cast.nanim[fb]);
        } else if (!a.alone) {
            fprintf(stderr, "no aiFollowPlayer sheet with a bundle - "
                            "going alone\n");
        }

        /* And somebody to fight, if asked.  A hunter is a sheet whose
         * cshBehaviour reads aiAttackPlayer, a world record wearing that
         * sheet - which is where his reach, his strength and his life points
         * come from - and the same joypad table everyone else uses, because
         * his bundle numbers its animations the same way. */
        if (a.fight) {
            int hs = sheets_by_behaviour(&game.sheets, AI_ATTACK_PLAYER);
            int hb = hs >= 0 ? game.sheets.sheet[hs].bundle : -1;
            int hw = world_of_sheet(&game.sheets, hs);
            if (hb >= 0 && hb < cast.ncast && hw >= 0) {
                int ih = cast_join(&game.act, &cast, hw, hs, hb,
                                   &control_quentin, AI_ATTACK_PLAYER);
                if (ih >= 0) {
                    hunter = ih;
                    printf("hunter: world %d, sheet %d, %s, bundle %d with %d "
                           "animations\n", hw, hs,
                           ai_command_name(AI_ATTACK_PLAYER), hb,
                           cast.nanim[hb]);
                }
            } else {
                fprintf(stderr, "no aiAttackPlayer sheet with a bundle and a "
                                "world record\n");
            }
        }

        /* Arrive.  The set the scene footer names is the one that owns it, so
         * there is no search to do on the way in. */
        int hint = scene_set(&scene.cam);
        if (!game_enter(&game, scene.cam.id, hint < SET_COUNT ? hint : -1)) {
            fprintf(stderr, "no set owns scene id %d\n", scene.cam.id);
            return 1;
        }
        /* The starting position: an explicit --pos wins over the arrival the
         * init table chose, which is what makes a run reproducible. */
        if (a.have_pos && ip >= 0)
            actor_place(&game.act.a[ip].actor, &game.set, a.pos[0], a.pos[2],
                        a.have_face ? a.face[1] : game.act.a[ip].actor.facing);
        /* Two metres in front of where the player is standing, facing him -
         * which is inside his sentry range and outside his reach, so the
         * first thing that happens is that he closes. */
        if (hunter >= 0 && ip >= 0) {
            const Actor *q = &game.act.a[ip].actor;
            double k = 2.0 * 3.14159265358979323846 / 256.0;
            int32_t hx = q->x + (int32_t)lrint(700 * sin(q->facing * k));
            int32_t hz = q->z + (int32_t)lrint(700 * cos(q->facing * k));
            if (!actor_place(&game.act.a[hunter].actor, &game.set, hx, hz,
                             (q->facing + 128) & 0xFF))
                actor_place(&game.act.a[hunter].actor, &game.set, q->x, q->z,
                            (q->facing + 128) & 0xFF);
            vm_register(&game.vm, game.act.a[hunter].world);
        }

        have_game = 1;
        for (int i = 0; i < game.act.n; i++)
            printf("  %-9s at (%6d,%6d) on triangle %3d facing %3d\n",
                   i == game.act.player ? "player"
                                        : i == hunter ? "hunter" : "companion",
                   game.act.a[i].actor.x, game.act.a[i].actor.z,
                   game.act.a[i].actor.tri, game.act.a[i].actor.facing);
        printf("  the machine starts on the first frame: MAINSCRIPT, and set"
               " %d's own script if it has one (%d bytes)\n",
               game.set.index, game.set.script_len);
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
    int last_stance = 0xFF, last_anim = -2, last_procs = -1;
    long last_trih = 0;

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

        if (bundle && !have_game) {
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

        /* Playing rather than looking: everybody in the active character
         * table, through exactly the same three calls. */
        if (have_game) {
            for (int i = 0; i < game.act.n; i++) {
                const Act *ac = &game.act.a[i];
                if (!(ac->flags & ACT_CREATED) || ac->cast < 0 ||
                    ac->actor.tri < 0)
                    continue;
                const Bundle *b = &cast.bundle[ac->cast];
                int32_t root[3];
                actor_root(&ac->actor, ac->anim, root);
                AnimFrame fr;
                const int8_t *ang = NULL;
                if (ac->anim) {
                    anim_frame(ac->anim, ac->actor.frame, &fr);
                    ang = fr.angle;
                }
                ActorPose pose[ACTOR_MAX_PIECES];
                actor_pose(b, ang, ac->actor.facing, root, pose);
                int fa, te, dr;
                draw_actor(&target, b, pose, &scene.cam, &opts, &fa, &te, &dr);
            }
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

        /* Playing: one call, and everything a game frame is happens behind
         * it - the script machine, the joypad for everybody, the animation
         * that moves them, and the event lines they cross.  What comes back
         * is a request or two the host has to act on. */
        if (have_game) {
            game_frame(&game, a.pad ? pad_at(frame) : in.pad);

            /* The handshake, and the whole of it: the machine has posted
             * EVENT_TYPE_CINEPAK + 1 and will not run another command until
             * it is cleared, so playing the film and clearing the event is
             * all a host has to do.  The window changes shape for the film
             * and back again afterwards. */
            if (game.want_film) {
                printf("frame %4ld: a script asks for the film at block %u\n",
                       game.frame, game.want_film);
                if (windowed) {
                    play_film_block(&a, game.want_film, 1, 0);
                    window_resize(SCENE_W, SCENE_H);
                }
                game.want_film = 0;
            }
            game.want_sample = -1;
            if (game.want_scene >= 0) {
                /* The set lists every view it can cut to, its own and the ones
                 * it borrows from next door, so the slot to load is found
                 * before the arrival changes which set we are in. */
                uint16_t id = (uint16_t)game.want_scene;
                game.want_scene = -1;
                int target = -1;
                for (int i = 0; i < game.set.nscenes; i++)
                    if (game.set.scene[i].id == id)
                        target = game.set.scene[i].scene;
                if (target >= 0 && scene_load(&scene, path_pict, target, key)) {
                    scene_index = target;
                    int hint = scene_set(&scene.cam);
                    uint16_t from = game.scene;
                    game_enter(&game, id, hint < SET_COUNT ? hint : -1);
                    printf("frame %4ld: cut to %s (id %d)\n", game.frame,
                           index_scene_name(&ix, scene_index), id);
                    if (game.entered_set) {
                        printf("  through the door into set %d, from view %d\n",
                               game.set.index, from);
                        for (int i = 0; i < game.act.n; i++)
                            printf("    %-9s arrives at (%6d,%6d) on triangle "
                                   "%3d facing %3d\n",
                                   i == game.act.player ? "player" : "companion",
                                   game.act.a[i].actor.x, game.act.a[i].actor.z,
                                   game.act.a[i].actor.tri,
                                   game.act.a[i].actor.facing);
                        last_tri = -2;
                    }
                } else if (target < 0) {
                    printf("frame %4ld: cut to id %d, which this set does not "
                           "list\n", game.frame, id);
                }
            }
            if (game.gameover) {
                printf("frame %4ld: a script called reset\n", game.frame);
                game.gameover = 0;
            }

            /* What PPCOLL did this frame, and only when it did something.  The
             * life points are the world state's own, which is where combat
             * writes them and where a script reads them back. */
            if (combat_stats.hits || combat_stats.parries) {
                for (int i = 0; i < game.act.n; i++) {
                    const Act *c = &game.act.a[i];
                    if (!(c->ctl.stance & (FSA_HIT | FSA_SHIELD)))
                        continue;
                    int life = c->world >= 0
                             ? game.vm.ws[c->world * WS_REC + WS_LIFE] : 0;
                    printf("frame %4ld: %s %s, life %d\n", game.frame,
                           i == game.act.player ? "player"
                             : i == hunter ? "hunter" : "companion",
                           (c->ctl.stance & FSA_HIT)
                               ? (life ? "is hit" : "is killed") : "parries",
                           life);
                }
            }

            if (game.vm.opcount[74] != last_trih) {
                /* `triangle_height`, which is how the sewers' sluice moves:
                 * four views, each raising one pair of collision triangles and
                 * dropping another (docs/11-script-vm.md 11.5). */
                printf("frame %4ld: a script moved the floor: triangle %d"
                       " to %d (%ld written)\n", game.frame,
                       game.vm.trih_tri, game.vm.trih_height,
                       game.vm.opcount[74]);
                last_trih = game.vm.opcount[74];
            }
            if (vm_active(&game.vm) != last_procs) {
                last_procs = vm_active(&game.vm);
                printf("frame %4ld: %d script process%s running\n",
                       game.frame, last_procs,
                       last_procs == 1 ? "" : "es");
            }

            /* The log line: what the player is doing, when it changes. */
            const Act *pl = game.act.player >= 0 ? &game.act.a[game.act.player]
                                                 : NULL;
            if (pl && (pl->ctl.stance != last_stance ||
                       pl->ctl.anim != last_anim)) {
                printf("frame %4ld: %-5s animation %2d%s\n", game.frame,
                       control_stance_name(pl->ctl.stance), pl->ctl.anim,
                       (pl->ctl.pad & PAD(JOY_DOUBLE)) ? "   (double tap)" : "");
                last_stance = pl->ctl.stance;
                last_anim   = pl->ctl.anim;
            }
            if (pl && (pl->actor.tri != last_tri ||
                       pl->actor.ground != last_ground ||
                       pl->actor.collided != last_collided)) {
                printf("frame %4ld: (%6d,%6d) triangle %3d floor %5d y %5d"
                       " facing %3d%s\n", game.frame, pl->actor.x, pl->actor.z,
                       pl->actor.tri, pl->actor.ground, pl->actor.y,
                       pl->actor.facing,
                       pl->actor.collided ? "  - stopped by a wall" : "");
                last_tri = pl->actor.tri;
                last_ground = pl->actor.ground;
                last_collided = pl->actor.collided;
            }
        }

        /* The character's own turn, for the viewer modes.  ANIM.GAS works in a
         * frame rate of 256 / fps, so at the animations' own 20 fps that is
         * 12. */
        if (bundle && !have_game) {
            int frate = anim && anim->fps ? 256 / anim->fps : 12;
            if (a.walk)
                actor.facing = (uint8_t)(actor.facing + in.dx * 3);
            if (a.play && anim && have_set) {
                actor_step(&actor, &set, anim, frate);
            } else if (a.play && anim) {
                AnimFrame fr;
                actor.frame = (actor.frame + 1) % anim->frames;
                if (actor.frame == 0)
                    actor.lift = 0;
                anim_frame(anim, actor.frame, &fr);
                actor.lift += fr.move[1];
                actor.facing = (uint8_t)(actor.facing + fr.turn);
            } else if (a.walk && have_set) {
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

            /* Say when the floor underfoot changes - which triangle, how high,
             * and whether a wall stopped the move. */
            if ((a.walk || a.play) && have_set &&
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

            /* The camera cuts alone, with no machine behind them, which is
             * what --events was before there was a game to run them in. */
            if (a.events && have_set && have_scene) {
                int id = events_only(&set, scene.cam.id, &actor);
                if (id >= 0) {
                    int target = -1;
                    for (int i = 0; i < set.nscenes; i++)
                        if (set.scene[i].id == id)
                            target = set.scene[i].scene;
                    if (target >= 0 &&
                        scene_load(&scene, path_pict, target, key)) {
                        scene_index = target;
                        printf("cut to %s (id %d) at (%d,%d) on triangle %d\n",
                               index_scene_name(&ix, scene_index), id,
                               actor.x, actor.z, actor.tri);
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
    if (have_game)
        game_close(&game);
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
