#include "model.h"
#include "../util/io.h"

#include <stdlib.h>
#include <string.h>

#define HEADER 24

int model_parse(Model *m, const uint8_t *d, size_t size, long off)
{
    memset(m, 0, sizeof *m);
    if (off < 0 || (size_t)off + HEADER > size)
        return 0;

    const uint8_t *h = d + off;
    unsigned len      = be16(h);
    unsigned norigins = h[3];
    unsigned nv       = be16(h + 4);
    unsigned nf       = be16(h + 6);
    uint32_t vlp      = be32(h + 8);
    uint32_t flp      = be32(h + 12);
    uint32_t slp      = be32(h + 16);

    if (!(len > HEADER && len <= 0x20000 && nv > 0 && nv < 4000 && nf > 0 && nf < 8000))
        return 0;
    /* The predicate that finds models with no false positives: the facet list
     * must start exactly where the vertex list ends. */
    if (flp != vlp + (nv + norigins) * 8)
        return 0;

    size_t vo = (size_t)off + HEADER;
    size_t total = nv + norigins;
    if (vo + total * 8 > size)
        return 0;

    size_t fo = vo + total * 8;
    Facet *facets = calloc(nf, sizeof *facets);
    if (!facets)
        return 0;
    for (unsigned i = 0; i < nf; i++) {
        if (fo + 12 > size) { free(facets); return 0; }
        unsigned nverts = be16(d + fo + 8);
        unsigned nwords = be16(d + fo + 10);
        if (nverts == 0 || nverts > MODEL_MAX_FACET_VERTS ||
            nwords * 4 < nverts || nwords > 8) { free(facets); return 0; }
        if (fo + 12 + nwords * 4 > size) { free(facets); return 0; }
        Facet *f = &facets[i];
        f->colour    = be16(d + fo);
        f->normal[0] = be16s(d + fo + 2);
        f->normal[1] = be16s(d + fo + 4);
        f->normal[2] = be16s(d + fo + 6);
        f->nverts    = (int)nverts;
        memcpy(f->vert, d + fo + 12, nverts);
        fo += 12 + nwords * 4;
    }

    /* The declared length covers the facet list, and the SLP payload after it
     * when there is one.  SLP always points at the byte past the facets. */
    size_t consumed = fo - (size_t)off;
    uint32_t base = vlp - HEADER;
    if (!(consumed == len ||
          (slp == base + consumed && consumed < len && len <= consumed + 0x400))) {
        free(facets);
        return 0;
    }

    m->vert = malloc(total * sizeof *m->vert);
    if (!m->vert) { free(facets); return 0; }
    for (size_t i = 0; i < total; i++)
        for (int k = 0; k < 3; k++)
            m->vert[i][k] = be16s(d + vo + i * 8 + (size_t)k * 2);

    m->offset   = off;
    m->base     = base;
    m->nverts   = (int)nv;
    m->norigins = (int)norigins;
    m->nfacets  = (int)nf;
    m->facet    = facets;
    return (int)len;
}

int model_scan(const uint8_t *d, size_t size, int min_verts, Model **out)
{
    int cap = 64, n = 0;
    Model *list = malloc((size_t)cap * sizeof *list);
    if (!list)
        return 0;
    for (long off = 0; (size_t)off + HEADER < size; ) {
        Model m;
        int len = model_parse(&m, d, size, off);
        if (len && m.nverts >= min_verts) {
            if (n == cap) {
                cap *= 2;
                Model *bigger = realloc(list, (size_t)cap * sizeof *list);
                if (!bigger) break;
                list = bigger;
            }
            list[n++] = m;
            off += len;
        } else {
            if (len)
                model_free(&m);
            off += 2;
        }
    }
    *out = list;
    return n;
}

void model_free(Model *m)
{
    free(m->vert);
    free(m->facet);
    m->vert = NULL;
    m->facet = NULL;
}

void model_free_all(Model *list, int n)
{
    for (int i = 0; i < n; i++)
        model_free(&list[i]);
    free(list);
}
