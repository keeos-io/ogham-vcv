# Firmware differences

Every place the plugin's behaviour departs from the Ogham module's, why, and
whether it is permanent.

The firmware repository is the source of truth and this project never commits to
it. Seven of its nine translation units are compiled into the plugin unmodified
— the seventh, the display transport, against dead pins — and only
`ogham_main.cpp` is re-created here. Divergence is therefore not accidental, but
it is real, and this file is where it is accounted for.

**The rule: a divergence that is not in this file is a bug.** Add the row in the
same commit that creates the difference.

Upstream: `keeos-io/ogham` @ `4dbd4c5` (firmware v1.16 content; upstream has no
v1.16 tag, so the sync records the commit). The sources are vendored in
`ogham-src/` rather than reached through a submodule — see `ogham-src/README.md`
for why, and `tools/upstream_check.py` for how that is kept honest.

| Status | Meaning |
|---|---|
| **live** | In effect in the current build |
| **planned** | Decided, not yet implemented |

---

## Architecture

| Area | Module | Plugin | Status | Permanent? |
|---|---|---|---|---|
| Application layer | `ogham_main.cpp` — a `main()` on file-scope globals with ISRs | Transcribed as `OghamApp`, per-instance. Phase 2 (voice and control) done; the menu and display follow in phase 3 | live | Yes. The consequence of leaving the firmware untouched; see the implementation plan, section 4 |
| Display transport | `tm1637.cpp` bit-bangs GPIO at ~9 ms per write | The same file, compiled verbatim against a no-op `daisy::GPIO`. `WriteSegments` caches the four segment bytes before clocking them out, so `GetLastSegs()` returns exactly what the hardware display would show | live | Yes — and better than the plan assumed, which was a hand-written stand-in and a second copy of the segment font |
| Time source | `System::GetNow()` from the STM32 tick | A process-wide hook: engine time in Rack, a virtual clock in the offline renderer, the steady clock by default | live | Yes |
| Control loop | Main loop, roughly 1 kHz, and irregular — the blocking display writes stall it for ~9 ms at a time | Exactly every 48 core samples. CV Out is therefore a clean 1 kHz staircase where the module's is a ragged one | live | Yes. The firmware's rate drifts with load; a fixed divider is the closest honest equivalent |
| Sample rate | 48 kHz native | 48 kHz core, converted at the Rack boundary | live | Yes. No native-rate mode, since that would mean editing the DSP |
| Latency | None | **~41.7 µs** — two core samples — when the host is not at 48 kHz. The Catmull-Rom window interpolates between the second and third of four points, so the read position trails the newest sample by two, not the one the plan estimated. Zero at 48 kHz, where the converter is bypassed entirely | live | Yes |
| Resampling below 48 kHz | n/a | At host rates under 48 kHz the core is decimated without an anti-alias filter, folding 22–24 kHz content down. Deliberate: the source is an 8-bit stream at an 8 kHz tick rate whose aliasing is the sound, and filtering it on the way out would be adding something the module does not have | live | Yes |
| Source delivery | n/a | Firmware sources vendored as verified copies, not a submodule: a recursive clone of a submodule descends into libDaisy and ST's CMSIS trees and fails | live | Yes |
| Persistence | QSPI, packed `OghamSettings` struct | JSON, named fields, plugin-side `schemaVersion` | planned | Yes. Hardware patch compatibility is explicitly not a goal |
| Instrumentation | DWT cycle counters, SWD telemetry struct | None | live | Yes |
| Encoder gestures | Turning and pressing are physically separate: fingers on the shaft, thumb on the button, and the firmware reads a raw button | One mouse button does both, so the WIDGET classifies the gesture and hands over the result — move and it is a turn, hold 600 ms and it is a long press, release before either and it is a click. `OghamApp` keeps its raw-button path for a host that has one, and accepts classified gestures alongside it. The vocabulary is the module's, unchanged: drag to change, click to switch voice or edit, hold to enter or leave the menu | live | Yes |
| Right-click menu entry | Hold the encoder | Also an Open/Leave the Menu item, routed through the same long-press path | live | Yes |
| Encoder | Quadrature scanned at 20 kHz on TIM5, with NVIC priority work behind it | Detent events from a Rack widget (phase 3). Until then the two function slots are ordinary params | planned | Yes |

