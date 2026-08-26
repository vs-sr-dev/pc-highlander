#include "r3d.h"

#include <math.h>
#include <string.h>

R3dStats r3d_stats;

void r3d_backdrop(R3dTarget *t, const Scene *s)
{
    memcpy(t->colour, s->colour, sizeof t->colour);
    for (int i = 0; i < R3D_W * R3D_H; i++)
        t->depth[i] = scene_z(s->depth[i]);
}

void r3d_clear(R3dTarget *t, uint16_t colour, int32_t depth)
{
    for (int i = 0; i < R3D_W * R3D_H; i++) {
        t->colour[i] = colour;
        t->depth[i]  = depth;
    }
}

void r3d_identity(int32_t out[9])
{
    static const int32_t id[9] = { 16384, 0, 0, 0, 16384, 0, 0, 0, 16384 };
    memcpy(out, id, sizeof id);
}

void r3d_mul(const int32_t a[9], const int32_t b[9], int32_t out[9])
{
    int32_t r[9];
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 3; k++) {
            int64_t s = 0;
            for (int n = 0; n < 3; n++)
                s += (int64_t)a[i * 3 + n] * b[n * 3 + k];
            r[i * 3 + k] = (int32_t)(s >> 14);
        }
    memcpy(out, r, sizeof r);
}

void r3d_face_matrix(int elevation, int azimuth, int twist, int32_t out[9])
{
    /* FORMMAT.GAS builds the object matrix from three angles on a 256-step
     * circle - a about x (elevation), b about y (azimuth), c about z (twist) -
     * as id1..id9 in row order.  With two angles zero each one reduces to the
     * plain rotation about its own axis, which is how the arrangement was
     * checked.  The game reads sin and cos from a table in s1.14; this uses
     * the library and rounds to the same scale. */
    double k = 2.0 * 3.14159265358979323846 / 256.0;
    double sa = sin(elevation * k), ca = cos(elevation * k);
    double sb = sin(azimuth   * k), cb = cos(azimuth   * k);
    double sc = sin(twist     * k), cc = cos(twist     * k);
    double m[9] = {
        cb * cc,                 sb * sa - sc * ca * cb,   sc * cb * sa + sb * ca,
        sc,                      ca * cc,                 -sa * cc,
        -sb * cc,                sb * sc * ca + sa * cb,  -sa * sc * sb + ca * cb
    };
    for (int i = 0; i < 9; i++)
        out[i] = (int32_t)lrint(m[i] * 16384.0);
}

void r3d_place(R3dXform *x, const SceneCam *cam,
               const int32_t obj_rot[9], const int32_t obj_pos[3])
{
    int32_t view[9];
    for (int i = 0; i < 9; i++)
        view[i] = cam->m[i];

    /* The engine transforms the vertices by the object matrix concatenated
     * with the view matrix, and adds the object's position carried through the
     * view matrix - ORP-VRP in 3DENGINE.GAS, the object position less the
     * camera's, rotated. */
    r3d_mul(view, obj_rot, x->rot);

    int32_t d[3];
    for (int i = 0; i < 3; i++)
        d[i] = obj_pos[i] - cam->pos[i];
    for (int i = 0; i < 3; i++) {
        int64_t s = 0;
        for (int k = 0; k < 3; k++)
            s += (int64_t)view[i * 3 + k] * d[k];
        x->pos[i] = (int32_t)(s >> 14);
    }
}

void r3d_to_view(const R3dXform *x, const int32_t w[3], int32_t out[3])
{
    for (int i = 0; i < 3; i++) {
        int64_t s = 0;
        for (int k = 0; k < 3; k++)
            s += (int64_t)x->rot[i * 3 + k] * w[k];
        out[i] = (int32_t)(s >> 14) + x->pos[i];
    }
}

static void xform(const R3dXform *x, const int16_t v[3], int32_t out[3])
{
    for (int i = 0; i < 3; i++) {
        int64_t s = 0;
        for (int k = 0; k < 3; k++)
            s += (int64_t)x->rot[i * 3 + k] * v[k];
        out[i] = (int32_t)(s >> 14) + x->pos[i];
    }
}

int r3d_project(const int32_t v[3], int *sx, int *sy)
{
    int32_t z = v[2] < 0 ? -v[2] : v[2];
    if (z < R3D_ZMIN)
        return 0;
    *sx = R3D_CX + (int)((int64_t)v[0] * R3D_XSCALE / z);
    /* The rasteriser draws scanline 199 - sy, so fold the flip in here. */
    *sy = (R3D_H - 1) - (R3D_CY + (int)((int64_t)v[1] * R3D_YSCALE / z));
    return 1;
}

/* ------------------------------------------------------------------ */

typedef struct { double x, y, z; } Vec;     /* view space, z negative in front */
typedef struct { double x, y, z; } Pt;      /* screen x, screen row, |z|       */

