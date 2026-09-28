#include "jk_film.h"
#include "jk_be.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static char why[192];

const char *jk_film_error(void)
{
    return why;
}

static int fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    return 0;
}

static int is_header(const uint8_t *p)
{
    uint32_t size = jk_be32(p + 4);
    return !memcmp(p, "FILM", 4) && !memcmp(p + 16, "FDSC", 4) &&
           size > 16 && size < 0x10000;
}

/* From a byte offset, forward to the first film header inside `span` bytes.
 * 'FILM' also occurs inside video, so the test is structural. */
static long seek_film(FILE *f, long from, size_t span)
{
    static uint8_t w[65536 + 20];

    while (span > 0) {
        size_t want = span < 65536 ? span : 65536;
        if (fseek(f, from, SEEK_SET) != 0)
            return -1;
        size_t n = fread(w, 1, want + 20, f);
        if (n < 20)
            return -1;
        for (size_t i = 0; i + 20 <= n && i < want; i++)
            if (w[i] == 'F' && is_header(w + i))
                return from + (long)i;
        from += (long)want;
        span -= want;
    }
    return -1;
}

static int read_header(JkFilm *fm)
{
    uint8_t h[16];

    if (fseek(fm->f, fm->start, SEEK_SET) != 0 || fread(h, 1, 16, fm->f) != 16)
        return fail("film at %ld: cannot read its header", fm->start);
    fm->header = jk_be32(h + 4);

    uint8_t *hdr = malloc(fm->header);
    if (!hdr)
        return fail("out of memory");
    if (fseek(fm->f, fm->start, SEEK_SET) != 0 ||
        fread(hdr, 1, fm->header, fm->f) != fm->header) {
        free(hdr);
        return fail("film at %ld: header runs off the track", fm->start);
    }

    /* The walk is by size: every block carries its own length. */
    int ok = 0;
    for (uint32_t p = 16; p + 8 <= fm->header; ) {
        uint32_t size = jk_be32(hdr + p + 4);
        if (size < 8 || p + size > fm->header)
            break;
        if (!memcmp(hdr + p, "FDSC", 4) && size >= 20) {
            memcpy(fm->codec, hdr + p + 8, 4);
            fm->codec[4] = 0;
            fm->height = (int)jk_be32(hdr + p + 12);
            fm->width  = (int)jk_be32(hdr + p + 16);
        } else if (!memcmp(hdr + p, "ADSC", 4) && size >= 20) {
            fm->has_adsc = 1;
            memcpy(fm->adsc, hdr + p + 8, 12);
        } else if (!memcmp(hdr + p, "CTAB", 4) && size >= 16) {
            fm->rate    = jk_be32(hdr + p + 8);
            fm->nchunks = (int)jk_be32(hdr + p + 12);
            if (fm->nchunks <= 0 ||
                (uint32_t)fm->nchunks * 16 + 16 != size) {
                free(hdr);
                return fail("film at %ld: CTAB of %u bytes for %d chunks",
                            fm->start, size, fm->nchunks);
            }
            fm->chunk = malloc((size_t)fm->nchunks * sizeof *fm->chunk);
            if (!fm->chunk) {
                free(hdr);
                return fail("out of memory");
            }
            for (int i = 0; i < fm->nchunks; i++) {
                const uint8_t *e = hdr + p + 16 + i * 16;
                fm->chunk[i].off  = jk_be32(e);
                fm->chunk[i].size = jk_be32(e + 4);
                fm->chunk[i].ts   = jk_be32(e + 8);
                fm->chunk[i].tag  = jk_be32(e + 12);
            }
            ok = 1;
        }
        p += size;
    }
    free(hdr);
    if (!ok)
        return fail("film at %ld: no chunk table", fm->start);

    JkFilmChunk *last = &fm->chunk[fm->nchunks - 1];
    fm->bytes = last->off + last->size;
    fm->ticks = last->ts;
    return 1;
}

int jk_film_open(JkFilm *fm, const char *track, uint32_t block)
{
    memset(fm, 0, sizeof *fm);
    fm->loaded = -1;
    fm->f = fopen(track, "rb");
    if (!fm->f)
        return fail("cannot open %s", track);
    fm->start = seek_film(fm->f, (long)block * JK_FILM_BLOCK, 3 * JK_FILM_BLOCK);
    if (fm->start < 0) {
        jk_film_close(fm);
        return fail("no film within three blocks of block %u", block);
    }
    if (!read_header(fm)) {
        jk_film_close(fm);
        return 0;
    }
    return 1;
}

void jk_film_close(JkFilm *fm)
{
    if (fm->f)
        fclose(fm->f);
    free(fm->chunk);
    free(fm->buf);
    memset(fm, 0, sizeof *fm);
}

