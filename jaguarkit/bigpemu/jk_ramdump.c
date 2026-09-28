//__BIGPEMU_SCRIPT_MODULE__
//__BIGPEMU_META_DESC__		"jaguarkit: dumps the Jaguar's whole DRAM at a fixed cadence."
//__BIGPEMU_META_AUTHOR__	"jaguarkit"

// A BigPEmu script module that takes the Jaguar's whole 2 MB every JK_EVERY
// frames, JK_MAX times, from frame JK_FIRST on, into Scripts/jkdump/ramN.bin,
// with a line per dump in Scripts/jkdump/meta.txt (the frame and the 68000's
// PC) and a heartbeat in Scripts/jkdump/alive.txt once a second.
//
// It dumps everything rather than reading addresses a source dump names,
// because a release can move them: Highlander's July 1995 source put the film
// player's screen at $0C0000 and the retail game does not.  The picture - or
// a table of known shape - is found in the dumps offline (findfb.py).
//
// Setting up: copy this into BigPEmu's Scripts/ and create Scripts/jkdump/
// (the script VM's filesystem is rooted at Scripts), then
//     python -m jaguarkit.bigpemu.config --disc GAME.cue --script jk_ramdump
// launch BigPEmu, Run with Images, and leave it.  Change the three numbers
// below for a game that needs another moment; the developer build recompiles
// Scripts/*.c on startup.

#include "bigpcrt.h"
#include "jagregs.h"

#define JK_RAM          0x00000000
#define JK_RAMSIZE      0x00200000      // the whole DRAM

#define JK_FIRST        240             // Jaguar frames before the first dump
#define JK_EVERY        240             // and one every this many
#define JK_MAX          30

#define JK_CHUNK        0x8000
#define JK_META_MAX     8192

static int sOnFrameEvent = -1;
static uint8_t *sBuf = 0;
static uint32_t sDumps = 0;
static uint32_t sDone = 0;
static char sMeta[JK_META_MAX];
static uint32_t sMetaLen = 0;

static uint32_t text_len(const char *p)
{
	uint32_t n = 0;
	while (p[n]) n++;
	return n;
}

static void meta_add(const char *pLine)
{
	uint32_t i = 0;
	while (pLine[i] && sMetaLen < (JK_META_MAX - 2))
	{
		sMeta[sMetaLen++] = pLine[i++];
	}
	if (sMetaLen < (JK_META_MAX - 1))
	{
		sMeta[sMetaLen++] = '\n';
	}
	sMeta[sMetaLen] = 0;
}

static void write_text(const char *pPath, const char *pText, const uint32_t len)
{
	uint64_t fh = fs_open(pPath, 1);
	if (fh)
	{
		fs_write(pText, len, fh);
		fs_close(fh);
	}
}

static int dump_region(const char *pPath, const uint32_t addr, const uint32_t size)
{
	uint64_t fh = fs_open(pPath, 1);
	uint32_t off;

	if (!fh)
	{
		return 0;
	}
	for (off = 0; off < size; off += JK_CHUNK)
	{
		uint32_t n = size - off;
		if (n > JK_CHUNK)
		{
			n = JK_CHUNK;
		}
		bigpemu_jag_sysmemread(sBuf, addr + off, n);
		fs_write(sBuf, n, fh);
	}
	fs_close(fh);
	return 1;
}

static uint32_t on_emu_frame(const int32_t eventHandle, void *pEventData)
{
	char path[128];
	char line[256];
	uint32_t frame;

	if (sDone || !sBuf)
	{
		return 0;
	}

	frame = (uint32_t)bigpemu_jag_get_frame_count();

	// A heartbeat: a run started from a shell has no eyes on the window.
	if ((frame % 60) == 0)
	{
		sprintf(line, "alive jagframe %i pc %08X dumps %i\n",
			(int)frame, (unsigned)bigpemu_jag_m68k_get_pc(), (int)sDumps);
		write_text("jkdump/alive.txt", line, text_len(line));
	}

	if (frame < JK_FIRST || (frame % JK_EVERY) != 0)
	{
		return 0;
	}

	sprintf(path, "jkdump/ram%i.bin", (int)sDumps);
	if (!dump_region(path, JK_RAM, JK_RAMSIZE))
	{
		printf_notify("jk_ramdump: cannot write %s - does Scripts/jkdump exist?", path);
		sDone = 1;
		return 0;
	}

	sprintf(line, "ram%i jagframe %i pc %08X", (int)sDumps, (int)frame,
		(unsigned)bigpemu_jag_m68k_get_pc());
	meta_add(line);
	write_text("jkdump/meta.txt", sMeta, sMetaLen);

	sDumps++;
	printf_notify("jk_ramdump: dump %i of %i at jag frame %i",
		(int)sDumps, (int)JK_MAX, (int)frame);
	if (sDumps >= JK_MAX)
	{
		sDone = 1;
		printf_notify("jk_ramdump: done, %i dumps in Scripts/jkdump", (int)sDumps);
	}
	return 0;
}

void bigp_init()
{
	void *pMod = bigpemu_get_module_handle();
	sBuf = (uint8_t *)bigpemu_vm_alloc(JK_CHUNK);
	sMeta[0] = 0;
	sMetaLen = 0;
	sDumps = 0;
	sDone = 0;
	sOnFrameEvent = bigpemu_register_event_emu_thread_frame(pMod, on_emu_frame);
	printf_notify("jk_ramdump: active, buf=%s", sBuf ? "ok" : "FAILED");
}

void bigp_shutdown()
{
	void *pMod = bigpemu_get_module_handle();
	bigpemu_unregister_event(pMod, sOnFrameEvent);
	sOnFrameEvent = -1;
	if (sBuf)
	{
		bigpemu_vm_free(sBuf);
		sBuf = 0;
	}
}
