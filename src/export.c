/* Offline rendering and export (export.h). */
#define _POSIX_C_SOURCE 200809L
#include "export.h"
#include "synth.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <lame/lame.h>
#include <ogg/ogg.h>
#include <opus.h>

#define BLOCK 960 /* 20 ms at 48 kHz: one Opus frame */

ExportOptions export_defaults(ExportFormat fmt)
{
	return (ExportOptions){.fmt = fmt, .sample_rate = 48000, .bitrate_kbps = 0, .vbr = fmt == EXP_MP3, .tail_s = 2.5f,
	                       .volume = 0.8f, .intensity = 0.6f};
}

const char *export_ext(ExportFormat fmt)
{
	static const char *e[EXP_COUNT] = {"wav", "mp3", "opus"};
	return fmt < EXP_COUNT ? e[fmt] : "bin";
}

static void fail(char *err, size_t n, const char *fmt, ...)
{
	if (!err || !n)
		return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, n, fmt, ap);
	va_end(ap);
}

/* ---- sinks -------------------------------------------------------------------- */

typedef struct {
	FILE *f;
	ExportFormat fmt;
	int sr;
	u64 frames;
	/* mp3 */
	lame_t lame;
	u8 *mp3buf;
	size_t mp3cap;
	/* opus */
	OpusEncoder *enc;
	ogg_stream_state os;
	int preskip;
	i64 granule;
	i64 packetno;
	float pending[BLOCK * 2];
	int npending;
	u8 packet[4000];
} Sink;

static void put_u32(u8 *b, u32 v)
{
	b[0] = (u8)v, b[1] = (u8)(v >> 8), b[2] = (u8)(v >> 16), b[3] = (u8)(v >> 24);
}
static void put_u16(u8 *b, u16 v) { b[0] = (u8)v, b[1] = (u8)(v >> 8); }

static bool wav_header(Sink *k, u32 data_bytes)
{
	u8 h[44];
	memcpy(h, "RIFF", 4);
	put_u32(h + 4, 36 + data_bytes);
	memcpy(h + 8, "WAVEfmt ", 8);
	put_u32(h + 16, 16);
	put_u16(h + 20, 1);
	put_u16(h + 22, 2);
	put_u32(h + 24, (u32)k->sr);
	put_u32(h + 28, (u32)k->sr * 4);
	put_u16(h + 32, 4);
	put_u16(h + 34, 16);
	memcpy(h + 36, "data", 4);
	put_u32(h + 40, data_bytes);
	return fwrite(h, 1, 44, k->f) == 44;
}

static bool ogg_flush(Sink *k, bool force)
{
	ogg_page pg;
	while (force ? ogg_stream_flush(&k->os, &pg) : ogg_stream_pageout(&k->os, &pg)) {
		if (fwrite(pg.header, 1, (size_t)pg.header_len, k->f) != (size_t)pg.header_len ||
		    fwrite(pg.body, 1, (size_t)pg.body_len, k->f) != (size_t)pg.body_len)
			return false;
	}
	return true;
}

