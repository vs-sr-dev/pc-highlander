#include "actor.h"
#include "../r3d/r3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- the bundle ---------------------------------------------------- */

static int is_root(const Model *m)
{
    return m->origin == 0 && m->norigins > 0;
}

int bundle_at(const Model *list, int count, int n)
{
    for (int i = 0; i < count; i++)
        if (is_root(&list[i]) && n-- == 0)
            return i;
    return -1;
}

int bundle_build(Bundle *b, const Model *list, int count, int first)
{
    memset(b, 0, sizeof *b);
    if (first < 0 || first >= count || !is_root(&list[first]))
        return 0;

    /* Which piece publishes each origin number.  The engine's own table is
     * indexed the same way, by the number with bit 7 cleared. */
    int owner[128];
    for (int i = 0; i < 128; i++)
        owner[i] = -1;

    b->piece[0]  = &list[first];
    b->parent[0] = -1;
    b->joint[0]  = -1;
    b->npieces   = 1;
    for (int k = 0; k < list[first].norigins; k++)
        owner[list[first].origin_id[k] & 0x7F] = 0;

    for (int i = first + 1; i < count && b->npieces < ACTOR_MAX_PIECES; i++) {
        const Model *m = &list[i];
        if (m->origin == 0)
            break;                      /* the next bundle, or something else */
        int p = owner[m->origin & 0x7F];
        if (p < 0)
            break;                      /* hangs on a joint nobody published  */
        int j = -1;
        for (int k = 0; k < b->piece[p]->norigins; k++)
            if (b->piece[p]->origin_id[k] == (uint8_t)m->origin)
                j = k;
        if (j < 0)
            break;
        int n = b->npieces++;
        b->piece[n]  = m;
        b->parent[n] = p;
        b->joint[n]  = j;
        for (int k = 0; k < m->norigins; k++)
            owner[m->origin_id[k] & 0x7F] = n;
    }
    return b->npieces;
}

/* ---- posing -------------------------------------------------------- */

static void rotate(const int32_t m[9], const int32_t v[3], int32_t out[3])
{
    for (int i = 0; i < 3; i++) {
        int64_t s = 0;
        for (int k = 0; k < 3; k++)
            s += (int64_t)m[i * 3 + k] * v[k];
        out[i] = (int32_t)(s >> 14);
    }
}

void actor_pose(const Bundle *b, const int8_t *angle, int facing,
                const int32_t root[3], ActorPose *out)
{
    for (int i = 0; i < b->npieces; i++) {
        const int8_t *a = angle ? angle + i * 3 : NULL;
        /* FORMMAT.GAS reads the instance's three rotation words as elevation
         * about x, twist about z and azimuth about y, in that order, and
         * ANIM.GAS writes the frame's three bytes into them in that order
         * having added the character's facing to the last. */
        int elev  = a ? (uint8_t)a[0] : 0;
        int twist = a ? (uint8_t)a[1] : 0;
        int azim  = ((a ? (uint8_t)a[2] : 0) + facing) & 0xFF;
        r3d_face_matrix(elev, azim, twist, out[i].rot);

        int p = b->parent[i];
        if (p < 0) {
            for (int k = 0; k < 3; k++)
                out[i].pos[k] = root[k];
        } else {
            /* The joint, carried through the parent's own rotation.  This is
             * the whole of the skeleton: 3DENGINE.GAS transforms the origin
             * point exactly as it transforms a vertex and saves the result for
             * whoever hangs on it. */
            const Model *pm = b->piece[p];
            const int16_t *o = MODEL_ORIGIN(pm, b->joint[i]);
            int32_t v[3] = { o[0], o[1], o[2] }, d[3];
            rotate(out[p].rot, v, d);
            for (int k = 0; k < 3; k++)
                out[i].pos[k] = out[p].pos[k] + d[k];
        }
    }
}

void actor_extent(const Bundle *b, const ActorPose *pose, int32_t *lo, int32_t *hi)
{
    int32_t l = 0x7FFFFFFF, h = -0x7FFFFFFF;
    for (int i = 0; i < b->npieces; i++) {
        const Model *m = b->piece[i];
        for (int v = 0; v < m->nverts; v++) {
            int32_t p[3] = { m->vert[v][0], m->vert[v][1], m->vert[v][2] }, w[3];
            rotate(pose[i].rot, p, w);
            int32_t y = w[1] + pose[i].pos[1];
            if (y < l) l = y;
            if (y > h) h = y;
        }
    }
    *lo = l;
    *hi = h;
}

/* ---- the floor ----------------------------------------------------- */

/* COLLIDE.GAS will not step up onto a floor more than this much higher than
 * the one the character is standing on: it shifts the rise right by eight and
 * treats anything left over as a wall.  "converts anything up to 1 metre to
 * 0", says the comment, so a unit is about 4 mm. */
#define STEP_UP 255