## Levels and calibration

The firmware carries measured corrections for real analog hardware. In software
each becomes its ideal value; reproducing them would be reproducing the errors.

| Area | Module | Plugin | Status | Permanent? |
|---|---|---|---|---|
| `AUDIO_OUT_LEVEL` | 0.51, halving the digital output to compensate an analog stage running ~2× hot | 1.0 | live | Converge if the hardware gain is ever halved |
| Audio scaling | ~10 Vpp at the jack | ×5 V. Settled, and it is hardware-true: the module's own jack does 4.97 V for digital 1.0 and 7.61 V on the demo's peaks against the plugin's 5.00 and 7.65 | live | Yes |
| CV Out | DAC 0–3.3 V through a TL072 to roughly 0–10 V | ×10 V from the same 0–1 the DAC is handed | live | Yes |
| Clock timestamps | `System::GetUs()`, which wraps every ~17.9 s and needs a signed-difference guard | A 64-bit core-sample counter, which cannot wrap in a session. The guard is not transcribed — one of the few places where less code is the faithful translation | live | Yes |
| Input edges at the boundary | An edge is an interrupt; the core is always running | Latched in `RateConverter` and delivered on the next core step. `advance()` returns zero for half the host samples at 96 kHz and three in four at 192 kHz, so an unlatched edge landing on one is dropped where it stands | live | Yes — a bug, fixed 2026-08-29 and confirmed in Rack at 44.1, 48 and 96 kHz |
| Clk / VOct jack | One jack into both a comparator and an ADC tap; the clock ISR runs whatever the Mode switch says | Edges are only read in Clock mode; in V/oct the jack is a pitch CV and nothing else | live | Yes |
| Output headroom | Analog stage clips | Nothing clamps. Rack's voltage standards ask for ±5 V typical and argue against enforcing it, and the module's own jack does 4.97 V for digital 1.0 against the plugin's 5.00. The FX chain reaches ±7.6 V on peaks here and ±7.61 V there | live | Yes — measured, see below |
| `RATE_POT_CENTER` | 0.459, the fleet mean 12-o'clock ADC reading | 0.5 — otherwise unity rate would not land at noon | live | Yes |
| `POT_FULL_SCALE` | 0.945, deliberately under the measured 0.9508 so every module reaches 255 | 1.0 | live | Yes |
| CV summing | MCP6004 inverting sum, `adc = 0.7501 - 0.9990·pot`, railing at 79 % of pot travel | Ideal sum of knob and CV, clamped 0–1. The isolated-CV split the Timbre route needs is therefore exact rather than recovered by subtracting a measured gain, and does not saturate at the rails | live | Yes |
| `TIMBRE_CV_K_A/B` | 1.3164 / 1.3271, matching knob and CV gain | 1.0 | live | Yes |
| V/oct input | ADC fractions, `VOCT_FRAC_PER_OCT` = 0.1948 | Exact 1 V/oct. `VOCT_RATE_TUNE` is kept — it tunes the bank, not the hardware | live | Yes |
| Tone knob centre | `LOFI_CENTER` = 0.458, the fleet mean 12-o clock ADC reading, with a +-0.02 clean band | The knob is MAPPED onto that scale rather than the constant being dropped, because it lives inside `SetLofiMacro` — one of the files compiled verbatim, which this project does not edit. Piecewise linear: 0 to 0, centre to 0.458, 1 to 0.9597. Without it the clean dot lit about 4% anticlockwise of noon, which is what it did until someone used it | live | Yes, while the calibration stays inside the DSP |
| ADC smoothing | Three one-pole filters against ADC noise | None. Rack params are exact; there is no noise to filter. The A/B sub-LSB hysteresis is kept anyway, so the two files stay readable against each other | live | Yes |
| EOC level | 5 V, from the 74AHCT1G125 on the +5 V rail | 10 V, the Rack convention | live | Yes |

