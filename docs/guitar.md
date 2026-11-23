# Guitars

## The string model (src/guitar.c)

Each string is an extended Karplus–Strong loop (Jaffe & Smith, "Extensions
of the Karplus-Strong Plucked-String Algorithm", CMJ 1983):

- a delay line one period long, read with a 4-point Lagrange fractional
  delay, so bends, slides and vibrato move the pitch continuously;
- a one-pole loop low-pass (brightness decays faster than the
  fundamental), its phase delay at the pitch subtracted from the line;
- a loop gain from the wanted T60 of the fundamental, corrected for the
  low-pass's gain there. T60 scales with 1/sqrt(f0) (low strings ring
  longer) and the patch's sustain;
- a pluck injects one period of excitation: a displaced triangle at the
  pick position plus noise, low-passed forward and backward by hardness
  (tone, velocity, up/down stroke), then comb-filtered at the pick
  position (harmonics with a node there are missing). Re-plucking a
  ringing string first damps it (the pick stops it);
- the DC blocker sits outside the loop: inside, its phase lead detuned the
  strings by 5–12 cents.

Measured (tests/test_guitar.c): single notes E2–E5 on the clean electric
and the steel acoustic are within 3 cents (autocorrelation pitch, 0.3 s
in), every note decays, renders are deterministic.

A string rings one note at a time. Chords (`FX_STRUM`) give a note per
string; the strum spreads them `strum` ms apart, down (low to high) or up
(high to low, a little softer on the bass strings). Single notes take
their string from the event or from the fretting hand's position (the
nearest fret, open strings cheap), and the hand follows the music.

## Articulations

| | |
|---|---|
| palm | T60 ~0.2 s, dark loop filter, damped at note-off in 50 ms |
| ring | ignores note-off: rings until the string is struck again |
| staccato | damped 20 ms after note-off |
| harmonic | the octave, picked at the node: pure, longer |
| dead | a noise burst through a fast-dying, dark string |
| slide / bend | pitch glides from notes[0] to notes[1] (bends ease out) |
| hammer | no pick: a ringing string is retuned and nudged (legato) |
| vibrato | finger vibrato (also on long notes of the lead model) |
| feedback | a bounded loop gain above 1, the octave harmonic blooming in after 0.8 s |

## Bodies, amps, cabinets

- Acoustic (steel, nylon, 12-string): the dry string plus four resonant
  band-passes (air mode ~98 Hz, top-plate modes ~204 and ~395 Hz, a
  bright band ~2.6 kHz).
- Electric: a magnetic pickup is a comb on the output at its position;
  then the amp: high-pass, pre-gain (1 + 60 gain²), asymmetric tanh
  clipping, a tone stack (bass shelf, a mid scoop under gain, treble), a
  second gain stage for the lead model; the cabinet: 80 Hz high-pass,
  5.2 kHz 4-pole low-pass, a 2.3 kHz presence peak.
- Bass: 4 strings E1–G2, 35 Hz high-pass, 3.5 kHz cabinet.
- 12-string: the four low courses get an octave string, the top two a
  unison string, struck 1.5 ms after (before, upstrokes) and 3 cents off.

Strummed G (every articulation in turn), the models sit within 3 dB:
RMS −11.8 (nylon) to −14.3 dBFS (steel).

## Chords (src/chords.c)

The library is generated for the instrument's tuning when a track is
selected: every root × 20 qualities (maj, m, 5, 7, maj7, m7, sus2, sus4,
dim, aug, add9, 6, m6, 9, m9, 7sus4, dim7, m7b5, maj9, madd9), searched
over 4-fret windows from the nut to the 12th fret. A voicing must have
the root in the bass, every essential tone (fifths may go in 7th and 9th
chords), at most 4 fingers (a barre counts as one), a 4-fret span and at
most one muted string inside. They are ranked by position, stretch,
fingers, barre, fullness, open strings, doubled thirds and unisons, and
the 16 best kept: 3807 voicings for standard tuning.

The classic shapes come first by themselves: C x32010, D xx0232,
A x02220, E 022100, Am x02210, Em 022000, Dm xx0231, E7 020100,
Cmaj7 x32000; F 133211 and Bm x2443x among the first three.

Strum patterns are strings of 16ths (`D` down, `u` up, `x` scratch, `H`
chug, `B`/`A` bass and alternate bass, `0`–`5` picked strings, `P`
pinch): folk, pop, island, rock 8ths and chugs, punk, gallop, metal
16ths, funk, reggae, ska, shuffle, arpeggios, Travis picking. See
`ft2002-cli patterns`.