void actor_root(const Actor *a, const Anim *an, int32_t out[3])
{
    out[0] = a->x;
    out[1] = a->y + (an ? an->height_start : 0) + a->lift;
    out[2] = a->z;
}

int actor_place(Actor *a, const Set *s, int32_t x, int32_t z, int facing)
{
    memset(a, 0, sizeof *a);
    a->radius = 100;                    /* WORLD_QUENTIN's, from the table */
    a->facing = (uint8_t)facing;
    a->x = x;
    a->z = z;
    a->tri = set_locate(s, x, z);
    if (a->tri < 0)
        return 0;
    a->ground = s->tri[a->tri].height;
    a->y = a->ground;
    return 1;
}

static double dist2_to_seg(double px, double pz,
                           double ax, double az, double bx, double bz)
{
    double ex = bx - ax, ez = bz - az;
    double dx = px - ax, dz = pz - az;
    double den = ex * ex + ez * ez;
    double num = dx * ex + dz * ez;
    if (den <= 0.0 || num <= 0.0)
        return dx * dx + dz * dz;
    if (num >= den) {
        double fx = px - bx, fz = pz - bz;
        return fx * fx + fz * fz;
    }
    double c = dx * ez - dz * ex;       /* twice the area */
    return c * c / den;
}

static int64_t side(int64_t ax, int64_t az, int64_t bx, int64_t bz,
                    int64_t px, int64_t pz)
{
    return (bx - ax) * (pz - az) - (bz - az) * (px - ax);
}

static int segments_cross(int64_t ax, int64_t az, int64_t bx, int64_t bz,
                          int64_t cx, int64_t cz, int64_t dx, int64_t dz)
{
    int64_t d1 = side(cx, cz, dx, dz, ax, az);
    int64_t d2 = side(cx, cz, dx, dz, bx, bz);
    int64_t d3 = side(ax, az, bx, bz, cx, cz);
    int64_t d4 = side(ax, az, bx, bz, dx, dz);
    if (d1 == 0 || d2 == 0 || d3 == 0 || d4 == 0)
        return 1;
    return ((d1 < 0) != (d2 < 0)) && ((d3 < 0) != (d4 < 0));
}

/* Does the circle of radius r, swept from (sx,sz) to (ex,ez), reach the edge
 * AB?  COLLIDE.GAS asks it as four tests - the endpoint within r of either
 * vertex, the endpoint within r of the line with both vertices near enough to
 * count, the two segments crossing, and either vertex within r of the line of
 * travel.  The first two together are the distance from the endpoint to the
 * segment, which is what this computes instead: same question, no guards to
 * get wrong. */
static int sweep_reaches(double sx, double sz, double ex, double ez, double r,
                         double ax, double az, double bx, double bz, int *crossed)
{
    double r2 = r * r;
    *crossed = 0;
    if (dist2_to_seg(ex, ez, ax, az, bx, bz) < r2)
        return 1;
    if (segments_cross((int64_t)sx, (int64_t)sz, (int64_t)ex, (int64_t)ez,
                       (int64_t)ax, (int64_t)az, (int64_t)bx, (int64_t)bz)) {
        *crossed = 1;
        return 1;
    }
    if (dist2_to_seg(ax, az, sx, sz, ex, ez) < r2)
        return 1;
    if (dist2_to_seg(bx, bz, sx, sz, ex, ez) < r2)
        return 1;
    return 0;
}

#define WORK 128                        /* COLLIDE.GAS's own list is 1 KB */

