// Aksak.cpp
// Clock divider for Bulgarian limping meters. Each incoming clock is one step;
// steps are grouped in 2s and 3s. Engine in AksakEngine.hpp.
#include "plugin.hpp"
#include "AksakEngine.hpp"

using namespace zv;

struct Aksak : Module {
	enum ParamId { METER_PARAM, MUTATE_PARAM, ENUMS(GROUP_PARAM, AKSAK_MAX_GROUPS), PARAMS_LEN };
	enum InputId { METER_INPUT, MUTATE_INPUT, CLOCK_INPUT, RESET_INPUT, INPUTS_LEN };
	enum OutputId { STEP_OUTPUT, ACCENT_OUTPUT, LONG_OUTPUT, END_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(GROUP_LIGHT, AKSAK_MAX_GROUPS * 2), LIGHTS_LEN };

	zv::Aksak a;
	int custom[AKSAK_MAX_GROUPS] = {2, 2, 3, 0, 0, 0, 0, 0};   // 0 = off
	int meter = 1;
	dsp::SchmittTrigger clockTrig, resetTrig;
	dsp::BooleanTrigger btn[AKSAK_MAX_GROUPS];
	dsp::PulseGenerator stepP, accentP, longP;
	float sinceReset = 1.f;

	Aksak() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		std::vector<std::string> names;
		for (int i = 0; i < 7; i++) names.push_back(AKSAK_TABLE[i].name);
		names.push_back("Custom (buttons below)");
		configSwitch(METER_PARAM, 0.f, 7.f, 1.f, "Meter", names);
		getParamQuantity(METER_PARAM)->snapEnabled = true;
		configParam(MUTATE_PARAM, 0.f, 1.f, 0.f, "Mutate: chance per cycle that a group flips 2 <> 3", "%", 0.f, 100.f);
		for (int i = 0; i < AKSAK_MAX_GROUPS; i++)
			configButton(GROUP_PARAM + i, string::f("Custom group %d: off > 2 > 3", i + 1));
		configInput(METER_INPUT, "Meter CV (each 1.25 V = next meter)");
		configInput(MUTATE_INPUT, "Mutate CV (0-10 V)");
		configInput(CLOCK_INPUT, "Clock (one pulse per step, e.g. 16ths)");
		configInput(RESET_INPUT, "Reset (next clock starts the cycle)");
		configOutput(STEP_OUTPUT, "Step trigger (every clock)");
		configOutput(ACCENT_OUTPUT, "Accent trigger (start of each group)");
		configOutput(LONG_OUTPUT, "Long trigger (start of each 3-group)");
		configOutput(END_OUTPUT, "End gate (high during the last group of the cycle)");
		applyPattern();
	}

	void applyPattern() {
		if (meter < AKSAK_CUSTOM) { a.setPreset(meter); return; }
		int g[AKSAK_MAX_GROUPS], n = 0;
		for (int i = 0; i < AKSAK_MAX_GROUPS; i++) if (custom[i]) g[n++] = custom[i];
		a.setPattern(g, n);
	}

	void onReset() override {
		int def[AKSAK_MAX_GROUPS] = {2, 2, 3, 0, 0, 0, 0, 0};
		for (int i = 0; i < AKSAK_MAX_GROUPS; i++) custom[i] = def[i];
		a.reset();
	}

	void process(const ProcessArgs& args) override {
		int m = clamp((int) std::round(params[METER_PARAM].getValue() + inputs[METER_INPUT].getVoltage() * 0.8f), 0, 7);
		bool changed = m != meter;
		meter = m;
		for (int i = 0; i < AKSAK_MAX_GROUPS; i++) {
			if (btn[i].process(params[GROUP_PARAM + i].getValue() > 0.f)) {
				custom[i] = custom[i] == 0 ? 2 : custom[i] == 2 ? 3 : 0;
				changed = true;
			}
		}
		if (changed) applyPattern();
		a.mutate = clamp(params[MUTATE_PARAM].getValue() + inputs[MUTATE_INPUT].getVoltage() / 10.f, 0.f, 1.f);

		if (resetTrig.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
			a.reset();
			sinceReset = 0.f;
		}
		sinceReset += args.sampleTime;
		// ignore a clock arriving within 1 ms of a reset, so they can share an edge
		if (clockTrig.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f) && (sinceReset > 1e-3f || !a.started)) {
			auto ev = a.clock();
			stepP.trigger(5e-3f);
			if (ev.accent) accentP.trigger(5e-3f);
			if (ev.longGroup) longP.trigger(5e-3f);
		}
		outputs[STEP_OUTPUT].setVoltage(stepP.process(args.sampleTime) ? 10.f : 0.f);
		outputs[ACCENT_OUTPUT].setVoltage(accentP.process(args.sampleTime) ? 10.f : 0.f);
		outputs[LONG_OUTPUT].setVoltage(longP.process(args.sampleTime) ? 10.f : 0.f);
		outputs[END_OUTPUT].setVoltage(a.inLastGroup() ? 10.f : 0.f);

		for (int i = 0; i < AKSAK_MAX_GROUPS; i++) {
			lights[GROUP_LIGHT + 2 * i].setBrightness(custom[i] == 2 ? 1.f : 0.f);       // green = 2
			lights[GROUP_LIGHT + 2 * i + 1].setBrightness(custom[i] == 3 ? 1.f : 0.f);   // red = 3
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* arr = json_array();
		for (int i = 0; i < AKSAK_MAX_GROUPS; i++) json_array_append_new(arr, json_integer(custom[i]));
		json_object_set_new(root, "custom", arr);
		return root;
	}
	void dataFromJson(json_t* root) override {
		json_t* arr = json_object_get(root, "custom");
		if (arr) {
			for (int i = 0; i < AKSAK_MAX_GROUPS; i++) {
				json_t* v = json_array_get(arr, i);
				int x = v ? (int) json_integer_value(v) : 0;
				custom[i] = (x == 2 || x == 3) ? x : 0;
			}
		}
		applyPattern();
	}
};

