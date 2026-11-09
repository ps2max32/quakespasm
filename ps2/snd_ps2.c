/* snd_ps2.c: Quakespasm's sound on maxsdk (snd_sdl.c of the SDL build),
   through mx/snd.h (mxsnd on the IOP).

   The sound effects play on the SPU2's 48 voices: each sound is encoded to
   SPU ADPCM when it is loaded (PS2_SoundLoaded, from S_LoadSound: its own
   rate, its loop) and loaded into SPU RAM, the sounds played longest ago
   freed when it is full. Each update (SNDDMA_Submit) copies Quake's channels
   to mxsnd's: a channel started since (ps2_start) takes a voice, its
   volumes follow Quake's spatialization, it stops when Quake's channel
   does; mxsnd gives the voices by priority (the player's sounds, then the
   others', then the static and ambient ones) and loudness. The mixer
   (snd_mix.c) still runs, for the channels' positions (their ends, loops)
   and the music: what it paints, 16-bit stereo at snd_mixspeed (11025 Hz),
   goes to the PCM stream (about 100 ms queued). -eemix mixes the effects
   on the EE as Quake does (all of them through the stream). */

#include <mx/snd.h>

#include "quakedef.h"

#define FRAMES 4096 /* the ring: 0.37 s at 11025 Hz, a power of two */

extern int paintedtime;

qboolean snd_ps2_voices;
int snd_ps2_starts;

static s16 ring[FRAMES * 2] __attribute__((aligned(64)));
static int sent; /* frames sent to the stream, as paintedtime counts them */

/* ---- the sounds in SPU RAM ------------------------------------------------- */

struct ps2_sound {
	struct mx_snd_sample s;
	int used; /* the update it last played in */
};

#define MAX_LOADED 1024
static sfx_t *loaded[MAX_LOADED];
static int nloaded, updates;

static void unload(int i)
{
	struct ps2_sound *p = loaded[i]->ps2;
	mx_snd_free(&p->s);
	free(p);
	loaded[i]->ps2 = NULL;
	loaded[i] = loaded[--nloaded];
}

void PS2_SoundLoaded(sfx_t *s, const byte *data, const wavinfo_t *info)
{
	if (s->ps2 || nloaded == MAX_LOADED)
		return;
	int n = info->samples;
	s16 *pcm = malloc(n * sizeof *pcm);
	u8 *adpcm = malloc(MX_SND_ADPCM_SIZE(n));
	struct ps2_sound *p = calloc(1, sizeof *p);
	if (!pcm || !adpcm || !p)
		goto out;
	for (int i = 0; i < n; i++)
		pcm[i] = info->width == 2 ? (s16)LittleShort(((const short *)data)[i]) : (s16)((data[i] - 128) << 8);
	u32 bytes = info->loopstart >= 0 && info->loopstart < n ? mx_snd_encode_loop(pcm, n, adpcm, info->loopstart)
	                                                          : mx_snd_encode(pcm, n, adpcm, 0);
	/* SPU RAM full: the sounds played longest ago go */
	while (mx_snd_load(&p->s, adpcm, bytes, info->rate) < 0 && nloaded) {
		int old = 0;
		for (int i = 1; i < nloaded; i++)
			if (((struct ps2_sound *)loaded[i]->ps2)->used < ((struct ps2_sound *)loaded[old]->ps2)->used)
				old = i;
		unload(old);
	}
	if (p->s.spu) {
		p->used = updates;
		s->ps2 = p;
		loaded[nloaded++] = s;
		p = NULL;
	}
out:
	free(pcm);
	free(adpcm);
	free(p);
}

/* ---- Quake's channels on mxsnd's ------------------------------------------- */

static struct {
	int ch;    /* mxsnd's channel, 0: none */
	int start; /* the ps2_start it plays */
	sfx_t *sfx;
} chans[MAX_CHANNELS];

