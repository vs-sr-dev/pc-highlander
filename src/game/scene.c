#include "scene.h"
#include "../util/io.h"

#include <stdio.h>
#include <string.h>

#define KEY_ADDR   0x30610
#define CODE_BASE  0x5000

int scene_key(const uint8_t *boot, size_t boot_size, uint8_t key[8192])
{
    /* The boot track opens with its content tag, "CODE" sixteen times; the
     * byte after it is address $5000, which anchors every other address. */
    static const char tag[] = "CODECODECODECODECODECODECODECODE"
                              "CODECODECODECODECODECODECODECODE";
    const size_t taglen = sizeof tag - 1;
    for (size_t i = 0; i + taglen <= boot_size; i++) {
        if (memcmp(boot + i, tag, taglen) != 0)
            continue;
        size_t off = i + taglen + (KEY_ADDR - CODE_BASE);
        if (off + 8192 > boot_size)
            return 0;
        memcpy(key, boot + off, 8192);
        return 1;
    }
    return 0;
}

int scene_load(Scene *s, const char *pict_path, int n, const uint8_t key[8192])
{
    static uint8_t slot[SCENE_SLOT];

    if (n < 0 || !io_read_at(pict_path, (long)n * SCENE_SLOT, slot, SCENE_SLOT))
        return 0;

    /* The key is applied once per 128,000-byte half, restarting each time -
     * the scene module resets A2_PIXEL between the two blits. */
    for (int half = 0; half < 2; half++) {
        uint8_t *p = slot + half * SCENE_HALF;
        for (int i = 0; i < SCENE_HALF; i++)
            p[i] ^= key[i & 8191];
    }

    for (int i = 0; i < SCENE_PIXELS; i++) {
        s->colour[i] = be16(slot + i * 2);
        s->depth[i]  = be16(slot + SCENE_HALF + i * 2);
    }

    const uint8_t *f = slot + SCENE_PAYLOAD;
    s->cam.id = be16(f);
    for (int i = 0; i < 9; i++)
        s->cam.m[i] = be16s(f + 2 + i * 2);
    for (int i = 0; i < 3; i++)
        s->cam.pos[i] = be32s(f + 20 + i * 4);
    s->cam.tail  = be32s(f + 32);
    s->cam.spare = be16s(f + 44);
    return 1;
}
