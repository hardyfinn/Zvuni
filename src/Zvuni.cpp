// Zvuni.cpp
// Connects the Zvuni engine (ZvuniEngine.hpp) to the panel, and draws the
// display: the roughness curve for the current melody note, with a dot where
// the partner sits.
#include "plugin.hpp"
#include "ZvuniEngine.hpp"

using namespace zv;

static std::string noteName(float v) {
	static const char* N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	float midi = 60.f + v * 12.f;
	int m = (int) std::lround(midi);
	int cents = (int) std::lround((midi - m) * 100.f);
	int oct = (int) std::floor(m / 12.f) - 1;
	std::string s = string::f("%s%d", N[((m % 12) + 12) % 12], oct);
	if (cents != 0) s += string::f(" %+dc", cents);
	return s;
}

struct TensionQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		float t = getValue();
		std::string w = t < 0.02f ? "unison" : t < T_PEAK - 0.03f ? "rub growing" : t < T_PEAK + 0.03f ? "peak ring"
			: t < T_WIDE ? "past the peak, opening" : t > 0.98f ? "open fifth" : "gliding to the fifth";
		return string::f("%.0f%% (%s)", t * 100.f, w.c_str());
	}
};

struct RangeQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		float r = getValue();
		if (r >= 12.5f) return "off";
		return string::f("%.1f semitones above the drone", r);
	}
};

struct Zvuni : Module {
	enum ParamId {
		TENSION_PARAM, TENSION_ATT_PARAM, RING_PARAM, DRIFT_PARAM, CLING_PARAM, RANGE_PARAM, MODE_PARAM, SIDE_PARAM,
		VOWEL_PARAM, OPEN_PARAM, TRES_PARAM, ENS_PARAM, NASAL_PARAM, BREATH_PARAM, DRONE_LVL_PARAM, PARTNER_LVL_PARAM,
		HEIGHT_PARAM, SNAP_PARAM, HOLD_PARAM, AUTO_PARAM, BOTH_PARAM, GLIDE_PARAM, TO_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		TENSION_INPUT, RING_INPUT, DRIFT_INPUT, FLIP_INPUT, ACCENT_INPUT,
		VOWEL_INPUT, OPEN_INPUT, TRES_INPUT, ENS_INPUT, PARTNER_ON_INPUT,
		YELP_INPUT, RESOLVE_INPUT,
		DRONE_INPUT, MELODY_INPUT, GATE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		DRONE_OUTPUT, MELODY_OUTPUT, PARTNER_OUTPUT, MIX_L_OUTPUT, MIX_R_OUTPUT,
		MEL_VOCT_OUTPUT, PTR_VOCT_OUTPUT, ROUGH_OUTPUT, INTERVAL_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId { RESOLVE_LIGHT, YELP_LIGHT, ACCENT_LIGHT, LIGHTS_LEN };

	Engine e;
	dsp::SchmittTrigger yelpTrig;
	dsp::PulseGenerator yelpFlash;
	int seekIndex = 2;
	static constexpr float SEEK_SPEEDS[4] = {0.5f, 1.f, 1.5f, 3.f};

