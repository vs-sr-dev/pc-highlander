/* sheet - the world state and the character sheets, read out of the resident
 * binary on track 2.
 *
 * These two tables are not on a data track: `$5310` and `$537A` copy them into
 * RAM at startup and relocate the pointers between them, which is how they
 * were found at all (docs/12-world-and-sheets.md 12.1).  So the addresses in
 * `wstSheet` and `cshNext` are RAM addresses, and an index into `sheet[]` is
 * what the rest of the engine should use instead.
 *
 * What a sheet is for here: it says what a character *is* - which bundle he
 * wears, how many animations that bundle has, and what he does when nobody is
 * driving him.  The last of those is `cshBehaviour`, which is one of
 * AICTRL.GAS's fourteen AI commands (ai.h).
 */
#ifndef HL_SHEET_H
#define HL_SHEET_H

#include <stdint.h>

#include "../util/io.h"

#define WS_COUNT   256
#define WS_REC     32
#define CSH_MAX    64

/* One world-state record - LOGICS.INC's `wstRecord`, 32 bytes.  Ypos is zero
 * in all 197 of them: a character stands at the height of the collision
 * triangle he is on, not at a height the table gives. */
typedef struct {
    uint16_t group;             /* wstSet >> 6: the scene group he lives in */
    uint16_t radius;            /* wstRadius: the collision circle          */
    uint32_t sheet;             /* wstSheet, as an address; 0 = none        */
    uint32_t parent;            /* wstParent: who owns him, or 0            */
    int32_t  x, y, z;
    uint8_t  face[3];
    uint8_t  sanity, person, strength, life, flags;
    int      used;
} WorldRec;

/* And the byte offsets inside one of those records, which is how the engine
 * and the scripts both address it - LOGICS.INC's `wstRecord`. */
#define WS_SET      0
#define WS_RADIUS   2
#define WS_SHEET    4
#define WS_PARENT   8
#define WS_XPOS     12
#define WS_YPOS     16
#define WS_ZPOS     20
#define WS_XFACE    24
#define WS_YFACE    25
#define WS_ZFACE    26
#define WS_SANITY   27
#define WS_PERSON   28
#define WS_STR      29
#define WS_LIFE     30
#define WS_FLAGS    31

/* `wstParent` and `wstSheet` are addresses, and the resident code relocates
 * both at startup - the world table from $15458 to $32660 and the sheets from
 * $17458 to $34760.  What we read off the disc is the pre-relocation form, so
 * a parent is an address in the $15458 table and these two turn it into an
 * index and back.  Zero stays what it is in the original: **no owner**, which
 * is a distinction an index alone cannot make, since the player is record 0. */
#define WS_ADDR 0x15458u

static inline int ws_owner_of(uint32_t addr)
{
    if (addr < WS_ADDR)
        return -1;
    uint32_t d = addr - WS_ADDR;
    return d % WS_REC == 0 && d / WS_REC < WS_COUNT ? (int)(d / WS_REC) : -1;
}

static inline uint32_t ws_addr_of(int rec)
{
    return rec < 0 ? 0u : WS_ADDR + (uint32_t)rec * WS_REC;
}

#define WST_INANIMATE   (1u << 1)
#define WST_AMMO        (1u << 3)
#define WST_WEAPON      (1u << 4)
#define WST_COLLECTABLE (1u << 5)
#define WST_DEACTIVATED (1u << 6)

/* One file record: the run at cshFileOff, which says what to load off the CD
 * and where in the sheet's long array to put it.  `entry` counts longs from
 * the start of the model run, so entry 0 is models[0] - the character's own
 * bundle - and anything from `models` up is an animation bank. */
typedef struct {
    uint16_t entry;
    uint16_t type;              /* the data type: 3 for every one of them   */
    uint32_t block;             /* the CD block, always a multiple of 56    */
} SheetFile;

#define SHEET_FILES 4

/* LOGICS.INC's `cshRecord`.  The four *Off fields index the long array from
 * the start of the record, so the first is always 4.
 *
 * cshBehaviour is declared `ds.w 1` and the retail build fills it as two
 * bytes: the AI command in the high byte, and a second byte that is zero for
 * every character and 10, 20, 30, 40 or 250 on the item and weapon sheets.
 * What that second byte is for is not known; it is carried through as
 * `property` rather than folded into the command. */
typedef struct {
    uint32_t  addr, next;
    uint16_t  flags;
    uint8_t   model_off, models, anim_off, anims;
    uint8_t   misc_off,  miscs,  file_off, files;
    uint8_t   behaviour;        /* the AI command: cshBehaviour's high byte */
    uint8_t   property;         /* its low byte, on the items only          */
    SheetFile file[SHEET_FILES];
    uint32_t  model0;           /* models[0] as it stands in the binary:
                                   filled in for the items, 0 for anyone who
                                   loads his own bundle off the CD           */
    int32_t   block;            /* the bundle's CD block on track 5, from the
                                   file record for entry 0, or -1            */
    int       bundle;           /* which cast bundle that is - filled in by
                                   sheets_bundles, -1 until then             */
} SheetRec;

typedef struct {
    Blob     file;
    WorldRec world[WS_COUNT];
    int      nworld;            /* records in use: the table is a prefix     */
    SheetRec sheet[CSH_MAX];
    int      nsheets;
} Sheets;

int  sheets_load(Sheets *s, const char *track2);
void sheets_free(Sheets *s);

/* The index of the sheet at a given address, or -1.  wstSheet is an address,
 * so this is how a world record names its sheet. */
int  sheets_of_addr(const Sheets *s, uint32_t addr);

/* Fills in every sheet's `bundle` by matching its block against the offsets
 * the model bundles were found at: a bundle for block b begins at
 * b * 56 * 2352 + 4, the four bytes being the slot's own length prefix.
 * `offset[i]` is bundle i's first piece's offset in the track. */
void sheets_bundles(Sheets *s, const long *offset, int count);

/* The first sheet whose behaviour is `command` and which loads a bundle, or
 * -1.  The chain is in the same order as the 1995 SHEET.S, so the first
 * aiFollowPlayer sheet is Ramirez. */
int  sheets_by_behaviour(const Sheets *s, int command);

/* Where a weapon sheet's animations begin inside the player's own bundle.
 *
 * `AICTRL.GAS`'s `.weapon_action` looks the animation number up in the sheet
 * of whatever the player is holding before falling back to his own, and
 * `COLLECT.GAS`'s `wpn` is what puts a sheet there - `animsheet`.  The player
 * carries all of it in one bundle: his thirty, then one bank of 28 per weapon
 * sheet in sheet order.  So the offset is the player's own count plus the
 * counts of every weapon sheet before this one, and the sheet index is all it
 * takes to find it.  Returns 0 for a sheet that is not one of them. */
int  sheets_anim_bank(const Sheets *s, int sheet);

#endif
