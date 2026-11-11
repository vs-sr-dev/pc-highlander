#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Blob io_read(const char *path)
{
    Blob b = { NULL, 0 };
    FILE *f = fopen(path, "rb");
    if (!f)
        return b;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return b; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return b; }
    rewind(f);
    b.data = malloc((size_t)n + 1);
    if (!b.data) { fclose(f); return b; }
    b.size = fread(b.data, 1, (size_t)n, f);
    b.data[b.size] = 0;
    fclose(f);
    return b;
}

void io_free(Blob *b)
{
    free(b->data);
    b->data = NULL;
    b->size = 0;
}

Blob io_read_track(const char *path)
{
    Blob b = io_read(path);
    if (b.data && b.size <= TRACK_ORIGIN) {
        io_free(&b);
        return b;
    }
    if (b.data) {
        b.size -= TRACK_ORIGIN;
        memmove(b.data, b.data + TRACK_ORIGIN, b.size + 1);
    }
    return b;
}

int io_read_track_at(const char *path, long off, void *buf, size_t count)
{
    return io_read_at(path, off + TRACK_ORIGIN, buf, count);
}

int io_read_at(const char *path, long off, void *buf, size_t count)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    int ok = fseek(f, off, SEEK_SET) == 0 && fread(buf, 1, count, f) == count;
    fclose(f);
    return ok;
}
