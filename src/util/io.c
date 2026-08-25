#include "io.h"

#include <stdio.h>
#include <stdlib.h>

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

int io_read_at(const char *path, long off, void *buf, size_t count)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    int ok = fseek(f, off, SEEK_SET) == 0 && fread(buf, 1, count, f) == count;
    fclose(f);
    return ok;
}
