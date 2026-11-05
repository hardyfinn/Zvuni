// ZvuniEngine.hpp
// The sound engine for Zvuni, with no VCV Rack code in it so it can be tested
// on its own (see test/engine_test.cpp).
//
// Three "parts" sing: a drone, a melody, and a partner that sits a microtonal
// second away from the melody. The partner is tuned to a target ROUGHNESS
// (how fast the two voices beat against each other) instead of a fixed
// interval, which is how Bulgarian village singers tune by ear.
//
// Each part is made of 1 to 6 "singers" (the Ensemble knob). Each singer is a
// glottal-pulse oscillator through three formant filters.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace zv {

static const float PI_F = 3.14159265358979f;
static const float C4_HZ = 261.6256f;          // 0 V = C4, VCV standard
static const int MAX_CH = 4;                   // polyphony
static const int MAX_SINGERS = 6;
static const int CTRL_DIV = 32;                // control-rate divider

inline float clampf(float x, float a, float b) { return std::min(std::max(x, a), b); }
inline float voltToHz(float v) { return C4_HZ * std::exp2(v); }
inline float hzToVolt(float f) { return std::log2(f / C4_HZ); }
inline float smoothstep(float a) { a = clampf(a, 0.f, 1.f); return a * a * (3.f - 2.f * a); }

struct Rng {
	uint32_t s;
	explicit Rng(uint32_t seed = 0x2545F491u) : s(seed ? seed : 1u) {}
	uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	float uni() { return (next() >> 8) * (1.f / 16777216.f); }   // 0..1
	float bi() { return uni() * 2.f - 1.f; }                     // -1..1
};

// ---------------------------------------------------------------------------
// Roughness model
// The ear's critical bandwidth at frequency f (Zwicker's approximation).
// Two tones are roughest when they are about a quarter of a critical band
// apart. "ring" scales where Zvuni puts that peak (Ring knob, 0.5x to 2x).
// ---------------------------------------------------------------------------
inline float criticalBand(float f) {
	float k = f / 1000.f;
	return 25.f + 75.f * std::pow(1.f + 1.4f * k * k, 0.69f);
}
inline float peakGapHz(float f, float ring = 1.f) { return 0.25f * criticalBand(f) * ring; }
inline float centsFromGap(float f, float gapHz) { return 1200.f * std::log2((f + gapHz) / f); }
inline float gapFromCents(float f, float cents) { return f * (std::exp2(cents / 1200.f) - 1.f); }

// How rough two tones gapHz apart sound, 0..1, peaking at 1 on the peak gap.
inline float roughness(float f, float gapHz) {
	float u = std::fabs(gapHz) / peakGapHz(f, 1.f);
	float r = u * std::exp(1.f - u);
	return r * r;   // squared so wide intervals fall away quickly, as the ear hears them
}

// ---------------------------------------------------------------------------
// Tension -> partner interval in cents (always positive; the Side switch
// flips the sign).
//   0 .. 0.6  gap grows in Hz from unison to peak roughness
//   0.6 .. 0.8  past the peak towards a wide whole tone (240 cents)
//   0.8 .. 1  glides out to a perfect fifth
// ---------------------------------------------------------------------------
static const float T_PEAK = 0.6f, T_WIDE = 0.8f;
static const float WIDE_CENTS = 240.f, FIFTH_CENTS = 701.955f, OCTAVE_CENTS = 1200.f;

inline float tensionCents(float t, float f, float ring) {
	t = clampf(t, 0.f, 1.f);
	float pg = peakGapHz(f, ring);
	float peakC = centsFromGap(f, pg);
	float wide = std::max(WIDE_CENTS, peakC + 20.f);
	if (t <= T_PEAK) return centsFromGap(f, pg * (t / T_PEAK));
	if (t <= T_WIDE) return peakC + (wide - peakC) * (t - T_PEAK) / (T_WIDE - T_PEAK);
	return wide + (FIFTH_CENTS - wide) * smoothstep((t - T_WIDE) / (1.f - T_WIDE));
}

