/* anim - the animation records, read off track 5 and track 8.
 *
 * Format: docs/03-data-formats.md 3.4, and the reader that found them,
 * tools/anim/animx.py.  A record is a 14-byte header and then NUMFRAMES frames
 * of FRAMESIZE bytes; a frame is 20 bytes of root motion and combat data and
 * then three angle bytes for each animated piece.
 *
 * The three angles are what ANIM.GAS writes straight into a piece's instance
 * data, in the order FORMMAT.GAS reads them back: elevation about x, twist
 * about z, azimuth about y - and the character's facing is added to the third.
 * They are absolute, not relative to the parent: the skeleton chains positions,
 * never orientations.  docs/14-characters.md 14.2.
 */
#ifndef HL_ANIM_H
#define HL_ANIM_H

#include <stdint.h>
#include <stddef.h>

#define ANIM_HEADER 14

typedef struct {
    int16_t move[3];            /* root motion, x y z                   */
    int8_t  turn;               /* change of facing this frame          */
    uint8_t flags;              /* bit 1 = play the sheet's footstep    */
    int8_t  spin;
    int8_t  hit;                /* positive attack, negative defence    */
    int16_t range;
    int8_t  dir[2], spread[2];
    int16_t high, low;          /* the pose's extent above the floor    */
    const int8_t *angle;        /* 3 per piece                          */
} AnimFrame;

typedef struct {
    long           offset;      /* the length prefix, in the file       */
    int            size;
    int            framesize;
    int            pieces;      /* ANIMMODELS: 15 for every character   */
    int            frames;
    int            fps;         /* 20 throughout                        */
    int            sound_sheet, sound_entry;
    int            height_start;/* the root's height above the floor    */
    const uint8_t *data;        /* the frames, inside the caller's blob */
} Anim;

/* Parses the record whose length prefix is at off.  Returns its total size
 * including that prefix, or 0 if there is not one there. */
int  anim_parse(Anim *a, const uint8_t *d, size_t size, long off);

/* Walks the whole file collecting every record, the way animx does.  The
 * caller frees the array; the Anims point into d, which must outlive them. */
int  anim_scan(const uint8_t *d, size_t size, Anim **out);

void anim_frame(const Anim *a, int frame, AnimFrame *f);

#endif
