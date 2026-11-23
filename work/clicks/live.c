/* Live playback on the real device: a generated rock song for N seconds
 * through audio.c, with the UI's kind of lock traffic (a lock / unlock
 * every 16 ms, a preview strum every 250 ms). Read the device's xruns with
 * sndctl while it plays (work/clicks/live.sh). */
#include "audio.h"
#include "autogen.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 10;
	Audio *a = audio_open(true);
	printf("%s\n", audio_device_name(a));
	Song s;
	AutogenOptions o = {.seed = 2327, .mood = MOOD_WARTIME, .band = BAND_ROCK, .key = -1};
	autogen(&s, &o);
	audio_lock(a);
	player_set_song(audio_player(a), &s);
	audio_player(a)->volume_target = 0.0f; /* silent: a test, not a concert */
	audio_unlock(a);
	u64 t0 = SDL_GetTicks();
	int k = 0;
	while (SDL_GetTicks() - t0 < (u64)secs * 1000) {
		SDL_Delay(16);
		audio_lock(a);
		s.mix.gain[0] = s.mix.gain[0];
		if (++k % 16 == 0) {
			NoteEvent e = note_event();
			e.ch = 6, e.fx = FX_STRUM, e.count = 6, e.strum = 8, e.len = 48;
			u8 nn[6] = {40, 47, 52, 0, 0, 0};
			memcpy(e.notes, nn, 6);
			player_preview(audio_player(a), &e);
		}
		audio_unlock(a);
	}
	audio_close(a);
	return 0;
}
