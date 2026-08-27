// -----------------------------------------------------------------------------
// Ogham for VCV Rack — the module
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// PHASE 1: the minimal module. Two voices, the four knobs that shape them, and
// Sync. No FX menu, no display, no CV outputs, no encoder — those are phases 2
// and 3. What this file is for is proving the boundary: that the firmware's own
// engine and pipeline run inside a Rack module, at any host rate, sounding like
// the module.
//
// The panel is a placeholder and so are the knobs; phase 4 replaces both with
// the module's own artwork.

#include "plugin.hpp"
#include "RateConverter.hpp"

#include "daisy_seed.h"

// The shim must be the daisy_seed.h that was found. If a real libDaisy ever
// lands on the include path ahead of src/shim, the plugin would compile against
// hardware headers and fail in ways that take a day to understand.
#ifndef OGHAM_SHIM_DAISY_SEED
#error "daisy_seed.h did not resolve to src/shim — check the include order"
#endif

#include "bytebeat_engine.h"
#include "ogham_audio_pipeline.h"
#include "formulas.h"

#include <string>

namespace {

// Out 1 and Out 2 leave the firmware's pipeline at roughly +/-1 and reach about
// +/-1.28 on peaks with the FX chain running. x5 puts nominal level at Eurorack
// +/-5 V and lets peaks sit above it, which real modules do too. Whether to keep
// that or scale down for headroom is an open question — ovcv-maw, and a row in
// docs/firmware-differences.md.
constexpr float kAudioVolts = 5.f;

// The firmware's AUDIO_OUT_LEVEL (0.51) is deliberately NOT applied. It halves
// the digital output to compensate an analog stage that runs about twice as hot
// as it should; it is a hardware correction, not part of the sound.

}  // namespace

struct Ogham : Module {
    enum ParamId {
        FUNC1_PARAM,
        FUNC2_PARAM,
        A_PARAM,
        B_PARAM,
        RATE_PARAM,
        TONE_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        SYNC_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        OUT1_OUTPUT,
        OUT2_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };

    // --- the firmware's own objects, compiled verbatim from the submodule ---
    BytebeatEngine engine;
    AudioPipeline  pipeline;
    FxChainConfig  fx;

    ogham::RateConverter conv;
    dsp::SchmittTrigger  syncTrigger;
    dsp::ClockDivider    controlDivider;

    // Last values pushed into the firmware objects. The setters are not all
    // cheap — SetLofiMacro and SetFxChain compute filter and envelope
    // coefficients with expf and sinf — so they are called on change only.
    int   lastFunc1 = -1, lastFunc2 = -1;
    int   lastA = -1, lastB = -1;
    float lastRate = -1.f, lastTone = -1.f;

    Ogham() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        const int lastSlot = GetFormulaCount() - 1;   // 100: the A440 reference
        configParam(FUNC1_PARAM, 0.f, (float)lastSlot, 0.f, "Voice 1 function");
        configParam(FUNC2_PARAM, 0.f, (float)lastSlot, 0.f, "Voice 2 function");
        paramQuantities[FUNC1_PARAM]->snapEnabled = true;
        paramQuantities[FUNC2_PARAM]->snapEnabled = true;

        configParam(A_PARAM, 0.f, 1.f, 0.5f, "Parameter A");
        configParam(B_PARAM, 0.f, 1.f, 0.5f, "Parameter B");
        configParam(RATE_PARAM, 0.f, 1.f, 0.5f, "Rate");
        configParam(TONE_PARAM, 0.f, 1.f, 0.5f, "Tone");

        configInput(SYNC_INPUT, "Sync");
        configOutput(OUT1_OUTPUT, "Voice 1");
        configOutput(OUT2_OUTPUT, "Voice 2");

        engine.Init();
        pipeline.Init();
        fx = AudioPipeline::DefaultFxChain();
        pipeline.SetFxChain(fx);

