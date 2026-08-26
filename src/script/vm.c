#include "vm.h"

#include <stdlib.h>
#include <string.h>

#include "../util/io.h"

/* ---- names ---------------------------------------------------------- */

/* OPCODES.INC's order, with 75 to 82 named from the retail handlers - July
 * has no source for those eight.  docs/11-script-vm.md 11.3. */
static const char *const OPNAME[VM_LASTOP] = {
    "not", "neg", "abs", "add", "sub", "cmp", "copy", "mult", "div",
    "inc", "dec", "sett", "cmpi", "exg", "bra", "quit", "bsr", "rts",
    "spawn", "suicide", "kill", "pause", "select", "swapchar", "chartoreg",
    "regtochar", "animate", "animhack", "setanim", "chase", "attack", "face",
    "goto", "turnto", "attplay", "freeze", "release", "waitforit",
    "waitforanim", "cinepak", "redbook", "sample", "camera", "charchange",
    "eventbit", "waitevent", "wstread", "wstwrite", "actread", "actwrite",
    "citread", "citwrite", "testowner", "testset", "testscene", "testbit",
    "setbit", "testprox", "testdist", "testevent", "waitredbook", "eventmask",
    "keytest", "keymask", "slideto", "restore", "sat", "patch", "activation",
    "reset", "fake_scene", "pickup", "default", "poke", "triangle_height",
    "testuse", "random", "getglobal", "setbitreg", "waveexit", "andi",
    "waitkey", "gvar",
};

const char *vm_opname(int op)
{
    return op >= 0 && op < VM_LASTOP ? OPNAME[op] : "?";
}

/* ---- the flags ------------------------------------------------------ */

/* A compare leaves the GPU's flags and `condtable` masks them.  Each of its
 * five entries is `(expected << 4) | mask`, and a branch is taken when
 * `(flags & mask) == expected`:
 *
 *   0  $00  always            3  $44  negative set   - register <  operand
 *   1  $11  zero set          4  $05  zero and negative clear - register >
 *   2  $22  carry set
 *
 * with bit 3 of the condition code inverting the answer.  `lt` reading as
 * "greater than" is SCRIPT.MAC's own naming and not a mistake here. */
static const uint32_t CONDTABLE[5] = { 0x00, 0x11, 0x22, 0x44, 0x05 };

static uint32_t flags_zn(int32_t v)
{
    return (v == 0 ? VM_Z : 0) | (v < 0 ? VM_N : 0);
}

static uint32_t flags_sub(int32_t a, int32_t b)
{
    int32_t r = (int32_t)((uint32_t)a - (uint32_t)b);
    return flags_zn(r) | ((uint32_t)a < (uint32_t)b ? VM_C : 0);
}

static uint32_t flags_add(int32_t a, int32_t b)
{
    uint32_t s = (uint32_t)a + (uint32_t)b;
    return flags_zn((int32_t)s) | (s < (uint32_t)a ? VM_C : 0);
}

/* ---- the world state, as bytes -------------------------------------- */

/* The scripts address the world table the way the engine does - a record
 * index and a field offset - so it is kept here as the 32-byte records it is,
 * and read back through these three.  The decoded WorldRec of sheet.h is a
 * snapshot of what came off the disc; this is the live table. */
static int32_t ws_read(const Vm *vm, int idx, int off, int size)
{
    if (idx < 0 || idx >= WS_COUNT || off < 0 || off >= WS_REC)
        return 0;
    const uint8_t *p = vm->ws + idx * WS_REC + off;
    switch (size) {
    case 0:  return p[0];
    case 1:  return be16(p);
    default: return (int32_t)be32(p);
    }
}

static void ws_write(Vm *vm, int idx, int off, int size, int32_t v)
{
    if (idx < 0 || idx >= WS_COUNT || off < 0 || off >= WS_REC)
        return;
    uint8_t *p = vm->ws + idx * WS_REC + off;
    switch (size) {
    case 0:
        p[0] = (uint8_t)v;
        break;
    case 1:
        p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
        break;
    default:
        p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
        p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
        break;
    }
}

void vm_register(Vm *vm, int world)
{
    /* WSTRegistered is what `select` looks for: it means this world entry has
     * an active character table record behind it. */
    if (world >= 0 && world < WS_COUNT)
        vm->ws[world * WS_REC + 31] |= 1u;
}

/* ---- the CIT and ACT fields ----------------------------------------- */

/* `citread` and `citwrite` name a field by its byte offset in LOGICS.INC's
 * citRecord.  Our instance is an Actor and a Control rather than that record
 * laid out in memory, so the offsets the shipped scripts actually use are
 * mapped here and anything else is counted and ignored - which is a thing the
 * check reports rather than a thing that quietly does nothing. */