	Zvuni() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<TensionQuantity>(TENSION_PARAM, 0.f, 1.f, 0.f, "Tension: unison > ringing second > open fifth");
		configParam(TENSION_ATT_PARAM, -1.f, 1.f, 1.f, "Tension CV amount", "%", 0.f, 100.f);
		configParam(RING_PARAM, 0.f, 1.f, 0.5f, "Ring: where the peak roughness sits", "x", 4.f, 0.5f);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.15f, "Drift: slow wander of the partner's tuning", " cents", 0.f, 25.f);
		configParam(CLING_PARAM, 0.f, 1.f, 0.f, "Cling: pull the melody to ringing spots against the drone", "%", 0.f, 100.f);
		configParam<RangeQuantity>(RANGE_PARAM, 3.f, 13.f, 13.f, "Range: keep the melody within this span of the drone");
		configSwitch(MODE_PARAM, 0.f, 2.f, 0.f, "Mode", {"Hz: tracks roughness (the singer)", "Cents: fixed interval (tempered-style)", "Seek: hunts and settles"});
		configSwitch(SIDE_PARAM, 0.f, 1.f, 0.f, "Partner side", {"Below the melody", "Above the melody"});
		configParam(VOWEL_PARAM, 0.f, 1.f, 0.12f, "Vowel: a > e > i > o > u");
		configParam(OPEN_PARAM, 0.f, 1.f, 0.6f, "Open throat (brightness and push)", "%", 0.f, 100.f);
		configParam(TRES_PARAM, 0.f, 1.f, 0.3f, "Tresene (shake on held notes)", "%", 0.f, 100.f);
		configParam(ENS_PARAM, 0.f, 1.f, 0.f, "Ensemble: village (1 singer) > choir (6 singers)", "%", 0.f, 100.f);
		configParam(NASAL_PARAM, 0.f, 1.f, 0.2f, "Nasal", "%", 0.f, 100.f);
		configParam(BREATH_PARAM, 0.f, 1.f, 0.1f, "Breath", "%", 0.f, 100.f);
		configParam(DRONE_LVL_PARAM, 0.f, 1.f, 0.8f, "Drone level", "%", 0.f, 100.f);
		configParam(PARTNER_LVL_PARAM, 0.f, 1.f, 0.8f, "Partner level", "%", 0.f, 100.f);
		configParam(HEIGHT_PARAM, 3.f, 12.f, 7.f, "Yelp height", " semitones");
		configParam(SNAP_PARAM, 0.f, 1.f, 0.4f, "Yelp snap (how fast it flips)", " ms", 7.5f, 20.f);
		configParam(HOLD_PARAM, 0.f, 1.f, 0.46f, "Auto yelp after notes held at least", " s", 20.f, 0.1f);
		configSwitch(AUTO_PARAM, 0.f, 1.f, 1.f, "Auto yelp at the end of long notes", {"Off", "On"});
		configSwitch(BOTH_PARAM, 0.f, 1.f, 1.f, "Partner yelps too", {"Off", "On"});
		configParam(GLIDE_PARAM, 0.f, 1.f, 0.45f, "Resolve glide", " s", 400.f, 0.01f);
		configSwitch(TO_PARAM, 0.f, 2.f, 0.f, "Resolve to", {"Unison", "Fifth", "Octave"});

		configInput(TENSION_INPUT, "Tension CV (+-5 V with amount)");
		configInput(RING_INPUT, "Ring CV (+-5 V, +5 V = 2x)");
		configInput(DRIFT_INPUT, "Drift CV (0-10 V)");
		configInput(FLIP_INPUT, "Flip side gate (high = other side)");
		configInput(ACCENT_INPUT, "Accent gate (stronger Cling; try Aksak's ACCENT)");
		configInput(VOWEL_INPUT, "Vowel CV (0-10 V)");
		configInput(OPEN_INPUT, "Open throat CV (0-10 V)");
		configInput(TRES_INPUT, "Tresene CV (0-10 V)");
		configInput(ENS_INPUT, "Ensemble CV (0-10 V)");
		configInput(PARTNER_ON_INPUT, "Partner on (gate; unpatched = always on)");
		configInput(YELP_INPUT, "Yelp trigger");
		configInput(RESOLVE_INPUT, "Resolve gate (try Aksak's END)");
		configInput(DRONE_INPUT, "Drone pitch (V/oct; unpatched = first note of each phrase)");
		configInput(MELODY_INPUT, "Melody pitch (V/oct, up to 4 channels)");
		configInput(GATE_INPUT, "Melody gate (poly; unpatched = always on)");

		configOutput(DRONE_OUTPUT, "Drone audio");
		configOutput(MELODY_OUTPUT, "Melody audio (poly)");
		configOutput(PARTNER_OUTPUT, "Partner audio (poly)");
		configOutput(MIX_L_OUTPUT, "Mix left");
		configOutput(MIX_R_OUTPUT, "Mix right");
		configOutput(MEL_VOCT_OUTPUT, "Melody pitch after Range and Cling (V/oct, poly)");
		configOutput(PTR_VOCT_OUTPUT, "Partner pitch (V/oct, poly)");
		configOutput(ROUGH_OUTPUT, "Roughness: how hard melody and partner ring (0-10 V, poly)");
		configOutput(INTERVAL_OUTPUT, "Partner interval (1 V = 1 semitone, poly)");
		configLight(RESOLVE_LIGHT, "Resolving");