        controlDivider.setDivision(48);   // ~1 kHz at 48 kHz, as the module runs
        onSampleRateChange();
    }

    void onSampleRateChange() override {
        conv.setHostRate(APP->engine->getSampleRate());
        controlDivider.setDivision(
            std::max(1, (int)(APP->engine->getSampleRate() / 1000.f)));
    }

    void onReset(const ResetEvent& e) override {
        Module::onReset(e);
        engine.Init();
        pipeline.Init();
        fx = AudioPipeline::DefaultFxChain();
        pipeline.SetFxChain(fx);
        conv.reset();
        lastFunc1 = lastFunc2 = lastA = lastB = -1;
        lastRate = lastTone = -1.f;
    }

    // Everything the module's main loop does at ~1 kHz. Change-detected, since
    // several of these setters do real work.
    void applyControls() {
        const int f1 = (int)std::round(params[FUNC1_PARAM].getValue());
        if (f1 != lastFunc1) { lastFunc1 = f1; engine.SetFormula1(f1); }

        const int f2 = (int)std::round(params[FUNC2_PARAM].getValue());
        if (f2 != lastFunc2) { lastFunc2 = f2; engine.SetFormula2(f2); }

        // 0..255, the range the formulas are written against.
        const int a = (int)std::round(clamp(params[A_PARAM].getValue(), 0.f, 1.f) * 255.f);
        if (a != lastA) { lastA = a; engine.SetParamA(a); }

        const int b = (int)std::round(clamp(params[B_PARAM].getValue(), 0.f, 1.f) * 255.f);
        if (b != lastB) { lastB = b; engine.SetParamB(b); }

        // The firmware's own knob-to-rate map, so the curve and the 1x-at-noon
        // centre are the module's. Controls::MapKnobToRate is static and lives
        // in a translation unit the plugin does not compile, so phase 2's
        // transcription brings it across properly; for now, the same exponential
        // 1/64x to 64x span.
        const float ratePot = clamp(params[RATE_PARAM].getValue(), 0.f, 1.f);
        if (ratePot != lastRate) {
            lastRate = ratePot;
            engine.SetRate(std::pow(2.f, (ratePot - 0.5f) * 12.f));
        }

        const float tone = clamp(params[TONE_PARAM].getValue(), 0.f, 1.f);
        if (tone != lastTone) { lastTone = tone; pipeline.SetLofiMacro(tone); }
    }

    void process(const ProcessArgs& args) override {
        if (args.sampleRate != conv.hostRate()) onSampleRateChange();

        if (controlDivider.process()) applyControls();

        // Sync: the module takes this from an interrupt and consumes the flag at
        // the top of the next sample. Here the edge is found at host rate and
        // applied before this sample's core steps, which is the same ordering.
        if (syncTrigger.process(inputs[SYNC_INPUT].getVoltage(), 0.1f, 1.f)) {
            engine.SyncReset();
            pipeline.LpgTrigger();
        }

        const int steps = conv.advance();
        for (int i = 0; i < steps; i++) {
            float l = 0.f, r = 0.f;
            float* out[2] = { &l, &r };
            pipeline.Process(engine, out, 1);
            const float channels[ogham::RateConverter::kChannels] = { l, r, 0.f };
            conv.push(channels, false);
        }

        outputs[OUT1_OUTPUT].setVoltage(conv.read(0) * kAudioVolts);
        outputs[OUT2_OUTPUT].setVoltage(conv.read(1) * kAudioVolts);
    }
};

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
            mm2px(Vec(13.0, 46.0)), module, Ogham::A_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(37.8, 46.0)), module, Ogham::B_PARAM));

        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(13.0, 66.0)), module, Ogham::RATE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(37.8, 66.0)), module, Ogham::TONE_PARAM));

        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(9.0, 96.0)), module, Ogham::SYNC_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(9.0, 112.0)), module, Ogham::OUT1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(24.0, 112.0)), module, Ogham::OUT2_OUTPUT));
    }
};

Model* modelOgham = createModel<Ogham, OghamWidget>("Ogham");
