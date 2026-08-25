"""Jaguar / Jaguar CD symbol tables.

Register names follow the Atari Jaguar Technical Reference Manual; the CD BIOS
jump table follows the Jaguar CD developer documentation (the table lives in
DRAM at $3000, which is why a CD boot binary is linked at $4000).
"""

HW = {}

def _add(base, names, step=2):
    for i, n in enumerate(names):
        if n:
            HW[base + i * step] = n

# --- Tom: video / object processor ------------------------------------------
_add(0xF00000, ["MEMCON1", "MEMCON2", "HC", "VC", "LPH", "LPV"])
_add(0xF00010, ["OB0", "OB1", "OB2", "OB3"])
HW[0xF00020] = "OLP"
HW[0xF00026] = "OBF"
_add(0xF00028, ["VMODE", "BORD1", "BORD2", "HP", "HBB", "HBE", "HS", "HVS",
                "HDB1", "HDB2", "HDE", "VP", "VBB", "VBE", "VS", "VDB", "VDE",
                "VEB", "VEE", "VI", "PIT0", "PIT1", "HEQ"])
HW[0xF00058] = "BG"
HW[0xF000E0] = "INT1"
HW[0xF000E2] = "INT2"
HW[0xF00400] = "CLUT"
HW[0xF00800] = "LBUF"

# --- Tom: GPU ---------------------------------------------------------------
_add(0xF02100, ["G_FLAGS", "G_MTXC", "G_MTXA", "G_END", "G_PC", "G_CTRL",
                "G_HIDATA", "G_DIVCTRL"], step=4)
HW[0xF03000] = "GPU_RAM"

# --- Tom: blitter -----------------------------------------------------------
_add(0xF02200, ["A1_BASE", "A1_FLAGS", "A1_CLIP", "A1_PIXEL", "A1_STEP",
                "A1_FSTEP", "A1_FPIXEL", "A1_INC", "A1_FINC", "A2_BASE",
                "A2_FLAGS", "A2_MASK", "A2_PIXEL", "A2_STEP"], step=4)
_add(0xF02238, ["B_CMD", "B_COUNT"], step=4)
_add(0xF02240, ["B_SRCD", "B_DSTD", "B_DSTZ", "B_SRCZ1", "B_SRCZ2", "B_PATD"],
     step=8)
_add(0xF02270, ["B_IINC", "B_ZINC", "B_STOP", "B_I3", "B_I2", "B_I1", "B_I0",
                "B_Z3", "B_Z2", "B_Z1", "B_Z0"], step=4)

# --- Jerry ------------------------------------------------------------------
_add(0xF10000, ["JPIT1", "JPIT2", "JPIT3", "JPIT4"])
HW[0xF10020] = "J_INT"
HW[0xF10030] = "ASICLK"
HW[0xF14000] = "JOYSTICK"
HW[0xF14002] = "JOYBUTS"
HW[0xF14800] = "GPIO_0"
HW[0xF15000] = "GPIO_1"
_add(0xF1A100, ["D_FLAGS", "D_MTXC", "D_MTXA", "D_END", "D_PC", "D_CTRL",
                "D_MOD", "D_DIVCTRL", "D_MACHI"], step=4)
_add(0xF1A148, ["L_I2S", "R_I2S", "SCLK", "SMODE"], step=4)
HW[0xF1B000] = "DSP_RAM"

# --- Jaguar CD BIOS jump table (in DRAM) ------------------------------------
CDBIOS = {
    0x3000: "BIOS_VER",
    0x3006: "CD_mode",
    0x300C: "CD_ack",
    0x3012: "CD_jeri",
    0x3018: "CD_spin",
    0x301E: "CD_stop",
    0x3024: "CD_mute",
    0x302A: "CD_umute",
    0x3030: "CD_paus",
    0x3036: "CD_upaus",
    0x303C: "CD_read",
    0x3042: "CD_uread",
    0x3048: "CD_setup",
    0x304E: "CD_ptr",
    0x3054: "CD_osamp",
    0x305A: "CD_getoc",
    0x3060: "CD_initm",
    0x3066: "CD_initf",
    0x306C: "CD_switch",
}


def name_for(addr):
    """Symbolic name for an absolute address, or None."""
    if addr in CDBIOS:
        return CDBIOS[addr]
    if addr in HW:
        return HW[addr]
    if 0xF03000 <= addr < 0xF04000:
        return "GPU_RAM+$%x" % (addr - 0xF03000)
    if 0xF1B000 <= addr < 0xF1D000:
        return "DSP_RAM+$%x" % (addr - 0xF1B000)
    if 0xF00400 <= addr < 0xF00800:
        return "CLUT+$%x" % (addr - 0xF00400)
    if 0xF00800 <= addr < 0xF00E00:
        return "LBUF+$%x" % (addr - 0xF00800)
    return None
