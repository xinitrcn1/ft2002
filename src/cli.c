/* ft2002-cli: the workstation without a window (docs/cli.md).
 *
 *   ft2002-cli gen [--seed N] [--mood M] [--band B] [--bpm X] [--key K] [--plain] -o OUT
 *        compose a song; OUT.ft2 saves the project, OUT.wav/.mp3/.opus renders it
 *   ft2002-cli render PROJECT.ft2 -o OUT.(wav|mp3|opus) [--bitrate K] [--cbr] [--rate HZ] [--solo T]
 *   ft2002-cli info PROJECT.ft2
 *   ft2002-cli chords [CHORD...]       the library's voicings (standard tuning)
 *   ft2002-cli patterns                the strum patterns */
#define _POSIX_C_SOURCE 200809L
#include "autogen.h"
#include "chords.h"
#include "export.h"
#include "guitar.h"
#include "project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int usage(void)
{
	fprintf(stderr, "usage: ft2002-cli gen [--seed N] [--mood M] [--band B] [--bpm X] [--key K] [--plain] -o OUT.(ft2|wav|mp3|opus)\n"
	                "       ft2002-cli render PROJECT.ft2 -o OUT.(wav|mp3|opus) [--bitrate K] [--cbr] [--rate HZ] [--solo T]\n"
	                "       ft2002-cli info PROJECT.ft2\n"
	                "       ft2002-cli chords [CHORD...]\n"
	                "       ft2002-cli patterns\n"
	                "moods: calm tense crisis triumph wartime frustrated; bands: synth+guitar rock acoustic metal electronic\n");
	return 2;
}

static const char *ext_of(const char *path)
{
	const char *d = strrchr(path, '.');
	return d ? d + 1 : "";
}

static void info(const Song *s)
{
	printf("%s\n", s->description[0] ? s->description : "(no description)");
	printf("%.1f BPM, key %s, %u bars, %.1f s, %u events, %d tracks\n", s->bpm, note_names[((s->key % 12) + 12) % 12],
	       s->total_bars, song_duration(s), s->nevents, s->ntracks);
	printf("form:");
	for (u32 i = 0; i < s->nsections; i++)
		printf(" %s %u", section_name(s->sections[i].kind), s->sections[i].bars);
	printf("\n");
	for (int t = 0; t < s->ntracks; t++) {
		u32 n = 0;
		for (u32 i = 0; i < s->nevents; i++)
			n += s->events[i].ch == t;
		const Patch *p = &s->patches[t];
		printf("  %2d %-16s %-8s %-18s %5u events  gain %.2f pan %+.2f\n", t, s->track_name[t],
		       p->inst == INST_GUITAR ? "guitar" : p->inst == INST_KIT ? "kit" : "synth",
		       p->inst == INST_GUITAR ? gtr_model_name(p->gtr) : "", n, s->mix.gain[t], s->mix.pan[t]);
	}
	ChordSeg segs[64];
	int n = harmony_extract(s, segs, 64);
	printf("chords:");
	for (int i = 0; i < n && i < 24; i++) {
		char c[16];
		chord_name(segs[i].root, segs[i].quality, c, sizeof c);
		printf(" %s", c);
	}
	printf(n > 24 ? " ...\n" : "\n");
}

