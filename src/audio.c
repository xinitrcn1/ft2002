/* Live audio (audio.h). */
#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <SDL3/SDL.h>

struct Audio {
	SDL_AudioStream *stream;
	Player *player;
	int rate;
	char name[128];
	u64 last_ns;
	float scratch[1024 * 2];
};

static void SDLCALL feed(void *user, SDL_AudioStream *stream, int additional, int total)
{
	(void)total;
	Audio *a = user;
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
		/* small buffers: the chord palette should answer at once */
		SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
		a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, a);
		if (a->stream) {
			const char *dn = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(a->stream));
			snprintf(a->name, sizeof a->name, "%s (%s)", dn ? dn : "audio", SDL_GetCurrentAudioDriver());
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
