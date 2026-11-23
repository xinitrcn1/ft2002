/* Projects on disk and the editing history (project.h). */
#define _POSIX_C_SOURCE 200809L
#include "project.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const inst_names[] = {"synth", "kit", "guitar", NULL};
static const char *const wave_names[] = {"pulse", "saw", "triangle", "sine", "noise", "table", NULL};
static const char *const gtr_names[] = {"steel", "nylon", "clean", "crunch", "lead", "bass", "12string", NULL};
static const char *const formant_names[] = {"off", "aah", "ooh", NULL};

#define F(name, lo, hi) {#name, PF_FLOAT, offsetof(Patch, name), lo, hi, NULL}
#define I(name, lo, hi) {#name, PF_INT, offsetof(Patch, name), lo, hi, NULL}
#define B(name) {#name, PF_BOOL, offsetof(Patch, name), 0, 1, NULL}
#define U(name, lo, hi) {#name, PF_U8, offsetof(Patch, name), lo, hi, NULL}
#define E(name, names) {#name, PF_ENUM, offsetof(Patch, name), 0, 0, names}

const PatchField patch_fields[] = {
    E(inst, inst_names),
    B(mono),
    E(w1, wave_names),
    E(w2, wave_names),
    U(table1, 0, 3),
    U(table2, 0, 3),
    F(mix2, 0, 1),
    F(detune, -50, 50),
    F(semi2, -24, 24),
    F(duty, 0.05f, 0.95f),
    F(pwm_depth, 0, 0.45f),
    F(pwm_rate, 0, 10),
    F(sub, 0, 1),
    I(unison, 0, 4),
    F(spread, 0, 60),
    B(poles4),
    E(formant, formant_names),
    F(attack, 0.001f, 4),
    F(decay, 0.005f, 4),
    F(sustain, 0, 1),
    F(release, 0.005f, 6),
    F(cutoff, 40, 18000),
    F(resonance, 0, 0.98f),
    F(fenv, -2, 6),
    F(fdecay, 0.005f, 4),
    F(key_track, 0, 1),
    F(glide, 0, 1),
    F(vib_depth, 0, 1),
    F(vib_rate, 0.1f, 12),
    F(vib_delay, 0, 2),
    F(crush_rate, 0, 20000),
    F(drive, 0, 1),
    F(gain, 0, 2),
    F(kick_pitch, 40, 400),
    F(kick_end, 20, 120),
    F(kick_decay, 0.05f, 1.5f),
    F(kick_click, 0, 1),
    F(kick_sweep, 0.005f, 0.2f),
    F(kick_drive, 0.5f, 6),
    F(snare_tone, 100, 400),
    F(snare_decay, 0.05f, 0.6f),
    F(hat_decay, 0.01f, 0.3f),
    F(hat_tone, 0.5f, 2),
    E(gtr, gtr_names),
    F(gtr_tone, 0, 1),
    F(gtr_pick, 0.05f, 0.5f),
    F(gtr_sustain, 0.25f, 4),
    F(gtr_gain, 0, 1),
    F(gtr_body, 0, 1),
    U(gtr_capo, 0, 12),
};
const int patch_field_count = (int)(sizeof patch_fields / sizeof patch_fields[0]);

float patch_get(const Patch *p, const PatchField *f)
{
	const u8 *b = (const u8 *)p + f->offset;
	switch (f->type) {
	case PF_FLOAT: {
		float v;
		memcpy(&v, b, sizeof v);
		return v;
	}
	case PF_INT: {
		int v;
		memcpy(&v, b, sizeof v);
		return (float)v;
	}
	case PF_BOOL:
		return *(const bool *)b ? 1.0f : 0.0f;
	default:
		return (float)*b;
	}
}

void patch_set(Patch *p, const PatchField *f, float v)
{
	u8 *b = (u8 *)p + f->offset;
	switch (f->type) {
	case PF_FLOAT:
		memcpy(b, &v, sizeof v);
		break;
	case PF_INT: {
		int i = (int)lrintf(v);
		memcpy(b, &i, sizeof i);
		break;
	}
	case PF_BOOL:
		*(bool *)b = v != 0.0f;
		break;
	default:
		*b = (u8)(v < 0 ? 0 : v > 255 ? 255 : lrintf(v));
		break;
	}
}

const PatchField *patch_field(const char *name)
{
	for (int i = 0; i < patch_field_count; i++)
		if (!strcmp(patch_fields[i].name, name))
			return &patch_fields[i];
	return NULL;
}

/* ---- writing ---------------------------------------------------------------- */

