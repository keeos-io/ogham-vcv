// -----------------------------------------------------------------------------
// Ogham for VCV Rack — offline parity renderer
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// Drives the firmware's own DSP objects at 48 kHz from a scripted CSV and
// renders the module's four outputs to a WAV. This is the reference the plugin
// is tested against: the same script, run through the Rack module, must produce
// the same audio.
//
// It deliberately mirrors the firmware's AudioCallback and main-loop ordering
// rather than doing anything cleverer, because the ordering is part of the
// behaviour: the CV output reads the PROCESSED audio and the pre-Lo-Fi clean
// buffers from the same sample, and the BPM estimator only ever sees the clean
// signal.
//
//   render <script.csv> <out.wav> [seconds] [--csv <state.csv>]
//
// Script format, one row per change, columns blank where nothing changes:
//
//   sample,formula1,formula2,A,B,rate,tone,fxfield,fxvalue,sync,clk
//
//   sample    when to apply the row, in 48 kHz samples
//   formula1  0-100 (100 = the A440 reference slot)
//   A, B      0-255, the values the engine sees
//   rate      playback rate multiplier, 1.0 = unity
//   tone      lo-fi macro pot, 0..1, 0.5 = clean centre
//   fxfield   menu field index 0-21, applied with fxvalue
//   sync      1 = hard-sync reset on this sample
//   clk       1 = external clock edge on this sample (recorded, not yet acted on:
//             tempo tracking lives in the transcribed app layer, not the core)
//
// The WAV is 4-channel 32-bit float at 48 kHz:
//   1 Out 1        -1..1, exactly as the engine and FX chain produce it. The
//                  firmware's AUDIO_OUT_LEVEL (0.51) is NOT applied — it
//                  compensates an over-gained analog stage and is not part of
//                  the sound. See docs/firmware-differences.md.
//   2 Out 2        as above
//   3 ENV Out      0..1, the value CvOutput writes to the DAC
//   4 EOC          0 or 1
//
// A 64-bit FNV-1a hash of the sample data is printed at the end, so a parity
// check is one line of output rather than a file comparison.

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "bytebeat_engine.h"
#include "ogham_audio_pipeline.h"
#include "ogham_cv_output.h"
#include "ogham_display.h"
#include "bpm_clock.h"
#include "tm1637.h"
#include "daisy_seed.h"
#include "ogham_clock.h"

namespace {

constexpr double kSampleRate   = 48000.0;
constexpr int    kControlDiv   = 48;    // 1 kHz, the firmware's main-loop rate
constexpr int    kDisplayEvery = 33;    // ~30 Hz, in control ticks

// ---------------------------------------------------------------------------
// FX menu field -> FxChainConfig byte.
//
// The struct's member order is not the menu's field order, so the mapping is
// explicit. This is a harness-local copy of the firmware's FxFieldPtr, which
// lives in ogham_main.cpp and is therefore not compiled here; the plugin gets a
// properly transcribed version when the application layer is ported. Recorded
// in docs/firmware-differences.md.
// ---------------------------------------------------------------------------
uint8_t* FxFieldPtr(FxChainConfig& f, int field) {
    switch (field) {
        case 0:  return &f.enabled;
        case 1:  return &f.parallel;
        case 2:  return &f.chorusLevel;
        case 3:  return &f.chorusType;
        case 4:  return &f.chorusP1;
        case 5:  return &f.chorusP2;
        case 6:  return &f.flangerLevel;
        case 7:  return &f.flangerType;
        case 8:  return &f.flangerP1;
        case 9:  return &f.flangerP2;
        case 10: return &f.phaserLevel;
        case 11: return &f.phaserType;
        case 12: return &f.phaserP1;
        case 13: return &f.phaserP2;
        case 14: return &f.cvOutMode;
        case 15: return &f.cvSlewRise;
        case 16: return &f.cvSlewFall;
        case 17: return &f.cvHold;
        case 18: return &f.lpgDecay;
        case 19: return &f.timbreCvRoute;
        case 20: return &f.paramQuant;
        case 21: return &f.out2Drone;
        default: return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Script
// ---------------------------------------------------------------------------

struct Row {
    uint64_t sample = 0;
    int   formula1 = -1, formula2 = -1;
    int   a = -1, b = -1;
    float rate = -1.0f, tone = -1.0f;
    int   fxField = -1, fxValue = 0;
    bool  sync = false, clk = false;
};

// A cell means "no change" when it is empty, whitespace, or a lone dash.
bool Blank(const std::string& s) {
    std::string t;
    for (char c : s) if (!std::isspace((unsigned char)c)) t += c;
    return t.empty() || t == "-";
}

int   CellInt(const std::string& s, int fallback) {
    return Blank(s) ? fallback : std::stoi(s);
}
float CellFloat(const std::string& s, float fallback) {
    return Blank(s) ? fallback : std::stof(s);
}

std::vector<Row> LoadScript(const char* path, std::string& err) {
    std::vector<Row> rows;
    std::ifstream in(path);
    if (!in) { err = std::string("cannot open ") + path; return rows; }

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        lineNo++;
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("sample", 0) == 0) continue;   // header

        std::vector<std::string> cell;
        std::stringstream ss(line);
        std::string c;
        while (std::getline(ss, c, ',')) cell.push_back(c);
        while (cell.size() < 11) cell.push_back("");

        try {
            Row r;
            r.sample   = (uint64_t)std::stoll(cell[0]);
            r.formula1 = CellInt(cell[1], -1);
            r.formula2 = CellInt(cell[2], -1);
            r.a        = CellInt(cell[3], -1);
            r.b        = CellInt(cell[4], -1);
            r.rate     = CellFloat(cell[5], -1.0f);
            r.tone     = CellFloat(cell[6], -1.0f);
            r.fxField  = CellInt(cell[7], -1);
            r.fxValue  = CellInt(cell[8], 0);
            r.sync     = CellInt(cell[9], 0) != 0;
            r.clk      = CellInt(cell[10], 0) != 0;
            rows.push_back(r);
        } catch (const std::exception&) {
            err = "malformed row at line " + std::to_string(lineNo);
            return {};
        }
    }
    return rows;
}

// ---------------------------------------------------------------------------
// WAV out — 4 channel, 32-bit float
// ---------------------------------------------------------------------------

void WriteU32(std::ofstream& f, uint32_t v) { f.write((const char*)&v, 4); }
void WriteU16(std::ofstream& f, uint16_t v) { f.write((const char*)&v, 2); }

bool WriteWav(const char* path, const std::vector<float>& interleaved, int channels) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    const uint32_t dataBytes = (uint32_t)(interleaved.size() * sizeof(float));
    const uint32_t byteRate  = (uint32_t)(kSampleRate * channels * sizeof(float));

