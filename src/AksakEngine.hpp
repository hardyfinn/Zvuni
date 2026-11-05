// AksakEngine.hpp
// A clock divider for Bulgarian "limping" (aksak) meters: each cycle is a
// row of groups of 2 or 3 clock steps, such as 2+2+3 for Rachenitsa.
// No VCV Rack code, so it can be tested on its own.
#pragma once
#include <cstdint>

namespace zv {

static const int AKSAK_MAX_GROUPS = 8;
static const int AKSAK_CUSTOM = 7;
static const int AKSAK_PRESETS = 8;   // 7 meters + Custom

struct AksakPreset { const char* name; int n; int g[AKSAK_MAX_GROUPS]; };
static const AksakPreset AKSAK_TABLE[7] = {
	{"5: 2+3 (Paydushko)", 2, {2, 3}},
	{"7: 2+2+3 (Rachenitsa)", 3, {2, 2, 3}},
	{"7: 3+2+2", 3, {3, 2, 2}},
	{"9: 2+2+2+3 (Daychovo)", 4, {2, 2, 2, 3}},
	{"11: 2+2+3+2+2 (Kopanitsa)", 5, {2, 2, 3, 2, 2}},
	{"13: 2+2+3+2+2+2", 6, {2, 2, 3, 2, 2, 2}},
	{"15: 2+2+2+2+3+2+2", 7, {2, 2, 2, 2, 3, 2, 2}},
};

struct AksakClock {
	int base[AKSAK_MAX_GROUPS] = {2, 2, 3};
	int nBase = 3;
	int cur[AKSAK_MAX_GROUPS] = {2, 2, 3};
	int nCur = 3;
	int group = 0, step = 0;
	bool started = false;
	float mutate = 0.f;
	uint32_t rng = 0x6C8E9CF5u;

	struct Events { bool step, accent, longGroup, cycleStart; };

	float rand01() {
		rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
		return (rng >> 8) * (1.f / 16777216.f);
	}

	// Set the base pattern. It takes effect at the start of the next cycle.
	// n = 0 falls back to 2+2+3.
	void setPattern(const int* g, int n) {
		if (n <= 0) { base[0] = 2; base[1] = 2; base[2] = 3; nBase = 3; return; }
		if (n > AKSAK_MAX_GROUPS) n = AKSAK_MAX_GROUPS;
		for (int i = 0; i < n; i++) base[i] = g[i] == 3 ? 3 : 2;
		nBase = n;
	}

	void setPreset(int p) {
		if (p < 0 || p >= 7) return;
		setPattern(AKSAK_TABLE[p].g, AKSAK_TABLE[p].n);
	}

	void newCycle() {
		nCur = nBase;
		for (int i = 0; i < nCur; i++) cur[i] = base[i];
		// Mutate: chance this cycle that one group flips 2 <-> 3,
		// and a smaller chance (mutate squared) that a second one does.
		if (nCur > 0 && rand01() < mutate) {
			int a = (int) (rand01() * nCur) % nCur;
			cur[a] = cur[a] == 2 ? 3 : 2;
			if (nCur > 1 && rand01() < mutate * mutate) {
				int b = (a + 1 + (int) (rand01() * (nCur - 1))) % nCur;
				cur[b] = cur[b] == 2 ? 3 : 2;
			}
		}
	}

	void reset() { started = false; group = 0; step = 0; }

	Events clock() {
		Events e{true, false, false, false};
		if (!started) {
			started = true;
			newCycle();
			group = 0;
			step = 0;
		} else {
			step++;
			if (step >= cur[group]) {
				step = 0;
				group++;
				if (group >= nCur) {
					newCycle();
					group = 0;
				}
			}
		}
		e.accent = step == 0;
		e.longGroup = step == 0 && cur[group] == 3;
		e.cycleStart = step == 0 && group == 0;
		return e;
	}

	int cycleLength() const {
		int s = 0;
		for (int i = 0; i < nCur; i++) s += cur[i];
		return s;
	}
	int stepInCycle() const {
		int s = 0;
		for (int i = 0; i < group; i++) s += cur[i];
		return s + step;
	}
	bool inLastGroup() const { return started && group == nCur - 1; }
};

}  // namespace zv
