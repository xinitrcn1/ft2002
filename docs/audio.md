# Audio

## The player (src/synth.c)

geosim's player, ported statement for statement (its docs/14-audio.md has
the sound design and the measurements): a tracker sequencer at 6 ticks a
16th; tonal voices of two PolyBLEP oscillators (saw, pulse with PWM,
triangle, sine, noise, the old 8-bit tables), up to 4 extra unison saws, a
sine sub, tanh drive, formant band-passes for the choir, a 12 or 24 dB
TPT state-variable low-pass with its own envelope; synthesised drums
(kick, snare, clap, 808-style metal hats and crash, toms, rim, shaker,
risers, downlifters, impacts, bus low-pass sweeps); per track gain, pan,
sidechain duck, stereo chorus, high-pass and sends; a ping-pong delay, a
Freeverb-style reverb, a DC blocker, a look-ahead limiter at -1 dBFS and
a soft clip.

What the workstation adds:
- tracks: up to 16, each a synth, a kit or a guitar (`gtr_render`, one
  GuitarState of 12 strings per track, allocated up front);
- the song is read live: patch and mix edits are heard at once (the
  derived filter / pan coefficients are refreshed every block);
- `player_seek`, a loop range, `running` separate from the attached song,
  mute and solo masks, decaying peak meters per track and per side, a
  metronome click after the effects;
- `player_preview`: notes and strums from the UI (the chord palette, a
  piano key, a tab note) on top of the song; a queue of 64 handed to the
  audio thread under the stream lock.

## Live (src/audio.c)

An SDL3 audio stream (float stereo, 48 kHz, 512-frame device buffers)
whose callback renders the player in 1024-frame blocks. The UI changes
the song only between `audio_lock` / `audio_unlock`
(`SDL_LockAudioStream`, the lock the callback runs under), so every edit
is atomic to the player; edits copy a few bytes, so the lock is short.

Without a device (headless runs, `--headless`), `audio_pump` renders the
wall-clock time since the last frame into a scratch buffer, so the
playhead and meters still move and previews still run.
