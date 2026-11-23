# ft2002-cli

The workstation without a window (src/cli.c):

    ft2002-cli gen --seed 7 --mood triumph --band rock -o song.mp3   # compose + render
    ft2002-cli gen --seed 7 --mood calm --band acoustic -o song.ft2  # compose, save the project
    ft2002-cli render song.ft2 -o song.opus [--bitrate 192] [--cbr] [--rate 44100] [--solo 3]
    ft2002-cli info song.ft2         # form, tracks, the chords read from the score
    ft2002-cli chords C G Am F#m7    # the library's voicings, best first
    ft2002-cli patterns              # the strum patterns