static int volume(int v)
{
	int x = (int)(v * sfxvolume.value * (MX_SND_VOL_MAX / 255.0f));
	return x < 0 ? 0 : x > MX_SND_VOL_MAX ? MX_SND_VOL_MAX : x;
}

static void voices_update(void)
{
	updates++;
	for (int i = 0; i < total_channels && i < MAX_CHANNELS; i++) {
		channel_t *ch = &snd_channels[i];
		struct ps2_sound *p = ch->sfx ? ch->sfx->ps2 : NULL;
		/* a new sound on the channel, or none: the old one stops */
		if (chans[i].ch && (ch->sfx != chans[i].sfx || ch->ps2_start != chans[i].start || !p)) {
			mx_snd_ch_stop(chans[i].ch);
			chans[i].ch = 0;
		}
		if (!p) {
			chans[i].sfx = NULL;
			continue;
		}
		int l = volume(ch->leftvol), r = volume(ch->rightvol);
		int looping = p->s.loop;
		if (chans[i].ch && mx_snd_ch_set(chans[i].ch, p->s.rate, l, r) == 0) {
			p->used = updates;
			continue;
		}
		chans[i].ch = 0;
		/* a voice for a sound started since, or a loop that is heard; a
		   one-shot that lost its voice stays without */
		if ((l || r) && (ch->ps2_start != chans[i].start || looping)) {
			int prio = i < NUM_AMBIENTS ? 1
			         : i >= NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS ? 1
			         : ch->entnum == cl.viewentity ? 3 : 2;
			chans[i].ch = mx_snd_ch_play(&p->s, p->s.rate, l, r, prio);
			p->used = updates;
		}
		chans[i].sfx = ch->sfx;
		chans[i].start = ch->ps2_start;
	}
	/* channels past total_channels (cleared: S_StopAllSounds) */
	for (int i = total_channels; i < MAX_CHANNELS && chans[i].sfx; i++) {
		if (chans[i].ch)
			mx_snd_ch_stop(chans[i].ch);
		chans[i].ch = 0, chans[i].sfx = NULL;
	}
}

/* for a timedemo's end (sys_ps2.c): the sounds in SPU RAM, the channels
   sounding, and the loudest of the last 512 samples of each core's voices */
void PS2_SoundReport(void)
{
	extern int mx__snd_peek(u32 spu, void *dst, u32 bytes);
	static s16 m[512] __attribute__((aligned(64)));
	u32 bytes = 0;
	int on = 0, peak[2] = {0, 0};
	for (int i = 0; i < nloaded; i++)
		bytes += ((struct ps2_sound *)loaded[i]->ps2)->s.size;
	for (int i = 0; i < MAX_CHANNELS; i++)
		on += chans[i].ch && mx_snd_ch_playing(chans[i].ch);
	for (int c = 0; c < 2; c++)
		if (mx__snd_peek(c ? 0x3000 : 0x2000, m, sizeof m) == 0)
			for (int i = 0; i < 512; i++)
				peak[c] = abs(m[i]) > peak[c] ? abs(m[i]) : peak[c];
	Con_Printf("ps2: sound: %d sounds in SPU RAM (%u KB), %d channels sounding, voice mix peaks %d %d\n",
	           nloaded, bytes / 1024, on, peak[0], peak[1]);
}

/* ---- the stream ---------------------------------------------------------------- */

qboolean SNDDMA_Init(dma_t *dma)
{
	int rate = (int)snd_mixspeed.value;
	if (mx_snd_init() < 0 || mx_snd_stream_format(rate, 16, 2) < 0) {
		Con_Printf("SNDDMA_Init: no sound module\n");
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
	snd_ps2_voices = !COM_CheckParm("-eemix");
	Con_Printf("Sound: %d Hz 16-bit stereo stream, effects %s\n", rate,
	           snd_ps2_voices ? "on the SPU2's voices" : "mixed on the EE");
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
	if (snd_ps2_voices)
		voices_update();
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
	mx_snd_commit(); /* the channels' changes, if the stream sent nothing */
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
