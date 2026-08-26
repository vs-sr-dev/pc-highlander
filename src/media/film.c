#include "film.h"

#include "../util/io.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define SYNC 0x31313131u                /* '1111', CINEPAK.S's sync_header  */

static char why[192];

const char *film_error(void)
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

/* ---- finding a film ------------------------------------------------ */

/* From a byte offset, forward to the film the player would land on: either a
 * 'FILM' header outright - which is film 0, at byte zero of the track with no
 * pad in front of it - or the sync tag, whose run of '1' bytes ends exactly
 * where the header begins. */
static long seek_film(FILE *f, long from)
{
    uint8_t w[3 * FILM_BLOCK];

    if (fseek(f, from, SEEK_SET) != 0)
        return -1;
    size_t n = fread(w, 1, sizeof w, f);

    for (size_t i = 0; i + 20 <= n; i++) {
        if (!memcmp(w + i, "FILM", 4) && !memcmp(w + i + 16, "FDSC", 4))
            return from + (long)i;
        if (be32(w + i) == SYNC) {
            while (i < n && w[i] == 0x31)
                i++;
            if (i + 20 <= n && !memcmp(w + i, "FILM", 4) &&
                !memcmp(w + i + 16, "FDSC", 4))
                return from + (long)i;
            return -1;
        }
    }
    return -1;
}

/* ---- the header ---------------------------------------------------- */

static int read_header(Film *fm)
{
    uint8_t h[16];

    if (fseek(fm->f, fm->start, SEEK_SET) != 0 || fread(h, 1, 16, fm->f) != 16)
        return fail("film at %ld: cannot read its header", fm->start);
    fm->header = be32(h + 4);
    if (fm->header < 36 || fm->header > 0x10000)
        return fail("film at %ld: header of %u bytes", fm->start, fm->header);

    uint8_t *hdr = malloc(fm->header);
    if (!hdr)
        return fail("out of memory");
    if (fseek(fm->f, fm->start, SEEK_SET) != 0 ||
        fread(hdr, 1, fm->header, fm->f) != fm->header) {
        free(hdr);
        return fail("film at %ld: header runs off the track", fm->start);
    }

    /* 'FILM' carries the length of the whole header, and the blocks inside it
     * carry their own, so the walk is by size rather than by fixed offsets. */
    int ok = 0;
    for (uint32_t p = 16; p + 8 <= fm->header; ) {
        uint32_t size = be32(hdr + p + 4);
        if (size < 8 || p + size > fm->header)
            break;
        if (!memcmp(hdr + p, "FDSC", 4) && size >= 20) {
            memcpy(fm->codec, hdr + p + 8, 4);
            fm->codec[4] = 0;
            fm->height = (int)be32(hdr + p + 12);
            fm->width  = (int)be32(hdr + p + 16);
        } else if (!memcmp(hdr + p, "CTAB", 4) && size >= 16) {
            fm->rate    = be32(hdr + p + 8);
            fm->nchunks = (int)be32(hdr + p + 12);
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
                fm->chunk[i].off  = be32(e);
                fm->chunk[i].size = be32(e + 4);
                fm->chunk[i].ts   = be32(e + 8);
                fm->chunk[i].tag  = be32(e + 12);
            }
            ok = 1;
        }
        p += size;
    }
    free(hdr);
    if (!ok)
        return fail("film at %ld: no chunk table", fm->start);

    FilmChunk *last = &fm->chunk[fm->nchunks - 1];
    fm->bytes = last->off + last->size;
    fm->ticks = last->ts;
    return 1;
}

int film_open(Film *fm, const char *track, uint32_t block)
{
    memset(fm, 0, sizeof *fm);
    fm->f = fopen(track, "rb");
    if (!fm->f)
        return fail("cannot open %s", track);

    fm->start = seek_film(fm->f, (long)block * FILM_BLOCK);
    if (fm->start < 0) {
        film_close(fm);
        return fail("no film at block %u: no sync tag inside three blocks of "
                    "it", block);
    }
    if (!read_header(fm)) {
        film_close(fm);
        return 0;
    }
    return 1;
}

void film_close(Film *fm)
{
    if (fm->f)
        fclose(fm->f);
    free(fm->chunk);
    free(fm->buf);
    memset(fm, 0, sizeof *fm);
}