static bool sink_open(Sink *k, const char *path, const ExportOptions *o, const char *title, const char *artist, char *err,
                      size_t errn)
{
	memset(k, 0, sizeof *k);
	k->fmt = o->fmt;
	k->sr = o->fmt == EXP_OPUS ? 48000 : o->sample_rate;
	k->f = fopen(path, "wb");
	if (!k->f) {
		fail(err, errn, "cannot write %s", path);
		return false;
	}
	switch (o->fmt) {
	case EXP_WAV:
		return wav_header(k, 0);
	case EXP_MP3: {
		k->lame = lame_init();
		if (!k->lame) {
			fail(err, errn, "LAME: lame_init failed");
			return false;
		}
		lame_set_in_samplerate(k->lame, k->sr);
		lame_set_out_samplerate(k->lame, k->sr);
		lame_set_num_channels(k->lame, 2);
		lame_set_mode(k->lame, JOINT_STEREO);
		lame_set_quality(k->lame, 2);
		if (o->vbr) {
			lame_set_VBR(k->lame, vbr_default);
			/* 0 -> V2 (about 190 kbps); otherwise the V level nearest the bitrate */
			int q = 2;
			if (o->bitrate_kbps > 0)
				q = o->bitrate_kbps >= 245 ? 0 : o->bitrate_kbps >= 225 ? 1 : o->bitrate_kbps >= 190 ? 2 : o->bitrate_kbps >= 175 ? 3 : o->bitrate_kbps >= 165 ? 4 : o->bitrate_kbps >= 130 ? 5 : 6;
			lame_set_VBR_quality(k->lame, (float)q);
		} else {
			lame_set_VBR(k->lame, vbr_off);
			lame_set_brate(k->lame, o->bitrate_kbps > 0 ? o->bitrate_kbps : 192);
		}
		id3tag_init(k->lame);
		id3tag_v2_only(k->lame);
		id3tag_set_title(k->lame, title);
		if (artist)
			id3tag_set_artist(k->lame, artist);
		id3tag_set_comment(k->lame, "FT2002");
		lame_set_write_id3tag_automatic(k->lame, 1);
		if (lame_init_params(k->lame) < 0) {
			fail(err, errn, "LAME: unsupported parameters");
			return false;
		}
		k->mp3cap = (size_t)(1.25 * BLOCK * 2 + 7200) * 4;
		k->mp3buf = malloc(k->mp3cap);
		return k->mp3buf != NULL;
	}
	case EXP_OPUS: {
		int e = 0;
		k->enc = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &e);
		if (!k->enc || e != OPUS_OK) {
			fail(err, errn, "Opus: %s", opus_strerror(e));
			return false;
		}
		opus_encoder_ctl(k->enc, OPUS_SET_BITRATE((o->bitrate_kbps > 0 ? o->bitrate_kbps : 160) * 1000));
		opus_encoder_ctl(k->enc, OPUS_SET_COMPLEXITY(10));
		opus_encoder_ctl(k->enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
		opus_encoder_ctl(k->enc, OPUS_SET_VBR(1));
		opus_int32 la = 0;
		opus_encoder_ctl(k->enc, OPUS_GET_LOOKAHEAD(&la));
		k->preskip = (int)la;
		ogg_stream_init(&k->os, 0x46543230); /* "FT20" */
		/* OpusHead (RFC 7845 5.1) */
		u8 head[19];
		memcpy(head, "OpusHead", 8);
		head[8] = 1;
		head[9] = 2;
		put_u16(head + 10, (u16)k->preskip);
		put_u32(head + 12, 48000);
		put_u16(head + 16, 0);
		head[18] = 0;
		ogg_packet op = {.packet = head, .bytes = 19, .b_o_s = 1, .granulepos = 0, .packetno = k->packetno++};
		ogg_stream_packetin(&k->os, &op);
		if (!ogg_flush(k, true))
			return false;
		/* OpusTags (5.2): vendor, then user comments */
		const char *vendor = opus_get_version_string();
		char c_title[512], c_artist[512], c_enc[64];
		snprintf(c_title, sizeof c_title, "TITLE=%s", title);
		snprintf(c_artist, sizeof c_artist, "ARTIST=%s", artist ? artist : "");
		snprintf(c_enc, sizeof c_enc, "ENCODER=FT2002");
		const char *cm[3] = {c_title, c_enc, c_artist};
		int ncm = artist ? 3 : 2;
		size_t tl = 8 + 4 + strlen(vendor) + 4;
		for (int i = 0; i < ncm; i++)
			tl += 4 + strlen(cm[i]);
		u8 *tags = malloc(tl);
		if (!tags)
			return false;
		size_t at = 0;
		memcpy(tags, "OpusTags", 8), at = 8;
		put_u32(tags + at, (u32)strlen(vendor)), at += 4;
		memcpy(tags + at, vendor, strlen(vendor)), at += strlen(vendor);
		put_u32(tags + at, (u32)ncm), at += 4;
		for (int i = 0; i < ncm; i++) {
			put_u32(tags + at, (u32)strlen(cm[i])), at += 4;
			memcpy(tags + at, cm[i], strlen(cm[i])), at += strlen(cm[i]);
		}
		ogg_packet ot = {.packet = tags, .bytes = (long)tl, .granulepos = 0, .packetno = k->packetno++};
		ogg_stream_packetin(&k->os, &ot);
		bool ok = ogg_flush(k, true);
		free(tags);
		k->granule = k->preskip;
		return ok;
	}
	default:
		fail(err, errn, "unknown format");
		return false;
	}
}