// ---------------------------------------------------------------------------
// Cling: pull a melody note (semitones above the drone) towards the nearest
// position that "rings" against the drone: unison, a ringing second either
// side, fourth, fifth, octave. Range folds notes into a window first.
// ---------------------------------------------------------------------------
inline float foldRange(float rel, float range) {
	if (range >= 12.5f) return rel;                 // knob fully up = off
	float lo = -2.f, hi = range;
	while (rel > hi) rel -= 12.f;
	while (rel < lo) rel += 12.f;
	if (rel > hi) rel = std::max(lo, hi - (rel - hi));
	return rel;
}

inline float clingTarget(float rel, float ringSemis) {
	const float c[] = {0.f, ringSemis, -ringSemis, 4.98f, 7.02f, 12.f - ringSemis, 12.f, 12.f + ringSemis,
		-4.98f, -7.02f, -12.f};
	float best = c[0], bd = 1e9f;
	for (float x : c) {
		float d = std::fabs(rel - x);
		if (d < bd) { bd = d; best = x; }
	}
	return best;
}

inline float applyCling(float rel, float cling, bool accent, float ringSemis) {
	if (cling <= 0.f) return rel;
	float s = accent ? std::min(1.f, cling * 1.5f) : cling * cling;
	float tgt = clingTarget(rel, ringSemis);
	return rel + (tgt - rel) * s;
}

// ---------------------------------------------------------------------------
// Filters
// ---------------------------------------------------------------------------
struct Biquad {
	float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
	void set(float B0, float B1, float B2, float A0, float A1, float A2) {
		float i = 1.f / A0;
		b0 = B0 * i; b1 = B1 * i; b2 = B2 * i; a1 = A1 * i; a2 = A2 * i;
	}
	// band-pass, 0 dB at the centre
	void bandpass(float fc, float bw, float sr) {
		fc = clampf(fc, 20.f, 0.45f * sr);
		float q = clampf(fc / std::max(bw, 10.f), 0.5f, 40.f);
		float w = 2.f * PI_F * fc / sr, al = std::sin(w) / (2.f * q), cw = std::cos(w);
		set(al, 0.f, -al, 1.f + al, -2.f * cw, 1.f - al);
	}
	void peaking(float fc, float q, float gainDb, float sr) {
		fc = clampf(fc, 20.f, 0.45f * sr);
		float A = std::pow(10.f, gainDb / 40.f);
		float w = 2.f * PI_F * fc / sr, al = std::sin(w) / (2.f * q), cw = std::cos(w);
		set(1.f + al * A, -2.f * cw, 1.f - al * A, 1.f + al / A, -2.f * cw, 1.f - al / A);
	}
	void notch(float fc, float q, float sr) {
		fc = clampf(fc, 20.f, 0.45f * sr);
		float w = 2.f * PI_F * fc / sr, al = std::sin(w) / (2.f * q), cw = std::cos(w);
		set(1.f, -2.f * cw, 1.f, 1.f + al, -2.f * cw, 1.f - al);
	}
	float process(float x) {
		float y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		return y;
	}
	void reset() { z1 = z2 = 0.f; }
};

// Soprano vowel formants (Hz, linear amplitude, bandwidth Hz): a e i o u
struct Formant { float f, a, bw; };
static const Formant VOWELS[5][3] = {
	{{800, 1.f, 80}, {1150, 0.50f, 90}, {2900, 0.025f, 120}},   // a
	{{350, 1.f, 60}, {2000, 0.10f, 100}, {2800, 0.178f, 120}},  // e
	{{270, 1.f, 60}, {2140, 0.25f, 90}, {2950, 0.050f, 100}},   // i
	{{450, 1.f, 70}, {800, 0.28f, 80}, {2830, 0.080f, 100}},    // o
	{{325, 1.f, 50}, {700, 0.16f, 60}, {2700, 0.018f, 170}},    // u
};

