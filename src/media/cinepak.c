#include "cinepak.h"

#include "../util/io.h"

#include <stdlib.h>
#include <string.h>

static const char *const errs[] = {
    "ok",
    "the frame ends inside a header",
    "the frame's own length is not the size the sample table gives",
    "the frame is not the size the film declares",
    "more strips than the format allows",
    "a strip overruns the frame",
    "a chunk overruns its strip",
    "the vectors end before the picture does"
};

const char *cinepak_why(int err)
{
    return (unsigned)err < sizeof errs / sizeof *errs ? errs[err] : "?";
}

int cinepak_open(Cinepak *c, int w, int h)
{
    memset(c, 0, sizeof *c);
    if (w <= 0 || h <= 0 || (w & 3) || (h & 3))
        return 0;                       /* blocks are 4x4, and never partial */
    c->w = w;
    c->h = h;
    c->rgb = calloc((size_t)w * h, 3);
    return c->rgb != NULL;
}

void cinepak_close(Cinepak *c)
{
    free(c->rgb);
    c->rgb = NULL;
}

static inline uint8_t clip(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
}

/* ---- the codebooks -------------------------------------------------- */

/* An entry is four Y and a signed U and V - one 2x2 block - and it is kept
 * converted, as four RGB triples, because every block that indexes it wants
 * it that way.  A `grey` chunk carries the Y alone.  An `update` chunk puts a
 * 32-bit flag word in front of every 32 entries and writes only the ones whose
 * bit is set, which is how an inter frame nudges a codebook rather than
 * sending it again.  Both run out of data on purpose: an encoder that has
 * nothing to say about the tail of the codebook simply stops. */
static void codebook(uint8_t *cb, const uint8_t *d, size_t size,
                     int update, int grey)
{
    const uint8_t *end = d + size;
    int n = grey ? 4 : 6;
    uint32_t flag = 0, mask = 0;

    for (int i = 0; i < 256; i++) {
        if (update) {
            if (!(mask >>= 1)) {
                if (d + 4 > end)
                    return;
                flag = be32(d);
                d += 4;
                mask = 0x80000000u;
            }
            if (!(flag & mask))
                continue;
        }
        if (d + n > end)
            return;

        int y[4];
        for (int k = 0; k < 4; k++)
            y[k] = *d++;
        int u = 0, v = 0;
        if (!grey) {
            u = (int8_t)*d++;
            v = (int8_t)*d++;
        }
        uint8_t *e = cb + i * 12;
        for (int k = 0; k < 4; k++) {
            e[k * 3 + 0] = clip(y[k] + 2 * v);
            e[k * 3 + 1] = clip(y[k] - (u >> 1) - v);
            e[k * 3 + 2] = clip(y[k] + 2 * u);
        }
    }
}

/* ---- painting ------------------------------------------------------- */

/* V4: four entries, one per quadrant of the 4x4 block. */
static void paint4(Cinepak *c, int x, int y, const uint8_t *e0,
                   const uint8_t *e1, const uint8_t *e2, const uint8_t *e3)
{
    const uint8_t *q[4] = { e0, e1, e2, e3 };
    size_t stride = (size_t)c->w * 3;

    for (int i = 0; i < 4; i++) {
        uint8_t *p = c->rgb + (size_t)(y + (i >> 1) * 2) * stride
                            + (size_t)(x + (i & 1) * 2) * 3;
        memcpy(p, q[i], 6);
        memcpy(p + stride, q[i] + 6, 6);
    }
}

/* V1: one entry, each of its four pixels doubled both ways. */
static void paint1(Cinepak *c, int x, int y, const uint8_t *e)
{
    size_t stride = (size_t)c->w * 3;

    for (int r = 0; r < 4; r++) {
        uint8_t *p = c->rgb + (size_t)(y + r) * stride + (size_t)x * 3;
        const uint8_t *a = e + (r >> 1) * 6;
        memcpy(p + 0, a,     3);
        memcpy(p + 3, a,     3);
        memcpy(p + 6, a + 3, 3);
        memcpy(p + 9, a + 3, 3);
    }
}

/* ---- the vectors ---------------------------------------------------- */

/* One flag stream serves both questions, interleaved as they are asked: on a
 * $31 chunk, first "is this block coded at all", then "V1 or V4".  A block
 * that is not coded keeps what the last frame left in it, which is the whole
 * of inter-frame Cinepak. */
