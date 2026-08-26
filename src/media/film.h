/* film - the FILM container that holds the 36 Cinepak films of track 7.
 *
 * Format and provenance: docs/09-text-and-fmv.md 9.2 and 9.3.  A film is a
 * header and then chunks of about a second each:
 *
 *   'FILM' size 0 0                      size is the whole header
 *   'FDSC' 20  'cvid' height width       always 240 x 320 on this disc
 *   'CTAB' size rate count               then 16 per chunk: off, size, ts, tag
 *
 * and every chunk opens with 64 bytes of its own tag repeated - the same sync
 * pad convention the CD tracks use - followed by
 *
 *   'STAB' size rate count               then 16 per sample: off, size, ts, type
 *
 * which interleaves the video frames with the speech.  Chunk offsets are from
 * the end of the film header; sample offsets are from the end of the sample
 * table, and an audio sample's offset is where it *ends*.
 *
 * A script names a film by CD block, and the player seeks to that block and
 * scans forward for the long '1111' - `CINEPAK.S`'s own `sync_header`, and
 * what the 64 bytes in front of every film are filled with.  That is why the
 * films need not be block aligned, and it is what film_open does.
 */
#ifndef HL_FILM_H
#define HL_FILM_H

#include <stdint.h>
#include <stdio.h>

#define FILM_BLOCK   2352       /* a CD block, which is what a script names  */
#define FILM_SYNC    64         /* the pad in front of a film and each chunk */
#define FILM_SAMPLES 256        /* a one-second chunk holds about fourteen   */
#define FILM_AUDIO_TS 0xFFFFFFFFu

typedef struct {
    uint32_t off;               /* from the end of the sample table; for
                                   audio it is the end, not the start        */
    uint32_t size;
    uint32_t ts;                /* film ticks, and all ones marks an audio
                                   block.  Bit 31 clear marks a frame the
                                   player may start on - see film_resume     */
    uint32_t dur;               /* video: how long the frame is held, in the
                                   film's own ticks.  50 at a rate of 600,
                                   2 at 24, 2 and 3 alternating at 30 - all
                                   of them 12 fps.  Audio carries 1          */
} FilmSample;

typedef struct { uint32_t off, size, ts, tag; } FilmChunk;

typedef struct {
    FILE      *f;
    long       start;           /* the 'FILM' long, as a track offset        */
    uint32_t   header;          /* the payload begins start + header         */
    int        width, height;
    char       codec[5];
    uint32_t   rate;            /* CTAB's tick rate: 600 on 34 of the 36     */
    uint32_t   ticks;           /* the last chunk's timestamp                */
    uint32_t   bytes;           /* the payload, past the header              */
    int        nchunks;
    FilmChunk *chunk;

    /* The chunk film_chunk last read, and the sample table it opens with. */
    int        loaded;
    uint8_t   *buf;
    size_t     cap;
    uint32_t   table;           /* sync pad plus STAB, in front of the data  */
    int        nsamples;
    FilmSample sample[FILM_SAMPLES];
} Film;

/* The whole track, film by film, as CD block numbers - the same numbers the
 * `cinepak` commands carry.  Returns how many it found, at most max. */
int  film_scan(const char *track, uint32_t *blocks, int max);

/* Seeks to a block, finds the sync, and reads the header and chunk table. */
int  film_open(Film *fm, const char *track, uint32_t block);
void film_close(Film *fm);

/* Reads chunk n and parses its sample table.  0 if it does not hold up. */
int  film_chunk(Film *fm, int n);

/* Bytes of the loaded chunk that no sample accounts for.  Zero everywhere on
 * this disc, which is what --check-film asserts. */
uint32_t film_unaccounted(const Film *fm);

/* Audio, or a video frame.  The disc marks audio by an all-ones timestamp:
 * the fourth field is a duration, not a type, and it only looks like one
 * because at a rate of 600 every frame lasts 50 ticks. */
static inline int film_audio(const FilmSample *s)
{
    return s->ts == FILM_AUDIO_TS;
}

/* Bit 31 of a video timestamp is clear on exactly the frames a player may
 * start on - a whole picture, or the first frame after an audio block.  That
 * holds for all 13,922 frames on the disc; docs/09-text-and-fmv.md 9.3. */
static inline int film_resume(const FilmSample *s)
{
    return !(s->ts >> 31);
}

static inline uint32_t film_ticks(const FilmSample *s)
{
    return s->ts & 0x7FFFFFFFu;
}

/* Where a sample's bytes are, in the loaded chunk. */
static inline const uint8_t *film_data(const Film *fm, const FilmSample *s)
{
    uint32_t o = film_audio(s) ? s->off - s->size : s->off;
    return fm->buf + fm->table + o;
}

/* Why the last call that returned 0 did. */
const char *film_error(void);

#endif
