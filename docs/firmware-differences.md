# Firmware differences

Every place the plugin's behaviour departs from the Ogham module's, why, and
whether it is permanent.

The firmware repository is the source of truth and this project never commits to
it. Six of its translation units are compiled into the plugin unmodified and a
seventh — the display transport — is compiled against dead pins; only
`ogham_main.cpp` is re-created here. Divergence is therefore not accidental, but
it is real, and this file is where it is accounted for.

**The rule: a divergence that is not in this file is a bug.** Add the row in the
same commit that creates the difference.

Upstream pin: `keeos-io/ogham` @ `4dbd4c5` (firmware v1.16 content; upstream has
no v1.16 tag, so the submodule pins the commit).

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
| Sample rate | 48 kHz native | 48 kHz core, converted at the Rack boundary | planned | Yes. No native-rate mode, since that would mean editing the DSP |
| Latency | None | ~20.8 µs when the host is not at 48 kHz, from the Hermite interpolator's one-sample lookahead. Zero at 48 kHz, where the converter is bypassed | planned | Yes |
| Persistence | QSPI, packed `OghamSettings` struct | JSON, named fields, plugin-side `schemaVersion` | planned | Yes. Hardware patch compatibility is explicitly not a goal |
| Instrumentation | DWT cycle counters, SWD telemetry struct | None | live | Yes |
| Encoder | Quadrature scanned at 20 kHz on TIM5, with NVIC priority work behind it | Detent events from a Rack widget | planned | Yes |

## Levels and calibration

The firmware carries measured corrections for real analog hardware. In software
each becomes its ideal value; reproducing them would be reproducing the errors.

| Area | Module | Plugin | Status | Permanent? |
|---|---|---|---|---|
| `AUDIO_OUT_LEVEL` | 0.51, halving the digital output to compensate an analog stage running ~2× hot | 1.0 | live | Converge if the hardware gain is ever halved |
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
| Two-compiler check | Phase 0 verified the verbatim units under MSVC 14.50 only; no MinGW or clang is installed on this machine. The GCC leg lands when the Rack toolchain is set up, and matters because Rack builds Windows plugins with MinGW |
