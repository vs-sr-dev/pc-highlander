/* jkfilm - the runtime's check, and a small tool.
 *
 *   jkfilm TRACK                     list the films, and decode every frame
 *   jkfilm TRACK FILM FRAME OUT.ppm  one frame, as the declared picture
 *
 * TRACK is a track file from block 0, as `python -m jaguarkit.disc --extract`
 * writes it.  The counts it prints are the ones `python -m jaguarkit.film
 * --check` prints; a frame it writes is byte for byte the Python decoder's.
 *
 *   cc -O2 -std=c99 -o jkfilm jkfilm.c jk_film.c jk_cvid.c
 */
#include "jk_cvid.h"
#include "jk_film.h"

#include <stdio.h>
#include <stdlib.h>

static int run(const char *track, uint32_t block, int want, const char *ppm,
               long *frames, long *whole, long *errors, long *loose)
{
    JkFilm fm;
    JkCvid cv;

    if (!jk_film_open(&fm, track, block)) {
        printf("block %u: %s\n", block, jk_film_error());
        (*errors)++;
        return 0;
    }
    if (!jk_cvid_open(&cv, fm.width, fm.height)) {
        jk_film_close(&fm);
        (*errors)++;
        return 0;
    }
    long n = 0;
    int done = 0;
    for (int k = 0; k < fm.nchunks && !done; k++) {
        if (!jk_film_chunk(&fm, k)) {
            printf("block %u: %s\n", block, jk_film_error());
            (*errors)++;
            break;
        }
        if (jk_film_unaccounted(&fm))
            (*loose)++;
        for (int i = 0; i < fm.nsamples; i++) {
            const JkFilmSample *s = &fm.sample[i];
            if (jk_film_audio(s))
                continue;
            int e = jk_cvid_frame(&cv, jk_film_data(&fm, s), s->size);
            if (e != JK_CVID_OK) {
                printf("block %u frame %ld: %s\n", block, n, jk_cvid_why(e));
                (*errors)++;
            }
            if (want >= 0 && n == want) {
                uint8_t *rgb = malloc((size_t)cv.show_w * cv.show_h * 3);
                FILE *f = fopen(ppm, "wb");
                if (rgb && f) {
                    jk_cvid_rgb24(&cv, rgb);
                    fprintf(f, "P6\n%d %d\n255\n", cv.show_w, cv.show_h);
                    fwrite(rgb, 1, (size_t)cv.show_w * cv.show_h * 3, f);
                }
                if (f)
                    fclose(f);
                free(rgb);
                done = 1;
                break;
            }
            n++;
        }
    }
    *frames += cv.frames;
    *whole += cv.keyframes;
    printf("block %6u  %-4s %3dx%-3d  %5ld frames  %4ld whole\n", block,
           fm.codec, fm.width, fm.height, cv.frames, cv.keyframes);
    jk_cvid_close(&cv);
    jk_film_close(&fm);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 5) {
        fprintf(stderr, "usage: jkfilm TRACK [FILM FRAME OUT.ppm]\n");
        return 2;
    }
    static uint32_t blocks[4096];
    int n = jk_film_scan(argv[1], blocks, 4096);
    long frames = 0, whole = 0, errors = 0, loose = 0;

    if (argc == 5) {
        int film = atoi(argv[2]);
        if (film < 0 || film >= n) {
            fprintf(stderr, "no film %d of %d\n", film, n);
            return 1;
        }
        run(argv[1], blocks[film], atoi(argv[3]), argv[4],
            &frames, &whole, &errors, &loose);
        return errors ? 1 : 0;
    }
    for (int i = 0; i < n; i++)
        run(argv[1], blocks[i], -1, NULL, &frames, &whole, &errors, &loose);
    printf("films %d frames %ld whole %ld errors %ld, %ld chunks with bytes "
           "no sample accounts for\n", n, frames, whole, errors, loose);
    return errors ? 1 : 0;
}
