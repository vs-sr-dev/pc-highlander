#include "set.h"
#include "../util/io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCENE_STRIDE_BLOCKS 110

static void *slot_read(const char *track3, int index)
{
    uint8_t *slot = malloc(SET_SLOT);
    if (!slot)
        return NULL;
    if (!io_read_at(track3, (long)index * SET_SLOT, slot, SET_SLOT)) {
        free(slot);
        return NULL;
    }
    return slot;
}

int set_load(Set *s, const char *track3, int index)
{
    memset(s, 0, sizeof *s);
    if (index < 0 || index >= SET_COUNT)
        return 0;
    uint8_t *d = slot_read(track3, index);
    if (!d)
        return 0;

    s->index      = index;
    s->hinum      = be32(d);
    s->lonum      = be32(d + 4);
    uint32_t ev   = be32(d + 8);
    uint32_t coll = be32(d + 12);
    uint32_t init = be32(d + 16);
    uint32_t sco  = be32(d + 20);
    s->script_off = be32(d + 24);

    if (ev >= SET_SLOT || coll >= SET_SLOT || init >= SET_SLOT || sco >= SET_SLOT) {
        free(d);
        return 0;
    }

    /* Scene table: a count, then id, pad, CD block. */
    s->nscenes = (int)be32(d + sco);
    if (s->nscenes < 0 || s->nscenes > 4096) { free(d); return 0; }
    s->scene = calloc((size_t)s->nscenes, sizeof *s->scene);
    for (int i = 0; i < s->nscenes; i++) {
        const uint8_t *p = d + sco + 4 + (size_t)i * 8;
        s->scene[i].id    = be16(p);
        s->scene[i].block = be32(p + 4);
        s->scene[i].scene = (int)(s->scene[i].block / SCENE_STRIDE_BLOCKS);
    }

    /* Init table: the doorways you can arrive through. */
    s->nentries = (int)be32(d + init);
    if (s->nentries < 0 || s->nentries > 4096) { free(d); return 0; }
    s->entry = calloc((size_t)s->nentries, sizeof *s->entry);
    for (int i = 0; i < s->nentries; i++) {
        const uint8_t *p = d + init + 4 + (size_t)i * 12;
        s->entry[i].id    = be16(p);
        s->entry[i].flags = be16(p + 2);
        s->entry[i].x     = be32s(p + 4);
        s->entry[i].z     = be32s(p + 8);
    }

    /* Events. */
    s->nevents = (int)be32(d + ev);
    if (s->nevents < 0 || s->nevents > 4096) { free(d); return 0; }
    s->event = calloc((size_t)s->nevents, sizeof *s->event);
    for (int i = 0; i < s->nevents; i++) {
        const uint8_t *p = d + ev + 4 + (size_t)i * 36;
        SetEvent *e = &s->event[i];
        e->scene     = be16(p);
        e->type      = be16(p + 2);
        e->x         = be32s(p + 4);
        e->z         = be32s(p + 8);
        e->height    = be32s(p + 12);
        e->radius2   = be32s(p + 16);
        e->status    = be16(p + 20);
        e->condition = be16(p + 22);
        for (int k = 0; k < 3; k++) e->cdata[k] = be16(p + 24 + k * 2);
        for (int k = 0; k < 3; k++) e->data[k]  = be16(p + 30 + k * 2);
    }

    /* Collision: a short header, the vertices, then the triangles. */
    uint32_t troff = be16(d + coll);
    s->ntris  = be16(d + coll + 2);
    s->nverts = be16(d + coll + 4);
    if (s->ntris > 8192 || s->nverts > 8192) { free(d); return 0; }
    s->vert = calloc((size_t)s->nverts, sizeof *s->vert);
    for (int i = 0; i < s->nverts; i++) {
        const uint8_t *p = d + coll + 8 + (size_t)i * 8;
        s->vert[i].x = be32s(p);
        s->vert[i].z = be32s(p + 4);
    }
    s->tri = calloc((size_t)s->ntris, sizeof *s->tri);
    for (int i = 0; i < s->ntris; i++) {
        const uint8_t *p = d + coll + troff + (size_t)i * 14;
        s->tri[i].height = be16s(p);
        for (int k = 0; k < 3; k++) s->tri[i].vert[k] = (uint8_t)be16(p + 2 + k * 2);
        for (int k = 0; k < 3; k++) s->tri[i].adj[k]  = be16s(p + 8 + k * 2);
    }

    free(d);
    return 1;
}

void set_free(Set *s)
{
    free(s->scene);
    free(s->entry);
    free(s->event);
    free(s->vert);
    free(s->tri);
    memset(s, 0, sizeof *s);
}