    f.write("RIFF", 4);   WriteU32(f, 36 + dataBytes);   f.write("WAVE", 4);
    f.write("fmt ", 4);   WriteU32(f, 16);
    WriteU16(f, 3);                                  // IEEE float
    WriteU16(f, (uint16_t)channels);
    WriteU32(f, (uint32_t)kSampleRate);
    WriteU32(f, byteRate);
    WriteU16(f, (uint16_t)(channels * sizeof(float)));
    WriteU16(f, 32);
    f.write("data", 4);   WriteU32(f, dataBytes);
    f.write((const char*)interleaved.data(), dataBytes);
    return f.good();
}

uint64_t Fnv1a(const std::vector<float>& v) {
    uint64_t h = 1469598103934665603ull;
    const uint8_t* p = (const uint8_t*)v.data();
    const size_t n = v.size() * sizeof(float);
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "usage: render <script.csv> <out.wav> [seconds] [--csv <state.csv>]\n");
        return 2;
    }
    const char* scriptPath = argv[1];
    const char* wavPath    = argv[2];
    double seconds = 10.0;
    const char* statePath = nullptr;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--csv") && i + 1 < argc) statePath = argv[++i];
        else seconds = std::atof(argv[i]);
    }

    std::string err;
    std::vector<Row> script = LoadScript(scriptPath, err);
    if (!err.empty()) { std::fprintf(stderr, "render: %s\n", err.c_str()); return 1; }

    // The display's flash timeouts run on rendered time, not wall-clock time.
    ogham::shim::VirtualClock clock(kSampleRate);
    clock.Install();

    // --- The module, as the firmware assembles it ---
    BytebeatEngine engine;
    AudioPipeline  pipeline;
    CvOutput       cvOutput;
    BpmClock       bpmClock;
    TM1637         tm1637;
    Display        display;
    daisy::DaisySeed seed;
    daisy::DacHandle dac;

    engine.Init();
    pipeline.Init();
    cvOutput.Init(&dac);
    bpmClock.Init();
    tm1637.Init(&seed, 9, 10);      // the module's CLK/DIO pins; pins do nothing here
    display.Init(&tm1637);

    FxChainConfig fx = AudioPipeline::DefaultFxChain();
    pipeline.SetFxChain(fx);
    pipeline.SetLofiMacro(0.5f);    // clean centre
    engine.SetFormula1(0);
    engine.SetFormula2(0);
    engine.SetParamA(128);
    engine.SetParamB(128);
    engine.SetRate(1.0f);

    const uint64_t total = (uint64_t)(seconds * kSampleRate);
    std::vector<float> out(total * 4);

    std::ofstream state;
    if (statePath) {
        state.open(statePath);
        state << "sample,out1,out2,env,eoc,seg0,seg1,seg2,seg3,bpm,locked\n";
    }

    size_t nextRow = 0;
    int    controlCount = 0, displayCount = 0;
    int    prevFormulaIdx = engine.GetFormula1Index();
    int    selectedVoice = 0;
    uint64_t clkEdges = 0;

    const auto wallStart = std::chrono::steady_clock::now();

    for (uint64_t n = 0; n < total; n++) {
        // --- Script events for this sample ---
        while (nextRow < script.size() && script[nextRow].sample <= n) {
            const Row& r = script[nextRow++];
            if (r.formula1 >= 0) engine.SetFormula1(r.formula1);
            if (r.formula2 >= 0) engine.SetFormula2(r.formula2);
            if (r.a >= 0)        engine.SetParamA(r.a);
            if (r.b >= 0)        engine.SetParamB(r.b);
            if (r.rate >= 0.0f)  engine.SetRate(r.rate);
            if (r.tone >= 0.0f)  pipeline.SetLofiMacro(r.tone);
            if (r.fxField >= 0) {
                if (uint8_t* p = FxFieldPtr(fx, r.fxField)) {
                    *p = (uint8_t)r.fxValue;
                    pipeline.SetFxChain(fx);
                    engine.SetParamQuant(fx.paramQuant);
                    engine.SetOut2Decoupled(fx.out2Drone != 0);
                    cvOutput.SetMode((CvOutput::Mode)fx.cvOutMode);
                }
            }
            if (r.sync) {
                // The gate ISR raises both flags; the engine consumes its own at
                // the top of the next sample, the LPG at the top of the next
                // Process(). Same ordering here.
                engine.SyncReset();
                pipeline.LpgTrigger();
            }
            if (r.clk) clkEdges++;
        }

        // --- Audio callback, block size 1 ---
        float l = 0.0f, r = 0.0f;
        float* outPtr[2] = { &l, &r };
        pipeline.Process(engine, outPtr, 1);

        const float* clean     = pipeline.GetCleanBuffer();
        const float* clean2    = pipeline.GetCleanBuffer2();
        const bool*  cap1      = pipeline.GetCvCaptureBuffer();
        const bool*  cap2      = pipeline.GetCvCaptureBuffer2();
        const float* holdSamp1 = pipeline.GetHoldSampleBuffer();
        const float* holdSamp2 = pipeline.GetHoldSampleBuffer2();

        cvOutput.ProcessSample(l, r, clean[0], clean2[0],
                               holdSamp1[0], holdSamp2[0], cap1[0], cap2[0]);
        bpmClock.ProcessSample(clean[0]);
        // AUDIO_OUT_LEVEL is deliberately not applied here — see the header.

        // --- Main loop, at 1 kHz ---
        if (++controlCount >= kControlDiv) {
            controlCount = 0;

            cvOutput.SetCaptureInterval(pipeline.GetCaptureSamples(),
                                        pipeline.GetCaptureSamples2());
            cvOutput.SetSlewRise(fx.cvSlewRise);
            cvOutput.SetSlewFall(fx.cvSlewFall);
            cvOutput.SetHold(fx.cvHold);
            cvOutput.UpdateOutput();

            const int idx = engine.GetFormula1Index();
            if (idx != prevFormulaIdx) { prevFormulaIdx = idx; bpmClock.RequestEstimate(); }
            bpmClock.Update(engine.GetRate());

            if (++displayCount >= kDisplayEvery) {
                displayCount = 0;
                display.Update();
                const int f = selectedVoice == 0 ? engine.GetFormula1Index()
                                                 : engine.GetFormula2Index();
                if (f == GetReferenceIndex()) display.ShowVoiceRef(selectedVoice + 1,
                                                                  pipeline.IsLofiClean());
                else                          display.ShowVoice(selectedVoice + 1, f,
                                                                pipeline.IsLofiClean());
            }
        }

        const float env = dac.Normalized();
        const float eoc = bpmClock.GetClockState() ? 1.0f : 0.0f;

        out[n * 4 + 0] = l;
        out[n * 4 + 1] = r;
        out[n * 4 + 2] = env;
        out[n * 4 + 3] = eoc;

        if (state.is_open() && (n % kControlDiv) == 0) {
            const uint8_t* segs = tm1637.GetLastSegs();
            state << n << ',' << l << ',' << r << ',' << env << ',' << eoc << ','
                  << (int)segs[0] << ',' << (int)segs[1] << ','
                  << (int)segs[2] << ',' << (int)segs[3] << ','
                  << bpmClock.GetBpm() << ',' << (bpmClock.IsLocked() ? 1 : 0) << '\n';
        }

        clock.Advance();
    }

    const auto wallEnd = std::chrono::steady_clock::now();
    const double wallSec =
        std::chrono::duration<double>(wallEnd - wallStart).count();

    if (!WriteWav(wavPath, out, 4)) {
        std::fprintf(stderr, "render: cannot write %s\n", wavPath);
        return 1;
    }

    std::printf("rendered  %.2f s (%llu samples) -> %s\n",
                seconds, (unsigned long long)total, wavPath);
    std::printf("script    %zu rows applied, %llu clock edges seen\n",
                script.size(), (unsigned long long)clkEdges);
    std::printf("bpm       %.2f (locked=%d, confidence=%.2f)\n",
                bpmClock.GetBpm(), bpmClock.IsLocked() ? 1 : 0,
                bpmClock.GetConfidence());
    std::printf("hash      %016llx\n", (unsigned long long)Fnv1a(out));
    std::printf("speed     %.2f s wall, %.1fx real time\n",
                wallSec, wallSec > 0.0 ? seconds / wallSec : 0.0);
    return 0;
}
