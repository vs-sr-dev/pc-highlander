#!/bin/sh
# Four boots of the same disc, to find out which pairing BigPEmu will actually
# run: the .cue against the .jcd, with and without the cartridge boot ROM.  The
# probe writes hlcine/alive.txt once a second with the 68000's PC, so the answer
# is "which of these got the PC into RAM and VMODE non-zero", not "which one
# looked right on screen".
set -u
ROOT="d:/Homebrew6/PC-Highlander"
EMU="$ROOT/BigPEmuDEV"
CUE="D:/Emulatori/Jaguar/Highlander - The Last of the MacLeods (USA).cue"
JCD="$EMU/Highlander - The Last of the MacLeods (USA).jcd"
BIOS="D:/Homebrew/Jaguar/Emu/[BIOS] Atari Jaguar (World).j64"
SECS=${SECS:-45}
OUT="$ROOT/build/emu"
mkdir -p "$OUT"

run() {
	name="$1"; disc="$2"; boot="$3"
	python "$ROOT/tools/emu/setup_cfg.py" --disc "$disc" --bootrom "$boot" --script hl_cine >/dev/null
	rm -f "$EMU/Scripts/hlcine/alive.txt"
	( cd "$EMU" && timeout -k 5 "$SECS" ./BigPEmuDev.exe >/dev/null 2>&1 )
	if [ -f "$EMU/Scripts/hlcine/alive.txt" ]; then
		cp "$EMU/Scripts/hlcine/alive.txt" "$OUT/alive-$name.txt"
		printf '%-12s %s\n' "$name" "$(cat "$OUT/alive-$name.txt")"
	else
		printf '%-12s no heartbeat\n' "$name"
	fi
}

run cue-bios  "$CUE" "$BIOS"
run cue-nobios "$CUE" ""
run jcd-bios  "$JCD" "$BIOS"
run jcd-nobios "$JCD" ""
