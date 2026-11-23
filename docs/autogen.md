# The autogenerator

`autogen` (src/autogen.c) composes with geosim's composer (seed + mood),
then arranges the result for a band.

1. **Harmony read back from the score.** For every half bar, the pitch
   classes sounding on the pad, choir, counter voice, bass and arp are
   weighted by duration (bass and pads most), thresholded at 18 % of the
   strongest, and matched against every chord (missing tones, foreign
   tones, the bass on the root). Equal neighbours merge.
2. **Rhythm guitars.** A pattern per section (picking in intros, chugs or
   punk 8ths in verses, open strums in choruses, whole notes in
   breakdowns, 16th arpeggios in the arp solo), a voicing per chord from
   the library chosen near the fretting hand (small movements), power
   chords in the loud sections of distorted guitars. Band guitars play
   triads (sevenths and added notes dropped). The rock and metal bands
   double-track: left low, right higher up the neck.
3. **Lead guitar** plays the composer's lead line in 52–86: vibrato on
   long notes, hammer-ons on quick steps, slides into leaps, bends into
   the last long note of a phrase.
4. **Bass guitar** plays the composer's bass line in E1–E3, staccato on
   short notes.

| band | keeps from the composer | adds |
|---|---|---|
| synth+guitar | everything | a clean or steel rhythm guitar |
| rock | drums, fx, a quiet pad | bass, two crunch guitars, lead |
| metal | drums, fx | bass, two high-gain chugging guitars, lead |
| acoustic | quieter drums, a quiet pad | bass, steel (or 12-string) strums, nylon lead |
| electronic | everything | nothing |

The arranger draws from its own random stream, so the composition for a
seed and mood is geosim's, whatever the band.
