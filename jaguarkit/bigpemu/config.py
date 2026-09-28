"""jaguarkit.bigpemu.config - point BigPEmu at a disc and switch a probe on.

BigPEmu keeps one JSON config, %APPDATA%\\BigPEmu\\BigPEmuConfig.bigpcfg.
There is no command-line switch for "enable this script module", so the
module list is edited in place; the original is kept beside it as
BigPEmuConfig.bigpcfg.jkbak and --restore puts it back.

Three things a Jaguar CD needs under BigPEmu, found the hard way in
Highlander's port and all of them settings:

* a **CD BIOS image**, under System - not the cartridge BIOS, which is a
  different setting.  Without it "audio discs will not be loaded", and every
  track of a Jaguar CD .cue is marked AUDIO.
* the **.cue**, not the .jcd: four boots of the same game, and only the
  cue/bin set got the 68000 running.
* an **empty cartridge slot**.

BigPEmu is not headless and will not start a disc by itself, so the run is by
hand: launch it, Run with Images, and leave it.  A probe should need nothing
else once started (a fixed cadence, not a key) and write a heartbeat file so
the run can be followed from a shell.

    python -m jaguarkit.bigpemu.config --disc GAME.cue --script jk_ramdump
    python -m jaguarkit.bigpemu.config --restore
"""

import argparse
import json
import os
import shutil
import sys

CFG = os.path.join(os.environ.get("APPDATA", ""), "BigPEmu", "BigPEmuConfig.bigpcfg")


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m jaguarkit.bigpemu.config")
    ap.add_argument("--disc", help="a .cue (the .jcd is not loaded)")
    ap.add_argument("--cart", help="a cartridge image instead of a disc")
    ap.add_argument("--script", action="append", default=[],
                    help="a script module to enable, by name")
    ap.add_argument("--bootrom", help='"" clears it')
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--cfg", default=CFG)
    a = ap.parse_args(argv)

    cfg, bak = a.cfg, a.cfg + ".jkbak"
    if a.restore:
        if not os.path.exists(bak):
            sys.exit("no backup at %s" % bak)
        shutil.copyfile(bak, cfg)
        print("restored", cfg)
        return 0
    if not os.path.exists(cfg):
        sys.exit("no config at %s - run BigPEmu once first" % cfg)
    if not os.path.exists(bak):
        shutil.copyfile(cfg, bak)
        print("backed up ->", bak)

    with open(cfg, "r", encoding="utf-8") as f:
        doc = json.load(f)
    root = doc["BigPEmuConfig"]
    if a.disc:
        disc = os.path.abspath(a.disc)
        if disc.lower().endswith(".jcd"):
            print("warning: BigPEmu does not load a .jcd; give it the .cue")
        root["SetDisc"] = disc
        root["SetCart"] = ""
        root["ROMPath"] = os.path.dirname(disc)
        print("disc   ->", disc)
    if a.cart:
        root["SetCart"] = os.path.abspath(a.cart)
        root["SetDisc"] = ""
        print("cart   ->", root["SetCart"])
    if a.bootrom is not None:
        root["BootROM"] = os.path.abspath(a.bootrom) if a.bootrom else ""
        print("boot   ->", repr(root["BootROM"]))
    if a.script:
        root["ScriptsEnabled"] = list(a.script)
        print("script ->", a.script)
    # Reload an edited probe without reinstalling it.
    root.setdefault("DevMode", {})["ScriptsAutoReload"] = 1

    with open(cfg, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=4)
    print("wrote", cfg)
    return 0


if __name__ == "__main__":
    sys.exit(main())