		e.setSampleRate(48000.f);
	}

	void onSampleRateChange(const SampleRateChangeEvent& ev) override { e.setSampleRate(ev.sampleRate); }

	float cv01(int in) { return inputs[in].getVoltage() / 10.f; }

	void process(const ProcessArgs& args) override {
		int chans = clamp(inputs[MELODY_INPUT].getChannels(), 1, MAX_CH);
		e.channels = chans;

		// settings (read every sample; cheap)
		TunerSettings& ts = e.ts;
		ts.tension = clamp(params[TENSION_PARAM].getValue() + inputs[TENSION_INPUT].getVoltage() / 10.f * params[TENSION_ATT_PARAM].getValue(), 0.f, 1.f);
		ts.ring = clamp(0.5f * std::pow(4.f, params[RING_PARAM].getValue()) * std::exp2(inputs[RING_INPUT].getVoltage() / 5.f), 0.25f, 4.f);
		ts.drift = clamp(params[DRIFT_PARAM].getValue() + cv01(DRIFT_INPUT), 0.f, 1.f);
		ts.mode = (int) params[MODE_PARAM].getValue();
		bool flip = inputs[FLIP_INPUT].getVoltage() >= 1.f;
		ts.below = (params[SIDE_PARAM].getValue() < 0.5f) != flip;
		ts.cling = params[CLING_PARAM].getValue();
		ts.range = params[RANGE_PARAM].getValue();
		ts.resolveTo = (int) params[TO_PARAM].getValue();
		ts.glide = 0.01f * std::pow(400.f, params[GLIDE_PARAM].getValue());
		ts.seekSpeed = SEEK_SPEEDS[clamp(seekIndex, 0, 3)];

		VoiceSettings& vs = e.vs;
		vs.vowel = clamp(params[VOWEL_PARAM].getValue() + cv01(VOWEL_INPUT), 0.f, 1.f);
		vs.open = clamp(params[OPEN_PARAM].getValue() + cv01(OPEN_INPUT), 0.f, 1.f);
		vs.tresene = clamp(params[TRES_PARAM].getValue() + cv01(TRES_INPUT), 0.f, 1.f);
		vs.ensemble = clamp(params[ENS_PARAM].getValue() + cv01(ENS_INPUT), 0.f, 1.f);
		vs.nasal = params[NASAL_PARAM].getValue();
		vs.breath = params[BREATH_PARAM].getValue();
		e.droneLevel = params[DRONE_LVL_PARAM].getValue();
		e.partnerLevel = params[PARTNER_LVL_PARAM].getValue();

		e.ys.height = params[HEIGHT_PARAM].getValue();
		e.ys.snap = 0.02f * std::pow(7.5f, params[SNAP_PARAM].getValue());
		e.yelpHold = 0.1f * std::pow(20.f, params[HOLD_PARAM].getValue());
		e.autoYelp = params[AUTO_PARAM].getValue() > 0.5f;
		e.yelpBoth = params[BOTH_PARAM].getValue() > 0.5f;

		// gates and pitches
		e.dronePatched = inputs[DRONE_INPUT].isConnected();
		if (e.dronePatched) e.droneV = inputs[DRONE_INPUT].getVoltage();
		bool gatePatched = inputs[GATE_INPUT].isConnected();
		for (int c = 0; c < chans; c++) {
			e.inMelV[c] = inputs[MELODY_INPUT].getPolyVoltage(c);
			e.inGate[c] = gatePatched ? inputs[GATE_INPUT].getPolyVoltage(c) >= 1.f : true;
		}
		for (int c = chans; c < MAX_CH; c++) e.inGate[c] = false;
		e.accent = inputs[ACCENT_INPUT].getVoltage() >= 1.f;
		e.partnerOn = inputs[PARTNER_ON_INPUT].isConnected() ? inputs[PARTNER_ON_INPUT].getVoltage() >= 1.f : true;
		e.resolve = inputs[RESOLVE_INPUT].getVoltage() >= 1.f;
		if (yelpTrig.process(inputs[YELP_INPUT].getVoltage(), 0.1f, 1.f)) {
			e.yelpAll();
			yelpFlash.trigger(0.15f);
		}

		e.process();

		outputs[DRONE_OUTPUT].setVoltage(e.outDrone);
		outputs[MIX_L_OUTPUT].setVoltage(e.outL);
		outputs[MIX_R_OUTPUT].setVoltage(e.outR);
		for (int o : {MELODY_OUTPUT, PARTNER_OUTPUT, MEL_VOCT_OUTPUT, PTR_VOCT_OUTPUT, ROUGH_OUTPUT, INTERVAL_OUTPUT})
			outputs[o].setChannels(chans);
		for (int c = 0; c < chans; c++) {
			Channel& C = e.ch[c];
			outputs[MELODY_OUTPUT].setVoltage(e.outMel[c], c);
			outputs[PARTNER_OUTPUT].setVoltage(e.outPartner[c], c);
			outputs[MEL_VOCT_OUTPUT].setVoltage(C.melV, c);
			outputs[PTR_VOCT_OUTPUT].setVoltage(C.partner.pitchV, c);
			outputs[ROUGH_OUTPUT].setVoltage(10.f * C.roughness, c);
			outputs[INTERVAL_OUTPUT].setVoltage(clamp(C.partnerCents / 100.f, -10.f, 10.f), c);
		}

		lights[RESOLVE_LIGHT].setBrightnessSmooth(e.ch[0].resolveAmt, args.sampleTime);
		lights[YELP_LIGHT].setBrightness(yelpFlash.process(args.sampleTime) || e.ch[0].melody.yelping ? 1.f : 0.f);
		lights[ACCENT_LIGHT].setBrightnessSmooth(e.accent ? 1.f : 0.f, args.sampleTime);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "seekIndex", json_integer(seekIndex));
		return root;
	}
	void dataFromJson(json_t* root) override {
		json_t* j = json_object_get(root, "seekIndex");
		if (j) seekIndex = clamp((int) json_integer_value(j), 0, 3);
	}
};
constexpr float Zvuni::SEEK_SPEEDS[4];

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
static const NVGcolor D_BG = nvgRGB(0x15, 0x16, 0x1A);
static const NVGcolor D_INK = nvgRGB(0xF1, 0xEC, 0xE0);
static const NVGcolor D_SOFT = nvgRGB(0x86, 0x82, 0x78);
static const NVGcolor D_RED = nvgRGB(0xE2, 0x4A, 0x4F);
static const NVGcolor D_GOLD = nvgRGB(0xF2, 0xC1, 0x4E);
static const NVGcolor D_GRID = nvgRGB(0x2A, 0x2B, 0x31);