/* ---- a chunk, and its sample table ---------------------------------- */

int film_chunk(Film *fm, int n)
{
    fm->loaded = -1;
    fm->nsamples = 0;
    if (n < 0 || n >= fm->nchunks)
        return fail("chunk %d of a film with %d", n, fm->nchunks);

    FilmChunk *c = &fm->chunk[n];
    if (c->size < FILM_SYNC + 16)
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

    /* The sync pad is the chunk's own tag, sixteen times over. */
    for (int i = 0; i < FILM_SYNC; i += 4)
        if (be32(fm->buf + i) != c->tag)
            return fail("chunk %d: the sync pad is not its tag", n);

    const uint8_t *s = fm->buf + FILM_SYNC;
    if (memcmp(s, "STAB", 4) != 0)
        return fail("chunk %d: no STAB behind the sync pad", n);
    uint32_t size  = be32(s + 4);
    uint32_t count = be32(s + 12);
    if (count > FILM_SAMPLES)
        return fail("chunk %d: %u samples", n, count);
    if (size != count * 16 + 16)
        return fail("chunk %d: STAB of %u bytes for %u samples", n, size, count);
    if (FILM_SYNC + size > c->size)
        return fail("chunk %d: its sample table is longer than it is", n);

    fm->table = FILM_SYNC + size;
    fm->nsamples = (int)count;
    uint32_t room = c->size - fm->table;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = s + 16 + i * 16;
        FilmSample *sm = &fm->sample[i];
        sm->off  = be32(e);
        sm->size = be32(e + 4);
        sm->ts   = be32(e + 8);
        sm->dur  = be32(e + 12);
        uint32_t begin = film_audio(sm) ? sm->off - sm->size : sm->off;
        if (film_audio(sm) && sm->size > sm->off) {
            fm->nsamples = 0;
            return fail("chunk %d sample %u: an audio block of %u bytes "
                        "ending at %u", n, i, sm->size, sm->off);
        }
        if (sm->size > room || begin > room - sm->size) {
            fm->nsamples = 0;
            return fail("chunk %d sample %u: %u bytes at %u, past the chunk's "
                        "%u", n, i, sm->size, begin, room);
        }
    }
    fm->loaded = n;
    return 1;
}

uint32_t film_unaccounted(const Film *fm)
{
    if (fm->loaded < 0)
        return 0;
    uint32_t end = 0;
    for (int i = 0; i < fm->nsamples; i++) {
        const FilmSample *s = &fm->sample[i];
        uint32_t e = film_audio(s) ? s->off : s->off + s->size;
        if (e > end)
            end = e;
    }
    return fm->chunk[fm->loaded].size - fm->table - end;
}

/* ---- the inventory -------------------------------------------------- */

/* Track 7 is a plain concatenation, so the films can be walked: read a header,
 * step over the payload it declares, and look for the next sync.  Between one
 * film's last byte and the next film's pad there are exactly 56 blocks of
 * zero fill, which is the read-ahead the player is allowed to run into. */
int film_scan(const char *track, uint32_t *blocks, int max)
{
    static uint8_t w[65536];
    FILE *f = fopen(track, "rb");
    if (!f) {
        fail("cannot open %s", track);
        return 0;
    }

    Film fm;
    int n = 0;
    long at = 0;

    while (n < max) {
        memset(&fm, 0, sizeof fm);
        fm.f = f;
        fm.start = seek_film(f, at);
        if (fm.start < 0 || !read_header(&fm)) {
            free(fm.chunk);
            break;
        }
        blocks[n++] = (uint32_t)(fm.start / FILM_BLOCK);
        at = fm.start + (long)fm.header + (long)fm.bytes;
        free(fm.chunk);

        /* Past the payload comes the zero fill, then the next film's pad.
         * Walk it rather than assuming its length. */
        long p = at;
        int found = 0;
        while (!found) {
            if (fseek(f, p, SEEK_SET) != 0)
                break;
            size_t got = fread(w, 1, sizeof w, f);
            if (got < 4)
                break;
            for (size_t i = 0; i + 4 <= got; i++)
                if (be32(w + i) == SYNC) {
                    at = p + (long)i;
                    found = 1;
                    break;
                }
            if (!found)
                p += (long)got - 3;
        }
        if (!found)
            break;
    }
    fclose(f);
    return n;
}
