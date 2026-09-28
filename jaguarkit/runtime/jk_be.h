/* jk_be.h - big-endian reads.  Everything on a Jaguar is big-endian, and
 * everything jaguarkit extracts is left that way. */
#ifndef JK_BE_H
#define JK_BE_H

#include <stdint.h>

static inline uint16_t jk_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t jk_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

#endif
