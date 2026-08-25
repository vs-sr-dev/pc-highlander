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
    int       nverts;                       /* drawn vertices                 */
    int       norigins;                     /* origin points, not drawn       */
    int       nfacets;
    int16_t (*vert)[3];
    Facet    *facet;
} Model;

/* Parses the model at offset off.  Returns 0 if there is not one there. */
int  model_parse(Model *m, const uint8_t *d, size_t size, long off);

/* Walks the whole file the way modelx does, collecting every model.  The
 * caller frees with model_free_all.  Returns the count. */
int  model_scan(const uint8_t *d, size_t size, int min_verts, Model **out);

void model_free(Model *m);
void model_free_all(Model *list, int n);

#endif