static int clip_near(const Vec *in, int n, Vec *out)
{
    /* One plane: keep what is at least R3D_ZMIN in front of the camera. */
    const double lim = -(double)R3D_ZMIN;
    int m = 0;
    for (int i = 0; i < n; i++) {
        Vec a = in[i], b = in[(i + 1) % n];
        int ain = a.z <= lim, bin = b.z <= lim;
        if (ain)
            out[m++] = a;
        if (ain != bin) {
            double t = (lim - a.z) / (b.z - a.z);
            Vec c = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, lim };
            out[m++] = c;
        }
    }
    return m;
}

static void span(R3dTarget *t, int row, double xa, double za, double xb, double zb,
                 uint16_t colour)
{
    if (row < 0 || row >= R3D_H)
        return;
    if (xa > xb) {
        double s = xa; xa = xb; xb = s;
        s = za; za = zb; zb = s;
    }
    int x0 = (int)ceil(xa - 0.5), x1 = (int)floor(xb - 0.5);
    if (x1 < x0)
        x1 = x0;                        /* never drop a thin facet entirely */
    double w = xb - xa;
    double dz = w > 1e-9 ? (zb - za) / w : 0.0;
    for (int x = x0; x <= x1; x++) {
        if (x < 0 || x >= R3D_W)
            continue;
        /* Clamp to the span: rounding, and the rule above that keeps a thin
         * facet alive, can put the sample just outside it, and a steep dz
         * would then extrapolate the depth wildly. */
        double u = x + 0.5 - xa;
        if (u < 0) u = 0;
        if (u > w) u = w;
        double z = za + dz * u;
        int32_t zi = (int32_t)(z + 0.5);
        int i = row * R3D_W + x;
        r3d_stats.tested++;
        if (zi < r3d_stats.znear) r3d_stats.znear = zi;
        if (zi > r3d_stats.zfar)  r3d_stats.zfar  = zi;
        if (zi < t->depth[i]) {
            t->depth[i]  = zi;
            t->colour[i] = colour;
            r3d_stats.drawn++;
        }
    }
}

static void fill(R3dTarget *t, const Pt *p, int n, uint16_t colour)
{
    double ymin = p[0].y, ymax = p[0].y;
    for (int i = 1; i < n; i++) {
        if (p[i].y < ymin) ymin = p[i].y;
        if (p[i].y > ymax) ymax = p[i].y;
    }
    int r0 = (int)ceil(ymin - 0.5), r1 = (int)floor(ymax - 0.5);
    if (r1 < r0)
        r1 = r0;
    if (r0 < 0) r0 = 0;
    if (r1 >= R3D_H) r1 = R3D_H - 1;

    for (int row = r0; row <= r1; row++) {
        double y = row + 0.5;
        double cx[MODEL_MAX_FACET_VERTS * 2], cz[MODEL_MAX_FACET_VERTS * 2];
        int hits = 0;
        for (int i = 0; i < n; i++) {
            const Pt *a = &p[i], *b = &p[(i + 1) % n];
            if ((a->y <= y) == (b->y <= y))
                continue;
            double s = (y - a->y) / (b->y - a->y);
            cx[hits] = a->x + (b->x - a->x) * s;
            cz[hits] = a->z + (b->z - a->z) * s;
            hits++;
        }
        for (int i = 1; i < hits; i++)          /* few crossings; insertion */
            for (int k = i; k > 0 && cx[k] < cx[k - 1]; k--) {
                double s = cx[k]; cx[k] = cx[k - 1]; cx[k - 1] = s;
                s = cz[k]; cz[k] = cz[k - 1]; cz[k - 1] = s;
            }
        for (int i = 0; i + 1 < hits; i += 2)
            span(t, row, cx[i], cz[i], cx[i + 1], cz[i + 1], colour);
    }
}

static uint16_t shade(uint16_t colour, double lum)
{
    if (lum < 0.0) lum = 0.0;
    if (lum > 1.0) lum = 1.0;
    unsigned r = (unsigned)(((colour >> 11) & 0x1F) * lum + 0.5);
    unsigned b = (unsigned)(((colour >>  6) & 0x1F) * lum + 0.5);
    unsigned g = (unsigned)(( colour        & 0x3F) * lum + 0.5);
    return (uint16_t)((r << 11) | (b << 6) | g);
}

void r3d_line(R3dTarget *t, const int32_t a[3], const int32_t b[3],
              uint16_t colour, int depth_test)
{
    Vec va = { a[0], a[1], a[2] }, vb = { b[0], b[1], b[2] };
    const double lim = -(double)R3D_ZMIN;
    if (va.z > lim && vb.z > lim)
        return;
    if (va.z > lim || vb.z > lim) {         /* clip the end that is too near */
        double s = (lim - va.z) / (vb.z - va.z);
        Vec c = { va.x + (vb.x - va.x) * s, va.y + (vb.y - va.y) * s, lim };
        if (va.z > lim) va = c; else vb = c;
    }
    double za = -va.z, zb = -vb.z;
    double xa = R3D_CX + va.x * R3D_XSCALE / za;
    double ya = (R3D_H - 1) - (R3D_CY + va.y * R3D_YSCALE / za);
    double xb = R3D_CX + vb.x * R3D_XSCALE / zb;
    double yb = (R3D_H - 1) - (R3D_CY + vb.y * R3D_YSCALE / zb);

    int steps = (int)(fabs(xb - xa) + fabs(yb - ya)) + 1;
    if (steps > 4096)
        steps = 4096;
    for (int i = 0; i <= steps; i++) {
        double u = (double)i / steps;
        int px = (int)(xa + (xb - xa) * u + 0.5);
        int py = (int)(ya + (yb - ya) * u + 0.5);
        if (px < 0 || px >= R3D_W || py < 0 || py >= R3D_H)
            continue;
        int32_t z = (int32_t)(za + (zb - za) * u + 0.5);
        int k = py * R3D_W + px;
        if (depth_test && z >= t->depth[k])
            continue;
        t->colour[k] = colour;
        if (depth_test)
            t->depth[k] = z;
    }
}