static void put_str(FILE *f, const char *s)
{
	fputc('"', f);
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			fputc('\\', f);
		if (*s == '\n')
			fputs("\\n", f);
		else
			fputc(*s, f);
	}
	fputc('"', f);
}

bool project_save(const Song *s, const char *path, char *err, size_t errn)
{
	char tmp[4096];
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		snprintf(err, errn, "cannot write %s: %s", tmp, strerror(errno));
		return false;
	}
	fprintf(f, "ft2002 1\n");
	fprintf(f, "song bpm=%.9g key=%d final_key=%d mode=%d seed=%llu mood=%s bars=%u style=", s->bpm, s->key, s->final_key,
	        s->mode, (unsigned long long)s->seed, mood_name(s->mood), s->total_bars);
	put_str(f, s->style);
	fprintf(f, " description=");
	put_str(f, s->description);
	fprintf(f, "\nmixbus delay_rows=%.9g delay_feedback=%.9g reverb_size=%.9g\n", s->mix.delay_rows, s->mix.delay_feedback,
	        s->mix.reverb_size);
	for (u32 i = 0; i < s->nsections; i++)
		fprintf(f, "section %s %u %u\n", section_name(s->sections[i].kind), s->sections[i].start_bar, s->sections[i].bars);
	for (int t = 0; t < s->ntracks; t++) {
		fprintf(f, "track %d ", t);
		put_str(f, s->track_name[t]);
		fprintf(f, " role=%d\n", (int)s->role[t] - 1);
		fprintf(f, "patch %d", t);
		const Patch *p = &s->patches[t];
		for (int k = 0; k < patch_field_count; k++) {
			const PatchField *pf = &patch_fields[k];
			float v = patch_get(p, pf);
			if (pf->type == PF_ENUM) {
				int iv = (int)v, n = 0;
				while (pf->enums[n])
					n++;
				fprintf(f, " %s=%s", pf->name, iv >= 0 && iv < n ? pf->enums[iv] : "?");
			} else if (pf->type == PF_FLOAT) {
				fprintf(f, " %s=%.9g", pf->name, v);
			} else {
				fprintf(f, " %s=%d", pf->name, (int)v);
			}
		}
		fprintf(f, " tuning=%d,%d,%d,%d,%d,%d\n", p->gtr_tuning[0], p->gtr_tuning[1], p->gtr_tuning[2], p->gtr_tuning[3],
		        p->gtr_tuning[4], p->gtr_tuning[5]);
		const MixSettings *m = &s->mix;
		fprintf(f, "mix %d gain=%.9g pan=%.9g delay=%.9g reverb=%.9g duck=%.9g bright=%d chorus=%.9g highpass=%.9g\n", t,
		        m->gain[t], m->pan[t], m->delay[t], m->reverb[t], m->duck[t], m->bright[t] ? 1 : 0, m->chorus[t],
		        m->highpass[t]);
	}
	for (u32 i = 0; i < s->nevents; i++) {
		const NoteEvent *e = &s->events[i];
		fprintf(f, "ev %u %u %u %u %u %u %u %u %d %u %u %u %u %u %u %u %u\n", e->ch, e->tick, e->len, e->vel, e->layer,
		        e->fx, e->count, e->art, e->strum, e->string, e->gate, e->notes[0], e->notes[1], e->notes[2],
		        e->notes[3], e->notes[4], e->notes[5]);
	}
	bool ok = !ferror(f);
	if (fclose(f) != 0)
		ok = false;
	if (!ok || rename(tmp, path) != 0) {
		snprintf(err, errn, "cannot write %s: %s", path, strerror(errno));
		remove(tmp);
		return false;
	}
	return true;
}

/* ---- reading ---------------------------------------------------------------- */

/* next token: a word, key=value, or a "quoted string" (unescaped in place) */
static char *token(char **p)
{
	char *s = *p;
	while (*s == ' ' || *s == '\t')
		s++;
	if (!*s)
		return NULL;
	char *start = s, *w = s;
	bool q = false;
	while (*s && (q || (*s != ' ' && *s != '\t'))) {
		if (*s == '"') {
			q = !q;
			s++;
			continue;
		}
		if (q && *s == '\\' && s[1]) {
			s++;
			*w++ = *s == 'n' ? '\n' : *s;
			s++;
			continue;
		}
		*w++ = *s++;
	}
	if (*s)
		s++;
	*w = 0;
	*p = s;
	return start;
}

