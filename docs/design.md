# FT2002 — design

## What it is

A VST-like instrument host, but for guitars first: a project is a set of
tracks, each one an instrument (a modelled guitar or bass, a synth, a drum
kit) with its own patch and mix settings, playing a score of notes and
chords on a 16th-note grid (6 ticks a row, tracker style).

- **Guitars** are physical models, not samples: extended Karplus–Strong
  strings, pick position and direction, body or amp + cabinet, per-string
  voices (a string rings one note at a time), strums with a per-string
  delay, and articulations (palm mute, let ring, staccato, harmonics, dead
  notes, slides, bends, hammer-ons, vibrato, feedback sustain).
- **Chords** come from a pre-generated library: every root × quality,
  voiced across the fretboard (open shapes, barre shapes, power chords,
  partial voicings), ranked by playability, plus strum patterns.
- **Other instruments**: geosim's subtractive synth (supersaw, PWM,
  formant choir, 24 dB filter) and its drum models.
- **Autogenerator**: geosim's composer writes form, harmony, motifs and
  grooves from a seed and a mood; FT2002 adds guitar parts on top
  (rhythm strums of the progression, power-chord riffs, arpeggiated
  picking, lead lines).
- **Export**: WAV, MP3 (LAME) and Opus (libopus in an Ogg stream written
  here), rendered offline faster than real time.

## Decisions

- C23, CMake + ctest, Vulkan for the UI, SDL3 for the window and audio
  (the base of freeroamtest).
- The UI is vxide's layer: square, VSCodium Dark/Light Modern colours,
  immediate-mode widgets on top (knobs, sliders, spins, toggles).
- One data model for composed songs and projects (src/song.h): the
  composer's channels are a project's first tracks.
- Nothing is sampled; renders are deterministic (same project, same bytes).

## Plan (one commit each)

1. Skeleton: build, the UI layer, the workbench shell, headless shots.
2. The synth and the composer from geosim, offline WAV rendering.
3. Guitars: the string model, bodies, amps, articulations.
4. Chords: the voicing library, strums.
5. Projects: tracks, editing, save / load.
6. Live audio and the transport.
7. The workbench views: arrangement, chord palette, instrument panel, mixer.
8. The autogenerator.
9. Export: MP3 and Opus.
