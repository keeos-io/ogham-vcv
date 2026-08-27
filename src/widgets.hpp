// -----------------------------------------------------------------------------
// Ogham for VCV Rack — panel widgets
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// Two widgets the module cannot do without: the four-digit display and the Func
// encoder. Both are deliberately thin — the display draws bytes the firmware
// produced, and the encoder reports detents and lets the firmware's own gesture
// machine decide what they mean.

#pragma once

#include "plugin.hpp"

#include <atomic>
#include <cmath>

namespace ogham {

// ---------------------------------------------------------------------------
// Four-digit seven-segment display.
//
// Draws the exact segment bytes TM1637::WriteSegments cached on its way to the
// (absent) wire. Not a font: the module's display quirks — the clean-centre dot,
// the edit flash, the top-bar divide hint on a clock ratio — are all just
// segments, so they come out right here with no special-casing at all.
// ---------------------------------------------------------------------------

struct SevenSegmentDisplay : Widget {
    // Set by the module each frame, from the audio thread's snapshot. Four
    // bytes, low digit first; bit 0 is segment a and bit 7 the decimal point.
    std::atomic<uint32_t>* segments = nullptr;
    uint32_t lastDrawn = 0xFFFFFFFF;

    NVGcolor lit    = nvgRGB(0xff, 0x9c, 0x2a);   // amber, as the hardware
    NVGcolor unlit  = nvgRGBA(0xff, 0x9c, 0x2a, 0x14);
    NVGcolor glass  = nvgRGB(0x0a, 0x0c, 0x0d);

    void drawSegment(NVGcontext* vg, float x, float y, float w, float h,
                     bool horizontal, bool on) {
        const float t = horizontal ? h : w;      // thickness
        const float b = t * 0.5f;                // bevel
        nvgBeginPath(vg);
        if (horizontal) {
            nvgMoveTo(vg, x + b,     y);
            nvgLineTo(vg, x + w - b, y);
            nvgLineTo(vg, x + w,     y + b);
            nvgLineTo(vg, x + w - b, y + h);
            nvgLineTo(vg, x + b,     y + h);
            nvgLineTo(vg, x,         y + b);
        } else {
            nvgMoveTo(vg, x,         y + b);
            nvgLineTo(vg, x + b,     y);
            nvgLineTo(vg, x + w,     y + b);
            nvgLineTo(vg, x + w,     y + h - b);
            nvgLineTo(vg, x + b,     y + h);
            nvgLineTo(vg, x,         y + h - b);
        }
        nvgClosePath(vg);
        nvgFillColor(vg, on ? lit : unlit);
        nvgFill(vg);
    }

    void drawDigit(NVGcontext* vg, float x, float y, float w, float h, uint8_t seg) {
        const float t  = w * 0.16f;              // stroke thickness
        const float gap = t * 0.35f;
        const float mid = y + h * 0.5f;

        // a top, b upper right, c lower right, d bottom, e lower left,
        // f upper left, g middle — the order of the bits in the byte.
        drawSegment(vg, x + t, y, w - 2 * t, t, true,  seg & 0x01);          // a
        drawSegment(vg, x + w - t, y + t + gap, t, h * 0.5f - t * 1.5f - gap,
                    false, seg & 0x02);                                       // b
        drawSegment(vg, x + w - t, mid + t * 0.5f + gap, t,
                    h * 0.5f - t * 1.5f - gap, false, seg & 0x04);            // c
        drawSegment(vg, x + t, y + h - t, w - 2 * t, t, true,  seg & 0x08);   // d
        drawSegment(vg, x, mid + t * 0.5f + gap, t, h * 0.5f - t * 1.5f - gap,
                    false, seg & 0x10);                                       // e
        drawSegment(vg, x, y + t + gap, t, h * 0.5f - t * 1.5f - gap,
                    false, seg & 0x20);                                       // f
        drawSegment(vg, x + t, mid - t * 0.5f, w - 2 * t, t, true, seg & 0x40); // g

        // Decimal point, bottom right.
        nvgBeginPath(vg);
        nvgCircle(vg, x + w + t * 0.9f, y + h - t * 0.5f, t * 0.55f);
        nvgFillColor(vg, (seg & 0x80) ? lit : unlit);
        nvgFill(vg);
    }

    void draw(const DrawArgs& args) override {
        // The glass, drawn whether or not a module is present so the browser
        // thumbnail looks like the module rather than a hole in the panel.
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 2.f);
        nvgFillColor(args.vg, glass);
        nvgFill(args.vg);

