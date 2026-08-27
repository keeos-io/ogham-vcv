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
| Application layer | `ogham_main.cpp` — a `main()` on file-scope globals with ISRs | Transcribed as `OghamApp`, per-instance | planned | Yes. The consequence of leaving the firmware untouched; see the implementation plan, section 4 |
| Display transport | `tm1637.cpp` bit-bangs GPIO at ~9 ms per write | The same file, compiled verbatim against a no-op `daisy::GPIO`. `WriteSegments` caches the four segment bytes before clocking them out, so `GetLastSegs()` returns exactly what the hardware display would show | live | Yes — and better than the plan assumed, which was a hand-written stand-in and a second copy of the segment font |
| Time source | `System::GetNow()` from the STM32 tick | A process-wide hook: engine time in Rack, a virtual clock in the offline renderer, the steady clock by default | live | Yes |
| Control loop | Main loop, roughly 1 kHz, whatever the audio callback leaves it | Exactly every 48 core samples | planned | Yes. The firmware's rate drifts with load; a fixed divider is the closest honest equivalent |
| Sample rate | 48 kHz native | 48 kHz core, converted at the Rack boundary | live | Yes. No native-rate mode, since that would mean editing the DSP |
| Latency | None | **~41.7 µs** — two core samples — when the host is not at 48 kHz. The Catmull-Rom window interpolates between the second and third of four points, so the read position trails the newest sample by two, not the one the plan estimated. Zero at 48 kHz, where the converter is bypassed entirely | live | Yes |
| Resampling below 48 kHz | n/a | At host rates under 48 kHz the core is decimated without an anti-alias filter, folding 22–24 kHz content down. Deliberate: the source is an 8-bit stream at an 8 kHz tick rate whose aliasing is the sound, and filtering it on the way out would be adding something the module does not have | live | Yes |
| Source delivery | n/a | Firmware sources vendored as verified copies, not a submodule: a recursive clone of a submodule descends into libDaisy and ST's CMSIS trees and fails | live | Yes |
| Persistence | QSPI, packed `OghamSettings` struct | JSON, named fields, plugin-side `schemaVersion` | planned | Yes. Hardware patch compatibility is explicitly not a goal |
| Instrumentation | DWT cycle counters, SWD telemetry struct | None | live | Yes |
| Encoder | Quadrature scanned at 20 kHz on TIM5, with NVIC priority work behind it | Detent events from a Rack widget | planned | Yes |

## Levels and calibration

The firmware carries measured corrections for real analog hardware. In software
each becomes its ideal value; reproducing them would be reproducing the errors.

| Area | Module | Plugin | Status | Permanent? |
|---|---|---|---|---|
| `AUDIO_OUT_LEVEL` | 0.51, halving the digital output to compensate an analog stage running ~2× hot | 1.0 | live | Converge if the hardware gain is ever halved |
| Audio scaling | ~10 Vpp at the jack | ×5 V, so nominal level is ±5 V and FX peaks reach about ±6.4 V | live | Open — `ovcv-maw` |
| Output headroom | Analog stage clips | The FX chain peaks around ±1.28 on the smoke script, so a nominal ×5 V scaling reaches ~±6.4 V on peaks. Real modules do this too; the alternative is scaling everything down and being quieter than the hardware | open | To decide in phase 2 |
| `RATE_POT_CENTER` | 0.459, the fleet mean 12-o'clock ADC reading | 0.5 — otherwise unity rate would not land at noon | planned | Yes |
| `POT_FULL_SCALE` | 0.945, deliberately under the measured 0.9508 so every module reaches 255 | 1.0 | planned | Yes |
| CV summing | MCP6004 inverting sum, `adc = 0.7501 - 0.9990·pot`, railing at 79 % of pot travel | Ideal sum of knob and CV, clamped 0–1 | planned | Yes |
| `TIMBRE_CV_K_A/B` | 1.3164 / 1.3271, matching knob and CV gain | 1.0 | planned | Yes |
| V/oct input | ADC fractions, `VOCT_FRAC_PER_OCT` = 0.1948 | Exact 1 V/oct. `VOCT_RATE_TUNE` is kept — it tunes the bank, not the hardware | planned | Yes |
| ADC smoothing | Three one-pole filters against ADC noise | None. Rack params are exact; there is no noise to filter | planned | Yes |
| EOC level | 5 V, from the 74AHCT1G125 on the +5 V rail | 10 V, the Rack convention | planned | Yes |

## Determinism

| Area | Module | Plugin | Status | Permanent? |
|---|---|---|---|---|
| Hold capture phase | Re-rolled from the microsecond timer when the Rate knob moves; a power cycle returns to the built-in phase 7 | The same gesture, but the RNG state is persisted, so a reopened patch sounds the way it was left | planned | Yes. A patch that reloads differently would be a bug in a plugin, however correct it is in a module |

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
