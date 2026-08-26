#include "anim.h"
#include "../util/io.h"

#include <stdlib.h>
#include <string.h>

int anim_parse(Anim *a, const uint8_t *d, size_t size, long off)
{
    memset(a, 0, sizeof *a);
    if (off < 0 || (size_t)off + 4 + ANIM_HEADER > size)
        return 0;
    uint32_t total = be32(d + off);
    if (!(total > ANIM_HEADER && total <= 0x20000) || (size_t)off + 4 + total > size)
        return 0;

    const uint8_t *h = d + off + 4;
    unsigned asize     = be16(h);
    unsigned hdr       = be16(h + 2);
    unsigned framesize = be16(h + 4);
    unsigned pieces    = h[6];
    unsigned frames    = h[7];

    /* The predicate: the header has to describe itself.  FRAMESIZE is
     * 20 + 3 * pieces rounded up to even and ANIMSIZE is the header plus the
     * frames, and both hold for every record on the disc. */
    if (hdr != ANIM_HEADER || asize != total || frames == 0 || pieces == 0 || pieces > 64)
        return 0;
    if (framesize != ((20 + 3 * pieces + 1) & ~1u))
        return 0;
    size_t need = ANIM_HEADER + (size_t)framesize * frames;
    if (!(need <= asize && asize <= need + 8))
        return 0;

    a->offset       = off;
    a->size         = (int)asize;
    a->framesize    = (int)framesize;
    a->pieces       = (int)pieces;
    a->frames       = (int)frames;
    a->fps          = h[8];
    a->sound_sheet  = h[9];
    a->sound_entry  = (int)be16(h + 10);
    a->height_start = be16s(h + 12);
    a->data         = h + ANIM_HEADER;
    return (int)(4 + total);
}

int anim_scan(const uint8_t *d, size_t size, Anim **out)
{
    int cap = 64, n = 0;
    Anim *list = malloc((size_t)cap * sizeof *list);
    if (!list)
        return 0;
    for (long off = 0; (size_t)off + 4 + ANIM_HEADER < size; ) {
        Anim a;
        int len = anim_parse(&a, d, size, off);
        if (len) {
            if (n == cap) {
                cap *= 2;
                Anim *bigger = realloc(list, (size_t)cap * sizeof *list);
                if (!bigger) break;
                list = bigger;
            }
            list[n++] = a;
            off += len;
        } else {
            off += 2;
        }
    }
    *out = list;
    return n;
}

void anim_frame(const Anim *a, int frame, AnimFrame *f)
{
    memset(f, 0, sizeof *f);
    if (a->frames <= 0)
        return;
    frame %= a->frames;
    if (frame < 0)
        frame += a->frames;
    const uint8_t *p = a->data + (size_t)frame * a->framesize;
    for (int k = 0; k < 3; k++)
        f->move[k] = be16s(p + k * 2);
    f->turn      = (int8_t)p[6];
    f->flags     = p[7];
    f->spin      = (int8_t)p[8];
    f->hit       = (int8_t)p[9];
    f->range     = be16s(p + 10);
    f->dir[0]    = (int8_t)p[12];
    f->dir[1]    = (int8_t)p[13];
    f->spread[0] = (int8_t)p[14];
    f->spread[1] = (int8_t)p[15];
    f->high      = be16s(p + 16);
    f->low       = be16s(p + 18);
    f->angle     = (const int8_t *)(p + 20);
}
