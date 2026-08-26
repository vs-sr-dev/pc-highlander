#include "sheet.h"

#include <stdlib.h>
#include <string.h>

/* Where the resident binary sits.  The track is the image from $4000 on and
 * the game's own code starts at $5000, $12600 bytes in - the two numbers
 * tools/world/worldx.py works from, and the ones the startup copies at $5310
 * and $537A are written in terms of. */
#define BASE    0x5000
#define OFF     0x12600
#define WS_SRC  0x15458
#define CS_SRC  0x17458

#define CD_BLOCK  2352L

static long fo(uint32_t mem)
{
    return (long)mem - BASE + OFF;
}

static int inside(const Sheets *s, long off, long len)
{
    return off >= 0 && len >= 0 && (size_t)(off + len) <= s->file.size;
}

int sheets_load(Sheets *s, const char *track2)
{
    memset(s, 0, sizeof *s);
    s->file = io_read(track2);
    if (!s->file.data)
        return 0;

    /* The world table: 256 records of 32 bytes. */
    if (!inside(s, fo(WS_SRC), WS_COUNT * WS_REC)) {
        io_free(&s->file);
        return 0;
    }
    int last = -1;
    for (int i = 0; i < WS_COUNT; i++) {
        const uint8_t *p = s->file.data + fo(WS_SRC) + (long)i * WS_REC;
        WorldRec *w = &s->world[i];
        w->group    = (uint16_t)(be16(p) >> 6);
        w->radius   = be16(p + 2);
        w->sheet    = be32(p + 4);
        w->parent   = be32(p + 8);
        w->x        = be32s(p + 12);
        w->y        = be32s(p + 16);
        w->z        = be32s(p + 20);
        for (int k = 0; k < 3; k++)
            w->face[k] = p[24 + k];
        w->sanity   = p[27];
        w->person   = p[28];
        w->strength = p[29];
        w->life     = p[30];
        w->flags    = p[31];
        if (be16(p) || w->sheet || w->x || w->z || w->sanity || w->person ||
            w->strength || w->life || w->flags)
            last = i;
    }
    /* The table is a prefix, so everything up to the last live record counts -
     * including record 1, Ramirez, whose only non-zero fields are his stats
     * and the WSTDeactivated bit. */
    s->nworld = last + 1;
    for (int i = 0; i < s->nworld; i++)
        s->world[i].used = 1;

    /* The sheets: a chain from $17458 through cshNext. */
    uint32_t p = CS_SRC;
    while (p && s->nsheets < CSH_MAX) {
        long o = fo(p);
        if (!inside(s, o, 16))
            break;
        int dup = 0;
        for (int i = 0; i < s->nsheets; i++)
            if (s->sheet[i].addr == p)
                dup = 1;
        if (dup)
            break;                              /* a loop: stop rather than
                                                   run round it for ever    */
        const uint8_t *d = s->file.data + o;
        SheetRec *sh = &s->sheet[s->nsheets++];
        memset(sh, 0, sizeof *sh);
        sh->addr      = p;
        sh->next      = be32(d);
        sh->flags     = be16(d + 4);
        sh->model_off = d[6];  sh->models = d[7];
        sh->anim_off  = d[8];  sh->anims  = d[9];
        sh->misc_off  = d[10]; sh->miscs  = d[11];
        sh->file_off  = d[12]; sh->files  = d[13];
        sh->behaviour = d[14];
        sh->property  = d[15];
        sh->block     = -1;
        sh->bundle    = -1;
        if (sh->models && inside(s, o + sh->model_off * 4, 4))
            sh->model0 = be32(d + sh->model_off * 4);
        int n = sh->files < SHEET_FILES ? sh->files : SHEET_FILES;
        for (int i = 0; i < n; i++) {
            long fp = o + (long)sh->file_off * 4 + (long)i * 8;
            if (!inside(s, fp, 8))
                break;
            const uint8_t *f = s->file.data + fp;
            sh->file[i].entry = be16(f);
            sh->file[i].type  = be16(f + 2);
            sh->file[i].block = be32(f + 4);
            /* Entry 0 is models[0]: the character's own bundle.  A weapon
             * sheet has its model in the binary already and its first record
             * lands on anims[0] instead, so it names no bundle here. */
            if (sh->file[i].entry == 0 && sh->models)
                sh->block = (int32_t)sh->file[i].block;
        }
        p = sh->next;
    }
    return s->nsheets > 0;
}

void sheets_free(Sheets *s)
{
    io_free(&s->file);
    s->nsheets = s->nworld = 0;
}

int sheets_of_addr(const Sheets *s, uint32_t addr)
{
    if (!addr)
        return -1;
    for (int i = 0; i < s->nsheets; i++)
        if (s->sheet[i].addr == addr)
            return i;
    return -1;
}

void sheets_bundles(Sheets *s, const long *offset, int count)
{
    for (int i = 0; i < s->nsheets; i++) {
        SheetRec *sh = &s->sheet[i];
        sh->bundle = -1;
        if (sh->block < 0)
            continue;
        /* The slot begins with a four-byte length prefix, so the bundle
         * itself starts four bytes in - which is where model_scan finds
         * it and what the cast reports as its offset. */
        long want = (long)sh->block * CD_BLOCK + 4;
        for (int b = 0; b < count; b++)
            if (offset[b] == want) {
                sh->bundle = b;
                break;
            }
    }
}

int sheets_by_behaviour(const Sheets *s, int command)
{
    for (int i = 0; i < s->nsheets; i++)
        if (s->sheet[i].behaviour == command && s->sheet[i].block >= 0)
            return i;
    return -1;
}