## Determinism

| Area | Module | Plugin | Status | Permanent? |
|---|---|---|---|---|
| Hold capture phase | Re-rolled from the microsecond timer when the Rate knob moves; a power cycle returns to the built-in phase 7 | The same gesture, seeded from the engine's own position (`coreSample ^ t`) rather than a timer. The phase itself is NOT persisted — `AudioPipeline` keeps it private with no accessor, and reaching it would mean editing the firmware. A reopened patch therefore starts on the built-in phase 7, which is exactly what the module does after a power cycle | live | Yes, and it matches the hardware rather than diverging from it |
| Boot splash | The firmware version is shown for ~1 s at power-on | None. Instantiating a module in a patch is not a power-on, and the plugin's version is not the firmware's | live | Yes |

## Shared state

| Area | Note |
|---|---|
| `g_lofiConfig` | A mutable global in `ogham_audio_pipeline.cpp`, and the only one reachable from the verbatim sources. It is written solely by the SWD monitor on hardware and read everywhere else, so plugin instances sharing it is safe. Audited, not assumed |

## Harness-only

Not shipped in the plugin, but recorded so it is not mistaken for the real thing
later.

| Area | Note |
|---|---|
| `FxFieldPtr` | `tests/parity/render.cpp` carries its own menu-field-to-struct-byte mapping, because the firmware's lives in `ogham_main.cpp` and is not compiled. It is duplicated logic and will be superseded by the transcribed version in phase 3 |
| Two-compiler check | Settled. All seven units compile unmodified under MSVC 14.50, MinGW-w64 GCC 14.2.0 and clang 19.1.7. GCC and clang render **bit-identical** audio; MSVC differs — see below |

---

## Build determinism

Not a module-versus-plugin divergence, but it decides what the parity harness
can claim, so it belongs here.

**Floating-point contraction must be off.** GCC and clang default to
`-ffp-contract=fast`, which lets the compiler fuse `a*b+c` into a single FMA
with a different result from the unfused pair. MSVC does not contract by
default. Every build that feeds a parity comparison — the harness and the Rack
plugin alike — carries `-ffp-contract=off`. Without it, the same source built
two ways renders audio that differs in the last bits and the hash stops meaning
anything.

**Bit-exactness holds within a toolchain family, not across libm
implementations.** With contraction off, GCC 14.2.0 and clang 19.1.7 produce
byte-identical WAVs — they share mingw-w64's libm. MSVC does not:

| | |
|---|---|
| Samples differing | 1.52 % of Out 1, 0.67 % of Out 2 |
| Maximum difference | 2.4 × 10⁻⁷, about −132 dBFS |
| ENV and EOC | Identical, to the byte |

That is a last-place difference in `expf`, `sinf` and friends, which the lo-fi
filter and LPG coefficients are built from — not a logic divergence. It is
inaudible and musically irrelevant, but it means the parity *reference* must be
built with the same toolchain as the plugin. Both are MinGW GCC, so this costs
nothing; `tools/build_host.bat` (MSVC) stays as a portability check only, and is
not a parity reference.

**Rack's default optimisation flags must not reach the firmware sources.**
`compile.mk` in the SDK builds plugins with `-O3 -funsafe-math-optimizations
-march=nehalem`. `-funsafe-math-optimizations` implies associative and
reciprocal maths, no signed zeros and no trapping — it lets the compiler
reassociate the DSP's arithmetic. Measured against the harness build on the
smoke script:

| | |
|---|---|
| Samples differing | 67.5 % of Out 1, 74.4 % of Out 2 |
| Maximum difference | 1.2 × 10⁻⁶, about −118 dBFS |
| ENV and EOC | Identical |
| Speed | 347× vs 335× real time — about 3 % |

