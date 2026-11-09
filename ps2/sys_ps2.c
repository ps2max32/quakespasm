/* sys_ps2.c: Quakespasm's system layer on maxsdk (sys_sdl_unix.c and
   main_sdl.c of the SDL build): main, time, files (stdio on the IOP's
   file server through ps2_file.c), errors. */

#include <mx/mx.h>
#include <mx/hires.h>
#include <mx/prof.h>
#include <mx/sif.h>
#include <mx/vm.h>
#include <mxgl.h>

#include "quakedef.h"

void mx_exit(int status) __attribute__((noreturn));

/* frames to run before the program ends by itself (0: never) with a
   picture, and more of the command line; set by the Makefile */
#include "runcfg.h"

qboolean isDedicated;
cvar_t sys_throttle = {"sys_throttle", "0.02", CVAR_ARCHIVE};

/* ---- files ------------------------------------------------------------------ */

#define MAX_HANDLES 32
static FILE *sys_handles[MAX_HANDLES];

static int findhandle(void)
{
	for (int i = 1; i < MAX_HANDLES; i++)
		if (!sys_handles[i])
			return i;
	Sys_Error("out of handles");
	return -1;
}

long Sys_filelength(FILE *f)
{
	long pos = ftell(f);
	fseek(f, 0, SEEK_END);
	long end = ftell(f);
	fseek(f, pos, SEEK_SET);
	return end;
}

int Sys_FileOpenRead(const char *path, int *hndl)
{
	int i = findhandle();
	FILE *f = fopen(path, "rb");
	if (!f) {
		*hndl = -1;
		return -1;
	}
	sys_handles[i] = f;
	*hndl = i;
	return Sys_filelength(f);
}

int Sys_FileOpenWrite(const char *path)
{
	int i = findhandle();
	FILE *f = fopen(path, "wb");
	if (!f)
		Sys_Error("Error opening %s", path);
	sys_handles[i] = f;
	return i;
}

void Sys_FileClose(int handle)
{
	fclose(sys_handles[handle]);
	sys_handles[handle] = NULL;
}
void Sys_FileSeek(int handle, int position) { fseek(sys_handles[handle], position, SEEK_SET); }
int Sys_FileRead(int handle, void *dest, int count) { return fread(dest, 1, count, sys_handles[handle]); }
int Sys_FileWrite(int handle, const void *data, int count)
{
	return fwrite(data, 1, count, sys_handles[handle]);
}

/* the path as the file server takes it: "./id1/x" -> "id1/x" */
static const char *fio_path(const char *path)
{
	while (path[0] == '.' && path[1] == '/')
		path += 2;
	return path;
}

int Sys_FileType(const char *path)
{
	path = fio_path(path);
	int fd = mx_fio_open(path, MX_FIO_RDONLY);
	if (fd >= 0) {
		mx_fio_close(fd);
		return FS_ENT_FILE;
	}
	fd = mx_fio_dopen(*path ? path : ".");
	if (fd >= 0) {
		mx_fio_dclose(fd);
		return FS_ENT_DIRECTORY;
	}
	return FS_ENT_NONE;
}

/* a directory that is there already is no error */
void Sys_mkdir(const char *path) { mx_fio_mkdir(fio_path(path), 0777); }

/* ---- the system -------------------------------------------------------------- */

void Sys_Init(void)
{
	host_parms->basedir = ".";
	host_parms->userdir = host_parms->basedir; /* code elsewhere relies on this */
	host_parms->numcpus = 1;
}

void Sys_Error(const char *error, ...)
{
	va_list ap;
	char text[1024];
	host_parms->errstate++;
	va_start(ap, error);
	q_vsnprintf(text, sizeof text, error, ap);
	va_end(ap);
	mx_printf("Sys_Error: %s\n", text);
	mx_exit(1);
}

void Sys_Printf(const char *fmt, ...)
{
	va_list ap;
	char text[2048];
	va_start(ap, fmt);
	q_vsnprintf(text, sizeof text, fmt, ap);
	va_end(ap);
	/* Quake's high-bit characters (coloured text) as plain ones */
	for (char *p = text; *p; p++)
		*p &= 0x7f;
	mx_printf("%s", text);
}

void Sys_Quit(void)
{
	Host_Shutdown();
	mx_printf("Sys_Quit\n");
	mx_exit(0);
}

static u64 cycles(void)
{
	static u32 last, high;
	u32 c = mx_count();
	if (c < last)
		high++;
	last = c;
	return (u64)high << 32 | c;
}

#if PS2_FIXED_FPS
/* FIXEDFPS: each frame 1/PS2_FIXED_FPS of a second (the PC reference
   build's time) */
static int frames_run;
double Sys_DoubleTime(void) { return (double)frames_run / PS2_FIXED_FPS; }
#else
double Sys_DoubleTime(void)
{
	static u64 base;
	u64 c = cycles();
	if (!base)
		base = c;
	return (double)(c - base) / MX_EE_HZ;
}
#endif

const char *Sys_ConsoleInput(void) { return NULL; }
void Sys_Sleep(unsigned long msecs) {}

void Sys_SendKeyEvents(void) { IN_SendKeyEvents(); }

/* console.c's log (-condebug) only: no POSIX files here */
int open(const char *path, int flags, ...) { return -1; }