static int render_to(const Song *s, const char *out, int argc, char **argv, int i0)
{
	const char *e = ext_of(out);
	ExportFormat fmt = !strcasecmp(e, "mp3") ? EXP_MP3 : !strcasecmp(e, "opus") || !strcasecmp(e, "ogg") ? EXP_OPUS : EXP_WAV;
	ExportOptions o = export_defaults(fmt);
	for (int i = i0; i < argc; i++) {
		if (!strcmp(argv[i], "--bitrate") && i + 1 < argc)
			o.bitrate_kbps = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--cbr"))
			o.vbr = false;
		else if (!strcmp(argv[i], "--rate") && i + 1 < argc)
			o.sample_rate = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--solo") && i + 1 < argc)
			o.solo = 1u << atoi(argv[++i]);
	}
	ExportReport r;
	char err[256] = "";
	if (!export_song(s, out, &o, &r, err, sizeof err, NULL, NULL)) {
		fprintf(stderr, "ft2002-cli: %s\n", err);
		return 1;
	}
	printf("wrote %s: %.1f s, peak %.2f dBFS, rms %.2f dBFS, %llu clipped, %llu non-finite, %llu bytes\n", out,
	       r.seconds, r.peak_dbfs, r.rms_dbfs, (unsigned long long)r.clipped, (unsigned long long)r.non_finite,
	       (unsigned long long)r.bytes);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return usage();
	const char *cmd = argv[1];
	if (!strcmp(cmd, "gen")) {
		AutogenOptions o = {.seed = 1, .mood = MOOD_CALM, .band = BAND_ROCK, .bpm = 0, .key = -1, .variations = true};
		const char *out = NULL;
		for (int i = 2; i < argc; i++) {
			if (!strcmp(argv[i], "--seed") && i + 1 < argc)
				o.seed = strtoull(argv[++i], NULL, 0);
			else if (!strcmp(argv[i], "--mood") && i + 1 < argc) {
				if (!parse_mood(argv[++i], &o.mood))
					return usage();
			} else if (!strcmp(argv[i], "--band") && i + 1 < argc) {
				if (!parse_band(argv[++i], &o.band))
					return usage();
			} else if (!strcmp(argv[i], "--bpm") && i + 1 < argc)
				o.bpm = strtof(argv[++i], NULL);
			else if (!strcmp(argv[i], "--key") && i + 1 < argc) {
				int r, q;
				if (!chord_parse(argv[++i], &r, &q))
					return usage();
				o.key = r;
			} else if (!strcmp(argv[i], "-o") && i + 1 < argc)
				out = argv[++i];
		}
		if (!out)
			return usage();
		Song s;
		if (!autogen(&s, &o)) {
			fprintf(stderr, "ft2002-cli: out of memory\n");
			return 1;
		}
		info(&s);
		int rc = 0;
		if (!strcasecmp(ext_of(out), "ft2")) {
			char err[256];
			if (!project_save(&s, out, err, sizeof err)) {
				fprintf(stderr, "ft2002-cli: %s\n", err);
				rc = 1;
			} else {
				printf("wrote %s\n", out);
			}
		} else {
			rc = render_to(&s, out, argc, argv, 2);
		}
		song_free(&s);
		return rc;
	}
	if (!strcmp(cmd, "render") || !strcmp(cmd, "info")) {
		if (argc < 3)
			return usage();
		Song s;
		song_init(&s);
		char err[256];
		if (!project_load(&s, argv[2], err, sizeof err)) {
			fprintf(stderr, "ft2002-cli: %s\n", err);
			return 1;
		}
		int rc = 0;
		if (!strcmp(cmd, "info")) {
			info(&s);
		} else {
			const char *out = NULL;
			for (int i = 3; i < argc; i++)
				if (!strcmp(argv[i], "-o") && i + 1 < argc)
					out = argv[++i];
			rc = out ? render_to(&s, out, argc, argv, 3) : usage();
		}
		song_free(&s);
		return rc;
	}
	if (!strcmp(cmd, "chords")) {
		static ChordLib lib;
		const int std[6] = {40, 45, 50, 55, 59, 64};
		chordlib_build(&lib, std, 6);
		for (int i = 2; i < argc; i++) {
			int r, q;
			if (!chord_parse(argv[i], &r, &q)) {
				fprintf(stderr, "ft2002-cli: not a chord: %s\n", argv[i]);
				continue;
			}
			printf("%-8s", argv[i]);
			for (int k = 0; k < chordlib_count(&lib, r, q); k++) {
				char b[32];
				voicing_shape(&lib, chordlib_get(&lib, r, q, k), b, sizeof b);
				printf(" %s", b);
			}
			printf("\n");
		}
		if (argc == 2) {
			int total = 0;
			for (int r = 0; r < 12; r++)
				for (int q = 0; q < Q_COUNT; q++)
					total += chordlib_count(&lib, r, q);
			printf("%d voicings for 12 roots x %d qualities\n", total, Q_COUNT);
		}
		return 0;
	}
	if (!strcmp(cmd, "patterns")) {
		for (int i = 0; i < strum_pattern_count; i++)
			printf("%-16s %-12s %s  %s\n", strum_patterns[i].name, strum_patterns[i].style, strum_patterns[i].code,
			       art_name(strum_patterns[i].art));
		return 0;
	}
	return usage();
}
