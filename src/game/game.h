/* game - the loop, and everything one game frame touches.
 *
 * Up to now `--drive` was a branch inside the viewer: a character, a floor, and
 * the doorway code written out where it was needed.  This is that, owning its
 * own state, in the order the original runs it:
 *
 *   1  the script machine, one command per process per frame until it yields;
 *   2  the events the machine posted, which the host has to act on before the
 *      machine will go any further - a camera cut, a film, a sample;
 *   3  ControlCode and ActionCode over every active character;
 *   4  the set's own event lines, which cut the camera and open the doors.
 *
 * What is left outside is anything that needs a screen or a disc: loading a
 * backdrop, drawing, playing a film.  Those are requests the host reads off
 * the Game and clears, which is exactly the handshake the script machine
 * already uses for the same three things (docs/11-script-vm.md 11.9).
 */
#ifndef HL_GAME_H
#define HL_GAME_H

#include <stdint.h>

#include "act.h"
#include "set.h"
#include "sheet.h"
#include "../script/vm.h"

/* Quentin and Ramirez, as the world table numbers them: `WORLD.INC` says
 * `WORLD_QUENTIN equ 0` and `WORLD_RAMIREZ equ 1`, and retail's records 0 and 1
 * are July's two, byte for byte. */
#define WORLD_PLAYER    0
#define WORLD_COMPANION 1

typedef struct {
    Sheets    sheets;
    Set       set;
    int       have_set;
    ActTable  act;
    Vm        vm;
    Blob      boot;             /* the resident binary: MAINSCRIPT lives in it */
    char      track3[512];

    uint16_t  scene;            /* the view on screen: CUS                   */

    /* What the host has to do before the next frame, and clear. */
    int       want_scene;       /* a view to cut to, or -1                   */
    uint32_t  want_film;        /* a cinepak block, or 0                     */
    uint32_t  want_redbook;
    int       want_sample;      /* a sound entry, or -1                      */
    int       gameover;

    /* For the log line, so a run can be read. */
    int       last_cut_from;
    int       entered_set;      /* set on the frame a doorway was crossed    */
    long      frame;
} Game;

/* Opens the machine and the world table.  It does not place anybody: add the
 * characters with act_add and then call game_enter. */
int  game_open(Game *g, const char *track3, const char *track2);
void game_close(Game *g);

/* Arrive at a view.  If it belongs to another set, that set is loaded and
 * everybody is put down from its init table, keyed on the view being left -
 * the player from the entry with bit 0 of the flags clear and the companion
 * from the one with it set.  `set_hint` is the set the scene footer names, or
 * -1 to look it up.  Returns 0 if the view could not be resolved to a set. */
int  game_enter(Game *g, uint16_t scene_id, int set_hint);

/* One game frame.  `rawpad` is the hardware pad in control.h's numbering. */
void game_frame(Game *g, uint32_t rawpad);

#endif
