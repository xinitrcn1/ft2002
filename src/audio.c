/* Live audio (audio.h). */
#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

struct Audio {
	SDL_AudioStream *stream;
	Player *player;
	int rate;
	char name[128];
	u64 last_ns;
	int period;      /* the device's buffer, frames */
	bool ftz;
	float scratch[1024 * 2];
};

/* Decaying filters and reverb tails reach denormal floats, which are slow
 * on x86: the audio thread flushes them to zero (set once, per thread). */
void audio_flush_denormals(void)
{
#if defined(__SSE__) || defined(__x86_64__)
	_mm_setcsr(_mm_getcsr() | 0x8040); /* FTZ | DAZ */
#endif
}

static void SDLCALL feed(void *user, SDL_AudioStream *stream, int additional, int total)
{
	(void)total;
	Audio *a = user;
	if (!a->ftz) {
		audio_flush_denormals();
		a->ftz = true;
	}
	int frames = additional / (int)(2 * sizeof(float));
	while (frames > 0) {
		int n = frames < 1024 ? frames : 1024;
		player_render(a->player, a->scratch, n);
		SDL_PutAudioStreamData(stream, a->scratch, n * 2 * (int)sizeof(float));
		frames -= n;
	}
}

Audio *audio_open(bool device)
{
	Audio *a = calloc(1, sizeof *a);
	if (!a)
		return NULL;
	a->rate = 48000;
	snprintf(a->name, sizeof a->name, "no audio device");
	if (device && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		SDL_AudioSpec spec = {.format = SDL_AUDIO_F32, .channels = 2, .freq = 48000};
		/* The device buffer. 512 frames (10.7 ms) crackled: on FreeBSD's OSS
		 * ("dsp") the callback arrives 5-10 ms late and the driver holds two
		 * fragments; measured with sndctl's xruns over a rock song (work/clicks/
		 * live.sh): 512 frames 112 underruns in 11 s, 1024 none in 82 s.
		 * Rendering a period ahead into the stream made it worse (51 at 1024).
		 * FT_AUDIO_FRAMES overrides. */
		int frames = 1024;
		const char *env = getenv("FT_AUDIO_FRAMES");
		if (env && atoi(env) >= 64 && atoi(env) <= 16384)
			frames = atoi(env);
		char hint[16];
		snprintf(hint, sizeof hint, "%d", frames);
		SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, hint);
		a->period = frames;
		a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, a);
		if (a->stream) {
			SDL_AudioSpec dev;
			int sf = 0;
			if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(a->stream), &dev, &sf) && sf > 0)
				a->period = sf;
			const char *dn = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(a->stream));
			snprintf(a->name, sizeof a->name, "%s (%s, %d frames, %.0f ms)", dn ? dn : "audio", SDL_GetCurrentAudioDriver(),
			         a->period, a->period * 1000.0 / 48000.0);
		} else {
			fprintf(stderr, "ft2002: no audio: %s\n", SDL_GetError());
		}
	}
	a->player = player_new((float)a->rate);
	if (!a->player) {
		audio_close(a);
		return NULL;
	}
	player_set_intensity_now(a->player, 0.6f);
	a->last_ns = SDL_GetTicksNS();
	if (a->stream)
		SDL_ResumeAudioStreamDevice(a->stream);
	return a;
}

void audio_close(Audio *a)
{
	if (!a)
		return;
	if (a->stream)
		SDL_DestroyAudioStream(a->stream);
	player_free(a->player);
	free(a);
}

const char *audio_device_name(const Audio *a) { return a->name; }
int audio_rate(const Audio *a) { return a->rate; }

void audio_lock(Audio *a)
{
	if (a->stream)
		SDL_LockAudioStream(a->stream);
}

void audio_unlock(Audio *a)
{
	if (a->stream)
		SDL_UnlockAudioStream(a->stream);
}

Player *audio_player(Audio *a) { return a->player; }

void audio_pump(Audio *a)
{
	if (a->stream)
		return;
	u64 now = SDL_GetTicksNS();
	u64 frames = (now - a->last_ns) * (u64)a->rate / 1000000000ull;
	if (frames > (u64)a->rate / 4)
		frames = (u64)a->rate / 4; /* a stall: do not catch up for long */
	if (!frames)
		return;
	a->last_ns = now;
	while (frames) {
		int n = frames < 1024 ? (int)frames : 1024;
		player_render(a->player, a->scratch, n);
		frames -= (u64)n;
	}
}