/* ---- platform (platform.h) ----------------------------------------------------- */

void PL_SetWindowIcon(void) {}
void PL_VID_Shutdown(void) {}
char *PL_GetClipboardData(void) { return NULL; }
void PL_ErrorDialog(const char *text) {}

/* ---- main --------------------------------------------------------------------- */

/* maxsdk's virtual memory (<mx/vm.h>): malloc (the hunk) in up to 128 MiB,
   paged to a swap file next to the game's data; what the GS reads by DMA
   (libgl: arenas, textures) is pinned memory, frames taken as needed.
   MX_MEMORY=phys: no VM, everything in the 32 MiB */
#if PS2_VM
const struct mx_vm_config mx_vm_config = {
	.swap = "quakespasm.swp",
};
#endif

static quakeparms_t parms;

int main(int argc, char **argv)
{
	/* the command line: the PS2's settings, then PS2_ARGS */
	/* r_oldwater 1: the liquids warped per vertex; 0 draws them into
	   textures each frame (glCopyTexSubImage2D: a read back of the frame
	   from GS memory each time on libgl, black with beam racing) */
	/* -mixspeed 11025: the game's sounds are 11025 Hz and the mixer
	   resamples by repeating samples, so mixing at more gives the same
	   sound for more EE time (audsrv plays it at 48 kHz) */
	/* -particles 4096, -consize 64 (KiB), max_edicts 2048: Quakespasm's
	   16384, 1024 and 8192 take 3 MiB of the hunk, zeroed (pages in RAM;
	   with them the timedemo pages to the swap file); id1 has at most 600
	   entities, GLQuake 2048 particles */
	static char line[512] = "quakespasm -basedir . -mixspeed 11025 -particles 4096 -consize 64 +max_edicts 2048 +r_oldwater 1 " PS2_ARGS;
	static char *args[64];
	int n = 0;
	for (char *p = strtok(line, " "); p && n < 63; p = strtok(NULL, " "))
		args[n++] = p;

	host_parms = &parms;
	parms.basedir = ".";
	parms.argc = n;
	parms.argv = args;
	parms.errstate = 0;
	COM_InitArgv(parms.argc, parms.argv);
	isDedicated = false;
	Sys_Init();
	Sys_Printf("Initializing QuakeSpasm v%s\n", QUAKESPASM_VER_STRING);

	/* the hunk: 24 MiB (the SDL build's 384 is for the PC; with 16, GLQuake's,
	   the models' cache is evicted every frame of demo1: each frame loads
	   them again), or -heapsize <KiB>; with VM only the pages used take
	   RAM */
	parms.memsize = 24 * 1024 * 1024;
	int t = COM_CheckParm("-heapsize");
	if (t && t + 1 < com_argc)
		parms.memsize = Q_atoi(com_argv[t + 1]) * 1024;
	parms.membase = malloc(parms.memsize);
	if (!parms.membase)
		Sys_Error("no memory for the hunk");
	mx_printf("Quakespasm on maxsdk: hunk %d MiB, virtual memory %s\n", parms.memsize >> 20,
	          mx_vm_active() ? "on" : "off");
	Host_Init();

	/* frames: those Host_Frame drew (host_framecount; it skips the calls
	   that come sooner than host_maxfps allows) */
	double oldtime = Sys_DoubleTime();
	int last = -1;
	for (;;) {
#if PS2_FIXED_FPS
		frames_run++;
#endif
		double newtime = Sys_DoubleTime();
		Host_Frame(newtime - oldtime);
		oldtime = newtime;
		/* a timedemo (ARGS="+timedemo demo1") ends the program when it is
		   done: its result is the console's last line (PROF=1: and its
		   profile) */
		{
			static int in_timedemo;
			if (cls.timedemo && !in_timedemo) {
				in_timedemo = 1;
#if PS2_PROF
				mx_prof_start(2000); /* PROF=1: the profile of the timedemo */
#endif
			} else if (!cls.timedemo && in_timedemo) {
				void PS2_SoundReport(void);
				if (snd_ps2_voices)
					PS2_SoundReport();
#if PS2_PROF
				mx_prof_stop();
				mx_prof_report(4000);
#endif
				mx_exit(0);
			}
		}
		if (host_framecount == last)
			continue;
		last = host_framecount;
		if (last % 60 == 0)
			mx_printf("ps2: frame %d at %d ms\n", last, (int)(newtime * 1000));
#if PS2_PROF
		/* the profile of the second half of the run */
		if (PS2_FRAMES && last == PS2_FRAMES / 2)
			mx_prof_start(2000);
#endif
		if (PS2_FRAMES && last + 1 >= PS2_FRAMES) {
#if PS2_PROF
			mx_prof_stop();
			mx_prof_report(4000);
#endif
			if (mxgl_mode_flags() & MXGL_BEAM)
				mx_printf("ps2: beam racing: largest record %u KB, frames that did not fit %u\n",
				          mx_hires_stats.rec_max / 1024, mx_hires_stats.rec_full);
			int r = mxgl_screenshot("quakespasm.tga");
			mx_printf("ps2: %d frames, picture quakespasm.tga: %d\n", last + 1, r);
			mx_exit(0);
		}
	}
	return 0;
}