inline void vowelAt(float v, Formant out[3]) {
	v = clampf(v, 0.f, 1.f) * 4.f;
	int i = std::min(3, (int) v);
	float t = v - i;
	for (int k = 0; k < 3; k++) {
		const Formant& A = VOWELS[i][k];
		const Formant& B = VOWELS[i + 1][k];
		out[k].f = A.f * std::pow(B.f / A.f, t);
		out[k].a = A.a + (B.a - A.a) * t;
		out[k].bw = A.bw + (B.bw - A.bw) * t;
	}
}

// ---------------------------------------------------------------------------
// Glottal pulse oscillator: the derivative of a Rosenberg-style flow pulse.
// "open" (0..1) shortens the open phase and sharpens the closing edge,
// which adds upper harmonics (the open-throat belt). The closing edge is a
// step, smoothed with polyBLEP so high notes don't alias badly.
// ---------------------------------------------------------------------------
inline float polyBlep(float t) {   // t in samples from the step
	if (t >= 0.f && t < 1.f) return 2.f * t - t * t - 1.f;
	if (t > -1.f && t < 0.f) return t * t + 2.f * t + 1.f;
	return 0.f;
}

struct Glottal {
	float phase = 0.f;
	float flow = 0.f;    // last flow value (0..1), used to gate breath noise
	float process(float dp, float open) {
		open = clampf(open, 0.f, 1.f);
		float oq = 0.72f - 0.30f * open;
		float tn = oq * (0.40f - 0.25f * open);
		float tp = oq - tn;
		float p = phase;
		float y;
		if (p < tp) {
			y = (PI_F / (2.f * tp)) * std::sin(PI_F * p / tp);
			flow = 0.5f - 0.5f * std::cos(PI_F * p / tp);
		} else if (p < oq) {
			float u = 0.5f * PI_F * (p - tp) / tn;
			y = -(PI_F / (2.f * tn)) * std::sin(u);
			flow = std::cos(u);
		} else {
			y = 0.f;
			flow = 0.f;
		}
		// step of +H at p = oq (closing edge snaps back to zero)
		float H = PI_F / (2.f * tn);
		float d = p - oq;
		if (d < 0.f && d > -dp) y += 0.5f * H * polyBlep(d / dp);
		else if (d >= 0.f && d < dp) y += 0.5f * H * polyBlep(d / dp);
		phase += dp;
		if (phase >= 1.f) phase -= 1.f;
		return y * tn * 0.9f;
	}
};

// ---------------------------------------------------------------------------
// Settings shared by all parts (from the knobs, already combined with CV)
// ---------------------------------------------------------------------------
struct VoiceSettings {
	float vowel = 0.12f;     // 0..1  a e i o u
	float open = 0.6f;       // 0..1
	float nasal = 0.2f;      // 0..1
	float tresene = 0.3f;    // 0..1
	float breath = 0.1f;     // 0..1
	float ensemble = 0.f;    // 0..1 village -> choir
};

struct YelpSettings {
	float height = 7.f;      // semitones
	float snap = 0.06f;      // seconds
};

// One singer: oscillator + three formants + own envelope, tuning and timing.
struct Singer {
	Glottal osc;
	Biquad bp[3];
	float amp[3] = {1, 0.5f, 0.05f};
	float env = 0.f;
	float detune = 0.f;       // cents, fixed per singer
	float wander = 0.f, wanderTgt = 0.f, wanderTime = 0.f;
	float vowelOff = 0.f;
	float pan = 0.f;
	float delay = 0.f;        // onset delay, seconds
	float gateTime = 0.f;     // how long the part's gate has been high
	float tresPhase = 0.f, tresRate = 7.f, tresOff = 0.f, tresAmp = 1.f;
	float dp = 0.f;
	float lastY = 0.f;

