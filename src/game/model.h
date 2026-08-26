/* model - the polyhedron format SKELSKIN.EXE emitted, read straight off the
 * disc rather than from the OBJ files tools/model/modelx.py writes.  The
 * engine wants what the game had: RGB16 facet colours and stored normals.
 *
 * Format: docs/03-data-formats.md 3.3.
 */
#ifndef HL_MODEL_H
#define HL_MODEL_H

#include <stdint.h>
#include <stddef.h>

#define MODEL_MAX_FACET_VERTS 32

typedef struct {
    uint16_t colour;                        /* RGB16, R5 B5 G6            */
    int16_t  normal[3];                     /* facet normal               */
    int      nverts;
    uint8_t  vert[MODEL_MAX_FACET_VERTS];
} Facet;

typedef struct {
    long      offset;                       /* where it was found in the file */
    uint32_t  base;                         /* the address it is linked at    */
    int       origin;                       /* the origin this piece hangs on */
    int       nverts;                       /* drawn vertices                 */
    int       norigins;                     /* origin points, not drawn       */
    int       nfacets;
    int16_t (*vert)[3];                     /* nverts + norigins entries      */
    uint8_t  *origin_id;                    /* norigins: what each one is called */
    Facet    *facet;
} Model;

/* The origin points are the skeleton.  A model's header byte says which origin
 * it hangs on - 0 for a root, 128..255 for a piece - and the origin points that
 * follow its vertices are numbered the same way, in the fourth word the drawn
 * vertices spend on a homogeneous 1.  3DENGINE.GAS transforms an origin point
 * exactly like a vertex and writes the result into a table indexed by that
 * number; the next model along reads its own position straight out of it.
 * docs/14-characters.md 14.1. */
#define MODEL_ORIGIN(m, k)  ((m)->vert[(m)->nverts + (k)])

/* Parses the model at offset off.  Returns 0 if there is not one there. */
int  model_parse(Model *m, const uint8_t *d, size_t size, long off);

/* Walks the whole file the way modelx does, collecting every model.  The
 * caller frees with model_free_all.  Returns the count. */
int  model_scan(const uint8_t *d, size_t size, int min_verts, Model **out);

void model_free(Model *m);
void model_free_all(Model *list, int n);

#endif