        const uint32_t packed = segments ? segments->load(std::memory_order_relaxed)
                                         : 0x06005B3Fu;   // "0.1 2 3" in the browser
        lastDrawn = packed;

        const float pad = box.size.x * 0.06f;
        const float dw  = (box.size.x - 2.f * pad) / 4.f;
        const float dh  = box.size.y - 2.f * pad;
        for (int i = 0; i < 4; i++) {
            const uint8_t seg = (uint8_t)((packed >> (8 * i)) & 0xFF);
            drawDigit(args.vg, pad + i * dw + dw * 0.12f, pad,
                      dw * 0.62f, dh, seg);
        }
    }

    // Repaint only when the bytes change: the display updates at 30 Hz and Rack
    // draws at 60, so half the frames have nothing new in them.
    void step() override {
        if (segments && segments->load(std::memory_order_relaxed) != lastDrawn) {
            if (FramebufferWidget* fb = dynamic_cast<FramebufferWidget*>(parent))
                fb->setDirty();
        }
        Widget::step();
    }
};

// ---------------------------------------------------------------------------
// The Func encoder.
//
// An endless control, so it has no value to show and no param behind it.
//
// On the module, turning and pressing are different fingers and cannot be
// confused. With a mouse they are the same button, and what a press MEANS is not
// knowable at the moment it happens — only afterwards, from what the pointer
// does next. So this widget waits, classifies, and hands the module a decided
// gesture; the transcribed application layer still decides what that gesture
// means, which is what keeps the behaviour the module's.
//
//   press, then move            -> a turn. Detents, and nothing else, ever.
//   press, hold still 600 ms    -> a long press. Fires once, while still held.
//   press, release before that  -> a click.
//   scroll wheel                -> detents, with no button involved at all.
//
// Two earlier attempts got this wrong in ways worth recording, because both
// looked correct until someone used them. Reporting the raw button meant every
// turn began with a press, so a slow turn crossed the long-press threshold and
// fell into the menu. Promoting a still button to a press after 140 ms meant a
// drag that began after any pause was swallowed — you had to move immediately or
// not at all, which is not how anyone uses a knob.
// ---------------------------------------------------------------------------

struct EncoderWidget : Widget {
    std::atomic<int>* detents = nullptr;
    std::atomic<int>* clicks  = nullptr;
    std::atomic<int>* longs   = nullptr;

    // Vertical travel per detent, in pixels. Tuned so a comfortable drag steps
    // about one function at a time and a fast flick crosses the bank — the same
    // relationship the hardware's acceleration curve assumes. Worth checking
    // against a real module by turning both.
    static constexpr float kPixelsPerDetent = 8.f;

    // How far the pointer must move before the gesture is a turn, in pixels:
    // enough to survive the shake of clicking, far less than a deliberate drag.
    static constexpr float kMoveThreshold = 3.f;

    // The module's own hold threshold. Kept identical so the gesture takes the
    // same time here as on the panel.
    static constexpr double kLongPressSec = 0.6;

    bool   armed = false;      // button down, gesture not yet decided
    bool   turning = false;
    bool   longFired = false;
    double downTime = 0.0;
    float  travel = 0.f;
    float  accum = 0.f;

    void pushDetents(int n) {
        if (detents && n != 0) detents->fetch_add(n, std::memory_order_relaxed);
    }

    // The gesture is over: if it never became a turn or a hold, it was a click.
    //
    // This has to run from onDragEnd, not from the button release, and that is
    // not a stylistic choice. Rack skips dispatching ButtonEvent entirely while
    // the cursor is locked (EventState::handleButton), and this widget locks the
    // cursor on drag start so the pointer stays put while you turn — so the
    // release is simply never delivered here. DragEnd always is. Clicks vanished
    // for exactly that reason: turning worked, holding worked, and clicking did
    // nothing at all.
    //
    // `armed` is the one-shot latch, so the two paths cannot both fire.
    void endGesture() {
        if (armed && !turning && !longFired && clicks)
            clicks->fetch_add(1, std::memory_order_relaxed);
        armed = false;
        turning = false;
    }

    void onButton(const event::Button& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT) { Widget::onButton(e); return; }

