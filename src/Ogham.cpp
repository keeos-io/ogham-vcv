// -----------------------------------------------------------------------------
// Ogham for VCV Rack — the module
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// PHASE 2: the voice and control layer. Every panel control and every jack the
// module has, driving the transcribed application layer in src/OghamApp.cpp.
//
// Still to come: the display, the encoder and the 22-field menu (phase 3), and
// the real panel (phase 4). Func 1 and Func 2 are ordinary knobs here; the
// encoder that replaces them arrives with the interface layer.

#include "plugin.hpp"
#include "RateConverter.hpp"

#include "daisy_seed.h"

// The shim must be the daisy_seed.h that was found. If a real libDaisy ever
// lands on the include path ahead of src/shim, the plugin would compile against
// hardware headers and fail in ways that take a day to understand.
#ifndef OGHAM_SHIM_DAISY_SEED
#error "daisy_seed.h did not resolve to src/shim — check the include order"
#endif

#include "OghamApp.hpp"
#include "formulas.h"
#include "ogham_clock.h"

#include <string>

namespace {

// Out 1 and Out 2 leave the pipeline at roughly ±1 and reach ±1.5 on peaks with
// the FX chain running. ×5 puts nominal level at Eurorack ±5 V and lets peaks
// sit above it, which real modules do too — see ovcv-maw and the register.
constexpr float kAudioVolts = 5.f;

// ENV Out is DC-coupled on the module: the Daisy's DAC through a TL072 gain
// stage, 0–3.3 V scaled up to roughly 0–10 V.
constexpr float kEnvVolts = 10.f;

// EOC is a 5 V gate on the module, from a 74AHCT1G125 on the +5 V rail. Rack's
// convention is 10 V, and its own trigger threshold is 1 V, so 10 V it is.
constexpr float kGateVolts = 10.f;

// Rack's engine time, in microseconds, for the display's flash timeouts. Shared
// by every instance, which is correct: it is wall-clock, and they all agree.
uint64_t RackEngineMicros() {
    if (!APP || !APP->engine) return 0;
    const float sr = APP->engine->getSampleRate();
    if (sr <= 0.f) return 0;
    return (uint64_t)(APP->engine->getFrame() * (1000000.0 / (double)sr));
}

std::string FunctionLabel(int index) {
    const FormulaInfo* f = GetFormulaAt(index);
    const std::string name = (f && f->name) ? f->name : "?";
    if (index == GetReferenceIndex()) return "AA · " + name;
    return string::f("F%02d · %s", index, name.c_str());
}

}  // namespace

// ---------------------------------------------------------------------------
// Param quantities — the Rack-native half of "hardware-true panel, Rack-native
// shortcuts". The panel says what the module's panel says; the tooltip says what
// the module can only show one field of at a time.
// ---------------------------------------------------------------------------

struct Ogham;

struct FunctionQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        return FunctionLabel((int)std::round(getValue()));
    }
};

struct ParamABQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        return string::f("%d", (int)std::round(clamp(getValue(), 0.f, 1.f) * 255.f));
    }
};

struct RateQuantity : ParamQuantity {
    std::string getDisplayValueString() override;
};

struct ToneQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        const float v = getValue();
        const float d = v - 0.5f;
        if (std::fabs(d) < 0.02f) return "Clean";
        if (d < 0.f) return string::f("Filter / fold  %.0f%%", -d * 200.f);
        return string::f("Crush / resonance  %.0f%%", d * 200.f);
    }
};

// ---------------------------------------------------------------------------

struct Ogham : Module {
    enum ParamId {
        FUNC1_PARAM, FUNC2_PARAM,
        A_PARAM, B_PARAM,
        RATE_PARAM, TONE_PARAM,
        MODE_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        CV_A_INPUT, CV_B_INPUT,
        SYNC_INPUT, CLK_VOCT_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        OUT1_OUTPUT, OUT2_OUTPUT,
        ENV_OUTPUT, EOC_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId { LIGHTS_LEN };

    ogham::OghamApp      app;
    ogham::RateConverter conv;
    dsp::SchmittTrigger  syncTrigger;
    dsp::SchmittTrigger  clockTrigger;

    int lastFunc1 = -1, lastFunc2 = -1;

    Ogham() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        const int lastSlot = GetFormulaCount() - 1;   // 100: the A440 reference
        configParam<FunctionQuantity>(FUNC1_PARAM, 0.f, (float)lastSlot, 0.f,
                                      "Voice 1 function");
        configParam<FunctionQuantity>(FUNC2_PARAM, 0.f, (float)lastSlot, 0.f,
                                      "Voice 2 function");
        paramQuantities[FUNC1_PARAM]->snapEnabled = true;
        paramQuantities[FUNC2_PARAM]->snapEnabled = true;

        configParam<ParamABQuantity>(A_PARAM, 0.f, 1.f, 0.5f, "Parameter A");
        configParam<ParamABQuantity>(B_PARAM, 0.f, 1.f, 0.5f, "Parameter B");
        configParam<RateQuantity>(RATE_PARAM, 0.f, 1.f, 0.5f, "Rate");
        configParam<ToneQuantity>(TONE_PARAM, 0.f, 1.f, 0.5f, "Tone");
        configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Mode", {"Clock", "V/oct"});

        configInput(CV_A_INPUT, "Parameter A CV");
        configInput(CV_B_INPUT, "Parameter B CV");
        configInput(SYNC_INPUT, "Sync");
        configInput(CLK_VOCT_INPUT, "Clock / V-oct");