int set_of_scene(const char *track3, uint16_t scene_id)
{
    /* A set's own views are the ones whose group is its own; the rest are
     * borrowed from next door.  Group 1 is set 0's neighbour, not set 0, so
     * ask the data rather than computing it. */
    for (int i = 0; i < SET_COUNT; i++) {
        Set s;
        if (!set_load(&s, track3, i))
            continue;
        int own = -1;
        for (int k = 0; k < s.nscenes && own < 0; k++)
            if (s.scene[k].id == scene_id)
                own = k;
        if (own >= 0) {
            /* The group a set owns is the one most of its views share. */
            int counts[64] = { 0 }, best = -1, bestn = 0;
            for (int k = 0; k < s.nscenes; k++) {
                int g = s.scene[k].id / 64;
                if (g >= 0 && g < 64 && ++counts[g] > bestn) {
                    bestn = counts[g];
                    best = g;
                }
            }
            int mine = best == scene_id / 64;
            set_free(&s);
            if (mine)
                return i;
        } else {
            set_free(&s);
        }
    }
    return -1;
}

/* ---- the floor ---------------------------------------------------- */

static int64_t cross(const SetVert *a, const SetVert *b, int32_t x, int32_t z)
{
    return (int64_t)(b->x - a->x) * (z - a->z) - (int64_t)(b->z - a->z) * (x - a->x);
}

/* The mesh is not wound consistently - 2,729 triangles turn one way and 2,613
 * the other - so "inside" cannot be a sign test against a fixed winding.  It is
 * the weaker one: no two edges disagree. */
static int inside(const Set *s, int t, int32_t x, int32_t z, int64_t e[3])
{
    const SetTri *tr = &s->tri[t];
    int neg = 0, pos = 0;
    for (int k = 0; k < 3; k++) {
        const SetVert *a = &s->vert[tr->vert[k]];
        const SetVert *b = &s->vert[tr->vert[(k + 1) % 3]];
        e[k] = cross(a, b, x, z);
        if (e[k] < 0) neg = 1;
        if (e[k] > 0) pos = 1;
    }
    return !(neg && pos);
}

int set_contains(const Set *s, int tri, int32_t x, int32_t z)
{
    int64_t e[3];
    return tri >= 0 && tri < s->ntris && inside(s, tri, x, z, e);
}

int set_locate(const Set *s, int32_t x, int32_t z)
{
    int64_t e[3];
    for (int t = 0; t < s->ntris; t++)
        if (inside(s, t, x, z, e))
            return t;
    return -1;
}

int set_find_tri(const Set *s, int32_t x, int32_t z, int from)
{
    return set_walk(s, x, z, from, NULL);
}

int set_walk(const Set *s, int32_t x, int32_t z, int from, int *steps)
{
    if (steps)
        *steps = -1;
    if (from < 0 || from >= s->ntris)
        return set_locate(s, x, z);

    /* FINDTRI.GAS is explicit that this is a search over a list and not a walk
     * down one path, and it says why:
     *
     *   so we can't just keep jumping to the first edge which the test point
     *   is outside of, because it would be possible to generate a structure
     *   where a point couldn't ever be reached (due to looping)
     *
     * So: put the current triangle on the list, take triangles off it, and for
     * each one that does not contain the point add its three neighbours,
     * skipping what is already there.  It spreads outwards over the connected
     * part of the mesh, which is exactly what a point on the far side of a
     * pillar needs.  Greedy really does get stuck: it fails on 258 of the
     * 5,342 triangles where this succeeds on all of them. */
    int *list = malloc((size_t)s->ntris * sizeof *list);
    char *queued = calloc((size_t)s->ntris, 1);
    if (!list || !queued) {
        free(list);
        free(queued);
        return set_locate(s, x, z);
    }
    int head = 0, tail = 0, found = -1;
    list[tail++] = from;
    queued[from] = 1;
    while (head < tail) {
        int t = list[head++];
        int64_t e[3];
        if (inside(s, t, x, z, e)) {
            found = t;
            break;
        }
        for (int k = 0; k < 3; k++) {
            int n = s->tri[t].adj[k];
            if (n >= 0 && n < s->ntris && !queued[n]) {
                queued[n] = 1;
                list[tail++] = n;
            }
        }
    }
    int visited = head;
    free(list);
    free(queued);
    if (found >= 0) {
        if (steps)
            *steps = visited - 1;       /* triangles looked at before this one */
        return found;
    }
    return set_locate(s, x, z);         /* not in this part of the mesh at all */
}

int set_step_away(const Set *s, int tri, int hops)
{
    int t = tri;
    for (int i = 0; i < hops && t >= 0; i++) {
        int next = -1;
        for (int k = 0; k < 3 && next < 0; k++) {
            int n = s->tri[t].adj[(i + k) % 3];
            if (n >= 0 && n < s->ntris && n != tri)
                next = n;
        }
        if (next < 0)
            break;
        t = next;
    }
    return t;
}

int set_ground(const Set *s, int32_t x, int32_t z, int from, int32_t *height)
{
    int t = set_find_tri(s, x, z, from);
    *height = t >= 0 ? s->tri[t].height : 0;
    return t;
}