static bool opus_block(Sink *k, const float *pcm, bool last, i64 end_granule)
{
	int n = opus_encode_float(k->enc, pcm, BLOCK, k->packet, (opus_int32)sizeof k->packet);
	if (n < 0)
		return false;
	k->granule += BLOCK;
	ogg_packet op = {.packet = k->packet, .bytes = n, .e_o_s = last, .granulepos = last ? end_granule : k->granule,
	                 .packetno = k->packetno++};
	ogg_stream_packetin(&k->os, &op);
	return ogg_flush(k, last);
}

static bool sink_write(Sink *k, const float *pcm, int frames)
{
	k->frames += (u64)frames;
	switch (k->fmt) {
	case EXP_WAV: {
		i16 buf[BLOCK * 2];
		for (int off = 0; off < frames; off += BLOCK) {
			int n = frames - off < BLOCK ? frames - off : BLOCK;
			for (int i = 0; i < n * 2; i++) {
				float x = pcm[off * 2 + i];
				x = x > 1.0f ? 1.0f : x < -1.0f ? -1.0f : x;
				buf[i] = (i16)lrintf(x * 32767.0f);
			}
			if (fwrite(buf, 4, (size_t)n, k->f) != (size_t)n)
				return false;
		}
		return true;
	}
	case EXP_MP3: {
		for (int off = 0; off < frames; off += BLOCK) {
			int n = frames - off < BLOCK ? frames - off : BLOCK;
			int b = lame_encode_buffer_interleaved_ieee_float(k->lame, pcm + off * 2, n, k->mp3buf, (int)k->mp3cap);
			if (b < 0 || fwrite(k->mp3buf, 1, (size_t)b, k->f) != (size_t)b)
				return false;
		}
		return true;
	}
	case EXP_OPUS: {
		for (int i = 0; i < frames; i++) {
			k->pending[k->npending * 2] = pcm[i * 2];
			k->pending[k->npending * 2 + 1] = pcm[i * 2 + 1];
			if (++k->npending == BLOCK) {
				k->npending = 0;
				if (!opus_block(k, k->pending, false, 0))
					return false;
			}
		}
		return true;
	}
	default:
		return false;
	}
}

static bool sink_close(Sink *k, bool ok)
{
	if (!k->f)
		return false;
	if (ok) {
		switch (k->fmt) {
		case EXP_WAV:
			ok = fseek(k->f, 0, SEEK_SET) == 0 && wav_header(k, (u32)(k->frames * 4));
			break;
		case EXP_MP3: {
			int b = lame_encode_flush(k->lame, k->mp3buf, (int)k->mp3cap);
			ok = b >= 0 && fwrite(k->mp3buf, 1, (size_t)b, k->f) == (size_t)b;
			/* the Xing/LAME header with the exact length (VBR seeking, gapless) */
			if (ok) {
				u8 tag[2880];
				size_t tn = lame_get_lametag_frame(k->lame, tag, sizeof tag);
				long id3v2 = (long)lame_get_id3v2_tag(k->lame, NULL, 0);
				if (tn > 0 && tn <= sizeof tag && fseek(k->f, id3v2, SEEK_SET) == 0)
					ok = fwrite(tag, 1, tn, k->f) == tn;
			}
			break;
		}
		case EXP_OPUS: {
			/* pad the last frame with silence; the final granule trims it */
			i64 end = k->preskip + (i64)k->frames;
			for (int i = k->npending; i < BLOCK; i++)
				k->pending[i * 2] = k->pending[i * 2 + 1] = 0.0f;
			ok = opus_block(k, k->pending, true, end);
			break;
		}
		default:
			break;
		}
	}
	if (k->lame)
		lame_close(k->lame);
	free(k->mp3buf);
	if (k->enc) {
		opus_encoder_destroy(k->enc);
		ogg_stream_clear(&k->os);
	}
	if (fclose(k->f) != 0)
		ok = false;
	k->f = NULL;
	return ok;
}