        configOutput(OUT1_OUTPUT, "Voice 1");
        configOutput(OUT2_OUTPUT, "Voice 2");
        configOutput(ENV_OUTPUT, "Envelope / CV");
        configOutput(EOC_OUTPUT, "End of cycle");

        ogham::shim::SetClockSource(&RackEngineMicros);
        app.Init();
        onSampleRateChange();
    }

    void onSampleRateChange() override {
        conv.setHostRate(APP->engine->getSampleRate());
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        app.Init();
        conv.reset();
        lastFunc1 = lastFunc2 = -1;
    }

    void process(const ProcessArgs& args) override {
        if (args.sampleRate != conv.hostRate()) onSampleRateChange();

        // Function selection is a param rather than app state, so that it can be
        // automated and MIDI-mapped — the one deliberate departure from
        // hardware-true. The encoder in phase 3 will drive these same params.
        const int f1 = (int)std::round(params[FUNC1_PARAM].getValue());
        if (f1 != lastFunc1) { lastFunc1 = f1; app.SetFormula1(f1); }
        const int f2 = (int)std::round(params[FUNC2_PARAM].getValue());
        if (f2 != lastFunc2) { lastFunc2 = f2; app.SetFormula2(f2); }

        ogham::AppInputs in;
        in.potA      = params[A_PARAM].getValue();
        in.potB      = params[B_PARAM].getValue();
        in.potRate   = params[RATE_PARAM].getValue();
        in.potTone   = params[TONE_PARAM].getValue();
        in.cvA       = inputs[CV_A_INPUT].getVoltage() / 5.f;
        in.cvB       = inputs[CV_B_INPUT].getVoltage() / 5.f;
        in.voctMode  = params[MODE_PARAM].getValue() > 0.5f;
        in.voctVolts = inputs[CLK_VOCT_INPUT].getVoltage();

        // Edges are found at host rate and handed to the FIRST core step of this
        // sample: worst-case error is one core sample, 20.8 us. On hardware they
        // arrive by interrupt and are consumed at the next sample, which is the
        // same ordering.
        const bool sync = syncTrigger.process(
            inputs[SYNC_INPUT].getVoltage(), 0.1f, 1.f);
        // The shared jack is a clock only in Clock mode; in V/oct it is a pitch
        // CV and must not be read as edges.
        const bool clock = !in.voctMode && clockTrigger.process(
            inputs[CLK_VOCT_INPUT].getVoltage(), 0.1f, 1.f);

        ogham::AppOutputs out;
        const int steps = conv.advance();
        for (int i = 0; i < steps; i++) {
            in.syncEdge  = sync  && (i == 0);
            in.clockEdge = clock && (i == 0);
            app.ProcessSample(in, out);
            const float channels[ogham::RateConverter::kChannels] = {
                out.out1, out.out2, out.env };
            conv.push(channels, out.eoc);
        }

        outputs[OUT1_OUTPUT].setVoltage(conv.read(0) * kAudioVolts);
        outputs[OUT2_OUTPUT].setVoltage(conv.read(1) * kAudioVolts);
        outputs[ENV_OUTPUT].setVoltage(conv.read(2) * kEnvVolts);
        outputs[EOC_OUTPUT].setVoltage(conv.gate() ? kGateVolts : 0.f);
    }
};

std::string RateQuantity::getDisplayValueString() {
    Ogham* m = dynamic_cast<Ogham*>(module);
    if (!m) return ParamQuantity::getDisplayValueString();

    const bool voct = m->params[Ogham::MODE_PARAM].getValue() > 0.5f;
    if (voct) {
        // Bipolar fine tune, ±12 semitones, 12 o'clock = 0.
        const float semis = (getValue() - 0.5f) * 24.f;
        return string::f("%+.2f semitones", semis);
    }
    const float rate = m->app.Rate();
    const char* how = m->app.ExternalClock() ? " (clocked)"
                    : m->app.ClockHeld()     ? " (clock held)"
                                             : "";
    if (rate >= 1.f) return string::f("%.2f×%s", rate, how);
    return string::f("1/%.2f×%s", 1.f / rate, how);
}

// ---------------------------------------------------------------------------
// Widget — placeholder. Phase 4 replaces the panel and every component with the
// module's own artwork; the VCV Component Library parts used here are CC BY-NC
// and go with it.
// ---------------------------------------------------------------------------

struct OghamWidget : ModuleWidget {
    OghamWidget(Ogham* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Ogham.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(
            Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Positions match res/Ogham.svg, in millimetres.
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(13.0, 24.0)), module, Ogham::FUNC1_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(37.8, 24.0)), module, Ogham::FUNC2_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(13.0, 44.0)), module, Ogham::A_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(37.8, 44.0)), module, Ogham::B_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(13.0, 62.0)), module, Ogham::RATE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(37.8, 62.0)), module, Ogham::TONE_PARAM));

        addParam(createParamCentered<CKSS>(
            mm2px(Vec(25.4, 78.0)), module, Ogham::MODE_PARAM));

        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(8.0, 92.0)), module, Ogham::CV_A_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(19.6, 92.0)), module, Ogham::CV_B_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(31.2, 92.0)), module, Ogham::SYNC_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(42.8, 92.0)), module, Ogham::CLK_VOCT_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(8.0, 110.0)), module, Ogham::OUT1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(19.6, 110.0)), module, Ogham::OUT2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(31.2, 110.0)), module, Ogham::ENV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(42.8, 110.0)), module, Ogham::EOC_OUTPUT));
    }
};

Model* modelOgham = createModel<Ogham, OghamWidget>("Ogham");
