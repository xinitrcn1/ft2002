/* Offline rendering and export (docs/export.md): plays a song through its
 * own player faster than real time and writes WAV (16-bit PCM), MP3 (LAME,
 * CBR or VBR, an ID3v2 title) or Opus (libopus in an Ogg stream written
 * here: OpusHead, OpusTags, 20 ms packets, exact end trimming through the
 * last granule position). Also measures the result. */
#ifndef FT_EXPORT_H
#define FT_EXPORT_H

#include "song.h"

typedef enum : u8 { EXP_WAV, EXP_MP3, EXP_OPUS, EXP_COUNT } ExportFormat;

typedef struct {
	ExportFormat fmt;
	int sample_rate;   /* WAV / MP3: 44100 or 48000; Opus always codes 48000 */
	int bitrate_kbps;  /* MP3 / Opus; 0 = the default (MP3 V2 VBR, Opus 160) */
	bool vbr;          /* MP3: VBR (quality from bitrate: 0 -> V2) */
	float tail_s;      /* after the last tick, for reverb and decays (default 2.5) */
	float volume;      /* master (default 0.8) */
	float intensity;   /* the live intensity layers (default 0.6) */
	u32 solo;          /* track mask, 0 = all */
	const char *title; /* tags; NULL = the song's style + seed */
	const char *artist;
} ExportOptions;

typedef struct {
	double seconds;
	double peak_dbfs, rms_dbfs;
	u64 clipped, non_finite; /* samples: |x| >= 1, NaN / inf */
	u64 bytes;               /* file size */
} ExportReport;

ExportOptions export_defaults(ExportFormat fmt);
const char *export_ext(ExportFormat fmt); /* "wav", "mp3", "opus" */
/* Render s and write path. progress (may be NULL) goes 0..1; cancel (may be
 * NULL) stops it. false + err on failure. */
bool export_song(const Song *s, const char *path, const ExportOptions *opt, ExportReport *rep, char *err, size_t errn,
                 volatile float *progress, volatile bool *cancel);
/* Render to memory: interleaved stereo float, *frames set. NULL on failure. */
float *render_song(const Song *s, const ExportOptions *opt, u64 *frames);

#endif