Inaudible, and it costs the parity hash entirely: two thirds of samples differ,
so a null test against the reference render would fail on every run for no real
reason. The plugin therefore appends `-ffp-contract=off
-fno-unsafe-math-optimizations` (via `EXTRA_FLAGS`, which `compile.mk` applies
last so they win) for the firmware translation units. Three percent of a
one-percent CPU budget is not a trade worth making.

**The shim must be C++11.** Rack builds plugins with `-std=c++11`, where a class
carrying a default member initialiser is not an aggregate — so `Pin{index}` in
`src/shim/daisy_seed.h` compiled fine in the C++17 harness and failed in the
plugin. Anything added to the shim has to build under both.

**`arch.mk` asks `$(CC) -dumpmachine`,** and make's built-in default for `CC` is
`cc`, which a WinLibs or MinGW toolchain does not ship. The Makefile overrides
the built-in default only, so an environment or command-line setting still wins.

**Rack does not deliver a button release while the cursor is locked.**
`EventState::handleButton` skips dispatching `ButtonEvent` entirely when
`isCursorLocked()`, and the encoder locks the cursor on drag start so the pointer
stays put while you turn. The press arrives (the lock is not yet taken), the
release never does. `DragEnd` is dispatched either way, so that is where the end
of a gesture has to be detected.

This is what made clicks disappear while turning and holding both worked, and it
is not visible in the widget's own logic — only in Rack's event dispatcher.

**The panel's components are Rack's, recoloured.** The panel artwork is the
module's own, but the screws, jacks, knobs and the Clk/VOct toggle are VCV
Component Library graphics, and the encoder cap is one of them recoloured gold.
They are CC BY-NC 4.0, which a free plugin may use with credit — see
THIRD-PARTY.md, which also records that redrawing those five is the only work
standing between this and being sellable outside the VCV Library.

**A plugin has to zero what a module gets for free.** `BpmClock` declares four
arrays with no default initialiser that `Init()` never writes — `fftBuffer_`,
`fluxLin_`, `corr_` and `bpmHist_`. On the module the object is a file-scope
static, so the loader zeroes it before `main()` and the omission is invisible.
In the plugin it is a member of a heap-allocated `Module`, where `new Ogham`
default-initialises it and those arrays hold whatever was there. `bpmHist_` is
the estimator's agreement history, so a fresh module could lock to a tempo
derived from nothing, differently on each instantiation.

`OghamApp` is therefore value-initialised — `app{}` — which zeroes the whole
object before `Init()` runs. It is one brace, and it is the difference between
deterministic start-up and an intermittent bug nobody could reproduce.

Found by the multi-instance test, and worth saying how: it presented as eight
instances sharing state. They were not. Each was starting from different
rubbish, and the harness's own solo runs — stack-allocated, one after another at
the same address — were inheriting each other's leftovers. The test now
value-initialises both paths, so it compares like with like.

**Nothing clamps the outputs, deliberately.** Rack's voltage standards ask for
±5 V typical and then argue against enforcing it: "It is much better to allow
voltages outside this range rather than use hard clipping … because in the best
case they will be attenuated by a module downstream, and in the worst case, they
will be hard clipped by the Audio module from Core." The ceiling that matters is
the ±12 V rail, and nothing here approaches it.

Measured, so the question does not have to be reopened:

| | |
|---|---|
| Nominal (digital 1.0) | ±5.00 V |
| Full wavefold, Tone hard anticlockwise | ±4.98 V — inside ±5 V |
| With the FX chain running | ±6.4 V typical, ±7.6 V on peaks |
| Rail | ±12 V, never approached |

The flat-topped look at full fold on a scope is the lo-fi macro's saturator
doing its job, not an overflow: 18.6 % of samples sit within 1 % of the peak but
only 0.002 % sit exactly on it, so the waveform is saturated rather than clipped.
Turning the scope's volts-per-division down shows the shape.
