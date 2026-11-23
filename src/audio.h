/* Live audio (docs/audio.md): an SDL3 audio stream whose callback runs the
 * player. The UI edits the song under audio_lock (SDL_LockAudioStream, the
 * lock the callback holds), so edits are atomic to the player and short.
 * Without a device (headless runs, no sound card) the player is driven by
 * the wall clock from audio_pump, so the transport still moves. */
#ifndef FT_AUDIO_H
#define FT_AUDIO_H

#include "synth.h"

typedef struct Audio Audio;

Audio *audio_open(bool device); /* device false: no SDL audio, audio_pump drives it */
void audio_close(Audio *a);
const char *audio_device_name(const Audio *a);
int audio_rate(const Audio *a);
void audio_lock(Audio *a);
void audio_unlock(Audio *a);
Player *audio_player(Audio *a); /* touch only under audio_lock */
/* No device: render the time since the last call (keeps meters and the
 * playhead moving). With a device: nothing. */
void audio_pump(Audio *a);

#endif
