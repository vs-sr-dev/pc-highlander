/* scene - one of the 672 pre-rendered backdrops, with its Z-buffer and camera.
 *
 * Format and provenance: docs/07-scene-format.md.  A slot on the PICT track is
 * 258,720 bytes; the first 256,000 are 64,000 RGB16 pixels then 64,000 depth
 * values, XORed with an 8,192-byte key that lives in the resident binary at
 * $30610, and the 48 bytes after that are the camera.
 */
#ifndef HL_SCENE_H
#define HL_SCENE_H

#include <stdint.h>

#define SCENE_SLOT     258720
#define SCENE_PAYLOAD  256000
#define SCENE_HALF     128000
#define SCENE_W        320
#define SCENE_H        200
#define SCENE_PIXELS   (SCENE_W * SCENE_H)

typedef struct {
    uint16_t id;                /* group * 64 + camera, unique per scene   */
    int16_t  m[9];              /* world -> view rotation, row major, s1.14 */
    int32_t  pos[3];            /* the camera's world position             */
    int32_t  set_block;         /* CD block of the set this view belongs to */
    int16_t  spare;             /* varies per scene; purpose unknown       */
} SceneCam;

/* The long at +32 is the set track block offset of the set the view belongs
 * to, so dividing by the 56-block slot names the set outright.  Checked on
 * every scene: for all 672, the set it names is one whose scene table lists
 * that scene's id, and all 48 sets are named by at least one view.  It settles
 * the two views the group vote of docs/10-set-track.md 10.6 could not.  */
#define SCENE_SET_SLOT 56
static inline int scene_set(const SceneCam *c)
{
    return c->set_block % SCENE_SET_SLOT ? -1
                                        : (int)(c->set_block / SCENE_SET_SLOT);
}

typedef struct {
    uint16_t colour[SCENE_PIXELS];  /* R5 B5 G6, straight off the disc      */
    uint16_t depth[SCENE_PIXELS];   /* the Z half, as stored: 65536 - |z|   */
    SceneCam cam;
} Scene;

/* The 8,192-byte XOR key, read out of the resident binary in the boot track.
 * Returns 0 if the track does not look like the boot track. */
int scene_key(const uint8_t *boot, size_t boot_size, uint8_t key[8192]);

/* Reads and decodes scene n from the PICT track.  Returns 0 on failure. */
int scene_load(Scene *s, const char *pict_path, int n, const uint8_t key[8192]);

/* R5 B5 G6 -> 0x00RRGGBB, the one pixel conversion the whole engine needs. */
static inline uint32_t scene_rgb(uint16_t px)
{
    uint32_t r = ((px >> 11) & 0x1F) * 255 / 31;
    uint32_t b = ((px >>  6) & 0x1F) * 255 / 31;
    uint32_t g = ( px        & 0x3F) * 255 / 63;
    return (r << 16) | (g << 8) | b;
}

/* The stored depth is the view-space z as a negative 16-bit value, so the
 * distance in front of the camera is 65536 - depth.  See docs/13-viewer.md. */
static inline int32_t scene_z(uint16_t depth)
{
    return 65536 - (int32_t)depth;
}

#endif
