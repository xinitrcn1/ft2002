// Parity dump of geosim's ORIGINAL composer (C++20), compiled standalone:
//   c++ -std=c++20 -O2 -I$GEOSIM/src/client/audio dump.cpp $GEOSIM/src/client/audio/composer.cpp
// Prints every field of every song in the canonical text form that dump.c
// prints for the C port. Fields the C port has and geosim lacks (guitar
// patch fields, notes[4..5], art/strum/string) are printed with the
// values the port must leave (patch_default / 0).
// usage: dump [first_seed last_seed [menu_last]]  (default 1 300 20)
#include "composer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace geosim::client::audio;

static void fnv(uint64_t& h, char const* s)
{
	for(; *s; ++s) h = (h ^ static_cast<unsigned char>(*s)) * 0x100000001B3ull;
}

static void dump(char const* head, song const& s)
{
	std::printf("%s\n", head);
	std::printf("bpm %a key %d final_key %d mode %d total_bars %u seed %llu mood %s\n", static_cast<double>(s.bpm), s.key, s.final_key, s.mode, s.total_bars,
	            static_cast<unsigned long long>(s.seed), mood_name(s.m));
	std::printf("style %s\n", s.style.c_str());
	std::printf("desc %s\n", s.description.c_str());
	std::printf("tracks %d\n", num_channels);
	for(int c = 0; c < num_channels; ++c) {
		patch const& p = s.patches[c];
		std::printf("track %d %s\n", c, channel_name(c));
		std::printf("patch %d %d %d %d %d %d %d %a %a %a %a %a %a %a %d %a %d %d %a %a %a %a %a %a %a %a %a %a %a %a %a %a %a %a", c, p.kit ? 1 : 0, p.mono ? 1 : 0,
		            static_cast<int>(p.w1), static_cast<int>(p.w2), p.table1, p.table2, static_cast<double>(p.mix2), static_cast<double>(p.detune), static_cast<double>(p.semi2),
		            static_cast<double>(p.duty), static_cast<double>(p.pwm_depth), static_cast<double>(p.pwm_rate), static_cast<double>(p.sub), p.unison,
		            static_cast<double>(p.spread), p.poles4 ? 1 : 0, p.formant, static_cast<double>(p.attack), static_cast<double>(p.decay), static_cast<double>(p.sustain),
		            static_cast<double>(p.release), static_cast<double>(p.cutoff), static_cast<double>(p.resonance), static_cast<double>(p.fenv), static_cast<double>(p.fdecay),
		            static_cast<double>(p.key_track), static_cast<double>(p.glide), static_cast<double>(p.vib_depth), static_cast<double>(p.vib_rate),
		            static_cast<double>(p.vib_delay), static_cast<double>(p.crush_rate), static_cast<double>(p.drive), static_cast<double>(p.gain));
		std::printf(" %a %a %a %a %a %a %a %a %a %a", static_cast<double>(p.kick_pitch), static_cast<double>(p.kick_end), static_cast<double>(p.kick_decay),
		            static_cast<double>(p.kick_click), static_cast<double>(p.kick_sweep), static_cast<double>(p.kick_drive), static_cast<double>(p.snare_tone),
		            static_cast<double>(p.snare_decay), static_cast<double>(p.hat_decay), static_cast<double>(p.hat_tone));
		// guitar fields: the port's patch_default() values
		std::printf(" %d %a %a %a %a %a %d %d %d %d %d %d %d\n", 0, 0.5, static_cast<double>(0.2f), 1.0, static_cast<double>(0.3f), 0.5, 0, 0, 0, 0, 0, 0, 0);
		mix_settings const& x = s.mix;
		std::printf("mix %d %a %a %a %a %a %d %a %a\n", c, static_cast<double>(x.gain[c]), static_cast<double>(x.pan[c]), static_cast<double>(x.delay[c]),
		            static_cast<double>(x.reverb[c]), static_cast<double>(x.duck[c]), x.bright[c] ? 1 : 0, static_cast<double>(x.chorus[c]), static_cast<double>(x.highpass[c]));
	}
	std::printf("mixg %a %a %a\n", static_cast<double>(s.mix.delay_rows), static_cast<double>(s.mix.delay_feedback), static_cast<double>(s.mix.reverb_size));
	std::printf("sections %zu\n", s.sections.size());
	for(section const& sec : s.sections) std::printf("sec %d %u %u\n", static_cast<int>(sec.kind), sec.start_bar, sec.bars);
	uint64_t h = 0xCBF29CE484222325ull;
	std::string evs;
	char line[256];
	for(note_event const& e : s.events) {
		std::snprintf(line, sizeof line, "ev %u %u %u %u %u %u %u %u %u %u %u %u 0 0 %u 0 0\n", e.tick, e.len, e.ch, e.vel, e.layer, static_cast<unsigned>(e.fx), e.count, 0u,
		              e.notes[0], e.notes[1], e.notes[2], e.notes[3], e.gate);
		fnv(h, line);
		evs += line;
	}
	std::printf("events %zu %016llx\n", s.events.size(), static_cast<unsigned long long>(h));
	std::fputs(evs.c_str(), stdout);
}

int main(int argc, char** argv)
{
	unsigned long long first = 1, last = 300, menu_last = 20;
	if(argc >= 3) first = std::strtoull(argv[1], nullptr, 0), last = std::strtoull(argv[2], nullptr, 0);
	if(argc >= 4) menu_last = std::strtoull(argv[3], nullptr, 0);
	char head[128];
	for(unsigned long long seed = first; seed <= last; ++seed) {
		for(int m = 0; m < mood_count; ++m) {
			std::snprintf(head, sizeof head, "song %llu %s", seed, mood_name(static_cast<mood>(m)));
			dump(head, compose(seed, static_cast<mood>(m)));
		}
	}
	for(unsigned long long seed = 1; seed <= menu_last; ++seed) {
		std::snprintf(head, sizeof head, "menu %llu", seed);
		dump(head, compose_menu(seed));
	}
	return 0;
}
