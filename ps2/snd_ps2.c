/* snd_ps2.c: Quakespasm's sound output on maxsdk (snd_sdl.c of the SDL
   build): the mixer (snd_dma.c, snd_mix.c) paints into a ring here, 16-bit
   stereo at snd_mixspeed (11025 Hz here), and SNDDMA_Submit sends what it
   has painted since the last call (up to paintedtime) to maxsdk's PCM
   stream (mx/snd.h: audsrv, which plays it at 48 kHz), as far as the
   stream has room; SNDDMA_GetDMAPos reports what has been sent as played.
   The sound plays the stream's ring later (about 100 ms). */

#include <mx/snd.h>

#include "quakedef.h"

#define FRAMES 4096 /* the ring: 0.37 s at 11025 Hz, a power of two */

extern int paintedtime;

static s16 ring[FRAMES * 2] __attribute__((aligned(64)));
static int sent; /* frames sent to the stream, as paintedtime counts them */

qboolean SNDDMA_Init(dma_t *dma)
{
	int rate = (int)snd_mixspeed.value;
	if (mx_snd_init() < 0 || mx_snd_stream_format(rate, 16, 2) < 0) {
		Con_Printf("SNDDMA_Init: no audsrv\n");
		return false;
	}
	memset(dma, 0, sizeof *dma);
	shm = dma;
	shm->channels = 2;
	shm->samplebits = 16;
	shm->speed = rate;
	shm->samples = FRAMES * 2;
	shm->samplepos = 0;
	shm->submission_chunk = 1;
	shm->buffer = (unsigned char *)ring;
	sent = 0;
	Con_Printf("Sound: %d Hz 16-bit stereo through audsrv\n", rate);
	return true;
}

int SNDDMA_GetDMAPos(void)
{
	shm->samplepos = (sent * 2) & (FRAMES * 2 - 1);
	return shm->samplepos;
}

void SNDDMA_LockBuffer(void) {}

void SNDDMA_Submit(void)
{
	if (paintedtime < sent) /* snd_dma.c chopped its counters */
		sent = paintedtime;
	int n = paintedtime - sent;
	int room = (int)(mx_snd_stream_room() / 4);
	if (n > room)
		n = room;
	while (n > 0) {
		int at = sent & (FRAMES - 1);
		int k = FRAMES - at < n ? FRAMES - at : n;
		int took = (int)(mx_snd_stream_queue(ring + at * 2, (u32)k * 4) / 4);
		sent += took;
		n -= took;
		if (took < k)
			break;
	}
}

void SNDDMA_BlockSound(void) {}
void SNDDMA_UnblockSound(void) {}

void SNDDMA_Shutdown(void)
{
	if (shm) {
		mx_snd_stream_stop();
		shm = NULL;
	}
}
