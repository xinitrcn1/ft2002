/* Instrument presets (presets.h). */
#include "presets.h"
#include "guitar.h"

#include <string.h>

static Patch g_steel(void) { return gtr_preset(GTR_STEEL); }
static Patch g_nylon(void) { return gtr_preset(GTR_NYLON); }
static Patch g_clean(void) { return gtr_preset(GTR_CLEAN); }
static Patch g_crunch(void) { return gtr_preset(GTR_CRUNCH); }
static Patch g_lead(void) { return gtr_preset(GTR_LEAD); }
static Patch g_bass(void) { return gtr_preset(GTR_BASS); }
static Patch g_12(void) { return gtr_preset(GTR_12STRING); }

static Patch synth(void)
{
	Patch p = patch_default();
	p.inst = INST_SYNTH;
	p.poles4 = true;
	return p;
}

static Patch s_supersaw(void)
{
	Patch p = synth();
	p.unison = 4, p.spread = 18, p.cutoff = 3800, p.resonance = 0.15f, p.fenv = 1.2f, p.fdecay = 0.25f;
	p.attack = 0.004f, p.decay = 0.3f, p.sustain = 0.75f, p.release = 0.25f, p.vib_depth = 0.08f, p.vib_rate = 5.5f,
	p.gain = 0.8f;
	return p;
}

/* the euro lead of geosim's fifth reference: a saw and its octave, detuned */
static Patch s_euro(void)
{
	Patch p = synth();
	p.w2 = WAVE_SAW, p.mix2 = 0.6f, p.semi2 = 12, p.detune = 4, p.unison = 1, p.spread = 14;
	p.cutoff = 5500, p.key_track = 0.4f, p.fenv = 0.8f, p.fdecay = 0.05f;
	p.attack = 0.004f, p.decay = 0.2f, p.sustain = 0.72f, p.release = 0.16f, p.vib_depth = 0.1f, p.vib_rate = 6.8f;
	return p;
}

static Patch s_pad(void)
{
	Patch p = synth();
	p.mono = false, p.unison = 3, p.spread = 22, p.cutoff = 2200, p.resonance = 0.1f;
	p.attack = 0.6f, p.decay = 1.0f, p.sustain = 0.85f, p.release = 1.2f, p.gain = 0.7f;
	return p;
}

static Patch s_strings(void)
{
	Patch p = synth();
	p.mono = false, p.w2 = WAVE_SAW, p.mix2 = 0.8f, p.detune = 9, p.cutoff = 3000, p.key_track = 0.6f;
	p.attack = 0.25f, p.decay = 0.5f, p.sustain = 0.9f, p.release = 0.6f, p.vib_depth = 0.06f, p.vib_rate = 5.0f,
	p.vib_delay = 0.3f, p.gain = 0.75f;
	return p;
}

static Patch s_choir(void)
{
	Patch p = synth();
	p.mono = false, p.unison = 3, p.spread = 16, p.formant = 1, p.cutoff = 6000;
	p.attack = 0.35f, p.decay = 0.5f, p.sustain = 0.9f, p.release = 0.8f, p.vib_depth = 0.12f, p.vib_rate = 5.2f;
	return p;
}

static Patch s_pluck(void)
{
	Patch p = synth();
	p.w1 = WAVE_PULSE, p.duty = 0.3f, p.cutoff = 1800, p.fenv = 2.5f, p.fdecay = 0.12f, p.resonance = 0.3f;
	p.attack = 0.002f, p.decay = 0.25f, p.sustain = 0.0f, p.release = 0.2f;
	return p;
}

static Patch s_acid(void)
{
	Patch p = synth();
	p.cutoff = 500, p.resonance = 0.8f, p.fenv = 3.0f, p.fdecay = 0.18f, p.glide = 0.06f, p.drive = 0.3f;
	p.attack = 0.002f, p.decay = 0.3f, p.sustain = 0.5f, p.release = 0.08f, p.key_track = 0.3f;
	return p;
}

static Patch s_sub(void)
{
	Patch p = synth();
	p.w1 = WAVE_SINE, p.sub = 0.4f, p.cutoff = 900, p.attack = 0.004f, p.decay = 0.3f, p.sustain = 0.9f, p.release = 0.12f;
	return p;
}

static Patch k_kit(void)
{
	Patch p = patch_default();
	p.inst = INST_KIT;
	p.mono = false;
	return p;
}

static Patch k_hard(void)
{
	Patch p = k_kit();
	p.kick_pitch = 210, p.kick_end = 52, p.kick_sweep = 0.02f, p.kick_drive = 3.5f, p.kick_decay = 0.3f;
	p.snare_tone = 220, p.snare_decay = 0.2f;
	return p;
}

const Preset presets[] = {
    {"Steel acoustic", INST_GUITAR, g_steel},  {"Nylon classical", INST_GUITAR, g_nylon},
    {"12-string", INST_GUITAR, g_12},          {"Electric clean", INST_GUITAR, g_clean},
    {"Electric crunch", INST_GUITAR, g_crunch}, {"Electric lead", INST_GUITAR, g_lead},
    {"Electric bass", INST_GUITAR, g_bass},    {"Supersaw", INST_SYNTH, s_supersaw},
    {"Euro lead", INST_SYNTH, s_euro},         {"Warm pad", INST_SYNTH, s_pad},
    {"Strings", INST_SYNTH, s_strings},        {"Choir aah", INST_SYNTH, s_choir},
    {"Pluck", INST_SYNTH, s_pluck},            {"Acid bass", INST_SYNTH, s_acid},
    {"Sub bass", INST_SYNTH, s_sub},           {"Drum kit", INST_KIT, k_kit},
    {"Hard kit", INST_KIT, k_hard},
};
const int preset_count = (int)(sizeof presets / sizeof presets[0]);

Patch preset_patch(int i) { return i >= 0 && i < preset_count ? presets[i].make() : patch_default(); }

int preset_find(const char *name)
{
	for (int i = 0; i < preset_count; i++)
		if (!strcmp(presets[i].name, name))
			return i;
	return -1;
}