	void reset() {
		env = 0.f;
		for (auto& b : bp) b.reset();
		osc.phase = 0.f;
	}
};

// ---------------------------------------------------------------------------
// A part: up to six singers on the same line.
// ---------------------------------------------------------------------------
struct ZvPart {
	Singer s[MAX_SINGERS];
	int n = 1;
	float pitchV = 0.f;        // target pitch, V/oct
	bool gate = false, prevGate = false;
	float heldTime = 0.f;      // time the gate has been high
	bool muted = false;        // after a yelp, until the next gate
	bool yelping = false;
	float yelpT = 0.f, yelpOffset = 0.f, yelpAmp = 1.f, yelpOpen = 0.f;
	float attack = 0.02f, release = 0.15f;
	bool useTres = true;
	float basePan = 0.f;
	Biquad notch, peak;
	float level = 1.f;
	Rng rng;

	void init(uint32_t seed, float pan) {
		rng = Rng(seed);
		basePan = pan;
		for (int i = 0; i < MAX_SINGERS; i++) {
			Singer& g = s[i];
			g.detune = i == 0 ? 0.f : rng.bi();
			g.vowelOff = i == 0 ? 0.f : rng.bi();
			g.pan = i == 0 ? 0.f : rng.bi();
			g.delay = i == 0 ? 0.f : rng.uni();
			g.osc.phase = rng.uni();
			g.tresRate = 6.f + 3.f * rng.uni();
			g.wanderTgt = rng.bi();
		}
	}

	void startYelp() {
		if (muted) return;
		yelping = true;
		yelpT = 0.f;
	}

	// control-rate update: pitch, envelopes' targets, formant coefficients
	void control(const VoiceSettings& vs, const YelpSettings& ys, float dt, float sr) {
		n = 1 + (int) std::lround(clampf(vs.ensemble, 0.f, 1.f) * (MAX_SINGERS - 1));
		bool rise = gate && !prevGate;
		if (rise) { muted = false; yelping = false; heldTime = 0.f; }
		prevGate = gate;
		if (gate) heldTime += dt;

		// yelp envelope
		yelpOffset = 0.f; yelpAmp = 1.f; yelpOpen = 0.f;
		if (yelping) {
			yelpT += dt;
			float snap = std::max(0.02f, ys.snap);
			float fall = 0.25f * snap + 0.01f;
			yelpOffset = ys.height * (1.f - std::exp(-4.f * yelpT / snap));
			yelpAmp = yelpT < snap ? 1.f : std::exp(-(yelpT - snap) / fall);
			yelpOpen = 0.5f * clampf(1.f - yelpT / (snap + fall), 0.f, 1.f);
			if (yelpT > snap + 6.f * fall) {
				yelping = false;
				muted = true;
				for (auto& g : s) g.env = 0.f;
			}
		}

		float open = clampf(vs.open + yelpOpen, 0.f, 1.f);
		Formant fm[3];
		float ens = clampf(vs.ensemble, 0.f, 1.f);
		for (int i = 0; i < n; i++) {
			Singer& g = s[i];
			// slow random tuning wander per singer
			g.wanderTime -= dt;
			if (g.wanderTime <= 0.f) { g.wanderTgt = rng.bi(); g.wanderTime = 0.3f + 0.5f * rng.uni(); }
			g.wander += (g.wanderTgt - g.wander) * std::min(1.f, dt / 0.4f);
			float cents = ens * (12.f * g.detune + 4.f * g.wander);

			// tresene: uneven shake that starts after the note has been held
			float tres = 0.f;
			g.tresAmp = 1.f;
			if (useTres && vs.tresene > 0.f && gate) {
				float ramp = clampf((heldTime - 0.25f) / 0.3f, 0.f, 1.f);
				g.tresPhase += g.tresRate * dt;
				if (g.tresPhase >= 1.f) { g.tresPhase -= 1.f; g.tresRate = 6.f + 3.f * rng.uni(); }
				float sn = std::sin(2.f * PI_F * g.tresPhase);
				tres = vs.tresene * ramp * 45.f * sn;
				g.tresAmp = 1.f - 0.12f * vs.tresene * ramp * (0.5f + 0.5f * sn);
			}
			float v = pitchV + (cents + tres) / 1200.f + yelpOffset / 12.f;
			float f = clampf(voltToHz(v), 20.f, 0.4f * sr);
			g.dp = f / sr;

			// envelope target (per-singer onset delay in choir mode)
			bool on = (gate || yelping) && !muted && heldTime >= g.delay * 0.04f * ens;
			float tgt = on ? 1.f : 0.f;
			float tau = on ? attack : release;
			g.env += (tgt - g.env) * std::min(1.f, dt / tau);

			// formants
			vowelAt(vs.vowel + 0.08f * ens * g.vowelOff, fm);
			for (int k = 0; k < 3; k++) {
				float ff = fm[k].f * (k == 0 ? 1.f + 0.12f * open : 1.f);
				float bw = fm[k].bw * (1.f - 0.3f * open);
				float a = fm[k].a * (k == 1 ? 1.f + 1.5f * open : k == 2 ? 1.f + 4.f * open : 1.f);
				// keep the formant above the fundamental, as singers do
				if (k == 0) ff = std::max(ff, f * 1.05f);
				g.bp[k].bandpass(ff, bw, sr);
				g.amp[k] = a;
			}
		}
		for (int i = n; i < MAX_SINGERS; i++) s[i].env = 0.f;

		notch.notch(1000.f, 1.4f, sr);
		peak.peaking(2500.f, 2.5f, 9.f * vs.nasal, sr);
	}