static int32_t cit_read(Vm *vm, const Act *a, int off)
{
    switch (off) {
    case 14: return a->ctl.stance;              /* citStance   .b */
    case 20: return a->actor.frame;             /* citFrame    .w */
    case 24: return a->actor.facing;            /* citFacing   .b */
    case 26: return a->actor.ground;            /* citHeight   .w */
    case 28: return a->actor.tri;               /* citTriangle .w */
    case 30: return a->actor.gravity;           /* citGravity  .w */
    case 32: return a->actor.speed;             /* citSpeed    .w */
    default: vm->unmapped++; return 0;
    }
}

static void cit_write(Vm *vm, Act *a, int off, int32_t v)
{
    switch (off) {
    case 14: a->ctl.stance    = (uint8_t)v; break;
    case 20: a->actor.frame   = (int)v;     break;
    case 24: a->actor.facing  = (uint8_t)v; break;
    case 26: a->actor.ground  = v;          break;
    case 30: a->actor.gravity = v;          break;
    case 32: a->actor.speed   = (int16_t)v; break;
    default: vm->unmapped++;                break;
    }
}

static int32_t act_read(Vm *vm, const Act *a, int off)
{
    switch (off) {
    case 0:  return a->world;                   /* actWorld     */
    case 8:  return a->flags;                   /* actFlags     */
    case 10: return a->status;                  /* actStatus    */
    case 12: return (int32_t)a->ctl.pad;        /* actJoypad    */
    case 20: return a->ctl.count;               /* actCount     */
    case 24: return a->ai.command;              /* actAICommand */
    case 28: return a->ai.data1;
    case 32: return a->ai.data2;
    default: vm->unmapped++; return 0;
    }
}

static void act_write(Vm *vm, Act *a, int off, int32_t v)
{
    switch (off) {
    case 8:  a->flags      = (uint16_t)v; break;
    case 10: a->status     = (uint16_t)v; break;
    case 12: a->ctl.pad    = (uint32_t)v; break;
    case 24: a->ai.command = (int)v;      break;
    case 28: a->ai.data1   = v;           break;
    case 32: a->ai.data2   = v;           break;
    default: vm->unmapped++;              break;
    }
}

/* ---- the process table ---------------------------------------------- */

int vm_active(const Vm *vm)
{
    int n = 0;
    while (n < VM_PROCS && vm->proc[n].pc)
        n++;
    return n;
}

/* The list is compacted, exactly as SCRIPT.GAS keeps it: a zero program
 * counter is the end marker, and killing a record shuffles the rest down over
 * it.  Which is why the frame loop below does not step its index when a
 * process dies. */
static void proc_remove(Vm *vm, int i)
{
    int n = vm_active(vm);
    if (i < 0 || i >= n)
        return;
    memmove(&vm->proc[i], &vm->proc[i + 1], (size_t)(n - i - 1) * sizeof(VmProc));
    memset(&vm->proc[n - 1], 0, sizeof(VmProc));
}

/* `spawn`.  The identifier is the set number in the top word and the offset
 * from the start of the owning script in the bottom, so a process cannot be
 * started twice: the search stops either at a record with the same identifier
 * - which mode 1 then restarts - or at the first free one. */
static void proc_spawn(Vm *vm, const VmProc *parent, uint32_t pc, int restart)
{
    uint32_t base_set = parent ? (parent->ident >> 16) : 0;
    uint32_t ident;
    if (base_set == 0)
        ident = VM_OFF(pc);     /* a MAINSCRIPT process: the offset alone.
                                   The original decides this by the set
                                   number rather than by the space, so set 0's
                                   own processes are numbered as if they were
                                   global; that quirk is kept. */
    else
        ident = (base_set << 16) | VM_OFF(pc);

    int n = vm_active(vm);
    int slot = -1;
    for (int i = 0; i < n; i++)
        if (vm->proc[i].ident == ident) {
            slot = i;
            break;
        }
    if (slot < 0) {
        if (n >= VM_PROCS)
            return;             /* the original walks off the end of the
                                   table here; refusing is the safe reading */
        slot = n;
    } else if (!restart) {
        return;                 /* already running, and this is not a restart */
    }

    VmProc *p = &vm->proc[slot];
    memset(p, 0, sizeof *p);
    p->pc    = pc;
    p->ident = ident;
    p->chr   = -1;
    p->sp    = VM_STACK;
}

