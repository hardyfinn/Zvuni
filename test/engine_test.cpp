// Tests for the Zvuni and Aksak engines (no VCV Rack needed).
//   c++ -std=c++11 -O2 -o test/engine_test test/engine_test.cpp && test/engine_test
#include "../src/ZvuniEngine.hpp"
#include "../src/AksakEngine.hpp"
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

using namespace zv;

static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) {
	if (ok) passes++;
	else { fails++; std::printf("FAIL: %s\n", what.c_str()); }
}
static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

static void testRoughness() {
	// the table in the spec
	struct Row { float f, gap, cents; } rows[] = {{220, 26, 193}, {440, 28, 108}, {660, 32, 83}};
	for (auto& r : rows) {
		float g = peakGapHz(r.f);
		check(near(g, r.gap, 1.f), "peak gap at " + std::to_string((int) r.f) + " Hz = " + std::to_string(g));
		float c = centsFromGap(r.f, g);
		check(near(c, r.cents, 3.f), "peak cents at " + std::to_string((int) r.f) + " = " + std::to_string(c));
		check(near(roughness(r.f, g), 1.f, 1e-4f), "roughness peaks at 1");
		check(roughness(r.f, 0.f) < 1e-6f, "unison is smooth");
		check(roughness(r.f, gapFromCents(r.f, FIFTH_CENTS)) < 0.05f, "a fifth is smooth");
	}
	// higher notes need fewer cents for the same ring
	check(centsFromGap(880, peakGapHz(880)) < centsFromGap(440, peakGapHz(440)), "ringing second narrows going up");
}

static void testTension() {
	float f = 440.f;
	check(near(tensionCents(0.f, f, 1.f), 0.f, 0.01f), "tension 0 = unison");
	check(near(tensionCents(T_PEAK, f, 1.f), centsFromGap(f, peakGapHz(f)), 0.1f), "tension 0.6 = peak");
	check(near(tensionCents(T_WIDE, f, 1.f), WIDE_CENTS, 0.1f), "tension 0.8 = wide tone");
	check(near(tensionCents(1.f, f, 1.f), FIFTH_CENTS, 0.1f), "tension 1 = fifth");
	float prev = -1.f;
	bool mono = true;
	for (int i = 0; i <= 100; i++) {
		float c = tensionCents(i / 100.f, f, 1.f);
		if (c < prev - 1e-3f) mono = false;
		prev = c;
	}
	check(mono, "tension rises steadily");
	// Ring 2x moves the peak wider
	check(tensionCents(T_PEAK, f, 2.f) > tensionCents(T_PEAK, f, 1.f), "Ring widens the peak");
	// at a very low note the peak is wider than 240 cents: still rises
	check(tensionCents(T_WIDE, 80.f, 2.f) > tensionCents(T_PEAK, 80.f, 2.f), "low note keeps rising");
}

static void testCling() {
	float ring = 1.08f;
	check(near(applyCling(1.5f, 1.f, false, ring), ring, 1e-4f), "full cling lands on ringing second");
	check(near(applyCling(6.5f, 1.f, false, ring), 7.02f, 1e-4f), "full cling lands on fifth");
	check(near(applyCling(3.f, 0.f, false, ring), 3.f, 1e-6f), "no cling passes through");
	float weak = applyCling(4.f, 0.5f, false, ring), strong = applyCling(4.f, 0.5f, true, ring);
	check(std::fabs(strong - 4.98f) < std::fabs(weak - 4.98f), "accent pulls harder");
	check(near(foldRange(14.f, 13.f), 14.f, 1e-6f), "range off");
	float r = foldRange(9.f, 7.f);
	check(r <= 7.f && r >= -2.f, "range folds into window");
	check(near(foldRange(-5.f, 7.f), 7.f, 1e-6f), "range folds up an octave");
}

static float rms(const std::vector<float>& v, size_t from) {
	double s = 0;
	for (size_t i = from; i < v.size(); i++) s += v[i] * v[i];
	return (float) std::sqrt(s / (v.size() - from));
}