int actor_move(Actor *a, const Set *s, int32_t dx, int32_t dz)
{
    if (a->tri < 0 || a->tri >= s->ntris)
        return 0;
    if (dx == 0 && dz == 0)
        return 1;

    double sx = a->x, sz = a->z;
    double ex = sx + dx, ez = sz + dz;
    double r = a->radius;

    int list[WORK], nlist = 0;
    char queued[8192];
    memset(queued, 0, (size_t)s->ntris);
    list[nlist++] = a->tri;
    queued[a->tri] = 1;

    int32_t new_height = 0;
    int lastinter = a->tri;
    int hit_ax = 0, hit_az = 0, hit_bx = 0, hit_bz = 0, hit = 0;

    for (int head = 0; head < nlist; head++) {
        const SetTri *tr = &s->tri[list[head]];
        for (int k = 0; k < 3; k++) {
            const SetVert *A = &s->vert[tr->vert[k]];
            const SetVert *B = &s->vert[tr->vert[(k + 1) % 3]];
            int n = tr->adj[k];

            /* What is on the other side.  A wall, or a floor more than a step
             * up, is a height of zero, which COLLIDE.GAS reads as "blocked". */
            int32_t nh = 0;
            if (n >= 0 && n < s->ntris && s->tri[n].height - a->y <= STEP_UP)
                nh = s->tri[n].height;
            if (nh && queued[n])
                continue;               /* already on the list, nothing to do */

            int crossed = 0;
            if (!sweep_reaches(sx, sz, ex, ez, r, A->x, A->z, B->x, B->z, &crossed))
                continue;

            if (!nh) {                  /* over a blocked edge: no move at all */
                hit = 1;
                hit_ax = A->x; hit_az = A->z;
                hit_bx = B->x; hit_bz = B->z;
                head = nlist;           /* stop both loops */
                break;
            }
            if (crossed)
                lastinter = n;
            if (nh > new_height)
                new_height = nh;
            if (nlist < WORK) {
                queued[n] = 1;
                list[nlist++] = n;
            }
        }
    }

    if (hit) {
        /* The original's whole response to a wall: abandon the move and turn
         * four steps of the 256-step circle towards sliding along it, keeping
         * the same sense for as long as the collision lasts. */
        int turn;
        if (a->collided) {
            turn = a->colturn ? -4 : 4;
        } else {
            /* Which side of the wall the movement lies on. */
            int64_t mx = (int64_t)dx, mz = (int64_t)dz;
            int64_t ca = mz * (hit_ax - (int64_t)a->x) + mx * ((int64_t)a->z - hit_az);
            int64_t cb = mz * (hit_bx - (int64_t)a->x) + mx * ((int64_t)a->z - hit_bz);
            int64_t sense;
            if ((ca < 0) == (cb < 0)) {
                sense = ca;             /* both ends of the wall one side   */
            } else {
                int64_t ax = hit_ax, az = hit_az, bx = hit_bx, bz = hit_bz;
                if (ca < 0) {           /* order the edge along the movement */
                    int64_t t;
                    t = ax; ax = bx; bx = t;
                    t = az; az = bz; bz = t;
                }
                sense = mx * (bx - ax) + mz * (bz - az);
            }
            a->colturn = sense >= 0;
            turn = a->colturn ? -4 : 4;
        }
        a->collided = 1;
        a->facing = (uint8_t)(a->facing + turn);
        return 0;
    }

    a->collided = 0;
    a->x = (int32_t)ex;
    a->z = (int32_t)ez;
    /* The floor is the highest the circle touched, and never lower than the
     * triangle the move actually crossed into. */
    if (s->tri[lastinter].height > new_height)
        new_height = s->tri[lastinter].height;
    a->ground = new_height;
    /* FINDTRI.GAS runs last, to make sure the triangle really does hold the
     * point the move ended on. */
    a->tri = set_find_tri(s, a->x, a->z, lastinter);
    if (a->tri < 0)
        a->tri = lastinter;
    return 1;
}

/* ---- the height chase ---------------------------------------------- */

#define GRAV 2450

void actor_settle(Actor *a, int frate)
{
    int32_t gap = a->y - a->ground;     /* positive: standing in the air */
    if (gap == 0) {
        a->gravity = 0;
        return;
    }
    if (gap > 0) {
        /* Falling: the speed builds up, and the fall is clamped so the feet
         * never go through the floor. */
        a->gravity += ((GRAV >> 2) * frate) >> 8;
        int32_t step = (a->gravity * frate) >> 5;
        if (step > gap)
            step = gap;
        a->y -= step;
        return;
    }
    /* Rising - a step up.  SMOOTH.TXT wants a fixed rate that grows only when
     * the step is taller than it, so that stairs taken quickly still work:
     * a downward speed is replaced by the standing rise, a step deeper than
     * the current speed replaces it, and a thirty-second of that is applied. */
    if (a->gravity >= 0)
        a->gravity = -(GRAV >> 5);
    if (gap < a->gravity)
        a->gravity = gap;
    int32_t step = (a->gravity * frate) >> 5;       /* negative: it rises */
    if (step < gap)
        step = gap;
    a->y -= step;
}

void actor_step(Actor *a, const Set *s, const Anim *an, int frate)
{
    if (!an || an->frames <= 0)
        return;
    actor_settle(a, frate);

    a->frame = (a->frame + 1) % an->frames;
    if (a->frame == 0)
        a->lift = 0;                    /* the height reset ANIM.GAS does on
                                           the loop back to frame zero      */
    AnimFrame f;
    anim_frame(an, a->frame, &f);

    a->facing = (uint8_t)(a->facing + f.turn);
    a->lift  += f.move[1];

    /* The frame's own x and z move, turned into the direction the character
     * faces.  ANIM.GAS: mx = x.cos + z.sin, mz = z.cos - x.sin. */
    double k = 2.0 * 3.14159265358979323846 / 256.0;
    double c = cos(a->facing * k), sn = sin(a->facing * k);
    int32_t mx = (int32_t)lrint(f.move[0] * c + f.move[2] * sn);
    int32_t mz = (int32_t)lrint(f.move[2] * c - f.move[0] * sn);
    actor_move(a, s, mx, mz);
}