static const NVGcolor A_BG = nvgRGB(0x15, 0x16, 0x1A);
static const NVGcolor A_INK = nvgRGB(0xF1, 0xEC, 0xE0);
static const NVGcolor A_SOFT = nvgRGB(0x86, 0x82, 0x78);
static const NVGcolor A_RED = nvgRGB(0xE2, 0x4A, 0x4F);
static const NVGcolor A_BOX = nvgRGB(0x2A, 0x2B, 0x31);

struct AksakDisplay : Widget {
	Aksak* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) { Widget::drawLayer(args, layer); return; }
		float W = box.size.x, H = box.size.y;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, W, H, mm2px(1.4f));
		nvgFillColor(args.vg, A_BG);
		nvgFill(args.vg);
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);

		int groups[AKSAK_MAX_GROUPS] = {2, 2, 3};
		int n = 3, g = 0, s = 0, meter = 1;
		bool started = true;
		if (module) {
			const zv::Aksak& a = module->a;
			n = a.nCur;
			for (int i = 0; i < n; i++) groups[i] = a.cur[i];
			g = a.group; s = a.step; started = a.started;
			meter = module->meter;
		}
		int total = 0;
		for (int i = 0; i < n; i++) total += groups[i];
		float pad = mm2px(1.4f);

		// title line: the pattern as numbers
		std::string pat;
		for (int i = 0; i < n; i++) pat += (i ? "+" : "") + std::to_string(groups[i]);
		std::string title = meter < AKSAK_CUSTOM ? AKSAK_TABLE[meter].name : "Custom";
		size_t paren = title.find(" (");
		std::string word = paren != std::string::npos ? title.substr(paren + 2, title.size() - paren - 3) : (meter == AKSAK_CUSTOM ? "Custom" : "");
		nvgFontSize(args.vg, mm2px(2.4f));
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		nvgFillColor(args.vg, A_INK);
		nvgText(args.vg, pad, pad, string::f("%d: %s", total, pat.c_str()).c_str(), NULL);
		nvgFontSize(args.vg, mm2px(2.0f));
		nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		nvgFillColor(args.vg, A_SOFT);
		nvgText(args.vg, W - pad, pad + mm2px(0.3f), word.c_str(), NULL);

		// boxes: one per step, groups separated by a gap
		float y = pad + mm2px(4.6f), h = H - y - pad;
		float gap = mm2px(1.0f);
		float stepW = (W - 2 * pad - gap * (n - 1)) / std::max(1, total);
		float x = pad;
		for (int i = 0; i < n; i++) {
			for (int k = 0; k < groups[i]; k++) {
				bool cur = started && i == g && k == s;
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x + 0.6f, y, stepW - 1.2f, h);
				nvgFillColor(args.vg, cur ? (k == 0 ? A_RED : A_INK) : A_BOX);
				nvgFill(args.vg);
				if (k == 0) {   // accent mark
					nvgBeginPath(args.vg);
					nvgRect(args.vg, x + 0.6f, y, stepW - 1.2f, mm2px(0.7f));
					nvgFillColor(args.vg, groups[i] == 3 ? A_RED : A_SOFT);
					nvgFill(args.vg);
				}
				x += stepW;
			}
			x += gap;
		}
		Widget::drawLayer(args, layer);
	}
};

struct AksakWidget : ModuleWidget {
	AksakWidget(Aksak* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Aksak.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		AksakDisplay* d = createWidget<AksakDisplay>(mm2px(Vec(4.0, 12.0)));
		d->box.size = mm2px(Vec(42.8, 16.0));
		d->module = module;
		addChild(d);

		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(14, 40)), module, Aksak::METER_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(37, 40)), module, Aksak::MUTATE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(14, 51.5)), module, Aksak::METER_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(37, 51.5)), module, Aksak::MUTATE_INPUT));

		const float bx[4] = {10.0f, 20.3f, 30.5f, 40.8f};
		for (int i = 0; i < AKSAK_MAX_GROUPS; i++) {
			float x = bx[i % 4], y = i < 4 ? 67.5f : 76.0f;
			addParam(createLightParamCentered<VCVLightBezel<GreenRedLight>>(mm2px(Vec(x, y)), module, Aksak::GROUP_PARAM + i, Aksak::GROUP_LIGHT + 2 * i));
		}

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(14, 92.3)), module, Aksak::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(37, 92.3)), module, Aksak::RESET_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(14, 107.4)), module, Aksak::STEP_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(37, 107.4)), module, Aksak::ACCENT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(14, 118.6)), module, Aksak::LONG_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(37, 118.6)), module, Aksak::END_OUTPUT));
	}
};

Model* modelAksak = createModel<Aksak, AksakWidget>("Aksak");