	// one audio sample. Returns mono; adds a stereo pair into L/R.
	float process(const VoiceSettings& vs, float open, Rng& noise, float& L, float& R) {
		float sum = 0.f, sl = 0.f, sr_ = 0.f;
		float ens = clampf(vs.ensemble, 0.f, 1.f);
		float op = clampf(open + yelpOpen, 0.f, 1.f);
		float breath = vs.breath * 0.35f + (yelping ? 0.15f : 0.f);
		for (int i = 0; i < n; i++) {
			Singer& g = s[i];
			if (g.env < 1e-5f) { g.lastY = 0.f; continue; }
			float src = g.osc.process(g.dp, op);
			src += breath * g.osc.flow * noise.bi();
			float y = 0.f;
			for (int k = 0; k < 3; k++) y += g.amp[k] * g.bp[k].process(src);
			y *= g.env * g.tresAmp;
			sum += y;
			float pan = clampf(basePan + 0.35f * ens * g.pan, -1.f, 1.f);
			sl += y * (0.5f - 0.5f * pan);
			sr_ += y * (0.5f + 0.5f * pan);
		}
		float norm = 1.f / std::sqrt((float) n);
		sum *= norm * yelpAmp * level;
		// nasal: notch around 1 kHz mixed in, plus a peak near 2.5 kHz
		float nz = notch.process(sum);
		sum = sum + vs.nasal * 0.7f * (nz - sum);
		sum = peak.process(sum);
		float k = norm * yelpAmp * level;
		L += sl * k;
		R += sr_ * k;
		return sum;
	}

	float envelope() const {
		float m = 0.f;
		for (int i = 0; i < n; i++) m = std::max(m, s[i].env);
		return m * yelpAmp;
	}
};

// ---------------------------------------------------------------------------
// Partner tuning settings and state per channel
// ---------------------------------------------------------------------------
enum Mode { MODE_HZ = 0, MODE_CENTS = 1, MODE_SEEK = 2 };
enum ResolveTo { TO_UNISON = 0, TO_FIFTH = 1, TO_OCTAVE = 2 };

