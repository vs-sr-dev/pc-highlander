/* vm - the script machine.
 *
 * The puzzles, the cutscene triggers, the menus and the cheat codes are not
 * 68000 code: they are bytecode for a small virtual machine that ran on the
 * GPU, several processes interleaved, one command per process per game frame
 * whenever a process yields.  Format, opcode numbering and what every script
 * on the disc turns out to do: docs/11-script-vm.md.  The disassembler that
 * read them is tools/script/scriptx.py, and this is the same machine with the
 * handlers written out rather than printed.
 *
 * Three things are worth knowing before reading the code.
 *
 * **A command is one big-endian long** - opcode, mode nibble, register nibble,
 * 16-bit operand - and thirteen of them read further longs through the program
 * counter.  The bottom of the opcode space is decoded by range rather than by
 * table (`cmpq #3`, `cmpq #9`, `cmpq #13` in SCRIPT.GAS), which is why
 * `add` writes to the register the *operand* names and `inc` to the one the
 * register nibble names.
 *
 * **`quit` is not a terminator.**  It yields for this game frame and the
 * process resumes at the next command.  Several handlers - `pause`, `camera`,
 * `waitevent`, `waitforit`, `slideto` - yield by stepping the program counter
 * *back* over their own command and quitting, so they are re-executed until
 * they are ready.  Only `rts`, `suicide` and an unconditional `bra` end a
 * trace.
 *
 * **Two scripts are live at once.**  `MAINSCRIPT` is resident and its
 * processes survive everything; the current set's script is loaded with the
 * set and its processes are dropped when you walk into another one.  The
 * original tells them apart by comparing the program counter with a fixed
 * address; here the program counter carries which of the two it is in its top
 * byte, which is the same test.
 */
#ifndef HL_VM_H
#define HL_VM_H

#include <stdint.h>

#include "../game/act.h"
#include "../game/set.h"
#include "../game/sheet.h"

#define VM_PROCS   16           /* statevectors: the original's is a compacted
                                   list too, and nothing on the disc gets
                                   near this many at once                   */
#define VM_REGS    15           /* scriptReg                                */
#define VM_STACK   11           /* scriptSpace - "who could possibly use 16" */
#define VM_LASTOP  83           /* the retail machine: July's 75 plus eight  */

/* The two script spaces.  Zero is not one of them, because a zero program
 * counter is what marks a process record free. */
#define VM_MAIN    1
#define VM_SET     2
#define VM_PC(space, off)  (((uint32_t)(space) << 24) | (uint32_t)(off))
#define VM_SPACE(pc)       ((pc) >> 24)
#define VM_OFF(pc)         ((pc) & 0xFFFFFF)

/* The GPU flag bits a compare leaves behind, which `condtable` then masks. */
#define VM_Z  1u
#define VM_C  2u
#define VM_N  4u

/* CDLINK.INC's event types.  The machine writes `type + 1` into scriptevent
 * and waits for the host to clear it, which is the whole handshake between
 * the script and everything that takes time. */
#define EV_SCENE        0
#define EV_SOUND        1
#define EV_CINEPAK      2
#define EV_CDAUDIO      4
#define EV_RESTOREITEM  6
#define EV_CHARCHANGE  14

typedef struct {
    uint32_t pc;                /* scriptPC: 0 means the slot is free       */
    uint32_t stack[VM_STACK];
    int      sp;
    uint32_t flags;             /* scriptFlags: the GPU flags, kept per
                                   process because processes interleave     */
    int32_t  pause;             /* scriptPause, in 256ths of a second       */
    int      action;            /* scriptAction                             */
    int      chr;               /* scriptChar, as an ACT slot; -1 for none  */
    uint32_t ident;             /* set number in the top word, offset in the
                                   bottom - what `spawn` dedupes on and what
                                   `kill` names                             */
    int32_t  r[VM_REGS];
} VmProc;

typedef struct {
    /* the code */
    const uint8_t *main;  int main_len;
    const uint8_t *code;  int code_len;   /* the current set's script       */
    int      script_set;        /* ScriptSet: which set the running set-based
                                   processes belong to                      */
    int      started;           /* whether MAINSCRIPT has been put up yet   */

    VmProc   proc[VM_PROCS];

    /* the machine's own memory */
    uint8_t  ws[WS_COUNT * WS_REC];   /* the live world state, as bytes: the
                                         scripts address it by field offset  */
    uint32_t gamestate[32];     /* 1024 game bits                           */
    int32_t  gvar[16];          /* the script globals                       */
    uint8_t  var[64];           /* $4E14, the byte/word block = gvar[8] on  */
    uint32_t seed;              /* `random`'s                               */
    uint32_t eventmask;         /* eventmode: which set events may fire     */
    uint16_t scriptscene;       /* the scene or entry an event names        */
    uint32_t scriptblock;       /* the CD block a film or a redbook names   */
    uint16_t scriptevent;       /* EVENT_TYPE + 1, or 0 when nothing waits  */
    int      used;              /* the "object being used" latch, -1 = none */
    uint32_t pad;               /* pad_now, in the hardware bit numbering   */
    int      gameover;          /* what `reset` sets                        */
    int      redbook_done;      /* redbookdetect, for waitredbook           */

    /* what it is running against */
    ActTable *act;
    Set      *set;
    uint16_t  scene;            /* CUS, the scene on screen                 */
    uint16_t  curset;           /* CurrSet                                  */
    int       frametime;        /* what `pause` counts down by each frame   */

    /* counters, so a run can be checked rather than watched */
    long     executed;
    long     unknown_op;        /* commands past the dispatch table         */
    long     unmapped;          /* act/cit fields this port has no home for */
    long     overrun;           /* processes that hit the command budget    */
    long     padding;           /* processes that ran into slot padding     */
    long     opcount[VM_LASTOP];
    int      trih_tri;          /* the last floor a script moved,  */
    int32_t  trih_height;       /* and what it moved it to         */
} Vm;

/* `world` is the world table as it comes off the disc; the machine takes its
 * own mutable copy, because the scripts write to it.  `main` and its length
 * are MAINSCRIPT.  The machine starts with nothing running: the first
 * vm_frame spawns MAINSCRIPT, exactly as `allnew` does. */
void vm_init(Vm *vm, const uint8_t *world, const uint8_t *main, int main_len);

/* The set's script, from ScriptOffset.  Pass NULL to say this set has none.
 * Changing it drops every set-based process and starts the new script, which
 * is what walking through a doorway has to do. */
void vm_set_script(Vm *vm, int set_index, const uint8_t *code, int len);

/* Marks a world entry as having an active character behind it, which is
 * WSTRegistered and what `select` looks for. */
void vm_register(Vm *vm, int world);

/* Starts a process at an address, with the first three registers set.  This
 * is not a script command: it is the door the engine itself uses.  MAINSCRIPT
 * carries a fourth block that nothing spawns - the "item used" handler, which
 * plays animation r0, adds r1 to Life and restores the item if r2 - and the
 * only way in is from outside the bytecode, exactly here. */
void vm_start(Vm *vm, int space, uint32_t off, int32_t r0, int32_t r1,
              int32_t r2);

/* One game frame: every process runs until it yields or dies. */
void vm_frame(Vm *vm);

/* How many processes are alive. */
int  vm_active(const Vm *vm);

const char *vm_opname(int op);

#endif