static void testEngineSound() {
	Engine e;
	e.setSampleRate(48000.f);
	e.dronePatched = true;
	e.droneV = -1.f;          // C3
	e.inMelV[0] = 0.75f;      // A4-ish
	e.inGate[0] = true;
	e.ts.mode = MODE_HZ;
	e.ts.tension = T_PEAK;
	std::vector<float> mel, par, dr, L;
	for (int i = 0; i < 48000; i++) {
		e.process();
		mel.push_back(e.outMel[0]);
		par.push_back(e.outPartner[0]);
		dr.push_back(e.outDrone);
		L.push_back(e.outL);
	}
	bool finite = true;
	float peak = 0.f;
	for (float x : L) { if (!std::isfinite(x)) finite = false; peak = std::max(peak, std::fabs(x)); }
	check(finite, "output is finite");
	check(peak <= 10.f, "output inside 10 V");
	float rm = rms(mel, 4800), rp = rms(par, 4800), rd = rms(dr, 4800), rl = rms(L, 4800);
	std::printf("  levels (V rms): melody %.2f partner %.2f drone %.2f mix L %.2f, mix peak %.2f\n", rm, rp, rd, rl, peak);
	float mp = 0; for (float x : mel) mp = std::max(mp, std::fabs(x));
	std::printf("  melody peak %.2f V\n", mp);
	check(rm > 0.8f && rm < 4.f, "melody at a sensible level");
	check(rp > 0.3f && rd > 0.3f, "partner and drone sound");
	float expect = -centsFromGap(voltToHz(0.75f), peakGapHz(voltToHz(0.75f)));
	check(near(e.ch[0].partnerCents, expect, 6.f), "partner sits at the ringing second below (" + std::to_string(e.ch[0].partnerCents) + " vs " + std::to_string(expect) + ")");
	check(e.ch[0].roughness > 0.8f, "roughness output high at peak");

	// Cents mode vs Hz mode an octave up: Hz mode narrows, Cents doesn't
	e.inMelV[0] = 1.75f;
	for (int i = 0; i < 4800; i++) e.process();
	float hzC = e.ch[0].partnerCents;
	e.ts.mode = MODE_CENTS;
	for (int i = 0; i < 4800; i++) e.process();
	float ctC = e.ch[0].partnerCents;
	check(std::fabs(hzC) < std::fabs(ctC), "Hz mode is narrower than Cents mode up high");

	// resolve to unison
	e.ts.mode = MODE_HZ;
	e.resolve = true;
	e.ts.glide = 0.1f;
	for (int i = 0; i < 24000; i++) e.process();
	check(std::fabs(e.ch[0].partnerCents) < 3.f, "resolve glides to unison");
	check(e.ch[0].roughness < 0.2f, "roughness drops on resolve");
	e.resolve = false;

	// seek: overshoots then settles
	e.ts.mode = MODE_SEEK;
	e.ts.seekSpeed = 1.5f;
	for (int i = 0; i < 48000 * 3; i++) e.process();
	check(near(e.ch[0].partnerCents, e.ch[0].targetCents, 3.f), "seek settles on target");
}

static void testYelp() {
	Engine e;
	e.setSampleRate(48000.f);
	e.dronePatched = true;
	e.inMelV[0] = 0.5f;
	e.inGate[0] = true;
	for (int i = 0; i < 48000; i++) e.process();   // held 1 s
	e.inGate[0] = false;                         // auto yelp
	bool sawYelp = false;
	float maxPitch = 0.f;
	for (int i = 0; i < 24000; i++) {
		e.process();
		if (e.ch[0].melody.yelping) sawYelp = true;
		maxPitch = std::max(maxPitch, e.ch[0].melody.yelpOffset);
	}
	check(sawYelp, "auto yelp after a held note");
	check(maxPitch > 5.f, "yelp flips up");
	check(e.ch[0].melody.muted && e.ch[0].melody.envelope() < 0.01f, "voice silent after the yelp");
	e.inGate[0] = true;
	for (int i = 0; i < 4800; i++) e.process();
	check(!e.ch[0].melody.muted && e.ch[0].melody.envelope() > 0.5f, "next note sings again");

	// a short note doesn't yelp
	e.inGate[0] = false;
	bool y2 = false;
	for (int i = 0; i < 4800; i++) { e.process(); y2 = y2 || e.ch[0].melody.yelping; }
	check(!y2, "short note: no yelp");
}

