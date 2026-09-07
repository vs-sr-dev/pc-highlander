//__BIGPEMU_SCRIPT_MODULE__
//__BIGPEMU_META_DESC__		"PC-Highlander: dumps Jaguar RAM while a film plays, for the frame comparator."
//__BIGPEMU_META_AUTHOR__	"PC-Highlander, session 14"

// Why this exists
// ---------------
// docs/09-text-and-fmv.md 9.5: our decoder goes cvid -> YUV -> 24-bit -> RGB16,
// and the note there says "the Jaguar's own decoder went straight to RGB16 and
// may well round differently, which is a question for a frame comparator
// against an emulator".  This is that comparator's Jaguar end.
//
// First try read the film player's framebuffer at the address CINEPAK.INC
// fixes - screen_start $000C0000, with 'FILM' at film_start $0010E000 - and
// that is wrong on the retail disc: with the boot film visibly playing,
// $0010E000 holds 12441245, which is two RGB16 pixels rather than a chunk tag.
// The source dump is a July 1995 build and the release moved its memory about.
//
// So this does not guess where the picture is.  It takes the whole of the
// Jaguar's 2 MB every few seconds, and the framebuffer is found offline by
// tools/emu/findfb.py, which looks for the one window of memory whose rows
// correlate at a 320-pixel stride.  That needs no address and no tag.

#include "bigpcrt.h"
#include "jagregs.h"

#define HL_RAM			0x00000000
#define HL_RAMSIZE		0x00200000				// the Jaguar's whole DRAM

// When to take them.  The disc has to boot and the CD has to spin up before
// there is a film at all, so the first dump is late and they are spaced wide
// enough that thirty of them cover two minutes.
#define HL_FIRST		240						// jag frames before the first
#define HL_EVERY		240						// and one every this many
#define HL_MAX			30

#define HL_CHUNK		0x8000

static int sOnFrameEvent = -1;
static uint8_t *sBuf = 0;
static uint32_t sDumps = 0;
static uint32_t sDone = 0;

#define HL_META_MAX		8192
static char sMeta[HL_META_MAX];
static uint32_t sMetaLen = 0;

static void meta_add(const char *pLine)
{
	uint32_t i = 0;
	while (pLine[i] && sMetaLen < (HL_META_MAX - 2))
	{
		sMeta[sMetaLen++] = pLine[i++];
	}
	if (sMetaLen < (HL_META_MAX - 1))
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
	for (off = 0; off < size; off += HL_CHUNK)
	{
		uint32_t n = size - off;
		if (n > HL_CHUNK)
		{
			n = HL_CHUNK;
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

	// A heartbeat, because a probe run from a shell has no eyes on the window.
	if ((frame % 60) == 0)
	{
		sprintf(line, "alive jagframe %i pc %08X dumps %i\n",
			(int)frame, (unsigned)bigpemu_jag_m68k_get_pc(), (int)sDumps);
		{
			uint32_t n = 0;
			while (line[n]) n++;
			write_text("hlcine/alive.txt", line, n);
		}
	}

	if (frame < HL_FIRST || (frame % HL_EVERY) != 0)
	{
		return 0;
	}

	sprintf(path, "hlcine/ram%i.bin", (int)sDumps);
	if (!dump_region(path, HL_RAM, HL_RAMSIZE))
	{
		printf_notify("hl_cine: cannot write %s - does Scripts/hlcine exist?", path);
		sDone = 1;
		return 0;
	}

	sprintf(line, "ram%i jagframe %i pc %08X", (int)sDumps, (int)frame,
		(unsigned)bigpemu_jag_m68k_get_pc());
	meta_add(line);
	write_text("hlcine/meta.txt", sMeta, sMetaLen);

	sDumps++;
	printf_notify("hl_cine: dump %i of %i at jag frame %i",
		(int)sDumps, (int)HL_MAX, (int)frame);

	if (sDumps >= HL_MAX)
	{
		sDone = 1;
		printf_notify("hl_cine: DONE, %i dumps in Scripts/hlcine", (int)sDumps);
	}

	return 0;
}

void bigp_init()
{
	void *pMod = bigpemu_get_module_handle();
	sBuf = (uint8_t *)bigpemu_vm_alloc(HL_CHUNK);
	sMeta[0] = 0;
	sMetaLen = 0;
	sDumps = 0;
	sDone = 0;
	sOnFrameEvent = bigpemu_register_event_emu_thread_frame(pMod, on_emu_frame);
	printf_notify("hl_cine: RAM probe active, buf=%s", sBuf ? "ok" : "FAILED");
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