static int enum_index(const char *const *names, const char *v)
{
	for (int i = 0; names[i]; i++)
		if (!strcmp(names[i], v))
			return i;
	return -1;
}

bool project_load(Song *s, const char *path, char *err, size_t errn)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		snprintf(err, errn, "cannot open %s: %s", path, strerror(errno));
		return false;
	}
	song_free(s);
	song_init(s);
	s->ntracks = 0;
	char *line = NULL;
	size_t cap = 0;
	int ln = 0;
	bool ok = true, header = false;
	while (getline(&line, &cap, f) > 0) {
		ln++;
		size_t n = strlen(line);
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		char *p = line;
		char *kw = token(&p);
		if (!kw || kw[0] == '#')
			continue;
		if (!header) {
			if (strcmp(kw, "ft2002") != 0) {
				snprintf(err, errn, "%s: not an FT2002 project", path);
				ok = false;
				break;
			}
			header = true;
			continue;
		}
		if (!strcmp(kw, "song") || !strcmp(kw, "mixbus")) {
			for (char *t; (t = token(&p));) {
				char *eq = strchr(t, '=');
				if (!eq)
					continue;
				*eq = 0;
				const char *v = eq + 1;
				if (!strcmp(t, "bpm"))
					s->bpm = strtof(v, NULL);
				else if (!strcmp(t, "key"))
					s->key = atoi(v);
				else if (!strcmp(t, "final_key"))
					s->final_key = atoi(v);
				else if (!strcmp(t, "mode"))
					s->mode = atoi(v);
				else if (!strcmp(t, "seed"))
					s->seed = strtoull(v, NULL, 10);
				else if (!strcmp(t, "mood"))
					parse_mood(v, &s->mood);
				else if (!strcmp(t, "bars"))
					s->total_bars = (u32)strtoul(v, NULL, 10);
				else if (!strcmp(t, "style"))
					snprintf(s->style, sizeof s->style, "%s", v);
				else if (!strcmp(t, "description"))
					snprintf(s->description, sizeof s->description, "%s", v);
				else if (!strcmp(t, "delay_rows"))
					s->mix.delay_rows = strtof(v, NULL);
				else if (!strcmp(t, "delay_feedback"))
					s->mix.delay_feedback = strtof(v, NULL);
				else if (!strcmp(t, "reverb_size"))
					s->mix.reverb_size = strtof(v, NULL);
			}
		} else if (!strcmp(kw, "section")) {
			char *k = token(&p), *a = token(&p), *b = token(&p);
			if (!k || !a || !b || s->nsections >= FT_MAX_SECTIONS)
				continue;
			int kind = 0;
			for (int i = 0; i < SEC_COUNT; i++)
				if (!strcmp(section_name((SectionKind)i), k))
					kind = i;
			s->sections[s->nsections++] = (Section){(SectionKind)kind, (u32)atoi(a), (u32)atoi(b)};
		} else if (!strcmp(kw, "track")) {
			char *ti = token(&p), *name = token(&p);
			int t = ti ? atoi(ti) : -1;
			if (t < 0 || t >= FT_MAX_TRACKS || !name) {
				snprintf(err, errn, "%s:%d: bad track", path, ln);
				ok = false;
				break;
			}
			snprintf(s->track_name[t], sizeof s->track_name[t], "%s", name);
			if (t + 1 > s->ntracks)
				s->ntracks = t + 1;
			for (char *kv; (kv = token(&p));)
				if (!strncmp(kv, "role=", 5)) {
					int r = atoi(kv + 5);
					s->role[t] = (u8)(r >= 0 && r < CH_COUNT ? r + 1 : 0);
				}
		} else if (!strcmp(kw, "patch")) {
			char *ti = token(&p);
			int t = ti ? atoi(ti) : -1;
			if (t < 0 || t >= FT_MAX_TRACKS)
				continue;
			Patch *pt = &s->patches[t];
			for (char *kv; (kv = token(&p));) {
				char *eq = strchr(kv, '=');
				if (!eq)
					continue;
				*eq = 0;
				const char *v = eq + 1;
				if (!strcmp(kv, "tuning")) {
					int tu[6] = {0};
					sscanf(v, "%d,%d,%d,%d,%d,%d", &tu[0], &tu[1], &tu[2], &tu[3], &tu[4], &tu[5]);
					for (int i = 0; i < 6; i++)
						pt->gtr_tuning[i] = (i8)tu[i];
					continue;
				}
				const PatchField *pf = patch_field(kv);
				if (!pf)
					continue;
				if (pf->type == PF_ENUM) {
					int iv = enum_index(pf->enums, v);
					if (iv >= 0)
						patch_set(pt, pf, (float)iv);
				} else {
					patch_set(pt, pf, strtof(v, NULL));
				}
			}
		} else if (!strcmp(kw, "mix")) {
			char *ti = token(&p);
			int t = ti ? atoi(ti) : -1;
			if (t < 0 || t >= FT_MAX_TRACKS)
				continue;
			MixSettings *m = &s->mix;
			for (char *kv; (kv = token(&p));) {
				char *eq = strchr(kv, '=');
				if (!eq)
					continue;
				*eq = 0;
				float v = strtof(eq + 1, NULL);
				if (!strcmp(kv, "gain"))
					m->gain[t] = v;
				else if (!strcmp(kv, "pan"))
					m->pan[t] = v;
				else if (!strcmp(kv, "delay"))
					m->delay[t] = v;
				else if (!strcmp(kv, "reverb"))
					m->reverb[t] = v;
				else if (!strcmp(kv, "duck"))
					m->duck[t] = v;
				else if (!strcmp(kv, "bright"))
					m->bright[t] = v != 0;
				else if (!strcmp(kv, "chorus"))
					m->chorus[t] = v;
				else if (!strcmp(kv, "highpass"))
					m->highpass[t] = v;
			}
		} else if (!strcmp(kw, "ev")) {
			unsigned v[17];
			int strum = 0;
			if (sscanf(p, "%u %u %u %u %u %u %u %u %d %u %u %u %u %u %u %u %u", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
			           &v[6], &v[7], &strum, &v[9], &v[10], &v[11], &v[12], &v[13], &v[14], &v[15], &v[16]) != 17) {
				snprintf(err, errn, "%s:%d: bad event", path, ln);
				ok = false;
				break;
			}
			NoteEvent e = {.ch = (u8)v[0], .tick = v[1], .len = (u16)v[2], .vel = (u8)v[3], .layer = (u8)v[4],
			               .fx = (NoteFx)v[5], .count = (u8)v[6], .art = (u8)v[7], .strum = (i8)strum,
			               .string = (u8)v[9], .gate = (u16)v[10]};
			for (int i = 0; i < 6; i++)
				e.notes[i] = (u8)v[11 + i];
			if (e.ch >= FT_MAX_TRACKS)
				continue;
			if (!song_add(s, e)) {
				snprintf(err, errn, "out of memory");
				ok = false;
				break;
			}
		}
	}
	free(line);
	fclose(f);
	if (ok && !header) {
		snprintf(err, errn, "%s: empty file", path);
		ok = false;
	}
	if (ok)
		song_sort(s);
	return ok;
}