void vm_start(Vm *vm, int space, uint32_t off, int32_t r0, int32_t r1,
              int32_t r2)
{
    VmProc seed;
    memset(&seed, 0, sizeof seed);
    seed.ident = space == VM_SET ? ((uint32_t)vm->curset << 16) : 0;
    proc_spawn(vm, &seed, VM_PC(space, off), 1);
    int n = vm_active(vm);
    for (int i = 0; i < n; i++)
        if (vm->proc[i].pc == VM_PC(space, off)) {
            vm->proc[i].r[0] = r0;
            vm->proc[i].r[1] = r1;
            vm->proc[i].r[2] = r2;
            break;
        }
}

/* ---- reading the code ----------------------------------------------- */

static const uint8_t *space_of(const Vm *vm, uint32_t pc, int *len)
{
    if (VM_SPACE(pc) == VM_MAIN) { *len = vm->main_len; return vm->main; }
    *len = vm->code_len;
    return vm->code;
}

static uint32_t fetch(const Vm *vm, uint32_t pc, int *ok)
{
    int len;
    const uint8_t *d = space_of(vm, pc, &len);
    uint32_t off = VM_OFF(pc);
    if (!d || off + 4 > (uint32_t)len) {
        *ok = 0;
        return 0;
    }
    *ok = 1;
    return be32(d + off);
}

/* ---- the machine ---------------------------------------------------- */

void vm_init(Vm *vm, const uint8_t *world, const uint8_t *main, int main_len)
{
    memset(vm, 0, sizeof *vm);
    if (world)
        memcpy(vm->ws, world, sizeof vm->ws);
    vm->main       = main;
    vm->main_len   = main_len;
    vm->script_set = -1;
    vm->used       = -1;
    vm->seed       = 1;
    vm->frametime  = 256 / 20;  /* the animations' own rate; `pause` counts in
                                   256ths of a second and this is the amount
                                   one game frame takes off it */
    vm->eventmask  = 0xFFFFFFFFu;
    for (int i = 0; i < VM_PROCS; i++)
        vm->proc[i].chr = -1;
}

void vm_set_script(Vm *vm, int set_index, const uint8_t *code, int len)
{
    vm->code     = code;
    vm->code_len = len;
    vm->curset   = (uint16_t)set_index;
}

/* One command.  Returns 1 to carry on with this process, 0 to yield (`quit`
 * and everything that waits by re-running itself), -1 when the process has
 * died and its record has already been removed. */