int jk_film_chunk(JkFilm *fm, int n)
{
    fm->loaded = -1;
    fm->nsamples = 0;
    if (n < 0 || n >= fm->nchunks)
        return fail("chunk %d of a film with %d", n, fm->nchunks);

    JkFilmChunk *c = &fm->chunk[n];
    if (c->size < JK_FILM_SYNC + 16)
        return fail("chunk %d is %u bytes", n, c->size);
    if (c->size > fm->cap) {
        uint8_t *p = realloc(fm->buf, c->size);
        if (!p)
            return fail("out of memory for a chunk of %u bytes", c->size);
        fm->buf = p;
        fm->cap = c->size;
    }
    long at = fm->start + (long)fm->header + (long)c->off;
    if (fseek(fm->f, at, SEEK_SET) != 0 ||
        fread(fm->buf, 1, c->size, fm->f) != c->size)
        return fail("chunk %d runs off the end of the track", n);

    for (int i = 0; i < JK_FILM_SYNC; i += 4)
        if (jk_be32(fm->buf + i) != c->tag)
            return fail("chunk %d: the sync pad is not its tag", n);

    const uint8_t *s = fm->buf + JK_FILM_SYNC;
    if (memcmp(s, "STAB", 4) != 0)
        return fail("chunk %d: no STAB behind the sync pad", n);
    uint32_t size  = jk_be32(s + 4);
    uint32_t count = jk_be32(s + 12);
    if (count > JK_FILM_SAMPLES)
        return fail("chunk %d: %u samples", n, count);
    if (size != count * 16 + 16)
        return fail("chunk %d: STAB of %u bytes for %u samples", n, size, count);
    if (JK_FILM_SYNC + size > c->size)
        return fail("chunk %d: its sample table is longer than it is", n);

    fm->table = JK_FILM_SYNC + size;
    fm->nsamples = (int)count;
    uint32_t room = c->size - fm->table;
    uint32_t prev = 0;                  /* where the last sample ended      */
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = s + 16 + i * 16;
        JkFilmSample *sm = &fm->sample[i];
        sm->off  = jk_be32(e);
        sm->size = jk_be32(e + 4);
        sm->ts   = jk_be32(e + 8);
        sm->dur  = jk_be32(e + 12);
        sm->at = sm->off;
        if (jk_film_audio(sm)) {
            /* The samples tile the chunk: an audio block starts where the
             * last sample ended, whichever end its offset names. */
            if (sm->off >= sm->size && sm->off - sm->size == prev)
                sm->at = sm->off - sm->size;
            else if (sm->off != prev) {
                fm->nsamples = 0;
                return fail("chunk %d sample %u: an audio block of %u bytes "
                            "at %u, and the last sample ended at %u", n, i,
                            sm->size, sm->off, prev);
            }
        }
        uint32_t begin = sm->at;
        prev = begin + sm->size;
        if (sm->size > room || begin > room - sm->size) {
            fm->nsamples = 0;
            return fail("chunk %d sample %u: %u bytes at %u, past the chunk's "
                        "%u", n, i, sm->size, begin, room);
        }
    }
    fm->loaded = n;
    return 1;
}

uint32_t jk_film_unaccounted(const JkFilm *fm)
{
    if (fm->loaded < 0)
        return 0;
    uint32_t end = 0;
    for (int i = 0; i < fm->nsamples; i++) {
        const JkFilmSample *s = &fm->sample[i];
        uint32_t e = s->at + s->size;
        if (e > end)
            end = e;
    }
    return fm->chunk[fm->loaded].size - fm->table - end;
}

/* The block a game names for a film: where its sync pad starts, when one is
 * in front of the header - Highlander's film 7 has its pad in block 16,782
 * and its header in 16,783, and the script says 16,782. */
static long film_block(FILE *f, long start)
{
    uint8_t pad[JK_FILM_SYNC];

    if (start >= JK_FILM_SYNC && fseek(f, start - JK_FILM_SYNC, SEEK_SET) == 0 &&
        fread(pad, 1, sizeof pad, f) == sizeof pad) {
        int same = 1;
        for (int i = 4; i < JK_FILM_SYNC; i++)
            if (pad[i] != pad[i & 3])
                same = 0;
        if (same)
            return (start - JK_FILM_SYNC) / JK_FILM_BLOCK;
    }
    return start / JK_FILM_BLOCK;
}

int jk_film_scan(const char *track, uint32_t *blocks, int max)
{
    FILE *f = fopen(track, "rb");
    if (!f) {
        fail("cannot open %s", track);
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long end = ftell(f);

    int n = 0;
    long at = 0;
    while (n < max && at < end) {
        JkFilm fm;
        memset(&fm, 0, sizeof fm);
        fm.f = f;
        fm.start = seek_film(f, at, (size_t)(end - at));
        if (fm.start < 0)
            break;
        if (!read_header(&fm)) {
            free(fm.chunk);
            at = fm.start + 4;          /* a header that did not hold up    */
            continue;
        }
        blocks[n++] = (uint32_t)(film_block(f, fm.start));
        at = fm.start + (long)fm.header + (long)fm.bytes;
        free(fm.chunk);
    }
    fclose(f);
    return n;
}
