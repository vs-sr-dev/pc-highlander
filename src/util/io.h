/* io - whole-file reads and big-endian accessors.
 *
 * Everything on the disc is big-endian: the tracks come off a 68000/GPU
 * machine and jcdinfo has already undone the 32-bit word swap the .jcd
 * container applies, so what is left is plain Motorola order.
 */
#ifndef HL_IO_H
#define HL_IO_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *data;
    size_t   size;
} Blob;

/* Reads the whole file.  Returns a blob with data == NULL on failure. */
Blob  io_read(const char *path);
void  io_free(Blob *b);

/* Reads count bytes at off from an already-open file into buf. */
int   io_read_at(const char *path, long off, void *buf, size_t count);

static inline uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline int16_t be16s(const uint8_t *p)
{
    return (int16_t)be16(p);
}

static inline uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static inline int32_t be32s(const uint8_t *p)
{
    return (int32_t)be32(p);
}

#endif
