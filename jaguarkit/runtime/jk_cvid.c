#include "jk_cvid.h"
#include "jk_be.h"

#include <stdlib.h>
#include <string.h>

static const char *const errs[] = {
    "ok",
    "the frame ends inside a header",
    "the frame's own length is not the size its container gives",
    "the frame is not the size the film declares",
    "more strips than the format allows",
    "a strip overruns the frame",
    "a chunk overruns its strip",
    "the vectors end before the picture does"
};

const char *jk_cvid_why(int err)
{
    return (unsigned)err < sizeof errs / sizeof *errs ? errs[err] : "?";
}

int jk_cvid_open(JkCvid *c, int show_w, int show_h)
{
    memset(c, 0, sizeof *c);
    if (show_w <= 0 || show_h <= 0)
        return 0;
    c->show_w = show_w;
    c->show_h = show_h;
    c->w = (show_w + 3) & ~3;
    c->h = (show_h + 3) & ~3;
    c->rgb = calloc((size_t)c->w * c->h, 3);
    return c->rgb != NULL;
}

void jk_cvid_close(JkCvid *c)
{
    free(c->rgb);
    c->rgb = NULL;
}

static inline uint8_t clip(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
}

/* An entry is four Y and a signed U and V - one 2x2 block - kept converted to
 * four RGB triples.  An `update` chunk puts a flag word in front of every 32
 * entries and writes only those whose bit is set; both kinds may stop early. */
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
                flag = jk_be32(d);
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

static void paint4(JkCvid *c, int x, int y, const uint8_t *e0,
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

static void paint1(JkCvid *c, int x, int y, const uint8_t *e)
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

/* One flag stream answers both questions, in the order they are asked: on a
 * $31 chunk "is this block coded at all" first, then "V1 or V4". */
static int vectors(JkCvid *c, const uint8_t *d, size_t size, int cid, int s,
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
                        return JK_CVID_VECTORS;
                    flag = jk_be32(d);
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
                        return JK_CVID_VECTORS;
                    flag = jk_be32(d);
                    d += 4;
                    mask = 0x80000000u;
                }
                one = !(flag & mask);
            }
            if (one) {
                if (d + 1 > end)
                    return JK_CVID_VECTORS;
                paint1(c, x, y, v1 + *d++ * 12);
            } else {
                if (d + 4 > end)
                    return JK_CVID_VECTORS;
                paint4(c, x, y, v4 + d[0] * 12, v4 + d[1] * 12,
                                v4 + d[2] * 12, v4 + d[3] * 12);
                d += 4;
            }
        }
    }
    return JK_CVID_OK;
}

static int strip(JkCvid *c, const uint8_t *d, size_t size, int s,
                 int y0, int y1)
{
    const uint8_t *end = d + size;

    while (d + 4 <= end) {
        uint32_t cid = jk_be16(d), csize = jk_be16(d + 2);
        if (csize < 4 || (size_t)(end - d) < csize)
            return JK_CVID_CHUNK;
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
            if (e != JK_CVID_OK)
                return e;
            break;
        }
        default:
            break;
        }
        d += csize;
    }
    return JK_CVID_OK;
}

int jk_cvid_frame(JkCvid *c, const uint8_t *d, size_t size)
{
    if (size < 10)
        return JK_CVID_SHORT;

    int flags = d[0];
    uint32_t length = jk_be32(d) & 0xFFFFFFu;
    int w = jk_be16(d + 4), h = jk_be16(d + 6), n = jk_be16(d + 8);

    /* A container may pad a frame to a multiple of four (Myst does). */
    if (length > size || size - length > 3)
        return JK_CVID_LENGTH;
    size = length;
    if (w != c->w || h != c->h)
        return JK_CVID_SIZE;
    if (n > JK_CVID_STRIPS)
        return JK_CVID_STRIPCOUNT;

    const uint8_t *p = d + 10, *end = d + size;
    int y = 0;

    for (int s = 0; s < n; s++) {
        if (p + 12 > end)
            return JK_CVID_STRIP;
        uint32_t ssize = jk_be16(p + 2);
        int height = jk_be16(p + 8);
        if (ssize < 12 || (size_t)(end - p) < ssize)
            return JK_CVID_STRIP;
        /* A strip that sends no codebook continues the one above it. */
        if (s > 0 && !(flags & 0x01)) {
            memcpy(c->v1[s], c->v1[s - 1], sizeof c->v1[s]);
            memcpy(c->v4[s], c->v4[s - 1], sizeof c->v4[s]);
        }
        int bottom = y + height;
        if (bottom > c->h)
            bottom = c->h;
        int e = strip(c, p + 12, ssize - 12, s, y, bottom);
        if (e != JK_CVID_OK)
            return e;
        y += height;
        p += ssize;
    }

    c->frames++;
    c->keyframe = !(flags & 0x01);
    if (c->keyframe)
        c->keyframes++;
    return JK_CVID_OK;
}

void jk_cvid_rgb24(const JkCvid *c, uint8_t *out)
{
    for (int y = 0; y < c->show_h; y++)
        memcpy(out + (size_t)y * c->show_w * 3,
               c->rgb + (size_t)y * c->w * 3, (size_t)c->show_w * 3);
}

void jk_cvid_rgb16(const JkCvid *c, uint16_t *out)
{
    for (int y = 0; y < c->show_h; y++) {
        const uint8_t *p = c->rgb + (size_t)y * c->w * 3;
        for (int x = 0; x < c->show_w; x++, p += 3) {
            unsigned r = ((unsigned)p[0] + 4) >> 3;
            unsigned b = ((unsigned)p[2] + 4) >> 3;
            unsigned g = ((unsigned)p[1] + 2) >> 2;
            if (r > 31) r = 31;
            if (g > 63) g = 63;
            if (b > 31) b = 31;
            *out++ = (uint16_t)((r << 11) | (b << 6) | g);
        }
    }
}
