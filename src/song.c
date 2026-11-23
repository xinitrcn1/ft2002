/* The score: names, defaults and the event list of a Song.
 *
 * Ported from geosim's procedural music (src/client/audio/song.hpp and the
 * name tables of composer.cpp, C++20) to C23. */
#include "song.h"

#include <stdlib.h>
#include <string.h>

const char *mood_name(Mood m)
{
	switch(m) {
	case MOOD_CALM: return "calm";
	case MOOD_TENSE: return "tense";
	case MOOD_CRISIS: return "crisis";
	case MOOD_TRIUMPH: return "triumph";
	case MOOD_WARTIME: return "wartime";
	case MOOD_FRUSTRATED: return "frustrated";
	default: break;
	}
	return "?";
}

bool parse_mood(const char *text, Mood *out)
{
	for(int i = 0; i < MOOD_COUNT; ++i) {
		if(strcmp(text, mood_name((Mood)i)) == 0) {
			*out = (Mood)i;
			return true;
		}
	}
	return false;
}

const char *channel_name(int ch)
{
	static const char *const names[CH_COUNT] = {"kick", "snare", "hats", "bass", "lead", "arp", "pad", "fx", "counter", "choir"};
	return ch >= 0 && ch < CH_COUNT ? names[ch] : "?";
}

const char *section_name(SectionKind k)
{
	switch(k) {
	case SEC_INTRO: return "intro";
	case SEC_VERSE: return "verse";
	case SEC_CHORUS: return "chorus";
	case SEC_BRIDGE: return "bridge";
	case SEC_BREAKDOWN: return "breakdown";
	case SEC_DROP: return "drop";
	case SEC_OUTRO: return "outro";
	case SEC_GROOVE: return "groove";
	case SEC_ARP_SOLO: return "arp-solo";
	case SEC_CHOIR: return "choir";
	default: break;
	}
	return "?";
}

const char *art_name(int a)
{
	static const char *const names[ART_COUNT] = {"normal", "palm", "ring", "staccato", "harmonic", "dead", "slide", "bend", "hammer", "vibrato", "feedback"};
	return a >= 0 && a < ART_COUNT ? names[a] : "?";
}

Patch patch_default(void)
{
	return (Patch){
		.inst = INST_SYNTH,
		.mono = true,
		.w1 = WAVE_SAW,
		.w2 = WAVE_SAW,
		.duty = 0.5f,
		.spread = 12.0f,
		.attack = 0.005f,
		.decay = 0.2f,
		.sustain = 0.7f,
		.release = 0.15f,
		.cutoff = 4000.0f,
		.resonance = 0.2f,
		.fdecay = 0.2f,
		.key_track = 0.5f,
		.vib_rate = 5.0f,
		.vib_delay = 0.2f,
		.gain = 1.0f,
		.kick_pitch = 160.0f,
		.kick_end = 48.0f,
		.kick_decay = 0.35f,
		.kick_click = 0.5f,
		.kick_sweep = 0.032f,
		.kick_drive = 1.6f,
		.snare_tone = 190.0f,
		.snare_decay = 0.16f,
		.hat_decay = 0.045f,
		.hat_tone = 1.0f,
		.gtr = GTR_STEEL,
		.gtr_tone = 0.5f,
		.gtr_pick = 0.2f,
		.gtr_sustain = 1.0f,
		.gtr_gain = 0.3f,
		.gtr_body = 0.5f,
	};
}

void song_init(Song *s)
{
	*s = (Song){0};
	s->mood = MOOD_CALM;
	s->bpm = 120.0f; /* the C++ member initialiser */
	s->mix.delay_rows = 3.0f; /* dotted eighth */
	s->mix.delay_feedback = 0.4f;
	s->mix.reverb_size = 0.8f;
	for(int i = 0; i < FT_MAX_TRACKS; ++i) s->patches[i] = patch_default();
}

void song_free(Song *s)
{
	free(s->events);
	s->events = nullptr;
	s->nevents = s->cap_events = 0;
}

void song_copy(Song *dst, const Song *src)
{
	if(dst == src) return;
	NoteEvent *ev = nullptr;
	if(src->nevents) {
		ev = malloc((size_t)src->nevents * sizeof *ev);
		if(ev) memcpy(ev, src->events, (size_t)src->nevents * sizeof *ev);
	}
	free(dst->events);
	*dst = *src;
	dst->events = ev;
	dst->nevents = ev ? src->nevents : 0; /* allocation failure: no events */
	dst->cap_events = dst->nevents;
}

NoteEvent *song_add(Song *s, NoteEvent e)
{
	if(s->nevents == s->cap_events) {
		const u32 cap = s->cap_events ? s->cap_events * 2 : 256;
		NoteEvent *ev = realloc(s->events, (size_t)cap * sizeof *ev);
		if(!ev) return nullptr;
		s->events = ev;
		s->cap_events = cap;
	}
	s->events[s->nevents] = e;
	return &s->events[s->nevents++];
}

/* geosim sorts with std::stable_sort by tick: equal ticks keep insertion
 * order. A bottom-up merge sort gives the same order. */
void song_sort(Song *s)
{
	const u32 n = s->nevents;
	if(n < 2) return;
	NoteEvent *a = s->events;
	NoteEvent *tmp = malloc((size_t)n * sizeof *tmp);
	if(!tmp) { /* no memory: a stable insertion sort */
		for(u32 i = 1; i < n; ++i) {
			const NoteEvent e = a[i];
			u32 j = i;
			while(j > 0 && a[j - 1].tick > e.tick) {
				a[j] = a[j - 1];
				--j;
			}
			a[j] = e;
		}
		return;
	}
	NoteEvent *src = a, *dst = tmp;
	for(u32 width = 1; width < n; width *= 2) {
		for(u32 lo = 0; lo < n; lo += 2 * width) {
			const u32 mid = lo + width < n ? lo + width : n;
			const u32 hi = mid + width < n ? mid + width : n;
			u32 i = lo, j = mid, k = lo;
			while(i < mid && j < hi) dst[k++] = src[j].tick < src[i].tick ? src[j++] : src[i++];
			while(i < mid) dst[k++] = src[i++];
			while(j < hi) dst[k++] = src[j++];
		}
		NoteEvent *t = src;
		src = dst;
		dst = t;
	}
	if(src != a) memcpy(a, src, (size_t)n * sizeof *a);
	free(tmp);
}