struct TunerSettings {
	float tension = 0.5f;    // 0..1
	float ring = 1.f;        // 0.5..2
	float drift = 0.f;       // 0..1 -> 0..25 cents
	int mode = MODE_HZ;
	bool below = true;
	float cling = 0.f;       // 0..1
	float range = 13.f;      // semitones, >= 12.5 = off
	int resolveTo = TO_UNISON;
	float glide = 0.3f;      // seconds
	float seekSpeed = 1.5f;  // Hz
};

struct ZvChannel {
	ZvPart melody, partner;
	float melV = 0.f;           // after range + cling
	float partnerCents = 0.f;   // signed, current
	float seekPos = 0.f, seekVel = 0.f;
	float driftState = 0.f, driftTgt = 0.f, driftTime = 0.f;
	float resolveAmt = 0.f;
	float roughness = 0.f;      // 0..1, how hard melody and partner ring
	float targetCents = 0.f;
};

// ---------------------------------------------------------------------------
// The whole module's engine
// ---------------------------------------------------------------------------
struct ZvEngine {
	float sr = 48000.f;
	int channels = 1;
	ZvChannel ch[MAX_CH];
	ZvPart drone;
	TunerSettings ts;
	VoiceSettings vs;
	YelpSettings ys;
	bool autoYelp = true;
	float yelpHold = 0.4f;      // seconds a note must be held for an auto yelp
	bool yelpBoth = true;       // partner yelps too
	float droneLevel = 0.8f, partnerLevel = 0.8f;
	float outGain = 30.f;
	int ctrlCount = 0;
	Rng noise{0x9E3779B9u};
	Rng rng{0x1234567u};

	// latched drone when no drone input is patched
	bool dronePatched = false;
	float droneV = 0.f;
	float latchedDrone = 0.f;
	float silentTime = 10.f;

	// per-sample inputs
	float inMelV[MAX_CH] = {};
	bool inGate[MAX_CH] = {};
	bool accent = false;
	bool partnerOn = true;
	bool resolve = false;

	// outputs
	float outDrone = 0.f, outMel[MAX_CH] = {}, outPartner[MAX_CH] = {}, outL = 0.f, outR = 0.f;

	ZvEngine() {
		drone.init(11, 0.f);
		drone.useTres = false;
		drone.attack = 0.08f;
		drone.release = 0.6f;
		for (int c = 0; c < MAX_CH; c++) {
			ch[c].melody.init(101 + c * 7, -0.25f);
			ch[c].partner.init(503 + c * 13, 0.25f);
		}
	}

	void setSampleRate(float s) { sr = s; }

	void yelpAll() {
		for (int c = 0; c < channels; c++) {
			ch[c].melody.startYelp();
			if (yelpBoth) ch[c].partner.startYelp();
		}
	}

	float pitchAtCents(float melHz) const {   // cents of the partner from Tension
		float f = ts.mode == MODE_CENTS ? 440.f : melHz;
		return tensionCents(ts.tension, f, ts.ring);
	}