static int step(Vm *vm, int slot)
{
    VmProc *p = &vm->proc[slot];
    int ok;
    uint32_t comm = fetch(vm, p->pc, &ok);
    if (!ok) {
        proc_remove(vm, slot);
        return -1;
    }
    /* Slot padding.  A script runs to the end of what was written and the
     * rest of its 56-block slot is zeros, and a zero command is `not r0` -
     * which the real machine would happily execute for ever, spinning the GPU
     * without stopping the game.  Two sets do exactly that: set 4's
     * ScriptOffset points straight at padding and set 37's script is two
     * commands and then padding.  The disassembler reached the same reading
     * independently, measuring those two at 0 and 8 bytes, and no shipped
     * script uses `not` at all - `neg` is the only one of the three that
     * appears.  So a zero command ends the process here.  It is a divergence
     * and this is it, stated. */
    if (comm == 0) {
        vm->padding++;
        proc_remove(vm, slot);
        return -1;
    }

    uint32_t at = p->pc;
    p->pc += 4;

    int      op   = (int)(comm >> 24);
    int      mode = (int)((comm >> 20) & 0xF);
    int      reg  = (int)((comm >> 16) & 0xF);
    int32_t  oper = (int16_t)(comm & 0xFFFF);
    uint16_t uoper = (uint16_t)(comm & 0xFFFF);

    vm->executed++;
    if (op >= 0 && op < VM_LASTOP)
        vm->opcount[op]++;
    if (reg >= VM_REGS)
        reg = VM_REGS - 1;

    /* The extra longs, read through the program counter.  `fake_scene` reads
     * one and does not step over it - both builds do that, and no shipped
     * script uses the opcode. */
    uint32_t ex[2] = { 0, 0 };
    int nex = 0;
    if (op == 32 || op == 33 || op == 58 || op == 64) nex = 2;
    else if (op == 39 || op == 40 || op == 57 || op == 63 || op == 67 ||
             op == 74) nex = 1;
    else if (op == 61) nex = mode == 0;
    else if (op == 80) nex = mode != 0;
    else if (op == 70) nex = -1;                /* read, do not step over    */
    for (int i = 0; i < (nex < 0 ? 1 : nex); i++) {
        ex[i] = fetch(vm, p->pc + (uint32_t)i * 4, &ok);
        if (!ok) {
            proc_remove(vm, slot);
            return -1;
        }
    }
    if (nex > 0)
        p->pc += (uint32_t)nex * 4;

    /* The three operand classes the bottom of the opcode space is decoded by.
     * Note where each writes back: an `add` names its destination in the
     * operand and an `inc` in the register nibble, which is a consequence of
     * the decoder and not of the mnemonics. */
    if (op < 13) {
        int32_t *dst, a, b = 0;
        if (op < 3) {
            dst = &p->r[reg];
            a = *dst;
            p->flags = flags_zn(a);             /* the `or val1,val1` in the
                                                   decoder's delay slot     */
        } else if (op < 9) {
            int o = (int)(uoper & 0xF);
            dst = &p->r[o];
            a = p->r[o];
            b = p->r[reg];
            p->flags = flags_zn(a);
        } else {
            dst = &p->r[reg];
            a = *dst;
            b = oper;
        }
        switch (op) {
        case 0:  *dst = ~a;            p->flags = flags_zn(*dst); break;
        case 1:  *dst = -a;            p->flags = flags_zn(*dst) |
                                                  (a ? VM_C : 0);  break;
        case 2:  *dst = a < 0 ? -a : a; p->flags = flags_zn(*dst); break;
        case 3:                                 /* add */
        case 9:  p->flags = flags_add(a, b); *dst = (int32_t)((uint32_t)a + (uint32_t)b); break;
        case 4:                                 /* sub */
        case 10: p->flags = flags_sub(a, b); *dst = (int32_t)((uint32_t)a - (uint32_t)b); break;
        case 5:                                 /* cmp  - no write back */
        case 12: p->flags = flags_sub(a, b); break;
        case 6:                                 /* copy - move sets no flags */
        case 11: *dst = b; break;
        case 7:  *dst = a * b;         p->flags = flags_zn(*dst); break;
        case 8:  *dst = b ? a / b : 0; break;   /* div leaves the flags alone */
        }
        return 1;
    }

    Act *chr = p->chr >= 0 && vm->act && p->chr < vm->act->n
               ? &vm->act->a[p->chr] : NULL;

    switch (op) {

    case 13:                                    /* exg */
    {
        int o = (int)(uoper & 0xF);
        int32_t t = p->r[o];
        p->r[o] = p->r[reg];
        p->r[reg] = t;
        return 1;
    }

    case 14:                                    /* bra */
    case 16:                                    /* bsr */
    {
        int cc = mode & 7;
        uint32_t m = CONDTABLE[cc < 5 ? cc : 0];
        int take = ((p->flags & (m & 0xF)) == (m >> 4));
        if (mode & 8)
            take = !take;
        if (!take)
            return 1;
        if (op == 16 && p->sp > 0)
            p->stack[--p->sp] = p->pc;
        p->pc += (uint32_t)oper;
        return 1;
    }

    case 15:                                    /* quit */
    case 59:                                    /* testevent: never written,
                                                   and shares quit's handler */
        return 0;

    case 17:                                    /* rts */
        if (p->sp < VM_STACK)
            p->pc = p->stack[p->sp++];
        return 1;

    case 18:                                    /* spawn */
        proc_spawn(vm, p, p->pc + (uint32_t)oper, mode != 0);
        return 1;

    case 19:                                    /* suicide */
        proc_remove(vm, slot);
        return -1;

    case 20:                                    /* kill */
    {
        uint32_t want = (p->ident & 0xFFFF0000u) | (uint32_t)uoper;
        if (want == p->ident)
            return 1;                           /* the suicide guard */
        int n = vm_active(vm);
        for (int i = 0; i < n; i++)
            if (vm->proc[i].ident == want) {
                proc_remove(vm, i);
                if (i < slot)
                    return -1;                  /* we have moved: the caller
                                                   must re-find us, and the
                                                   simplest honest answer is
                                                   to give this frame up     */
                break;
            }
        return 1;
    }

    case 21:                                    /* pause */
        if (p->pause == 0) {
            p->pause = uoper;
        } else {
            p->pause -= vm->frametime;
            if (p->pause < 0)
                p->pause = 0;                   /* the sat16, whose flags are
                                                   what the original tests   */
            if (p->pause == 0) {
                return 1;                       /* counted out: carry on     */
            }
        }
        p->pc = at;                             /* replay until it counts out */
        return 0;

    case 22:                                    /* select */
    {
        int w = (int)uoper;
        int a = -1;
        if (w >= 0 && w < WS_COUNT && (vm->ws[w * WS_REC + 31] & 1u))
            a = act_of_world(vm->act, w);
        if (mode == 0)
            p->chr = a;
        else
            p->r[reg] = a;
        return 1;
    }

    case 23:                                    /* swapchar */
    {
        int t = p->chr;
        p->chr = (int)p->r[reg];
        p->r[reg] = t;
        return 1;
    }

    case 24:  p->r[reg] = p->chr;      return 1;    /* chartoreg */
    case 25:  p->chr = (int)p->r[reg]; return 1;    /* regtochar */

    case 26:                                    /* animate  */
    case 27:                                    /* animhack */
    case 28:                                    /* setanim  */
    {
        /* All three end in `genanim`: point the instance at an animation,
         * reset the frame, and set FSALock so the AI cannot take it over.
         * Which animation is a sheet entry for `animate`, a register for
         * `animhack`, and one of the *set's* own animations for `setanim` -
         * the last of which this port has no bank for yet. */
        Act *a = mode == 0 ? chr : (p->r[reg] >= 0 && vm->act &&
                                    p->r[reg] < vm->act->n
                                    ? &vm->act->a[p->r[reg]] : NULL);
        int k = op == 27 ? (int)p->r[reg] : (int)uoper;
        if (!a)
            return 1;
        if (op == 28) {
            vm->unmapped++;                     /* the set sheet's animations */
            return 1;
        }
        if (k < 0 || k >= a->nanims)
            return 0;                           /* genanim waits for an
                                                   animation that is not
                                                   there rather than failing */
        a->anim = &a->anims[k];
        a->actor.frame = 0;
        a->ctl.anim = k;
        a->ctl.stance |= FSA_LOCK;
        return 1;
    }

    case 29:                                    /* chase   -> aiGotoPerson  */
    case 30:                                    /* attack  -> aiAttackPerson */
    case 31:                                    /* face    -> aiFacePerson  */
    {
        Act *a = mode == 0 ? chr : (p->r[reg] >= 0 && vm->act &&
                                    p->r[reg] < vm->act->n
                                    ? &vm->act->a[p->r[reg]] : NULL);
        if (!a)
            return 1;
        a->ai.command = op == 29 ? AI_GOTO_PERSON :
                        op == 30 ? AI_ATTACK_PERSON : AI_FACE_PERSON;
        a->ai.data1   = (int32_t)uoper;         /* a world entry */
        a->flags     |= ACT_CONTROLLED;
        return 1;
    }

    case 32:                                    /* goto   -> aiGotoPosition */
    case 33:                                    /* turnto -> aiFacePosition */
    {
        Act *a = mode == 0 ? chr : (p->r[reg] >= 0 && vm->act &&
                                    p->r[reg] < vm->act->n
                                    ? &vm->act->a[p->r[reg]] : NULL);
        if (!a)
            return 1;
        a->ai.command = op == 32 ? AI_GOTO_POSITION : AI_FACE_POSITION;
        a->ai.data1   = (int32_t)ex[0];
        a->ai.data2   = (int32_t)ex[1];
        a->flags     |= ACT_CONTROLLED;
        return 1;
    }

    case 34:                                    /* attplay */
    case 35:                                    /* freeze  */
    case 36:                                    /* release */
    case 72:                                    /* default */
    {
        Act *a = mode == 0 ? chr : (p->r[reg] >= 0 && vm->act &&
                                    p->r[reg] < vm->act->n
                                    ? &vm->act->a[p->r[reg]] : NULL);
        if (!a)
            return 1;
        if (op == 34) {
            a->ai.command = AI_ATTACK_PLAYER;
            a->flags &= (uint16_t)~ACT_CONTROLLED;
        } else if (op == 35) {
            a->ai.command = AI_NOP;
            a->flags |= ACT_CONTROLLED;
        } else if (op == 36) {
            a->flags &= (uint16_t)~ACT_CONTROLLED;
        } else {
            a->ai.command = AI_DEFAULT;
        }
        return 1;
    }

    case 37:                                    /* waitforit */
    {
        Act *a = mode == 0 ? chr : (p->r[reg] >= 0 && vm->act &&
                                    p->r[reg] < vm->act->n
                                    ? &vm->act->a[p->r[reg]] : NULL);
        if (!a || a->ai.command == AI_NOP)
            return 1;                           /* arrived: AIGotoCode drops
                                                   to aiNop when it does     */
        p->pc = at;
        return 0;
    }

    case 38:                                    /* waitforanim */
    {
        Act *a = mode == 0 ? chr : (p->r[reg] >= 0 && vm->act &&
                                    p->r[reg] < vm->act->n
                                    ? &vm->act->a[p->r[reg]] : NULL);
        /* ANIMEND, which the engine sets when an animation runs out.  We do
         * not carry the bit, but the condition it stands for is exactly that
         * the current animation has no frames left. */
        if (!a || !a->anim || a->actor.frame + 1 >= a->anim->frames)
            return 1;
        p->pc = at;
        return 0;
    }

    case 39:                                    /* cinepak */
    case 40:                                    /* redbook */
        if (vm->scriptevent) {                  /* one event at a time */
            p->pc = at;
            return 0;
        }
        vm->scriptblock = ex[0];
        vm->scriptevent = (uint16_t)((op == 39 ? EV_CINEPAK : EV_CDAUDIO) + 1);
        if (op == 40)
            vm->redbook_done = 0;
        return 0;                               /* both leave, to let the load
                                                   happen before anything else */

    case 41:                                    /* sample */
        if (mode != 0)
            return 0;                           /* the handler refuses every
                                                   mode but the set's own    */
        vm->scriptscene = uoper;
        vm->scriptevent = (uint16_t)(EV_SOUND + 1);
        return 1;

    case 42:                                    /* camera */
        if (vm->scriptevent) {
            p->pc = at;
            return 0;
        }
        vm->scriptscene = uoper;
        vm->scriptevent = (uint16_t)(EV_SCENE + 1);
        return 1;

    case 43:                                    /* charchange */
        if (vm->scriptevent) {
            p->pc = at;
            return 0;
        }
        vm->scriptscene = uoper;
        vm->scriptevent = (uint16_t)(EV_CHARCHANGE + 1);
        return 0;

    case 44:                                    /* eventbit */
        if (mode)
            vm->eventmask |= 1u << (uoper & 31);
        else
            vm->eventmask &= ~(1u << (uoper & 31));
        return 1;

    case 45:                                    /* waitevent */
        if (vm->scriptevent == 0)
            return 1;
        p->pc = at;
        return 0;

    case 46:                                    /* wstread  */
    case 47:                                    /* wstwrite */
    {
        int off  = (int)(uoper >> 8);
        int size = mode >> 2;
        int idx;
        if (mode & 2)
            idx = (int)(uoper & 0xFF);          /* a world entry, outright   */
        else {
            const Act *a = (mode & 1)
                ? (p->r[uoper & 0xF] >= 0 && vm->act &&
                   p->r[uoper & 0xF] < vm->act->n
                   ? &vm->act->a[p->r[uoper & 0xF]] : NULL)
                : chr;
            if (!a)
                return 1;
            idx = a->world;
        }
        if (op == 46) {
            p->r[reg] = ws_read(vm, idx, off, size);
            p->flags  = flags_zn(p->r[reg]);
        } else {
            ws_write(vm, idx, off, size, p->r[reg]);
        }
        return 1;
    }

    case 48:                                    /* actread  */
    case 49:                                    /* actwrite */
    case 50:                                    /* citread  */
    case 51:                                    /* citwrite */
    {
        int off = (int)(uoper >> 8);
        Act *a = (mode & 1)
            ? (p->r[uoper & 0xF] >= 0 && vm->act &&
               p->r[uoper & 0xF] < vm->act->n
               ? &vm->act->a[p->r[uoper & 0xF]] : NULL)
            : chr;
        if (!a)
            return 1;
        if (op == 48 || op == 50) {
            p->r[reg] = op == 48 ? act_read(vm, a, off) : cit_read(vm, a, off);
            p->flags  = flags_zn(p->r[reg]);
        } else if (op == 49) {
            act_write(vm, a, off, p->r[reg]);
        } else {
            cit_write(vm, a, off, p->r[reg]);
        }
        return 1;
    }

    case 52:                                    /* testowner */
    {
        const Act *a = mode == 0 ? chr : (p->r[reg] >= 0 && vm->act &&
                                          p->r[reg] < vm->act->n
                                          ? &vm->act->a[p->r[reg]] : NULL);
        /* wstParent holds an address in the original, so the comparison is
         * "is this entry's owner the selected character's world record".  Our
         * parents are addresses off the disc too, so compare on the index the
         * address resolves to - which the host fills in as a plain index. */
        int32_t owner = ws_read(vm, (int)uoper, 8, 2);
        p->flags = flags_sub(owner, a ? a->world : 0);
        return 1;
    }

    case 53:  p->flags = flags_sub(vm->curset, (int32_t)uoper); return 1;
    case 54:  p->flags = flags_sub(vm->scene,  (int32_t)uoper); return 1;

    case 55:                                    /* testbit */
    {
        int b = (int)(uoper & 0x3FF);
        int v = (vm->gamestate[b >> 5] >> (b & 31)) & 1;
        p->flags = flags_zn(v);
        return 1;
    }

    case 56:                                    /* setbit    */
    case 78:                                    /* setbitreg */
    {
        int b = (op == 78 ? (int)p->r[reg] : (int)uoper) & 0x3FF;
        if (mode)
            vm->gamestate[b >> 5] |=  (1u << (b & 31));
        else
            vm->gamestate[b >> 5] &= ~(1u << (b & 31));
        return 1;
    }

    case 57:                                    /* testprox */
    case 58:                                    /* testdist */
    {
        const Act *a = mode == 0 ? chr
            : (p->r[reg] >= 0 && vm->act && p->r[reg] < vm->act->n
               ? &vm->act->a[p->r[reg]] : NULL);
        if (!a)
            return 1;
        int32_t tx, tz, limit;
        if (op == 57) {
            tx = ws_read(vm, (int)uoper, 12, 2);
            tz = ws_read(vm, (int)uoper, 20, 2);
            limit = (int32_t)ex[0];
        } else {
            tx = (int32_t)ex[0];
            tz = (int32_t)ex[1];
            limit = oper;
        }
        int64_t dx = tx - a->actor.x, dz = tz - a->actor.z;
        int64_t d2 = dx * dx + dz * dz;
        p->flags = flags_sub((int32_t)d2, limit * limit);
        return 1;
    }

    case 60:                                    /* waitredbook */
        if (vm->redbook_done)
            return 1;
        p->pc = at;
        return 0;

    case 61:                                    /* eventmask */
        vm->eventmask = mode == 0 ? ex[0] : (uint32_t)uoper;
        return 1;

    case 62:                                    /* keytest */
    {
        int b = (mode ? (int)p->r[reg] : (int)uoper) & 31;
        p->flags = flags_zn((int32_t)((vm->pad >> b) & 1));
        return 1;
    }

    case 63:                                    /* keymask */
        p->flags = flags_zn((int32_t)(vm->pad & ex[0]));
        return 1;

    case 64:                                    /* slideto */
    {
        Act *a = mode == 0 ? chr : (p->r[reg] >= 0 && vm->act &&
                                    p->r[reg] < vm->act->n
                                    ? &vm->act->a[p->r[reg]] : NULL);
        if (!a)
            return 1;
        if (p->pause == 0)
            p->pause = uoper ? uoper : 1;       /* how many frames to take   */
        int32_t x = ws_read(vm, a->world, 12, 2);
        int32_t z = ws_read(vm, a->world, 20, 2);
        int32_t dx = ((int32_t)ex[0] - x) / p->pause;
        int32_t dz = ((int32_t)ex[1] - z) / p->pause;
        ws_write(vm, a->world, 12, 2, x + dx);
        ws_write(vm, a->world, 20, 2, z + dz);
        a->actor.x += dx;
        a->actor.z += dz;
        if (--p->pause == 0)
            return 1;
        p->pc = at;
        return 0;
    }

    case 65:                                    /* restore */
        if (vm->scriptevent) {
            p->pc = at;
            return 0;
        }
        vm->scriptevent = (uint16_t)(EV_RESTOREITEM + 1);
        return 1;

    case 66:                                    /* sat */
        if (mode) {
            if (p->r[reg] < 0)      p->r[reg] = 0;
            if (p->r[reg] > 0xFFFF) p->r[reg] = 0xFFFF;
        } else {
            if (p->r[reg] < 0)    p->r[reg] = 0;
            if (p->r[reg] > 0xFF) p->r[reg] = 0xFF;
        }
        return 1;

    case 67:                                    /* patch */
        /* It copies longs from one place in the script over another, so it
         * needs the code to be writable.  Ours is a const view of the track,
         * and no shipped script reaches this before the check does. */
        vm->unmapped++;
        return 1;

    case 68:                                    /* activation */
    {
        int idx;
        if (mode & 2)
            idx = (int)(uoper & 0xFF);
        else {
            const Act *a = (mode & 1)
                ? (p->r[reg] >= 0 && vm->act && p->r[reg] < vm->act->n
                   ? &vm->act->a[p->r[reg]] : NULL)
                : chr;
            if (!a)
                return 1;
            idx = a->world;
        }
        int32_t f = ws_read(vm, idx, 31, 0);
        f = (f & ~(1 << 6)) | (((mode >> 2) & 1) << 6);   /* WSTDeactivated */
        ws_write(vm, idx, 31, 0, f);
        return 1;
    }

    case 69:                                    /* reset */
        vm->gameover = 1;
        return 1;

    case 70:                                    /* fake_scene */
        /* Reads the long at the program counter and never steps over it, in
         * both builds, so the block number would be executed next.  No script
         * on the disc uses the opcode. */
        return 1;

    case 71:                                    /* pickup */
        vm->scriptscene = uoper;
        vm->scriptevent = (uint16_t)(EV_RESTOREITEM + 1);
        return 1;

    case 73:                                    /* poke */
        vm->unmapped++;                         /* an absolute RAM address   */
        return 1;

    case 74:                                    /* triangle_height */
        if (vm->set && (int)uoper < vm->set->ntris)
            vm->set->tri[uoper].height = (int16_t)ex[0];
        return 1;

    case 75:                                    /* testuse */
        p->flags = flags_sub(vm->used, (int32_t)uoper);
        vm->used = -1;                          /* the handler clears the
                                                   latch as it reads it      */
        return 1;

    case 76:                                    /* random */
        vm->seed = vm->seed * 1103515245u + 12345u;
        p->r[reg] = uoper ? (int32_t)(1 + (vm->seed >> 16) % uoper)
                          : (int32_t)vm->seed;
        return 1;

    case 77:                                    /* getglobal */
        p->r[reg] = vm->gvar[uoper & 15];
        return 1;

    case 79:                                    /* waveexit */
        return 1;                               /* shuts the sample player
                                                   down; phase 7's problem   */

    case 80:                                    /* andi */
        p->r[reg] &= mode ? (int32_t)ex[0] : (int32_t)(uint32_t)uoper;
        p->flags = flags_zn(p->r[reg]);
        return 1;

    case 81:                                    /* waitkey */
        if (mode) {
            if (vm->pad == 0)
                return 1;
        } else {
            uint32_t want = 1u << (uoper & 31);
            if (vm->pad == want)
                return 1;
            if (vm->pad != 0) {
                p->pc += (uint32_t)(oper >> 5);
                return 0;
            }
        }
        p->pc = at;
        return 0;

    case 82:                                    /* gvar */
    {
        int off = uoper & 63, size = (mode >> 1) & 3;
        if (mode & 1) {
            int32_t v = p->r[reg];
            if (size == 0) vm->var[off] = (uint8_t)v;
            else if (size == 1) { vm->var[off] = (uint8_t)(v >> 8);
                                  vm->var[off + 1] = (uint8_t)v; }
            else { for (int i = 0; i < 4; i++)
                       vm->var[off + i] = (uint8_t)(v >> (24 - 8 * i)); }
        } else {
            int32_t v = 0;
            if (size == 0) v = vm->var[off];
            else if (size == 1) v = be16(vm->var + off);
            else v = (int32_t)be32(vm->var + off);
            p->r[reg] = v;
            p->flags  = flags_zn(v);
        }
        return 1;
    }

    default:
        /* Past the dispatch table.  The original abandons the process, and
         * says so: `moveq #sc_suicide,sccomm  ; abandon if hit bad command`. */
        vm->unknown_op++;
        proc_remove(vm, slot);
        return -1;
    }
}

