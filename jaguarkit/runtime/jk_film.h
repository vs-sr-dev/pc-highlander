/* jk_film - the Cinepak film container of Jaguar CD games, for an engine.
 *
 * The same container as jaguarkit/film.py, which documents it: a header
 *
 *   'FILM' size 0 0
 *   'FDSC' 20 codec height width
 *   'ADSC' 20 ...                         in some films; kept, not read
 *   'CTAB' size rate count                16 bytes a chunk: off size ts tag
 *
 * and chunks, each a 64-byte sync pad (its tag sixteen times) and then
 *
 *   'STAB' size rate count                16 bytes a sample: off size ts dur
 *
 * interleaving video frames with audio.  An all-ones timestamp marks audio.
 * An audio sample's offset is its end in some films and its start in others;
 * the samples tile the chunk in table order, and jk_film_chunk resolves each
 * one to its start (`at`) by that.
 *
 * A track file is expected to start at the track's block 0 - its lead-in -
 * which is how `python -m jaguarkit.disc --extract` writes it, so a block
 * number a game uses is a block number here.  Films are found by structure,
 * so a file cut elsewhere still yields its films, but block numbers then
 * count from wherever it was cut.
 *
 * Checked on Highlander (36 films, 13,922 frames), Battle Morph, Baldies and
 * Myst: see jkfilm.c and the kit's README.
 */
#ifndef JK_FILM_H
#define JK_FILM_H

#include <stdint.h>
#include <stdio.h>

#define JK_FILM_BLOCK    2352
#define JK_FILM_SYNC     64
#define JK_FILM_SAMPLES  256
#define JK_FILM_AUDIO_TS 0xFFFFFFFFu

typedef struct {
    uint32_t off;               /* as stored, from the end of the sample
                                   table; for audio the end or the start    */
    uint32_t at;                /* where it starts, resolved                */
    uint32_t size;
    uint32_t ts;                /* film ticks; all ones marks audio.  Bit 31
                                   clear marks a frame a player may start on */
    uint32_t dur;               /* video: how long the frame is held        */
} JkFilmSample;

typedef struct { uint32_t off, size, ts, tag; } JkFilmChunk;

typedef struct {
    FILE        *f;
    long         start;         /* the 'FILM' long, as a file offset        */
    uint32_t     header;        /* the payload begins at start + header     */
    int          width, height; /* as declared; frames are coded rounded up
                                   to multiples of four                     */
    char         codec[5];
    int          has_adsc;
    uint8_t      adsc[12];
    uint32_t     rate;          /* CTAB's ticks per second                  */
    uint32_t     ticks;         /* the last chunk's timestamp               */
    uint32_t     bytes;         /* the payload, past the header             */
    int          nchunks;
    JkFilmChunk *chunk;

    /* The chunk jk_film_chunk last read, and its sample table. */
    int          loaded;
    uint8_t     *buf;
    size_t       cap;
    uint32_t     table;         /* sync pad plus STAB, in front of the data */
    int          nsamples;
    JkFilmSample sample[JK_FILM_SAMPLES];
} JkFilm;

/* Every film in a track file, as the block numbers a game names them by -
 * the block its sync pad starts in.  Returns how many, <= max. */
int  jk_film_scan(const char *track, uint32_t *blocks, int max);

/* Seeks to a block, finds the film header within three blocks of it, and
 * reads the header and the chunk table.  1 on success. */
int  jk_film_open(JkFilm *fm, const char *track, uint32_t block);
void jk_film_close(JkFilm *fm);

/* Reads chunk n and parses its sample table.  1 on success. */
int  jk_film_chunk(JkFilm *fm, int n);

/* Bytes of the loaded chunk that no sample accounts for. */
uint32_t jk_film_unaccounted(const JkFilm *fm);

static inline int jk_film_audio(const JkFilmSample *s)
{
    return s->ts == JK_FILM_AUDIO_TS;
}

static inline int jk_film_resume(const JkFilmSample *s)
{
    return !(s->ts >> 31);
}

static inline uint32_t jk_film_ticks(const JkFilmSample *s)
{
    return s->ts & 0x7FFFFFFFu;
}

static inline const uint8_t *jk_film_data(const JkFilm *fm, const JkFilmSample *s)
{
    return fm->buf + fm->table + s->at;
}

/* Why the last call that failed did. */
const char *jk_film_error(void);

#endif