        if (e.action == GLFW_PRESS) {
            armed = true;
            turning = false;
            longFired = false;
            downTime = system::getTime();
            travel = 0.f;
            accum = 0.f;
            e.consume(this);
        } else if (e.action == GLFW_RELEASE) {
            // Reached only when the cursor is NOT locked; see endGesture.
            endGesture();
        }
        Widget::onButton(e);
    }

    void step() override {
        // A long press fires while the button is still down, as it does on the
        // module — you feel the mode change under your thumb rather than on
        // release. It cannot fire once the gesture has become a turn.
        if (armed && !turning && !longFired &&
            (system::getTime() - downTime) >= kLongPressSec) {
            longFired = true;
            if (longs) longs->fetch_add(1, std::memory_order_relaxed);
        }
        Widget::step();
    }

    void onDragStart(const event::DragStart& e) override {
        APP->window->cursorLock();
        Widget::onDragStart(e);
    }

    void onDragEnd(const event::DragEnd& e) override {
        APP->window->cursorUnlock();
        endGesture();
        Widget::onDragEnd(e);
    }

    void onDragMove(const event::DragMove& e) override {
        // Movement makes it a turn — at any point, not only in the first
        // moments. A press, a pause, then a drag is a turn, because that is what
        // it looks like to the person doing it. Once the long press has fired the
        // gesture is settled and dragging does nothing, matching a module whose
        // menu you have just entered with the shaft still under your thumb.
        if (armed && !longFired) {
            travel += std::fabs(e.mouseDelta.y) + std::fabs(e.mouseDelta.x);
            if (!turning && travel > kMoveThreshold) turning = true;
        }
        if (turning) {
            // Up is clockwise, matching every other knob in Rack.
            accum += -e.mouseDelta.y / kPixelsPerDetent;
            const int whole = (int)accum;
            if (whole != 0) {
                accum -= (float)whole;
                pushDetents(whole);
            }
        }
        Widget::onDragMove(e);
    }

    void onHoverScroll(const event::HoverScroll& e) override {
        // The wheel stands in for a drag anywhere a drag would turn the encoder,
        // and touches the button not at all — which makes it the unambiguous way
        // to turn, and the one to reach for while editing a menu value.
        const int n = (e.scrollDelta.y > 0.f) ? 1 : (e.scrollDelta.y < 0.f ? -1 : 0);
        if (n != 0) {
            pushDetents(n);
            e.consume(this);
        }
        Widget::onHoverScroll(e);
    }

    void draw(const DrawArgs& args) override {
        const float r = box.size.x * 0.5f;
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, r, r, r);
        nvgFillColor(args.vg, nvgRGB(0x1c, 0x20, 0x22));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(0x3b, 0x46, 0x49));
        nvgStrokeWidth(args.vg, 1.f);
        nvgStroke(args.vg);

        // Knurling: a ring of ticks, so it reads as a thing you turn rather than
        // a thing you point.
        for (int i = 0; i < 24; i++) {
            const float a = (float)i / 24.f * 2.f * M_PI;
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, r + std::cos(a) * r * 0.72f,
                               r + std::sin(a) * r * 0.72f);
            nvgLineTo(args.vg, r + std::cos(a) * r * 0.92f,
                               r + std::sin(a) * r * 0.92f);
            nvgStrokeColor(args.vg, nvgRGBA(0xb0, 0x8d, 0x3f, 0x88));
            nvgStrokeWidth(args.vg, 0.8f);
            nvgStroke(args.vg);
        }

        // The cap shows what the gesture has become: amber while a hold is being
        // counted towards the menu, and the hold's progress as a ring, so it is
        // clear whether you are about to change mode or about to turn.
        NVGcolor cap = nvgRGB(0x23, 0x2b, 0x2e);
        if (turning)        cap = nvgRGB(0x2a, 0x33, 0x36);
        else if (longFired) cap = nvgRGB(0x3a, 0x44, 0x33);
        else if (armed)     cap = nvgRGB(0x33, 0x2e, 0x22);
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, r, r, r * 0.6f);
        nvgFillColor(args.vg, cap);
        nvgFill(args.vg);

        if (armed && !turning && !longFired) {
            const float t = (float)((system::getTime() - downTime) / kLongPressSec);
            if (t > 0.05f) {
                nvgBeginPath(args.vg);
                nvgArc(args.vg, r, r, r * 0.78f, -M_PI * 0.5f,
                       -M_PI * 0.5f + std::min(t, 1.f) * 2.f * M_PI, NVG_CW);
                nvgStrokeColor(args.vg, nvgRGB(0xb0, 0x8d, 0x3f));
                nvgStrokeWidth(args.vg, 1.6f);
                nvgStroke(args.vg);
            }
        }
    }
};

}  // namespace ogham
