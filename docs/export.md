# Rendering and export

`export_song` (src/export.c) plays a copy of the song through its own
player, faster than real time (a 3.5-minute rock-band song: ~20 s with
the MP3 encoder), adds a tail (2.5 s) for reverb and decays, and measures
peak, RMS, clipped and non-finite samples.

- **WAV**: 16-bit PCM, 44.1 or 48 kHz.
- **MP3**: LAME; VBR (V2 by default, or the V level nearest a bitrate) or
  CBR; an ID3v2 title / artist / comment; the LAME/Xing tag is rewritten
  at the end, so players know the exact length (gapless).
- **Opus**: libopus at 48 kHz (160 kbps VBR by default, complexity 10,
  music signal) in an Ogg stream written here with libogg (RFC 7845):
  an OpusHead with the encoder's lookahead as pre-skip, OpusTags (title,
  encoder, artist), 20 ms packets, and the last page's granule position
  trimming the padding, so the decoded length is exact.

stb (used for the PNG screenshots) has no audio encoders; LAME, libopus
and libogg are system packages (`lame`, `opus`, `libogg`).

Checked with ffprobe / ffmpeg: both decode cleanly; durations equal the
render (212.500 s MP3, 75.727 s Opus); a project saved, loaded and
rendered again gives a byte-identical Opus file.
