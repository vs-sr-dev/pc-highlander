#!/usr/bin/env python3
"""Point BigPEmu at the Highlander disc and switch the hl_* probe on.

BigPEmu keeps one JSON config in %APPDATA%\\BigPEmu.  -localdata did not move
it here, and there is no command-line switch for "enable this script module",
so the module list is edited in place.  The original is kept beside it as
BigPEmuConfig.bigpcfg.hlbak, and --restore puts it back.

  python tools/emu/setup_cfg.py --disc <path.jcd> --script hl_cine
  python tools/emu/setup_cfg.py --restore
"""

import argparse
import json
import os
import shutil
import sys

CFG = os.path.join(os.environ.get("APPDATA", ""), "BigPEmu", "BigPEmuConfig.bigpcfg")
BAK = CFG + ".hlbak"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--disc")
    ap.add_argument("--script", action="append", default=[])
    ap.add_argument("--bootrom")            # "" clears it
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--cfg", default=CFG)
    args = ap.parse_args()

    cfg, bak = args.cfg, args.cfg + ".hlbak"
    if args.restore:
        if not os.path.exists(bak):
            sys.exit("no backup at %s" % bak)
        shutil.copyfile(bak, cfg)
        print("restored", cfg)
        return

    if not os.path.exists(bak):
        shutil.copyfile(cfg, bak)
        print("backed up ->", bak)

    with open(cfg, "r", encoding="utf-8") as f:
        doc = json.load(f)
    root = doc["BigPEmuConfig"]

    if args.disc:
        disc = os.path.abspath(args.disc)
        root["SetDisc"] = disc
        root["SetCart"] = ""
        root["ROMPath"] = os.path.dirname(disc)
        print("disc  ->", disc)

    if args.bootrom is not None:
        root["BootROM"] = os.path.abspath(args.bootrom) if args.bootrom else ""
        print("boot  ->", repr(root["BootROM"]))

    if args.script:
        root["ScriptsEnabled"] = list(args.script)
        print("script->", args.script)

    # Auto-reload so an edited probe is picked up without a fresh install, and
    # windowed at a fixed size so a screenshot of the notify text is readable.
    root.setdefault("DevMode", {})["ScriptsAutoReload"] = 1

    with open(cfg, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=4)
    print("wrote", cfg)


if __name__ == "__main__":
    main()
