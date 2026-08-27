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
// An endless control, so it has no value to show and no param behind it. It
// converts dragging and scrolling into detents and pushes them at the module;
// everything that decides what a detent MEANS — acceleration, long-press,
// navigate versus edit — stays in the transcribed application layer, which is
// what keeps the feel of the gesture the module's rather than Rack's.
//
// TURNING AND PRESSING ARE THE SAME MOUSE BUTTON, which the hardware never has
// to deal with: on the module you turn the shaft with your fingers and press it
// with your thumb, and the two cannot be confused. Reporting the mouse button
// straight through made every turn begin with a press, so a slow turn crossed
// the 600 ms long-press threshold and dropped into the menu, and a quick turn
// released as a short click and switched voice.
//
// So the press is deferred and then decided by what the pointer does:
//
//   moves past kMoveThreshold   -> a turn. No press is ever reported.
//   held still past kHoldMs     -> a press. The app then times its own long
//                                  press from there, so hold-to-enter works.
//   released before either      -> a click. A press and release are emitted
//                                  back to back so the app sees a short click.
//
// The result is that a drag never enters the menu and a click never turns.
// ---------------------------------------------------------------------------

struct EncoderWidget : Widget {
    std::atomic<int>*  detents = nullptr;
    std::atomic<bool>* pressed = nullptr;

    // Vertical travel per detent, in pixels. Tuned so a comfortable drag steps
    // about one function at a time and a fast flick crosses the bank — the same
    // relationship the hardware's acceleration curve assumes. Worth checking
    // against a real module by turning both.
    static constexpr float kPixelsPerDetent = 8.f;

    // How far the pointer must move before a press is reinterpreted as a turn,
    // in pixels: enough to survive the shake of clicking, far less than a
    // deliberate drag.
    static constexpr float kMoveThreshold = 3.f;

    // How long the button must be held still before it counts as a press. Well
    // under the app's 600 ms long-press, so holding still still reaches the menu
    // without a perceptible delay.
    static constexpr double kHoldMs = 140.0;

    // How long a click's press is held before it is released, in seconds. It has
    // to outlast at least one of the app's 1 kHz control ticks, and be shorter
    // than the 600 ms that would make it a long press.
    static constexpr double kClickHoldSec = 0.03;

    // What this gesture has turned out to be. A gesture is undecided until the
    // pointer moves or the clock runs out, which is the whole point: turning and
    // pressing share one mouse button, and the hardware never has to tell them
    // apart because a thumb and fingers cannot be confused.
    enum class Gesture { None, Undecided, Pressing, Turning, Clicking };
    Gesture gesture = Gesture::None;

    double gestureStart = 0.0;
    double clickUntil   = 0.0;
    float  travel = 0.f;
    float  accum  = 0.f;

    void push(int n) {
        if (detents && n != 0) detents->fetch_add(n, std::memory_order_relaxed);
    }
    void setPressed(bool v) {
        if (pressed) pressed->store(v, std::memory_order_relaxed);
    }

    // Finish a click that is still being held out. Called before anything else
    // starts, so the app always sees a clean press-then-release pair and never a
    // stray release in the middle of the next gesture — which is what made a
    // click followed quickly by a turn read as two clicks, and toggled the menu
    // field's edit mode straight back off.
    void endClick() {
        if (gesture == Gesture::Clicking) {
            setPressed(false);
            gesture = Gesture::None;
        }
    }

    void onButton(const event::Button& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            endClick();
            gesture = Gesture::Undecided;
            gestureStart = system::getTime();
            travel = 0.f;
            accum = 0.f;
            e.consume(this);
        } else if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_RELEASE) {
            if (gesture == Gesture::Pressing) {
                setPressed(false);
                gesture = Gesture::None;
            } else if (gesture == Gesture::Undecided) {
                // Never moved, never held long enough: a click.
                setPressed(true);
                gesture = Gesture::Clicking;
                clickUntil = system::getTime() + kClickHoldSec;
            } else if (gesture == Gesture::Turning) {
                gesture = Gesture::None;
            }
        }
        Widget::onButton(e);
    }

    void step() override {
        const double now = system::getTime();
        if (gesture == Gesture::Clicking && now >= clickUntil) {
            endClick();
        } else if (gesture == Gesture::Undecided &&
                   (now - gestureStart) * 1000.0 >= kHoldMs) {
            gesture = Gesture::Pressing;
            setPressed(true);
        }
        Widget::step();
    }

    void onDragStart(const event::DragStart& e) override {
        APP->window->cursorLock();
        Widget::onDragStart(e);
    }

    void onDragEnd(const event::DragEnd& e) override {
        APP->window->cursorUnlock();
        Widget::onDragEnd(e);
    }

    void onDragMove(const event::DragMove& e) override {
        if (gesture == Gesture::Undecided) {
            travel += std::fabs(e.mouseDelta.y) + std::fabs(e.mouseDelta.x);
            if (travel > kMoveThreshold) gesture = Gesture::Turning;
        }
        if (gesture == Gesture::Turning) {
            // Up is clockwise, matching every other knob in Rack.
            accum += -e.mouseDelta.y / kPixelsPerDetent;
            const int whole = (int)accum;
            if (whole != 0) {
                accum -= (float)whole;
                push(whole);
            }
        }
        Widget::onDragMove(e);
    }

    void onHoverScroll(const event::HoverScroll& e) override {
        const int n = (e.scrollDelta.y > 0.f) ? 1 : (e.scrollDelta.y < 0.f ? -1 : 0);
        if (n != 0) {
            push(n);
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
        // Cap. It lights while the button is genuinely held, which is the only
        // feedback that a hold is being counted rather than a turn beginning.
        const bool held = (gesture == Gesture::Pressing || gesture == Gesture::Clicking);
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, r, r, r * 0.6f);
        nvgFillColor(args.vg, held ? nvgRGB(0x3a, 0x44, 0x33)
                   : (gesture == Gesture::Turning) ? nvgRGB(0x2a, 0x33, 0x36)
                                                   : nvgRGB(0x23, 0x2b, 0x2e));
        nvgFill(args.vg);
    }
};

}  // namespace ogham