static std::shared_ptr<window::Font> displayFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}

struct ZvuniDisplay : Widget {
	Zvuni* module = nullptr;

	static float xOfCents(float c) { return std::sqrt(clamp(std::fabs(c), 0.f, 750.f) / 750.f); }

	void text(const DrawArgs& a, float x, float y, const std::string& s, NVGcolor c, int align, float size) {
		nvgFontSize(a.vg, size);
		nvgTextAlign(a.vg, align);
		nvgFillColor(a.vg, c);
		nvgText(a.vg, x, y, s.c_str(), NULL);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) { Widget::drawLayer(args, layer); return; }
		float W = box.size.x, H = box.size.y;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, W, H, mm2px(1.4f));
		nvgFillColor(args.vg, D_BG);
		nvgFill(args.vg);
		std::shared_ptr<window::Font> font = displayFont();
		if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);

		// sample values (or a demo picture in the module browser)
		float melV = 0.75f, cents = -108.f, peakRing = 1.f, res = 0.f, rough = 0.f;
		int mode = MODE_HZ, chans = 1;
		bool yelp = false;
		float droneV = -1.f;
		if (module) {
			Channel& C = module->e.ch[0];
			melV = C.melV;
			cents = C.partnerCents;
			peakRing = module->e.ts.ring;
			res = C.resolveAmt;
			rough = C.roughness;
			mode = module->e.ts.mode;
			chans = module->e.channels;
			yelp = C.melody.yelping;
			droneV = module->e.droneV;
		} else {
			rough = 0.95f;
		}
		float f = voltToHz(melV);

		// curve area
		float pad = mm2px(1.6f);
		float cx = pad, cy = pad + mm2px(1.5f), cw = W * 0.64f, ch = H - cy - mm2px(4.2f);
		nvgStrokeWidth(args.vg, 1.f);
		for (float c : {100.f, 200.f, 300.f, 498.f, 702.f}) {
			float x = cx + cw * xOfCents(c);
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, x, cy);
			nvgLineTo(args.vg, x, cy + ch);
			nvgStrokeColor(args.vg, D_GRID);
			nvgStroke(args.vg);
		}
		const char* lab[5] = {"semi", "tone", "", "4th", "5th"};
		const float labC[5] = {100.f, 200.f, 300.f, 498.f, 702.f};
		for (int i = 0; i < 5; i++)
			if (lab[i][0]) text(args, cx + cw * xOfCents(labC[i]), cy + ch + mm2px(2.6f), lab[i], D_SOFT, NVG_ALIGN_CENTER, mm2px(2.0f));
		text(args, cx, cy + ch + mm2px(2.6f), "unison", D_SOFT, NVG_ALIGN_LEFT, mm2px(2.0f));
		text(args, cx, cy - mm2px(0.4f), "roughness against the melody", D_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, mm2px(1.9f));

		// the curve
		nvgBeginPath(args.vg);
		for (int i = 0; i <= 120; i++) {
			float u = i / 120.f;
			float c = 750.f * u * u;
			float r = roughness(f, gapFromCents(f, c));
			float x = cx + cw * u, y = cy + ch - ch * r;
			if (i == 0) nvgMoveTo(args.vg, x, y);
			else nvgLineTo(args.vg, x, y);
		}
		nvgStrokeColor(args.vg, D_INK);
		nvgStrokeWidth(args.vg, 1.3f);
		nvgStroke(args.vg);

		// where Ring puts the target peak
		float pc = centsFromGap(f, peakGapHz(f, peakRing));
		float px = cx + cw * xOfCents(pc);
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, px, cy + ch);
		nvgLineTo(args.vg, px, cy + ch - mm2px(1.6f));
		nvgStrokeColor(args.vg, D_RED);
		nvgStrokeWidth(args.vg, 1.6f);
		nvgStroke(args.vg);

		// the partner dot
		float pr = roughness(f, gapFromCents(f, std::fabs(cents)));
		float dx = cx + cw * xOfCents(cents), dy = cy + ch - ch * pr;
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, dx, dy, mm2px(1.0f));
		nvgFillColor(args.vg, D_GOLD);
		nvgFill(args.vg);

		// right column: numbers
		float tx = cx + cw + mm2px(4.f), ty = pad + mm2px(2.4f), lh = mm2px(3.4f);
		float fs = mm2px(2.5f);
		text(args, tx, ty, "MELODY  " + noteName(melV), D_INK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, fs);
		text(args, tx, ty + lh, "DRONE   " + noteName(droneV), D_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, fs);
		text(args, tx, ty + 2 * lh, string::f("PARTNER %+.0f cents %s", cents, cents < 0 ? "below" : "above"), D_GOLD, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, fs);
		float beat = std::fabs(voltToHz(melV + cents / 1200.f) - f);
		text(args, tx, ty + 3 * lh, string::f("BEATING %.1f Hz", beat), D_INK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, fs);
		static const char* MODES[3] = {"Hz (singer)", "Cents (fixed)", "Seek"};
		text(args, tx, ty + 4 * lh, string::f("MODE %s", MODES[clamp(mode, 0, 2)]), D_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, fs);
		std::string st = res > 0.05f ? "RESOLVING" : yelp ? "YELP!" : chans > 1 ? string::f("%d channels (shows ch 1)", chans) : "";
		text(args, tx, ty + 5 * lh, st, res > 0.05f || yelp ? D_RED : D_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, fs);

		// roughness meter
		float mx = tx, my = H - pad - mm2px(1.6f), mw = W - tx - pad;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, mx, my, mw, mm2px(1.4f));
		nvgFillColor(args.vg, D_GRID);
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgRect(args.vg, mx, my, mw * clamp(rough, 0.f, 1.f), mm2px(1.4f));
		nvgFillColor(args.vg, D_RED);
		nvgFill(args.vg);
		text(args, mx, my - mm2px(0.5f), "RING", D_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, mm2px(1.9f));

		Widget::drawLayer(args, layer);
	}
};