void vm_frame(Vm *vm)
{
    /* Starting up, and changing set.  The original tells a set-based process
     * from a global one by comparing the program counter with a fixed address;
     * here the space in the top byte says it.  On a set change every set-based
     * process is dropped and the globals are kept, then the new set's script
     * starts as one fresh process. */
    if (!vm->started) {
        proc_spawn(vm, NULL, VM_PC(VM_MAIN, 0), 1);
        vm->started = 1;
        vm->script_set = -1;            /* MAINSCRIPT is up, and the set's
                                           script starts on the next pass -
                                           which is what `allnew` arranges by
                                           leaving behind a set number that
                                           cannot match the real one        */
    } else if (vm->script_set != (int)vm->curset) {
        for (int i = 0; i < VM_PROCS; ) {
            if (vm->proc[i].pc && VM_SPACE(vm->proc[i].pc) == VM_SET)
                proc_remove(vm, i);
            else if (vm->proc[i].pc)
                i++;
            else
                break;
        }
        vm->script_set = (int)vm->curset;
        if (vm->code && vm->code_len > 0) {
            VmProc seed;
            memset(&seed, 0, sizeof seed);
            seed.ident = (uint32_t)vm->curset << 16;
            proc_spawn(vm, &seed, VM_PC(VM_SET, 0), 1);
        }
    }

    for (int i = 0; i < VM_PROCS && vm->proc[i].pc; ) {
        int budget = 4096;              /* the original has none; a script
                                           that never yields would hang the
                                           GPU.  Counting the overrun is how
                                           a check can see one.            */
        int r = 1;
        while (r > 0 && budget-- > 0)
            r = step(vm, i);
        if (budget <= 0) {
            vm->overrun++;
            r = 0;
        }
        if (r == 0)
            i++;                        /* yielded: on to the next process  */
        /* r < 0 means the record died and the list has shuffled down over
         * it, so the same index is now the next process. */
    }
}