	void control() {
		float dt = CTRL_DIV / sr;

		// drone pitch: patched input, or first note of a phrase
		bool any = false;
		for (int c = 0; c < channels; c++) any = any || inGate[c];
		if (!dronePatched) {
			if (any && silentTime > 0.25f) latchedDrone = inMelV[0];
			droneV = latchedDrone;
		}
		silentTime = any ? 0.f : silentTime + dt;

		// drift (shared slow wander)
		for (int c = 0; c < channels; c++) {
			ZvChannel& C = ch[c];
			C.driftTime -= dt;
			if (C.driftTime <= 0.f) { C.driftTgt = rng.bi(); C.driftTime = 0.4f + 0.6f * rng.uni(); }
			C.driftState += (C.driftTgt - C.driftState) * std::min(1.f, dt / 0.3f);

			// melody: range fold + cling around the drone
			float rel = (inMelV[c] - droneV) * 12.f;
			rel = foldRange(rel, ts.range);
			float melHz0 = voltToHz(droneV + rel / 12.f);
			float ringSemis = tensionCents(T_PEAK, melHz0, ts.ring) / 100.f;
			rel = applyCling(rel, ts.cling, accent, ringSemis);
			C.melV = droneV + rel / 12.f;
			float melHz = voltToHz(C.melV);

			// partner interval
			float cents = pitchAtCents(melHz);
			// a big choir pulls the partner towards tempered semitones
			float nearest = std::round(cents / 100.f) * 100.f;
			cents += (nearest - cents) * 0.5f * clampf(vs.ensemble, 0.f, 1.f);
			cents += 25.f * ts.drift * C.driftState;
			float sign = ts.below ? -1.f : 1.f;
			float target = sign * cents;

			// resolve: glide to unison / fifth / octave while the gate is high
			float rTo = ts.resolveTo == TO_UNISON ? 0.f : ts.resolveTo == TO_FIFTH ? FIFTH_CENTS : OCTAVE_CENTS;
			float g = std::max(0.01f, ts.glide);
			C.resolveAmt += ((resolve ? 1.f : 0.f) - C.resolveAmt) * std::min(1.f, dt / (g * 0.33f));
			target = target + (sign * rTo - target) * C.resolveAmt;
			C.targetCents = target;

			if (ts.mode == MODE_SEEK) {
				// a damped spring: overshoots and settles, like finding the spot by ear
				float w = 2.f * PI_F * clampf(ts.seekSpeed, 0.1f, 8.f);
				float zeta = 0.25f;
				float acc = w * w * (target - C.seekPos) - 2.f * zeta * w * C.seekVel;
				C.seekVel += acc * dt;
				C.seekPos += C.seekVel * dt;
				C.partnerCents = C.seekPos;
			} else {
				C.partnerCents += (target - C.partnerCents) * std::min(1.f, dt / 0.01f);
				C.seekPos = C.partnerCents;
				C.seekVel = 0.f;
			}

			// gates and pitches into the parts
			bool g0 = inGate[c];
			if (C.melody.prevGate && !g0 && autoYelp && C.melody.heldTime >= yelpHold) {
				C.melody.startYelp();
				if (yelpBoth && partnerOn) C.partner.startYelp();
			}
			C.melody.gate = g0;
			C.partner.gate = g0 && partnerOn;
			C.melody.pitchV = C.melV;
			C.partner.pitchV = C.melV + C.partnerCents / 1200.f;
			C.partner.level = partnerLevel;
			C.melody.control(vs, ys, dt, sr);
			C.partner.control(vs, ys, dt, sr);

			float fm = voltToHz(C.melV), fp = voltToHz(C.partner.pitchV);
			C.roughness = roughness(std::min(fm, fp), fp - fm) * C.melody.envelope() * C.partner.envelope();
		}

		drone.gate = any;
		drone.pitchV = droneV;
		drone.level = droneLevel;
		drone.control(vs, ys, dt, sr);
	}

	void process() {
		if (ctrlCount <= 0) { control(); ctrlCount = CTRL_DIV; }
		ctrlCount--;
		float L = 0.f, R = 0.f;
		outDrone = soft(drone.process(vs, vs.open, noise, L, R) * outGain);
		for (int c = 0; c < channels; c++) {
			outMel[c] = soft(ch[c].melody.process(vs, vs.open, noise, L, R) * outGain);
			outPartner[c] = soft(ch[c].partner.process(vs, vs.open, noise, L, R) * outGain);
		}
		float k = outGain / std::sqrt((float) std::max(1, channels));
		outL = soft(L * k * 1.4f);
		outR = soft(R * k * 1.4f);
	}

	// gentle limiter that keeps outputs inside +-10 V
	static float soft(float x) { return 10.f * std::tanh(x * 0.1f); }
};

}  // namespace zv
