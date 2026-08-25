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
    int   have_pos, pos[3];
    int   have_face, face[3];
    R3dCull cull;
    int   list_scenes, list_models, list_objects;
} Args;

static char path_pict[512], path_boot[512], path_t5[512];

static void paths(const char *dir)
{
    snprintf(path_pict, sizeof path_pict, "%s/track04_pict.bin", dir);
    snprintf(path_boot, sizeof path_boot, "%s/track02_00004000.bin", dir);
    snprintf(path_t5,   sizeof path_t5,   "%s/track05_data.bin", dir);
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
    if (strcmp(src, "boot") == 0 || strcmp(src, "items") == 0) {
        path = path_boot;
        ms->from_boot = 1;
    } else if (strcmp(src, "track5") == 0 || strcmp(src, "t5") == 0) {
        path = path_t5;
        ms->from_boot = 0;
    } else {
        fprintf(stderr, "unknown model source '%s' - try boot:N or track5:N\n", src);
        return 0;
    }
    ms->file = io_read(path);
    if (!ms->file.data) {
        fprintf(stderr, "cannot read %s\n", path);
        return 0;
    }
    ms->count = model_scan(ms->file.data, ms->file.size, 3, &ms->list);
    return ms->count > 0;
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
"  --model SRC:N     boot:N for the 19 item models, track5:N for the 220\n"
"  --object NAME     place the model at a world record (CA_WINE, #5)\n"
"  --pos X,Y,Z       place it explicitly instead\n"
"  --face E,A,T      the three rotations, on a 256-step circle\n"
"  --spin            turn the model about its azimuth\n"
"  --wire            draw facet edges instead of filling\n"
"  --flat            do not shade the facets\n"
"  --cull MODE       back (default), front or none\n"
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
               0, 0, 0, 0, 1, 3, 0, 0, {0,0,0}, 0, {0,0,0}, R3D_CULL_BACK, 0, 0, 0 };

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
            a.cull = !strcmp(m, "none")  ? R3D_CULL_NONE :
                     !strcmp(m, "front") ? R3D_CULL_FRONT : R3D_CULL_BACK;
        }
        else if (!strcmp(argv[i], "--depth"))     a.depth = 1;
        else if (!strcmp(argv[i], "--spin"))      a.spin = 1;
        else if (!strcmp(argv[i], "--wire"))      a.wire = 1;
        else if (!strcmp(argv[i], "--flat"))      a.shade = 0;
        else if (!strcmp(argv[i], "--no-window")) a.no_window = 1;
        else if (!strcmp(argv[i], "--list-scenes"))  a.list_scenes = 1;
        else if (!strcmp(argv[i], "--list-models"))  a.list_models = 1;
        else if (!strcmp(argv[i], "--list-objects")) a.list_objects = 1;
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
    }

    static R3dTarget target;
    R3dOpts opts = { a.cull, a.shade, a.wire };
    int spin = 0, show_depth = a.depth, spinning = a.spin, shots = 0;
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
            pos[0] += in.dx * 20;
            pos[2] += in.dz * 20;
        }
        if (spinning)
            spin = (spin + 2) & 255;
        if (a.frames && frame + 1 >= a.frames)
            break;
        if (!windowed)
            break;
    }

    if (windowed)
        window_close();
    if (ms.list)
        model_free_all(ms.list, ms.count);
    io_free(&ms.file);
    json_free(&ix.j);
    return 0;
}