static void testPolyAndChoir() {
	Engine e;
	e.setSampleRate(44100.f);
	e.channels = 4;
	e.vs.ensemble = 1.f;
	e.vs.tresene = 1.f;
	e.vs.open = 1.f;
	e.vs.nasal = 1.f;
	e.vs.breath = 1.f;
	e.dronePatched = false;
	for (int c = 0; c < 4; c++) { e.inMelV[c] = 0.2f * c + 1.5f; e.inGate[c] = true; }
	float peak = 0.f;
	bool finite = true;
	for (int i = 0; i < 44100; i++) {
		e.process();
		for (int c = 0; c < 4; c++) if (!std::isfinite(e.outMel[c]) || !std::isfinite(e.outPartner[c])) finite = false;
		peak = std::max(peak, std::fabs(e.outL));
	}
	check(finite, "4 channels, full choir: finite");
	check(e.ch[0].melody.n == MAX_SINGERS, "ensemble full = 6 singers");
	check(near(e.droneV, 1.5f, 1e-6f), "unpatched drone latches the first note");
	std::printf("  full choir 4 ch mix peak %.2f V\n", peak);
	// choir pulls towards tempered: peak at A4 is ~108 c, pulled halfway to 100
	Engine f;
	f.dronePatched = true;
	f.inMelV[0] = 0.75f;
	f.inGate[0] = true;
	f.vs.ensemble = 1.f;
	f.ts.tension = T_PEAK;
	for (int i = 0; i < 9600; i++) f.process();
	float c = std::fabs(f.ch[0].partnerCents);
	float raw = centsFromGap(voltToHz(0.75f), peakGapHz(voltToHz(0.75f)));
	check(std::fabs(c - 100.f) < std::fabs(raw - 100.f), "choir pulls the partner towards tempered");
}

static void testAksak() {
	Aksak a;
	a.setPreset(1);   // 2+2+3
	std::string acc;
	for (int i = 0; i < 14; i++) {
		auto e = a.clock();
		acc += e.accent ? (e.longGroup ? 'L' : 'A') : '.';
	}
	check(acc == "A.A.L..A.A.L..", "rachenitsa accents: " + acc);
	a.setPreset(4);   // kopanitsa takes over at the next cycle
	acc.clear();
	bool sawStart = false;
	for (int i = 0; i < 11; i++) { auto e = a.clock(); acc += e.accent ? (e.longGroup ? 'L' : 'A') : '.'; if (i == 0) sawStart = e.cycleStart; }
	check(sawStart && acc == "A.A.L..A.A.", "kopanitsa after the cycle: " + acc);
	a.reset();
	auto e = a.clock();
	check(e.cycleStart && a.stepInCycle() == 0, "reset restarts the cycle");
	// mutate 0 = fixed, mutate 1 = always changes
	a.setPreset(1);
	a.reset();
	a.mutate = 0.f;
	bool same = true;
	for (int i = 0; i < 70; i++) { a.clock(); if (a.cycleLength() != 7) same = false; }
	check(same, "mutate 0 keeps the meter");
	a.mutate = 1.f;
	a.reset();
	int changed = 0, cycles = 0;
	for (int i = 0; i < 200; i++) {
		auto ev = a.clock();
		if (ev.cycleStart) {
			cycles++;
			bool diff = a.nCur != a.nBase;
			for (int k = 0; k < a.nCur; k++) if (a.cur[k] != a.base[k]) diff = true;
			if (diff) changed++;
		}
	}
	check(changed == cycles && cycles > 10, "mutate 1 changes every cycle");
	// last group
	a.mutate = 0.f;
	a.setPreset(1);
	a.reset();
	std::string last;
	for (int i = 0; i < 7; i++) { a.clock(); last += a.inLastGroup() ? '1' : '0'; }
	check(last == "0000111", "end gate during last group: " + last);
	a.setPattern(nullptr, 0);
	check(a.nBase == 3, "empty custom falls back to 2+2+3");
}

int main() {
	testRoughness();
	testTension();
	testCling();
	testEngineSound();
	testYelp();
	testPolyAndChoir();
	testAksak();
	std::printf("%d passed, %d failed\n", passes, fails);
	return fails ? 1 : 0;
}