void r3d_draw_model(R3dTarget *t, const Model *m, const R3dXform *x,
                    const R3dOpts *o)
{
    static int32_t view[4096][3];
    r3d_stats.facets = r3d_stats.tested = r3d_stats.drawn = 0;
    r3d_stats.znear = 0x7FFFFFFF;
    r3d_stats.zfar = 0;
    int total = m->nverts + m->norigins;
    if (total > 4096)
        total = 4096;
    for (int i = 0; i < total; i++)
        xform(x, m->vert[i], view[i]);

    for (int f = 0; f < m->nfacets; f++) {
        const Facet *fa = &m->facet[f];
        Vec in[MODEL_MAX_FACET_VERTS], clipped[MODEL_MAX_FACET_VERTS * 2];
        int n = 0;
        for (int i = 0; i < fa->nverts; i++) {
            int vi = fa->vert[i];
            if (vi >= total)
                continue;
            Vec v = { (double)view[vi][0], (double)view[vi][1], (double)view[vi][2] };
            in[n++] = v;
        }
        if (n < 3)
            continue;
        int cn = clip_near(in, n, clipped);
        if (cn < 3)
            continue;

        Pt p[MODEL_MAX_FACET_VERTS * 2];
        for (int i = 0; i < cn; i++) {
            double z = -clipped[i].z;               /* distance in front */
            if (z < R3D_ZMIN)
                z = R3D_ZMIN;
            p[i].x = R3D_CX + clipped[i].x * R3D_XSCALE / z;
            p[i].y = (R3D_H - 1) - (R3D_CY + clipped[i].y * R3D_YSCALE / z);
            p[i].z = z;
        }

        /* Signed area on screen, where y counts downward: a facet whose
         * vertices wind clockwise in the source comes out positive.
         *
         * This is 3DENGINE.GAS's *second* cull, the one it keeps for facets
         * with no normal.  Its first is to transform the stored facet normal
         * and drop the facet when the result points away - and since every one
         * of the 6,821 facets on the disc carries a normal of (0,0,0), that
         * test always passes and nothing is ever culled.  So no culling is the
         * faithful setting, and the Z-buffer does the work: a back face is
         * behind the front face at the same pixel by construction.  Culling by
         * winding is left available and is not the default, because the
         * shipped models do not agree about which way round they are - the
         * item models were mirrored on export (docs/03-data-formats.md 3.3
         * swaps two axes) and read the opposite way to the characters. */
        double area = 0;
        for (int i = 0; i < cn; i++) {
            const Pt *a = &p[i], *b = &p[(i + 1) % cn];
            area += a->x * b->y - b->x * a->y;
        }
        if (o->cull == R3D_CULL_BACK  && area <= 0) continue;
        if (o->cull == R3D_CULL_FRONT && area >= 0) continue;

        r3d_stats.facets++;
        uint16_t colour = fa->colour;
        if (o->shade) {
            /* The set's own lights are not located yet, so this is the
             * viewer's: one directional light over the camera's shoulder,
             * plus ambient.  The facet normal is rotated into view space. */
            double nv[3];
            for (int i = 0; i < 3; i++) {
                double s = 0;
                for (int k = 0; k < 3; k++)
                    s += (double)x->rot[i * 3 + k] * fa->normal[k];
                nv[i] = s;
            }
            double len = sqrt(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]);
            double d = len > 0 ? (nv[0] * 0.31 + nv[1] * 0.50 + nv[2] * 0.81) / len : 0;
            if (d < 0)
                d = -d;                 /* light both faces, the game's models
                                           are not consistently wound */
            colour = shade(colour, 0.35 + 0.65 * d);
        }

        if (o->wire) {
            for (int i = 0; i < cn; i++) {
                const Pt *a = &p[i], *b = &p[(i + 1) % cn];
                int steps = (int)(fabs(b->x - a->x) + fabs(b->y - a->y)) + 1;
                for (int s = 0; s <= steps; s++) {
                    double u = (double)s / steps;
                    int xi = (int)(a->x + (b->x - a->x) * u);
                    int yi = (int)(a->y + (b->y - a->y) * u);
                    if (xi < 0 || xi >= R3D_W || yi < 0 || yi >= R3D_H)
                        continue;
                    t->colour[yi * R3D_W + xi] = colour;
                }
            }
        } else {
            fill(t, p, cn, colour);
        }
    }
}