/* ---- rendering -------------------------------------------------------------- */

typedef struct {
	Player *p;
	u64 frames_total, done;
	double sum2;
	float peak;
	u64 clipped, non_finite;
} Render;

static bool render_begin(Render *r, const Song *s, const ExportOptions *o, int sr)
{
	memset(r, 0, sizeof *r);
	r->p = player_new((float)sr);
	if (!r->p)
		return false;
	player_set_intensity_now(r->p, o->intensity);
	player_set_volume(r->p, o->volume);
	r->p->volume = o->volume; /* no fade-in */
	r->p->solo = o->solo;
	player_set_song(r->p, s);
	r->frames_total = (u64)((song_duration(s) + (double)o->tail_s) * sr);
	return true;
}

static int render_block(Render *r, float *out)
{
	u64 left = r->frames_total - r->done;
	int n = left < BLOCK ? (int)left : BLOCK;
	if (n <= 0)
		return 0;
	player_render(r->p, out, n);
	for (int i = 0; i < n * 2; i++) {
		float x = out[i];
		if (!isfinite(x)) {
			r->non_finite++;
			out[i] = 0;
			continue;
		}
		float a = fabsf(x);
		r->peak = a > r->peak ? a : r->peak;
		r->clipped += a >= 1.0f;
		r->sum2 += (double)x * x;
	}
	r->done += (u64)n;
	return n;
}

static void render_report(const Render *r, int sr, ExportReport *rep)
{
	if (!rep)
		return;
	rep->seconds = (double)r->done / sr;
	rep->peak_dbfs = r->peak > 1e-10f ? 20.0 * log10(r->peak) : -200.0;
	double rms = r->done ? sqrt(r->sum2 / (double)(r->done * 2)) : 0;
	rep->rms_dbfs = rms > 1e-10 ? 20.0 * log10(rms) : -200.0;
	rep->clipped = r->clipped;
	rep->non_finite = r->non_finite;
}

bool export_song(const Song *s, const char *path, const ExportOptions *o, ExportReport *rep, char *err, size_t errn,
                 volatile float *progress, volatile bool *cancel)
{
	char title[256];
	if (o->title)
		snprintf(title, sizeof title, "%s", o->title);
	else
		snprintf(title, sizeof title, "%s %llu", s->style[0] ? s->style : "FT2002", (unsigned long long)s->seed);
	int sr = o->fmt == EXP_OPUS ? 48000 : o->sample_rate;
	Sink k;
	if (!sink_open(&k, path, o, title, o->artist, err, errn)) {
		if (err && !err[0])
			fail(err, errn, "cannot start the encoder");
		sink_close(&k, false);
		return false;
	}
	Render r;
	if (!render_begin(&r, s, o, sr)) {
		sink_close(&k, false);
		fail(err, errn, "out of memory");
		return false;
	}
	float buf[BLOCK * 2];
	bool ok = true;
	int n;
	while ((n = render_block(&r, buf)) > 0) {
		if (!sink_write(&k, buf, n)) {
			ok = false;
			fail(err, errn, "write failed: %s", path);
			break;
		}
		if (progress)
			*progress = (float)((double)r.done / (double)r.frames_total);
		if (cancel && *cancel) {
			ok = false;
			fail(err, errn, "cancelled");
			break;
		}
	}
	player_free(r.p);
	if (!sink_close(&k, ok) && ok) {
		ok = false;
		fail(err, errn, "could not finish %s", path);
	}
	render_report(&r, sr, rep);
	if (rep) {
		struct stat st;
		rep->bytes = stat(path, &st) == 0 ? (u64)st.st_size : 0;
	}
	if (!ok)
		remove(path);
	return ok;
}

float *render_song(const Song *s, const ExportOptions *o, u64 *frames)
{
	int sr = o->sample_rate;
	Render r;
	if (!render_begin(&r, s, o, sr))
		return NULL;
	float *out = malloc((size_t)(r.frames_total + BLOCK) * 2 * sizeof(float));
	if (!out) {
		player_free(r.p);
		return NULL;
	}
	int n;
	while ((n = render_block(&r, out + r.done * 2)) > 0)
		;
	*frames = r.done;
	player_free(r.p);
	return out;
}
