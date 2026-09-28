/* jk_cvid - a Cinepak ('cvid') decoder, for an engine.
 *
 * The C twin of jaguarkit/cvid.py, which documents the format.  Written from
 * the published codec, not from any game's hand-written GPU decoder.
 *
 * A film declares the picture it shows (jk_film's width and height), and that
 * need not be a multiple of four - Myst's are 92x134 and 129x89 - while the
 * frames are coded in whole 4x4 blocks.  So the decoder works at the size
 * rounded up (w, h) and hands back the declared one (show_w, show_h).
 */
#ifndef JK_CVID_H
#define JK_CVID_H

#include <stddef.h>
#include <stdint.h>

#define JK_CVID_STRIPS 32

enum {
    JK_CVID_OK = 0,
    JK_CVID_SHORT,              /* the data ends inside a header            */
    JK_CVID_LENGTH,             /* the frame's own length is not its size   */
    JK_CVID_SIZE,               /* not the picture the film declared        */
    JK_CVID_STRIPCOUNT,
    JK_CVID_STRIP,              /* a strip overruns the frame               */
    JK_CVID_CHUNK,              /* a chunk overruns its strip               */
    JK_CVID_VECTORS             /* the vectors end mid-picture              */
};

typedef struct {
    int      show_w, show_h;    /* declared                                 */
    int      w, h;              /* coded: rounded up to multiples of four   */
    uint8_t *rgb;               /* w*h*3, and kept: inter frames are
                                   differences against what is in it        */
    long     frames, keyframes;
    int      keyframe;          /* was the frame just decoded a whole one?  */
    uint8_t  v1[JK_CVID_STRIPS][256 * 12];
    uint8_t  v4[JK_CVID_STRIPS][256 * 12];
} JkCvid;

int  jk_cvid_open(JkCvid *c, int show_w, int show_h);
void jk_cvid_close(JkCvid *c);

/* Decodes one frame into c->rgb.  JK_CVID_OK, or one of the codes above. */
int  jk_cvid_frame(JkCvid *c, const uint8_t *data, size_t size);

const char *jk_cvid_why(int err);

/* The declared picture as RGB24, show_w * show_h * 3 bytes. */
void jk_cvid_rgb24(const JkCvid *c, uint8_t *out);

/* The declared picture in the Jaguar's RGB16 - R5 B5 G6, rounded, which is
 * what the hardware's own decoder produces (Highlander 9.5). */
void jk_cvid_rgb16(const JkCvid *c, uint16_t *out);

#endif
