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
#include "prefs.hpp"

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
    // Whether a drag turns the encoder. A per-installation setting, not patch
    // state: how the encoder answers a mouse belongs to the desk it is used at,
    // so opening someone else's patch must not change it. See prefs.hpp.
    //
    // Switched off, the encoder still turns by scroll and still takes clicks and
    // holds; only the drag goes quiet.
    bool DragTurns() const { return ogham::prefs::DragTurnsEncoder(); }

    std::atomic<int>* detents = nullptr;
    std::atomic<int>* clicks  = nullptr;
    std::atomic<int>* longs   = nullptr;

    // The cap: Rack's big knob background, recoloured gold by
    // tools/build_encoder_knob.py. The background frame rather than the knob
    // proper, because that one carries an indicator line and an endless encoder
    // has nothing for a pointer to point at. Its ring of facets still shows the
    // cap turning, which is the feedback that matters.
    FramebufferWidget* fb = nullptr;
    TransformWidget*   tw = nullptr;
    SvgWidget*         sw = nullptr;
    float drawnAngle = 1e9f;
    float capScale = 1.f;

    // The cap is drawn from Rack's big knob, which is 45 px across, and the
    // module's encoder is the same size as its pots. RoundBlackKnob is 28.35 px,
    // so the cap is scaled to that rather than to a number chosen by eye — if
    // either graphic changes, this still matches.
    static constexpr float kPotDiameterPx = 28.34759f;

    EncoderWidget() {
        fb = new FramebufferWidget;
        tw = new TransformWidget;
        sw = new SvgWidget;
        sw->setSvg(Svg::load(asset::plugin(pluginInstance,
                                           "res/components/OghamEncoder.svg")));
        tw->addChild(sw);
        fb->addChild(tw);
        addChild(fb);

        capScale = (sw->box.size.x > 0.f) ? kPotDiameterPx / sw->box.size.x : 1.f;
        tw->box.size = sw->box.size.mult(capScale);
        fb->box.size = tw->box.size;
        box.size     = tw->box.size;
    }

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

    // Where the knob is pointing, in detents. The module's encoder is 24 PPR, so
    // the graphic turns exactly as the real one does: a detent is 15 degrees, and
    // 24 of them is a revolution. Purely cosmetic — an endless encoder has no
    // position — but it is the feedback that says a turn was registered, which
    // matters most when a drag is small.
    static constexpr int kDetentsPerRev = 24;
    float angleDetents = 0.f;

    void pushDetents(int n) {
        if (n == 0) return;
        angleDetents += (float)n;
        if (detents) detents->fetch_add(n, std::memory_order_relaxed);
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

        // Turn the cap. Repainted only when it has actually moved, since the
        // framebuffer is the expensive part.
        //
        // The scale sits inside the transform rather than around it, so the
        // framebuffer renders at the size it is drawn and stays crisp. Read the
        // calls in reverse: the centre goes to the origin, is scaled, is
        // rotated, and comes back to where the scaled centre belongs.
        const float angle = angleDetents * (2.f * M_PI / kDetentsPerRev);
        if (sw && sw->svg && angle != drawnAngle) {
            drawnAngle = angle;
            const math::Vec centre = sw->box.getCenter();
            tw->identity();
            tw->translate(centre.mult(capScale));
            tw->rotate(angle);
            tw->scale(capScale);
            tw->translate(centre.neg());
            fb->setDirty();
        }
        Widget::step();
    }

    // The cursor is locked only when a drag can actually turn something. Locking
    // it otherwise would hide the pointer for a gesture that does nothing, and
    // would also suppress the button release (see endGesture) for no reason.
    void onDragStart(const event::DragStart& e) override {
        if (DragTurns()) APP->window->cursorLock();
        Widget::onDragStart(e);
    }

    void onDragEnd(const event::DragEnd& e) override {
        if (DragTurns()) APP->window->cursorUnlock();
        endGesture();
        Widget::onDragEnd(e);
    }

    void onDragMove(const event::DragMove& e) override {
        // Movement makes it a turn — at any point, not only in the first
        // moments. A press, a pause, then a drag is a turn, because that is what
        // it looks like to the person doing it. Once the long press has fired the
        // gesture is settled and dragging does nothing, matching a module whose
        // menu you have just entered with the shaft still under your thumb.
        // With drag turned off the gesture never becomes a turn, so the press is
        // still live as a click or a hold when the button comes up. Moving the
        // mouse then does nothing at all rather than something unexpected.
        if (armed && !longFired && DragTurns()) {
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
        // Scrolling turns the encoder only when Rack has been told that scroll
        // wheels adjust knobs. That setting is off by default, and it exists to
        // settle exactly this conflict: without it, scroll belongs to the view.
        //
        // Taking the wheel unconditionally would break panning over the
        // encoder, and worst on a Mac: a two-finger trackpad gesture IS a
        // scroll, so the natural way to move around a patch would turn the
        // function instead whenever the pointer sat over the knob.
        //
        // Not consuming the event is the whole point: unconsumed, it travels on
        // and the view scrolls, exactly as it does over any other module.
        if (!settings::knobScroll) {
            Widget::onHoverScroll(e);
            return;
        }

        const int n = (e.scrollDelta.y > 0.f) ? 1 : (e.scrollDelta.y < 0.f ? -1 : 0);
        if (n != 0) {
            pushDetents(n);
            e.consume(this);
        }
        Widget::onHoverScroll(e);
    }

    void draw(const DrawArgs& args) override {
        // The cap itself, from the SVG.
        Widget::draw(args);

        const float r = box.size.x * 0.5f;

        // A hold darkens the cap and runs its progress round the rim — the only
        // sign that the menu is about to open rather than a turn beginning.
        if (armed && !turning && !longFired) {
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, r, r, r * 0.86f);
            nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 0x3a));
            nvgFill(args.vg);

            const float t = (float)((system::getTime() - downTime) / kLongPressSec);
            if (t > 0.05f) {
                nvgBeginPath(args.vg);
                nvgArc(args.vg, r, r, r * 0.72f, -M_PI * 0.5f,
                       -M_PI * 0.5f + std::min(t, 1.f) * 2.f * M_PI, NVG_CW);
                nvgStrokeColor(args.vg, nvgRGB(0xff, 0xf0, 0xc8));
                nvgStrokeWidth(args.vg, 2.0f);
                nvgStroke(args.vg);
            }
        } else if (longFired) {
            // Held long enough: a brief confirmation that it landed.
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, r, r, r * 0.72f);
            nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xf0, 0xc8, 0x88));
            nvgStrokeWidth(args.vg, 2.0f);
            nvgStroke(args.vg);
        }
    }
};

