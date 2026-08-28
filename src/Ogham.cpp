// -----------------------------------------------------------------------------
// Ogham for VCV Rack — the module
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// PHASE 3: the interface layer. Every panel control and every jack the module
// has, plus the Func encoder, the four-digit display and the 22-field menu
// behind them, driving the transcribed application layer in src/OghamApp.cpp.
//
// Still to come: the real panel and component art (phase 4).

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
#include "widgets.hpp"
#include "menu/FxMenu.hpp"
#include "formulas.h"
#include "ogham_clock.h"

#include <atomic>
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
        // The clean band comes from the same constants the panel dot does, so
        // the tooltip and the display cannot disagree — they did, before the
        // knob was mapped onto the pot scale SetLofiMacro expects.
        const float v = getValue();
        if (v >= ogham::lofi::CleanKnobLow() && v <= ogham::lofi::CleanKnobHigh())
            return "Clean";
        if (v < 0.5f) return string::f("Filter / fold  %.0f%%", (0.5f - v) * 200.f);
        return string::f("Crush / resonance  %.0f%%", (v - 0.5f) * 200.f);
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

    // Value-initialised, and that brace is load-bearing.
    //
    // BpmClock has four arrays with no default initialiser that Init() never
    // touches — fftBuffer_, fluxLin_, corr_ and bpmHist_. On the module that
    // costs nothing: the object is a file-scope static, so the loader zeroes it.
    // Here it is a member of a heap-allocated Module, so `new Ogham` would
    // default-initialise it and leave those arrays holding whatever was in that
    // memory. bpmHist_ is the estimator's agreement history, so a fresh module
    // could lock to a tempo derived from nothing at all, differently each time.
    //
    // Found by the multi-instance test, which caught it as instances appearing
    // to share state — they were not; each was starting from different rubbish.
    ogham::OghamApp      app{};
    ogham::RateConverter conv;
    dsp::SchmittTrigger  syncTrigger;
    dsp::SchmittTrigger  clockTrigger;

    // The encoder lives on the UI thread and the app on the audio thread, so
    // detents and the button cross between them as atomics. Detents accumulate
    // and are drained by the app; nothing is lost if a frame lands between
    // control ticks.
    // The encoder lives on the UI thread and the app on the audio thread. It
    // sends detents and already-classified gestures — a mouse cannot supply a
    // clean button, since the button that presses the encoder is the one that
    // turns it. Counts rather than flags, so nothing is lost on a sample where
    // the app's 1 kHz poll does not run.
    std::atomic<int>      encDetents{0};
    std::atomic<int>      encClicks{0};
    std::atomic<int>      encLongs{0};
    std::atomic<bool>     menuToggle{false};
    std::atomic<uint32_t> displaySegments{0};

    int lastFunc1 = -1, lastFunc2 = -1;

    Ogham() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        const int lastSlot = GetFormulaCount() - 1;   // 100: the A440 reference
        configParam<FunctionQuantity>(FUNC1_PARAM, 0.f, (float)lastSlot, 0.f,
                                      "Voice 1 function");
        // Voice 2 defaults to function 1, not 0. BytebeatEngine::Init picks a
        // distinct default deliberately, "so Out2 differs from Out1" — and a
        // param defaulting to 0 would quietly overwrite that on construction,
        // leaving both voices on the same function.
        configParam<FunctionQuantity>(FUNC2_PARAM, 0.f, (float)lastSlot, 1.f,
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

        // Function selection is a param rather than app state, so it can be
        // automated and MIDI-mapped — the one deliberate departure from
        // hardware-true. The binding runs both ways: a param move (knob, cable,
        // automation) pushes into the app, and the encoder's edits push back
        // out, so neither source is second-class and they cannot fight.
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
        in.encDelta       = encDetents.exchange(0, std::memory_order_relaxed);
        in.encClicks      = encClicks.exchange(0, std::memory_order_relaxed);
        in.encLongPresses = encLongs.exchange(0, std::memory_order_relaxed);
        in.menuToggle     = menuToggle.exchange(false, std::memory_order_relaxed);

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
            in.syncEdge   = sync  && (i == 0);
            in.clockEdge  = clock && (i == 0);
            // Gestures are counts and the app accumulates them, so they must be
            // delivered exactly once however many core steps this sample runs.
            if (i > 0) {
                in.encDelta = 0;
                in.encClicks = 0;
                in.encLongPresses = 0;
                in.menuToggle = false;
            }
            app.ProcessSample(in, out);
            const float channels[ogham::RateConverter::kChannels] = {
                out.out1, out.out2, out.env };
            conv.push(channels, out.eoc);
        }

        outputs[OUT1_OUTPUT].setVoltage(conv.read(0) * kAudioVolts);
        outputs[OUT2_OUTPUT].setVoltage(conv.read(1) * kAudioVolts);
        outputs[ENV_OUTPUT].setVoltage(conv.read(2) * kEnvVolts);
        outputs[EOC_OUTPUT].setVoltage(conv.gate() ? kGateVolts : 0.f);

        // The encoder may have moved the selection; write it back to the params
        // so the panel, the tooltips and the patch all agree.
        const int a1 = app.Formula1();
        if (a1 != lastFunc1) { lastFunc1 = a1; params[FUNC1_PARAM].setValue((float)a1); }
        const int a2 = app.Formula2();
        if (a2 != lastFunc2) { lastFunc2 = a2; params[FUNC2_PARAM].setValue((float)a2); }

        displaySegments.store(app.DisplaySegments(), std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------------
    // Persistence.
    //
    // Named fields rather than a mirror of the firmware's packed struct: patch
    // compatibility with the hardware is explicitly not a goal, and a diffable
    // patch file is worth more than a wire format nobody reads.
    //
    // Knob positions are Rack params and are saved by the host, so they are not
    // written here.
    // -----------------------------------------------------------------------

    json_t* dataToJson() override {
        const FxChainConfig& fx = app.Fx();
        json_t* root = json_object();
        json_object_set_new(root, "schemaVersion", json_integer(1));
        json_object_set_new(root, "selectedVoice", json_integer(app.SelectedVoice()));
        json_object_set_new(root, "menuField", json_integer(app.MenuField()));

        json_t* j = json_object();
        json_object_set_new(j, "enabled", json_boolean(fx.enabled != 0));
        json_object_set_new(j, "parallel", json_boolean(fx.parallel != 0));
        for (int stage = 0; stage < 3; stage++) {
            static const char* names[3] = {"chorus", "flanger", "phaser"};
            const uint8_t* p = (stage == 0) ? &fx.chorusLevel
                             : (stage == 1) ? &fx.flangerLevel : &fx.phaserLevel;
            json_t* s = json_object();
            json_object_set_new(s, "level", json_integer(p[0]));
            json_object_set_new(s, "type",  json_integer(p[1]));
            json_object_set_new(s, "p1",    json_integer(p[2]));
            json_object_set_new(s, "p2",    json_integer(p[3]));
            json_object_set_new(j, names[stage], s);
        }
        json_object_set_new(root, "fx", j);

        json_t* cv = json_object();
        json_object_set_new(cv, "mode",     json_integer(fx.cvOutMode));
        json_object_set_new(cv, "slewRise", json_integer(fx.cvSlewRise));
        json_object_set_new(cv, "slewFall", json_integer(fx.cvSlewFall));
        json_object_set_new(cv, "hold",     json_integer(fx.cvHold));
        json_object_set_new(root, "cvOut", cv);

        json_object_set_new(root, "lpgDecay", json_integer(fx.lpgDecay));
        json_object_set_new(root, "timbreRoute", json_integer(fx.timbreCvRoute));
        json_object_set_new(root, "paramQuant", json_integer(fx.paramQuant));

        // The drone is a frozen phase increment and a frozen A/B. Saving the
        // toggle alone would bring it back at the wrong pitch.
        json_t* dr = json_object();
        json_object_set_new(dr, "on", json_boolean(fx.out2Drone != 0));
        json_object_set_new(dr, "inc",
            json_string(string::f("0x%016llx",
                (unsigned long long)app.Engine().GetDroneInc()).c_str()));
        json_object_set_new(dr, "a", json_integer(app.Engine().GetDroneParamA()));
        json_object_set_new(dr, "b", json_integer(app.Engine().GetDroneParamB()));
        json_object_set_new(root, "drone", dr);
        return root;
    }

    void dataFromJson(json_t* root) override {
        if (!root) return;
        FxChainConfig& fx = app.Fx();

        auto num = [&](json_t* obj, const char* key, int fallback) -> int {
            if (!obj) return fallback;
            json_t* v = json_object_get(obj, key);
            return v ? (int)json_integer_value(v) : fallback;
        };
        auto flag = [&](json_t* obj, const char* key, bool fallback) -> bool {
            if (!obj) return fallback;
            json_t* v = json_object_get(obj, key);
            return v ? json_is_true(v) : fallback;
        };

        app.SetSelectedVoice(num(root, "selectedVoice", 0));
        app.SetMenuField(num(root, "menuField", 0));

        json_t* j = json_object_get(root, "fx");
        fx.enabled  = flag(j, "enabled", true) ? 1 : 0;
        fx.parallel = flag(j, "parallel", false) ? 1 : 0;
        for (int stage = 0; stage < 3; stage++) {
            static const char* names[3] = {"chorus", "flanger", "phaser"};
            uint8_t* p = (stage == 0) ? &fx.chorusLevel
                       : (stage == 1) ? &fx.flangerLevel : &fx.phaserLevel;
            json_t* sj = j ? json_object_get(j, names[stage]) : nullptr;
            if (!sj) continue;
            p[0] = (uint8_t)clamp(num(sj, "level", p[0]), 0, 99);
            p[1] = (uint8_t)clamp(num(sj, "type",  p[1]), 0, FX_TYPE_MAX);
            p[2] = (uint8_t)clamp(num(sj, "p1",    p[2]), 0, 99);
            p[3] = (uint8_t)clamp(num(sj, "p2",    p[3]), 0, 99);
        }

        json_t* cv = json_object_get(root, "cvOut");
        fx.cvOutMode  = (uint8_t)clamp(num(cv, "mode",     fx.cvOutMode), 0, 3);
        fx.cvSlewRise = (uint8_t)clamp(num(cv, "slewRise", fx.cvSlewRise), 0, 99);
        fx.cvSlewFall = (uint8_t)clamp(num(cv, "slewFall", fx.cvSlewFall), 0, 99);
        fx.cvHold     = (uint8_t)clamp(num(cv, "hold",     fx.cvHold), 0, 8);

        fx.lpgDecay      = (uint8_t)clamp(num(root, "lpgDecay", fx.lpgDecay), 0, 99);
        fx.timbreCvRoute = (uint8_t)clamp(num(root, "timbreRoute", fx.timbreCvRoute), 0, 2);
        fx.paramQuant    = (uint8_t)clamp(num(root, "paramQuant", fx.paramQuant), 0, 128);

        json_t* dr = json_object_get(root, "drone");
        fx.out2Drone = flag(dr, "on", false) ? 1 : 0;
        app.ApplyFxChain();

        if (fx.out2Drone && dr) {
            json_t* incj = json_object_get(dr, "inc");
            unsigned long long inc = 0;
            if (incj && json_is_string(incj))
                inc = std::strtoull(json_string_value(incj), nullptr, 0);
            if (inc != 0) {
                app.Engine().RestoreOut2Drone((uint64_t)inc,
                                              num(dr, "a", 128), num(dr, "b", 128));
            }
        }
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
// Right-click menu.
//
// The panel is hardware-true, and on the panel the way into the menu is to hold
// the encoder for 600 ms. That is a fine gesture with a thumb and an awkward one
// with a mouse, so the same thing is available here as a single click. The full
// mirror of all 22 fields lands with the rest of the Rack-native shortcuts.
// ---------------------------------------------------------------------------

struct MenuToggleItem : MenuItem {
    Ogham* module = nullptr;
    void onAction(const event::Action& e) override {
        if (module) module->menuToggle.store(true, std::memory_order_relaxed);
    }
};

// A numeric menu field, driven by a slider. The byte is written straight into
// the app; the coefficients are recomputed on the audio thread at the next
// control tick — see OghamApp::SetMenuValue.
struct FxFieldQuantity : Quantity {
    Ogham* module = nullptr;
    int field = 0;
    int maxValue = 99;

    void setValue(float v) override {
        if (module) module->app.SetMenuValue(field, (int)std::round(clamp(v, 0.f, (float)maxValue)));
    }
    float getValue() override {
        return module ? (float)module->app.MenuValue(field) : 0.f;
    }
    float getMinValue() override { return 0.f; }
    float getMaxValue() override { return (float)maxValue; }
    float getDefaultValue() override { return 0.f; }
    std::string getLabel() override { return ogham::FieldSpecs()[field].name; }
    std::string getDisplayValueString() override {
        return string::f("%d", (int)getValue());
    }
    std::string getUnit() override { return ""; }
};

struct FxFieldSlider : ui::Slider {
    FxFieldSlider(Ogham* module, int field, int maxValue) {
        FxFieldQuantity* q = new FxFieldQuantity;
        q->module = module;
        q->field = field;
        q->maxValue = maxValue;
        quantity = q;
        box.size.x = 220.f;
    }
    ~FxFieldSlider() override { delete quantity; }
};

// One menu field, presented the way its values want to be: named options where
// there is a small set of them, a slider where it is a number.
inline MenuItem* createFieldItem(Ogham* module, int field) {
    const ogham::FieldSpec& spec = ogham::FieldSpecs()[field];
    const int value = module->app.MenuValue(field);

    if (field == FX_FIELD_QUANT) {
        // Stored as the grid step itself, not an index.
        const std::vector<uint8_t>& steps = ogham::QuantSteps();
        int idx = 0;
        for (size_t i = 0; i < steps.size(); i++) if (steps[i] == value) idx = (int)i;
        return createIndexSubmenuItem(spec.name, spec.options,
            [=]() { return idx; },
            [=](int i) { module->app.SetMenuValue(field, ogham::QuantSteps()[i]); });
    }

    if (!spec.options.empty()) {
        return createIndexSubmenuItem(spec.name, spec.options,
            [=]() { return module->app.MenuValue(field); },
            [=](int i) { module->app.SetMenuValue(field, i); });
    }

    return createSubmenuItem(spec.name, string::f("%d", value),
        [=](Menu* sub) {
            if (spec.detail && spec.detail[0])
                sub->addChild(createMenuLabel(spec.detail));
            sub->addChild(new FxFieldSlider(module, field, spec.max));
        });
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

        // Four screws, where the module's mounting slots actually are:
        // PanelPCB-v2 puts them 7.5 mm in from each edge and 3 mm down from the
        // top and up from the bottom, which is the Eurorack standard and not
        // quite Rack's default grid positions.
        for (float x : {7.65f, 43.21f})
            for (float y : {3.0f, 125.5f})
                addChild(createWidgetCentered<ScrewBlack>(mm2px(Vec(x, y))));

        // Positions come from the production panel: PanelPCB-v2's Edge.Cuts
        // for the holes, and the legends in the production graphics for which
        // hole is which. res/Ogham.svg is generated from the same numbers by
        // tools/build_panel.py, and tools/panel_check.py asserts the two agree.
        //
        // Panel millimetres plus 0.15, the padding that takes the real 50.5 mm
        // panel out to Rack's 10 HP.

        // The display, in a framebuffer so it only repaints when the segments
        // change — 30 Hz of content against Rack's 60 Hz of frames.
        FramebufferWidget* fb = new FramebufferWidget;
        ogham::SevenSegmentDisplay* seg = new ogham::SevenSegmentDisplay;
        seg->box.pos  = mm2px(Vec(12.55, 12.62));
        seg->box.size = mm2px(Vec(29.50, 12.50));
        if (module) seg->segments = &module->displaySegments;
        fb->addChild(seg);
        addChild(fb);

        // Func encoder, top left.
        // The encoder sizes itself from its SVG, so it is only positioned here.
        ogham::EncoderWidget* enc = new ogham::EncoderWidget;
        enc->box.pos = mm2px(Vec(7.53, 45.19)).minus(enc->box.size.div(2));
        if (module) {
            enc->detents = &module->encDetents;
            enc->clicks  = &module->encClicks;
            enc->longs   = &module->encLongs;
        }
        addChild(enc);

        // Top row: the Clk/VOct switch, and Rate/Fine.
        addParam(createParamCentered<ogham::ModeToggle>(
            mm2px(Vec(25.43, 45.19)), module, Ogham::MODE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(43.43, 45.19)), module, Ogham::RATE_PARAM));

        // Bottom row: A, B, Tone.
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(7.53, 65.69)), module, Ogham::A_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(25.43, 65.69)), module, Ogham::B_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(
            mm2px(Vec(43.43, 65.69)), module, Ogham::TONE_PARAM));

        // Inputs. The panel's order is CV A, CV B, Clk/VOct, Sync — the
        // production legends, which put Clk third and Sync fourth.
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(5.93, 97.27)), module, Ogham::CV_A_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(18.93, 97.27)), module, Ogham::CV_B_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(31.93, 97.27)), module, Ogham::CLK_VOCT_INPUT));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(44.93, 97.27)), module, Ogham::SYNC_INPUT));

        // Outputs: Out 1, Out 2, CV Out, EOC.
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(5.93, 113.77)), module, Ogham::OUT1_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(18.93, 113.77)), module, Ogham::OUT2_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(31.93, 113.77)), module, Ogham::ENV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(44.93, 113.77)), module, Ogham::EOC_OUTPUT));

        // The two function slots have no panel control of their own — on the
        // module the encoder is the only way to them, and the panel has no hole
        // to spare. They remain params so a patch can carry them and the
        // right-click browser can set them; the cost is that Rack's MIDI mapper,
        // which maps by clicking a widget, cannot reach them. Recorded in
        // docs/firmware-differences.md.
    }

    void appendContextMenu(Menu* menu) override {
        Ogham* m = dynamic_cast<Ogham*>(module);
        if (!m) return;

        menu->addChild(new MenuSeparator);

        // --- Functions -----------------------------------------------------
        // The module can only show you a number, and only the one you are on.
        // Here is the whole bank, by character and by name.
        for (int voice = 0; voice < 2; voice++) {
            const int paramId = voice == 0 ? Ogham::FUNC1_PARAM : Ogham::FUNC2_PARAM;
            const int current = (int)std::round(m->params[paramId].getValue());
            menu->addChild(createSubmenuItem(
                string::f("Voice %d function", voice + 1),
                FunctionLabel(current),
                [=](Menu* sub) {
                    for (const ogham::Category& cat : ogham::kCategories) {
                        sub->addChild(createSubmenuItem(cat.name,
                            (current >= cat.first && current <= cat.last)
                                ? string::f("F%02d", current) : "",
                            [=](Menu* list) {
                                for (int i = cat.first; i <= cat.last; i++) {
                                    list->addChild(createCheckMenuItem(
                                        ogham::FunctionLabelFor(i), "",
                                        [=]() { return current == i; },
                                        [=]() { m->params[paramId].setValue((float)i); }));
                                }
                            }));
                    }
                    sub->addChild(new MenuSeparator);
                    const int ref = GetReferenceIndex();
                    sub->addChild(createCheckMenuItem(
                        ogham::FunctionLabelFor(ref), "440 Hz",
                        [=]() { return current == ref; },
                        [=]() { m->params[paramId].setValue((float)ref); }));
                }));
        }

        // --- How the encoder takes a mouse ----------------------------------
        menu->addChild(new MenuSeparator);
        // Stored per installation rather than in the patch: how the encoder
        // answers a mouse is a property of this desk, and opening someone
        // else's patch should not change it.
        menu->addChild(createCheckMenuItem(
            "Drag turns the encoder", "this computer",
            []() { return ogham::prefs::DragTurnsEncoder(); },
            []() { ogham::prefs::SetDragTurnsEncoder(
                       !ogham::prefs::DragTurnsEncoder()); }));

        // --- The Menu ------------------------------------------------------
        menu->addChild(new MenuSeparator);

        MenuToggleItem* toggle = new MenuToggleItem;
        toggle->module = m;
        toggle->text = m->app.InMenu() ? "Leave the Menu" : "Open the Menu";
        toggle->rightText = "or hold the encoder";
        menu->addChild(toggle);

        menu->addChild(createSubmenuItem("Settings",
            string::f("%d fields", FX_NUM_FIELDS),
            [=](Menu* sub) {
                sub->addChild(createMenuLabel("Everything behind the four digits"));
                for (int f = 0; f < FX_NUM_FIELDS; f++) {
                    if (f == 2 || f == 6 || f == 10 || f == FX_FIELD_CVOUT)
                        sub->addChild(new MenuSeparator);
                    sub->addChild(createFieldItem(m, f));
                }
            }));

        // --- State ---------------------------------------------------------
        menu->addChild(new MenuSeparator);
        if (m->app.InMenu()) {
            const ogham::FieldSpec& spec = ogham::FieldSpecs()[m->app.MenuField()];
            menu->addChild(createMenuLabel(
                string::f("On %s%s", spec.name, m->app.Editing() ? ", editing" : "")));
        } else {
            menu->addChild(createMenuLabel(
                string::f("Encoder is editing voice %d", m->app.SelectedVoice() + 1)));
        }
        menu->addChild(createMenuLabel(
            m->app.ExternalClock() ? string::f("Clocked, %.2f x", m->app.Rate())
          : m->app.ClockHeld()     ? string::f("Clock held, %.2f x", m->app.Rate())
                                   : string::f("Free running, %.2f x", m->app.Rate())));
    }
};

Model* modelOgham = createModel<Ogham, OghamWidget>("Ogham");