// ---------------------------------------------------------------------------
// Panel. Positions in mm, matching res/make_panel.py
// ---------------------------------------------------------------------------
struct ZvuniWidget : ModuleWidget {
	ZvuniWidget(Zvuni* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Zvuni.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		ZvuniDisplay* d = createWidget<ZvuniDisplay>(mm2px(Vec(6.0, 12.0)));
		d->box.size = mm2px(Vec(150.56, 25.0));
		d->module = module;
		addChild(d);

		const float kA = 52.5f, cvA = 65.5f, kB = 78.5f, cvB = 90.0f, kC = 101.0f;
		auto knob = [&](float x, float y, int p) { addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(x, y)), module, p)); };
		auto small = [&](float x, float y, int p) { addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(x, y)), module, p)); };
		auto in = [&](float x, float y, int i) { addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, y)), module, i)); };
		auto out = [&](float x, float y, int o) { addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x, y)), module, o)); };

		// partner
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(19.f, 53.0f)), module, Zvuni::TENSION_PARAM));
		knob(43, kA, Zvuni::RING_PARAM);
		in(11, cvA, Zvuni::TENSION_INPUT);
		addParam(createParamCentered<Trimpot>(mm2px(Vec(27, cvA)), module, Zvuni::TENSION_ATT_PARAM));
		in(43, cvA, Zvuni::RING_INPUT);
		knob(11, kB, Zvuni::DRIFT_PARAM);
		knob(27, kB, Zvuni::CLING_PARAM);
		knob(43, kB, Zvuni::RANGE_PARAM);
		in(11, cvB, Zvuni::DRIFT_INPUT);
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(11, kC)), module, Zvuni::MODE_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(22, kC)), module, Zvuni::SIDE_PARAM));
		in(33, kC, Zvuni::FLIP_INPUT);
		in(44, kC, Zvuni::ACCENT_INPUT);
		addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(48.6f, kC - 4.2f)), module, Zvuni::ACCENT_LIGHT));

		// voice
		knob(64, kA, Zvuni::VOWEL_PARAM);
		knob(80, kA, Zvuni::OPEN_PARAM);
		knob(96, kA, Zvuni::TRES_PARAM);
		in(64, cvA, Zvuni::VOWEL_INPUT);
		in(80, cvA, Zvuni::OPEN_INPUT);
		in(96, cvA, Zvuni::TRES_INPUT);
		knob(64, kB, Zvuni::ENS_PARAM);
		knob(80, kB, Zvuni::NASAL_PARAM);
		knob(96, kB, Zvuni::BREATH_PARAM);
		in(64, cvB, Zvuni::ENS_INPUT);
		small(64, kC, Zvuni::DRONE_LVL_PARAM);
		small(80, kC, Zvuni::PARTNER_LVL_PARAM);
		in(96, kC, Zvuni::PARTNER_ON_INPUT);

		// phrase
		knob(117, kA, Zvuni::HEIGHT_PARAM);
		knob(133, kA, Zvuni::SNAP_PARAM);
		knob(149, kA, Zvuni::HOLD_PARAM);
		in(117, cvA, Zvuni::YELP_INPUT);
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(121.6f, cvA - 4.2f)), module, Zvuni::YELP_LIGHT));
		addParam(createParamCentered<CKSS>(mm2px(Vec(133, cvA)), module, Zvuni::AUTO_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(149, cvA)), module, Zvuni::BOTH_PARAM));
		knob(117, kB, Zvuni::GLIDE_PARAM);
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(133, kB)), module, Zvuni::TO_PARAM));
		in(149, kB, Zvuni::RESOLVE_INPUT);
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(153.6f, kB - 4.2f)), module, Zvuni::RESOLVE_LIGHT));

		// bottom row
		const float jy = 117.6f;
		in(11, jy, Zvuni::DRONE_INPUT);
		in(24, jy, Zvuni::MELODY_INPUT);
		in(37, jy, Zvuni::GATE_INPUT);
		const int outs[9] = {Zvuni::DRONE_OUTPUT, Zvuni::MELODY_OUTPUT, Zvuni::PARTNER_OUTPUT, Zvuni::MIX_L_OUTPUT, Zvuni::MIX_R_OUTPUT,
			Zvuni::MEL_VOCT_OUTPUT, Zvuni::PTR_VOCT_OUTPUT, Zvuni::ROUGH_OUTPUT, Zvuni::INTERVAL_OUTPUT};
		for (int i = 0; i < 9; i++) out(56 + 12 * i, jy, outs[i]);
	}

	void appendContextMenu(Menu* menu) override {
		Zvuni* m = getModule<Zvuni>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Seek speed (Mode: Seek)",
			{"Slow (0.5 Hz)", "Medium (1 Hz)", "Normal (1.5 Hz)", "Fast (3 Hz)"}, &m->seekIndex));
	}
};

Model* modelZvuni = createModel<Zvuni, ZvuniWidget>("Zvuni");
