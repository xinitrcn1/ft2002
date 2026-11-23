# The autogenerator

`autogen` (src/autogen.c) composes with geosim's composer (seed + mood),
then arranges the result for a band.

1. **Harmony read back from the score.** For every half bar, the pitch
   classes sounding on the pad, choir, counter voice, bass and arp are
   weighted by duration (bass and pads most), thresholded at 18 % of the
   strongest, and matched against every chord (missing tones, foreign
   tones, the bass on the root). Equal neighbours merge.
2. **Rhythm guitars.** A pattern per section from a list for the band and
   the section (picking and rolls in intros, chugs, pushes and shuffles in
   verses, open strums and calypso in choruses, whole notes and slow
   arpeggios in breakdowns, 16th cascades in the arp solo); a repeated
   section keeps its pattern half the time and takes the next one
   otherwise; in most sections of 8 bars or more the last two bars of each
   phrase take a busier fill. Patterns are locked to the bar, and a chord
   change on a rest of the pattern is struck anyway. a voicing per chord from
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

The arranger draws from its own random stream. With "More variations"
(the default; `ft2002-cli gen --plain` turns it off) the composition uses
`compose_ex`'s extra progressions and arps (docs/composer.md); without
it, the composition for a seed and mood is geosim's, whatever the band.