/* ---- undo ------------------------------------------------------------------ */

static int slot(const History *h, int i) { return (h->head + i) % UNDO_DEPTH; }

void history_init(History *h, const Song *s)
{
	memset(h, 0, sizeof *h);
	song_copy(&h->snap[0], s);
	snprintf(h->what[0], sizeof h->what[0], "open");
	h->n = 1;
	h->at = 0;
}

void history_free(History *h)
{
	for (int i = 0; i < UNDO_DEPTH; i++)
		song_free(&h->snap[i]);
	memset(h, 0, sizeof *h);
}

void history_push(History *h, const Song *s, const char *what)
{
	/* drop the redo branch */
	for (int i = h->at + 1; i < h->n; i++)
		song_free(&h->snap[slot(h, i)]);
	h->n = h->at + 1;
	if (h->n == UNDO_DEPTH) {
		song_free(&h->snap[slot(h, 0)]);
		h->head = (h->head + 1) % UNDO_DEPTH;
		h->n--;
		h->at--;
	}
	int k = slot(h, h->n);
	song_copy(&h->snap[k], s);
	snprintf(h->what[k], sizeof h->what[k], "%s", what);
	h->n++;
	h->at++;
}

void history_amend(History *h, const Song *s) { song_copy(&h->snap[slot(h, h->at)], s); }

const char *history_current(const History *h) { return h->what[slot(h, h->at)]; }

const char *history_undo(History *h, Song *s)
{
	if (h->at <= 0)
		return NULL;
	const char *what = h->what[slot(h, h->at)];
	h->at--;
	song_copy(s, &h->snap[slot(h, h->at)]);
	return what;
}

const char *history_redo(History *h, Song *s)
{
	if (h->at + 1 >= h->n)
		return NULL;
	h->at++;
	song_copy(s, &h->snap[slot(h, h->at)]);
	return h->what[slot(h, h->at)];
}
