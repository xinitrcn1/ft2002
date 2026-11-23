# The workbench

The drawing layer is vxide's (gpu.c, font.c, draw.c, theme.c, menu.c):
everything is square quads in one instanced draw, VSCodium's Dark Modern /
Light Modern colours. On top, ui.c has the shell and immediate-mode
widgets (button, icon button, knob, slider, toggle, spin, text field):
input is gathered between frames, and each widget asks while it draws
whether it was clicked or dragged. Knob drags record one undo step
("~name" edits coalesce while they repeat).

| place | what |
|---|---|
| menu bar | File, Edit, Track, Generate, Export, View, Help; the project's name (● modified) |
| transport | to start, play / pause, stop, loop, metronome; bar.beat.16th and time; tempo; the section; master volume and output meters |
| activity bar | Tracks, Instrument, Chords, Mixer, Generate, Export, Settings |
| main area | the arrangement: sections and bars on the ruler (click seeks, Shift+drag sets the loop), a lane per track, the cursor and the playhead; wheel scrolls, Ctrl+wheel zooms, Shift+wheel scrolls the lanes |
| bottom panel | the selected track's editor: tablature (guitars, basses), piano roll (synths), step grid (kits) |
| status bar | messages; key, tempo, length, tracks |

## Side views

- **Tracks**: select, mute, solo, volume, meters; add guitar / bass / synth / drums; delete.
- **Instrument**: presets; for guitars the model, tone, pick position,
  sustain, drive, body, level, capo, tunings (standard, drop D, half
  down, DADGAD, open G, open D) and the articulation for inserted notes;
  for synths oscillators, filter, amp, movement, 24 dB, mono, vowel; for
  kits the kick, snare and hats.
- **Chords**: 12 roots × 20 qualities, the library's voicings as chord
  boxes for the selected track's tuning (click strums it on the track,
  right click up-strums, double click inserts), the strum pattern and the
  length to insert at the cursor (Enter).
- **Mixer**: per track volume, pan, reverb, delay, chorus, high-pass; the
  effects bus (room, delay time in 16ths, feedback).
- **Generate**: seed (and a die), mood, band, tempo and key overrides; the
  current song's description.
- **Export**: MP3 / Opus / WAV, bitrate, VBR, sample rate, the file; the
  export runs on a thread with a progress bar and its report.
- **Settings**: open / save as / new, theme, follow the playhead,
  metronome, the intensity layers, the audio device.

## The editors

- **Tab**: a line per string, fret numbers at the notes, the stroke
  (↓ ↑) and articulation under each strum, the chord name where it
  changes. Single notes are placed on strings the way the player does
  (the hand's position). Click selects (and plays) a note or strum,
  double click inserts the palette chord, right click flips a strum, Del
  deletes, ←/→ move the cursor by a 16th (Shift: a bar), Enter inserts.
- **Piano roll**: keys preview, double click adds a beat-long note, drag
  moves, wheel scrolls the pitch.
- **Drum grid**: a row per drum, a cell per 16th; click toggles (Shift:
  softer).

## Scripts and tests

`ft2002 --headless WxH --script FILE` drives the UI (src/main.c lists the
commands: click, drag, wheel, key, type, wait, cmd, shot, expect-status).
`tests/t_ui.sh` (ctest `ui`) runs every script in tests/ui: a tour of the
views with screenshots, and an editing session (insert, undo, redo, a
drum hit, add and delete a track, an Opus export from the UI).
