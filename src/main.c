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
#include "game/actor.h"
#include "game/set.h"
#include "r3d/r3d.h"
#include "platform/window.h"

typedef struct {
    const char *tracks;
    const char *manifest;
    const char *scene;
    const char *model;
    const char *object;
    const char *shot;
    int   no_window, depth, spin, wire, shade, scale, frames;
    int   mesh, no_ground;
    int   character, anim, frame, play, walk, events;
    int   have_pos, pos[3];
    int   have_face, face[3];
    R3dCull cull;
    int   list_scenes, list_models, list_objects, list_chars;
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
 * one the animations stored between it and the next.  Nothing on the disc
 * links a character sheet to a bundle (docs/12-world-and-sheets.md 12.7), but
 * the track's own layout does - a bundle and then its animations, one slot
 * each - and the pose check below confirms the pairing independently. */
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
"hlview - Highlander phase 3 viewer\n"
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
"  --no-window       render without opening a window\n"
"  --frames N        stop after N frames\n"
"  --scale N         window magnification (default 3)\n"
"  --list-scenes | --list-models | --list-objects\n");
}

static int parse_triple(const char *s, int *out)
{
    return sscanf(s, "%d,%d,%d", &out[0], &out[1], &out[2]) == 3;
}

int main(int argc, char **argv)
{
    Args a = { "assets/tracks", "assets/manifest.json", NULL, NULL, NULL, NULL,
               0, 0, 0, 0, 1, 3, 0, 0, 0,
               -1, 0, -1, 0, 0, 0,
               0, {0,0,0}, 0, {0,0,0},
               R3D_CULL_NONE, 0, 0, 0, 0 };

    for (int i = 1; i < argc; i++) {
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if      (!strcmp(argv[i], "--tracks")   && v) a.tracks = argv[++i];
        else if (!strcmp(argv[i], "--manifest") && v) a.manifest = argv[++i];
        else if (!strcmp(argv[i], "--scene")    && v) a.scene = argv[++i];
        else if (!strcmp(argv[i], "--model")    && v) a.model = argv[++i];
        else if (!strcmp(argv[i], "--object")   && v) a.object = argv[++i];
        else if (!strcmp(argv[i], "--shot")     && v) a.shot = argv[++i];
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
        else if (!strcmp(argv[i], "--check-mesh")) { paths(a.tracks); return !check_mesh(); }
        else if (!strcmp(argv[i], "--check-char")) { paths(a.tracks); return !check_char(); }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
        else { usage(); return argv[i][0] == '-' ? 1 : 0; }
    }
    paths(a.tracks);

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
    if (a.character >= 0 || a.list_chars) {
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
            if (set.nentries > 0) {
                sx = set.entry[0].x;
                sz = set.entry[0].z;
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
    Input in = { 0, 0, 0, 0, 0, 0, 0, 0 };

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

        if (a.shot && shots == 0) {
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

            /* Say when the floor underfoot changes - which triangle, how
             * high, and whether a wall stopped the move.  That is the whole of
             * what movement over the mesh has to get right. */
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

            if (a.events && have_set && have_scene) {
                int id = events_scan(&set, scene.cam.id, &actor);
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
                        /* A doorway leads into another set, and its floor and
                         * its events are the ones that matter from now on. */
                        if (id / 64 != (int)(set.scene[0].id / 64) ||
                            !set_contains(&set, actor.tri, actor.x, actor.z)) {
                            int si = scene_set(&scene.cam);
                            if (si < 0 || si >= SET_COUNT)
                                si = set_of_scene(path_set, scene.cam.id);
                            Set next;
                            if (si >= 0 && si != set.index &&
                                set_load(&next, path_set, si)) {
                                set_free(&set);
                                set = next;
                                actor_place(&actor, &set, actor.x, actor.z,
                                            actor.facing);
                                printf("  and into set %d\n", si);
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