// ---------------------------------------------------------------------------
// The Clk / VOct toggle.
//
// The module's is a metal bat switch through a threaded bushing with a hex nut,
// which is what Rack's NKK frames draw. Two of its three positions: the lever
// points at the legend it selects, and the panel prints Clk above and VOct
// below, so param 0 (Clock) is the UP frame and param 1 (V/oct) is the DOWN one.
//
// NKK_0 is the down throw and NKK_2 the up one, which is the opposite way round
// from what the numbering suggests — checked by looking at them rather than
// assuming.
// ---------------------------------------------------------------------------

struct ModeToggle : app::SvgSwitch {
    // The library part is drawn for a bigger panel than this one. A fifth off
    // puts it in proportion with the pots either side of it.
    static constexpr float kScale = 0.8f;

    ModeToggle() {
        shadow->opacity = 0.0;
        addFrame(Svg::load(asset::system("res/ComponentLibrary/NKK_2.svg")));  // up: Clk
        addFrame(Svg::load(asset::system("res/ComponentLibrary/NKK_0.svg")));  // down: V/oct
        // addFrame sizes the widget from the first frame; shrink it afterwards
        // so the hit box matches what is drawn.
        box.size = box.size.mult(kScale);
    }

    void draw(const DrawArgs& args) override {
        nvgSave(args.vg);
        nvgScale(args.vg, kScale, kScale);
        app::SvgSwitch::draw(args);
        nvgRestore(args.vg);
    }
};

}  // namespace ogham
