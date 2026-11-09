/* sys_pc.c: the system layer of the PC build (pc/): the same engine
   (Quake/) on the host's OpenGL, off screen, as the reference picture for
   libgl. The same command line as ps2/sys_ps2.c, FRAMES and ARGS the same
   way; the picture of the last frame as quakespasm-pc.tga. Time is the
   frames': each frame 1/PC_FPS of a second (the PS2 build's FIXEDFPS), so
   a run gives the same picture each time. No sound, input or network. */

#include <dirent.h>
#include <sys/stat.h>

#include "quakedef.h"

#include "runcfg.h"

#ifndef PC_FPS
#define PC_FPS 50 /* PAL with beam racing: a frame a field */
#endif

int pc_screenshot(const char *path);

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

int Sys_FileType(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return FS_ENT_NONE;
	return S_ISDIR(st.st_mode) ? FS_ENT_DIRECTORY : S_ISREG(st.st_mode) ? FS_ENT_FILE : FS_ENT_NONE;
}

void Sys_mkdir(const char *path) { mkdir(path, 0777); }

/* ---- the system -------------------------------------------------------------- */

void Sys_Init(void)
{
	host_parms->basedir = ".";
	host_parms->userdir = host_parms->basedir;
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
	fprintf(stderr, "Sys_Error: %s\n", text);
	exit(1);
}

void Sys_Printf(const char *fmt, ...)
{
	va_list ap;
	char text[2048];
	va_start(ap, fmt);
	q_vsnprintf(text, sizeof text, fmt, ap);
	va_end(ap);
	for (char *p = text; *p; p++)
		*p &= 0x7f;
	fputs(text, stdout);
}

void Sys_Quit(void) { exit(0); }

static int frames_run;
double Sys_DoubleTime(void) { return (double)frames_run / PC_FPS; }

const char *Sys_ConsoleInput(void) { return NULL; }
void Sys_Sleep(unsigned long msecs) {}
void Sys_SendKeyEvents(void) {}

void PL_SetWindowIcon(void) {}
void PL_VID_Shutdown(void) {}
char *PL_GetClipboardData(void) { return NULL; }
void PL_ErrorDialog(const char *text) {}

/* ---- main --------------------------------------------------------------------- */

static quakeparms_t parms;

int main(int argc, char **argv)
{
	/* ps2/sys_ps2.c's command line (no sound here) */
	static char line[512] = "quakespasm -basedir . -mixspeed 11025 -particles 4096 -consize 64 +max_edicts 2048 +r_oldwater 1 " PS2_ARGS;
	static char *args[64];
	int n = 0;
	for (char *p = strtok(line, " "); p && n < 63; p = strtok(NULL, " "))
		args[n++] = p;

	host_parms = &parms;
	parms.basedir = ".";
	parms.argc = n;
	parms.argv = args;
	COM_InitArgv(parms.argc, parms.argv);
	Sys_Init();
	parms.memsize = 24 * 1024 * 1024; /* as ps2/sys_ps2.c */
	parms.membase = malloc(parms.memsize);
	Host_Init();

	double oldtime = Sys_DoubleTime();
	int last = -1;
	for (;;) {
		frames_run++;
		double newtime = Sys_DoubleTime();
		Host_Frame(newtime - oldtime);
		oldtime = newtime;
		if (host_framecount == last)
			continue;
		last = host_framecount;
		if (PS2_FRAMES && last + 1 >= PS2_FRAMES) {
			int r = pc_screenshot("quakespasm-pc.tga");
			printf("pc: %d frames, picture quakespasm-pc.tga: %d\n", last + 1, r);
			exit(0);
		}
	}
	return 0;
}

/* ---- no sound, input or network ---------------------------------------------- */

qboolean SNDDMA_Init(dma_t *dma) { return false; }
int SNDDMA_GetDMAPos(void) { return 0; }
void SNDDMA_LockBuffer(void) {}
void SNDDMA_Submit(void) {}
void SNDDMA_BlockSound(void) {}
void SNDDMA_UnblockSound(void) {}
void SNDDMA_Shutdown(void) {}

void IN_Init(void) {}
void IN_Shutdown(void) {}
void IN_Commands(void) {}
void IN_Move(usercmd_t *cmd) {}
void IN_MouseMotion(int dx, int dy) {}
void IN_SendKeyEvents(void) {}
void IN_UpdateInputMode(void) {}
void IN_ClearStates(void) {}
void IN_Activate(void) {}
void IN_Deactivate(qboolean free_cursor) {}