static int vectors(Cinepak *c, const uint8_t *d, size_t size, int cid, int s,
                   int y0, int y1)
{
    const uint8_t *end = d + size;
    const uint8_t *v1 = c->v1[s], *v4 = c->v4[s];
    int inter  = cid & 0x01;
    int v1only = cid & 0x02;
    uint32_t flag = 0, mask = 0;

    for (int y = y0; y + 4 <= y1; y += 4) {
        for (int x = 0; x + 4 <= c->w; x += 4) {
            if (inter) {
                if (!(mask >>= 1)) {
                    if (d + 4 > end)
                        return CVID_VECTORS;
                    flag = be32(d);
                    d += 4;
                    mask = 0x80000000u;
                }
                if (!(flag & mask))
                    continue;
            }
            int one = 1;
            if (!v1only) {
                if (!(mask >>= 1)) {
                    if (d + 4 > end)
                        return CVID_VECTORS;
                    flag = be32(d);
                    d += 4;
                    mask = 0x80000000u;
                }
                one = !(flag & mask);
            }
            if (one) {
                if (d + 1 > end)
                    return CVID_VECTORS;
                paint1(c, x, y, v1 + *d++ * 12);
            } else {
                if (d + 4 > end)
                    return CVID_VECTORS;
                paint4(c, x, y, v4 + d[0] * 12, v4 + d[1] * 12,
                                v4 + d[2] * 12, v4 + d[3] * 12);
                d += 4;
            }
        }
    }
    return CVID_OK;
}

/* ---- a strip, and a frame ------------------------------------------- */

static int strip(Cinepak *c, const uint8_t *d, size_t size, int s,
                 int y0, int y1)
{
    const uint8_t *end = d + size;

    while (d + 4 <= end) {
        uint32_t cid = be16(d), csize = be16(d + 2);
        if (csize < 4 || (size_t)(end - d) < csize)
            return CVID_CHUNK;
        const uint8_t *body = d + 4;
        size_t blen = csize - 4;
        cid >>= 8;
        switch (cid) {
        case 0x20: case 0x21: case 0x24: case 0x25:
            codebook(c->v4[s], body, blen, cid & 1, cid & 4);
            break;
        case 0x22: case 0x23: case 0x26: case 0x27:
            codebook(c->v1[s], body, blen, cid & 1, cid & 4);
            break;
        case 0x30: case 0x31: case 0x32: {
            int e = vectors(c, body, blen, cid, s, y0, y1);
            if (e != CVID_OK)
                return e;
            break;
        }
        default:
            break;                      /* nothing on this disc uses one    */
        }
        d += csize;
    }
    return CVID_OK;
}

int cinepak_frame(Cinepak *c, const uint8_t *d, size_t size)
{
    if (size < 10)
        return CVID_SHORT;

    int flags = d[0];
    uint32_t length = be32(d) & 0xFFFFFFu;
    int w = be16(d + 4), h = be16(d + 6), n = be16(d + 8);

    if (length != size)
        return CVID_LENGTH;
    if (w != c->w || h != c->h)
        return CVID_SIZE;
    if (n > CVID_STRIPS)
        return CVID_STRIPCOUNT;

    const uint8_t *p = d + 10, *end = d + size;
    int y = 0;

    for (int s = 0; s < n; s++) {
        if (p + 12 > end)
            return CVID_STRIP;
        uint32_t ssize = be16(p + 2);
        int height = be16(p + 8);
        if (ssize < 12 || (size_t)(end - p) < ssize)
            return CVID_STRIP;

        /* A strip that sends no codebook of its own continues the one above
         * it - which is what the empty $21 and $23 of this disc's second
         * strip mean, on the frames that carry a whole picture. */
        if (s > 0 && !(flags & 0x01)) {
            memcpy(c->v1[s], c->v1[s - 1], sizeof c->v1[s]);
            memcpy(c->v4[s], c->v4[s - 1], sizeof c->v4[s]);
        }
        int bottom = y + height;
        if (bottom > c->h)
            bottom = c->h;
        int e = strip(c, p + 12, ssize - 12, s, y, bottom);
        if (e != CVID_OK)
            return e;
        y += height;
        p += ssize;
    }

    c->frames++;
    c->keyframe = !(flags & 0x01);
    if (c->keyframe)
        c->keyframes++;
    return CVID_OK;
}

void cinepak_rgb16(const Cinepak *c, uint16_t *out)
{
    const uint8_t *p = c->rgb;
    int n = c->w * c->h;

    for (int i = 0; i < n; i++, p += 3)
        out[i] = (uint16_t)(((p[0] >> 3) << 11) |    /* R5 */
                            ((p[2] >> 3) <<  6) |    /* B5 */
                             (p[1] >> 2));           /* G6 */
}
