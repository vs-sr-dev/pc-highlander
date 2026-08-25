/* r3d - the flat-shaded, Z-buffered rasteriser.
 *
 * It follows 3DENGINE.GAS: a right-handed view space with y up and the camera
 * looking down -z, a 3x3 rotation in s1.14 applied as v' = M . v, and the
 * perspective divide
 *
 *     sx = 159 + x * 300 / max(|z|, 40)
 *     sy =  99 + y * 246 / max(|z|, 40)        246 = 300 * $347A / 2^14
 *
 * with sy measured from the bottom of the screen, so the scanline the blitter
 * writes is 199 - sy.  Depth is kept the way the backdrops store it, as the
 * distance in front of the camera, and smaller wins.
 */
#ifndef HL_R3D_H
#define HL_R3D_H

#include <stdint.h>

#include "../game/model.h"
#include "../game/scene.h"

#define R3D_W       SCENE_W
#define R3D_H       SCENE_H
#define R3D_CX      159
#define R3D_CY      99
#define R3D_XSCALE  300
#define R3D_ASPECT  0x347A                              /* 3DENGINE.GAS   */
#define R3D_YSCALE  ((R3D_XSCALE * R3D_ASPECT) >> 14)   /* = 246          */
#define R3D_ZMIN    40                                  /* the divide clamp */
#define R3D_ZFAR    0x7E00                              /* the cull limit   */

typedef struct {
    uint16_t colour[R3D_W * R3D_H];     /* RGB16, as the Jaguar had it     */
    int32_t  depth[R3D_W * R3D_H];      /* |z| in front of the camera      */
} R3dTarget;

/* v_view = (rot / 16384) . v + pos */
typedef struct {
    int32_t rot[9];                     /* row major, s1.14                */
    int32_t pos[3];
} R3dXform;

typedef enum { R3D_CULL_NONE, R3D_CULL_BACK, R3D_CULL_FRONT } R3dCull;

typedef struct {
    R3dCull cull;
    int     shade;                      /* 0 = flat colour, 1 = lit        */
    int     wire;                       /* draw facet edges instead        */
} R3dOpts;

/* What the last model cost, which is how the depth test is checked: a model
 * standing behind scenery loses pixels to it. */
typedef struct {
    int facets;                         /* facets that survived culling    */
    int tested;                         /* pixels the rasteriser reached   */
    int drawn;                          /* pixels that beat the Z-buffer   */
    int32_t znear, zfar;                /* the depth range it covered      */
} R3dStats;

extern R3dStats r3d_stats;

void r3d_backdrop(R3dTarget *t, const Scene *s);
void r3d_clear(R3dTarget *t, uint16_t colour, int32_t depth);

/* The object rotation the game builds from a world record's three angles,
 * in FORMMAT.GAS's order and its 256-step circle. */
void r3d_face_matrix(int elevation, int azimuth, int twist, int32_t out[9]);

void r3d_identity(int32_t out[9]);
void r3d_mul(const int32_t a[9], const int32_t b[9], int32_t out[9]);   /* a . b */

/* Builds the transform that takes model space to the view space of a scene:
 * the camera's own matrix concatenated with the object's, and the object's
 * world position carried through the camera. */
void r3d_place(R3dXform *x, const SceneCam *cam,
               const int32_t obj_rot[9], const int32_t obj_pos[3]);

void r3d_draw_model(R3dTarget *t, const Model *m, const R3dXform *x,
                    const R3dOpts *o);

/* Projection of one view-space point.  Returns 0 if it is behind the near
 * clamp.  sy comes back as a scanline, counted from the top. */
int  r3d_project(const int32_t v[3], int *sx, int *sy);

#endif
