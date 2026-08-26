/* cinepak - the `cvid` decoder the films are in.
 *
 * This is not the disc's own decoder.  `CINEPAK.S` is a hand-written Jaguar
 * one, all blitter and GPU, and docs/05-roadmap.md 5.2 says to read that sort
 * of thing rather than transcribe it: the container is the disc's own and had
 * to be worked out, but the codec inside it is standard, published Cinepak.
 * So this is written from the format, and checked against the disc - every
 * frame of all 36 films, and one frame against a second decoder written in
 * Python (tools/cinepak/filmdec.py).
 *
 * A frame is ten bytes and then strips, each strip a horizontal band with its
 * own pair of codebooks and the vectors that index them:
 *
 *   flags(1) length(3) width(2) height(2) strips(2)
 *   strip:  id(2) size(2) y0(2) x0(2) height(2) width(2)
 *   $20/$21  V4 codebook, whole / updated under a flag word
 *   $22/$23  V1 codebook, likewise
 *   $30      vectors: one flag bit per block, V1 or V4
 *   $31      vectors: a bit for "coded at all", then the V1/V4 bit
 *   $32      vectors: V1 throughout
 *
 * A codebook entry is four Y and a signed U and V, and it paints a 2x2 block.
 * V1 doubles one entry over a whole 4x4; V4 puts four entries in its four
 * quadrants.  The colour is Cinepak's own,
 *
 *   R = Y + 2V     G = Y - U/2 - V     B = Y + 2U
 *
 * An inter frame leaves the blocks it does not code alone, so the decoder
 * keeps the last picture in `rgb` and the caller gets it back frame by frame.
 */
#ifndef HL_CINEPAK_H
#define HL_CINEPAK_H

#include <stdint.h>
#include <stddef.h>

#define CVID_STRIPS 32          /* the format's own cap; this disc uses two */

enum {
    CVID_OK = 0,
    CVID_SHORT,                 /* the data ends inside a header            */
    CVID_LENGTH,                /* the frame's own length is not its size   */
    CVID_SIZE,                  /* not the picture the film declared        */
    CVID_STRIPCOUNT,
    CVID_STRIP,                 /* a strip overruns the frame               */
    CVID_CHUNK,                 /* a chunk overruns its strip               */
    CVID_VECTORS                /* the vectors end mid-picture              */
};

typedef struct {
    int      w, h;
    uint8_t *rgb;               /* w*h*3, and kept: inter frames are
                                   differences against what is in it        */
    long     frames, keyframes;
    int      keyframe;          /* was the frame just decoded a whole one?  */
    uint8_t  v1[CVID_STRIPS][256 * 12];
    uint8_t  v4[CVID_STRIPS][256 * 12];
} Cinepak;

int  cinepak_open(Cinepak *c, int w, int h);
void cinepak_close(Cinepak *c);

/* Decodes one frame into c->rgb.  CVID_OK, or one of the codes above. */
int  cinepak_frame(Cinepak *c, const uint8_t *data, size_t size);

const char *cinepak_why(int err);

/* The picture as the engine's framebuffer wants it: R5 B5 G6, the Jaguar's
 * order, which is what scene.h's scene_rgb unpacks. */
void cinepak_rgb16(const Cinepak *c, uint16_t *out);

#endif
