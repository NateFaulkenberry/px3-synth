# AnalogEngine — Architecture

Signal flow, insertion points and state management. Written before
implementation; measured values live in `docs/ANALOG_ENGINE_TUNING.md`, and the
reasoning behind the chosen approach is in `docs/ANALOG_ENGINE_RESEARCH.md`.

---

## 1. The existing signal path, as traced

Traced through `PX3SynthAudioProcessor::processBlock`, not inferred from
filenames.

```
SynthVoice × 64
  │   per voice: sources → VIBE (applyVibeSourceStage) → filters → amp env
  ▼
oscillatorBusBuffer          4 MONO channels: SUB, OSC1, OSC2, OSC3
  │
  ├─ polyphony gain (per source, per sample)
  │
  ▼  ── per-sample loop ────────────────────────────────────────────────
  │
  │   for each of the 4 sources:
  │       sampleValue = source × phaseInvert
  │       dry  += sampleValue × level × pan(L/R) × dryGate
  │       send += sampleValue × kSendCentreGain × sendGain × sendGate
  │
  │   DRY CHANNEL: dryL/R × dryGain × dryPhase × dryGate × dryPan × √2
  │       └─► dryBusBuffer
  │
  │   FX CHAIN:   stage = send, then the 8 FX stages in fxOrder
  │   FX RETURN:  fx = (stage − send) × fxHeadroom × returnGain × pan × gate
  │       └─► fxBusBuffer
  │
  │   MASTER:     applyOutputCeiling((dry + fx) × outputBoost)
  │       └─► masterBusBuffer ──► output buffer
  ▼
```

Two facts from the trace shape everything below.

**The four source channels are mono.** `oscillatorBusBuffer` holds four mono
channels; panning into stereo happens during summing. That is exactly a console:
a mono channel strip panned into a stereo bus. The channel stage is therefore
genuinely mono, and it is mono because the architecture already was — not as a
simplification.

**The FX return is a difference.** `fx = (stage − send)`, i.e. the *wet delta*.
Anything inserted into the FX path has to respect that subtraction or it will
leak dry signal into the return.

---

## 2. Insertion points

```
                    ┌──────────────────────────────────┐
 SUB  ─────────────►│ AnalogEngine  CHANNEL  (mono ×4) │
 OSC1 ─────────────►│  forward transfer                │
 OSC2 ─────────────►│  + channel colour                │
 OSC3 ─────────────►└──────────────┬───────────────────┘
                                   │   level · pan · gates
                    ┌──────────────┴───────────────┐
                    │                              │
                    ▼ dry sum                      ▼ send sum
        ┌───────────────────────┐      ┌───────────────────────┐
        │ AnalogEngine DRY_BUS  │      │ AnalogEngine FX_BUS   │
        │  inverse transfer     │      │  inverse transfer     │
        │  + bus colour         │      │  (lighter)            │
        └───────────┬───────────┘      └───────────┬───────────┘
                    │                              │
                    │                          FX CHAIN
                    │                              │
                    │                         FX RETURN (delta)
                    │                              │
                    └──────────────┬───────────────┘
                                   ▼
                    ┌──────────────────────────────────┐
                    │ AnalogEngine  MASTER             │
                    │  output-stage transfer + colour  │
                    └──────────────┬───────────────────┘
                                   ▼
                          applyOutputCeiling  ──►  output
```

### CHANNEL — mono, ×4

Placed **after** phase invert and **before** level/pan, so it sits where a
console strip sits: the fader is downstream of the amplifier stage, and pan is
downstream of both.

Runs the profile's **forward** transfer. On its own this is not a saturator — it
is one half of an invertible pair, and it is undone by the bus.

### DRY_BUS — stereo

Runs the **inverse** transfer on the summed dry signal, plus the bus colour
stages. Placed after the dry channel's gain/pan/gate, immediately before the
write to `dryBusBuffer`.

This is where the character actually appears. `inverse(Σ forward(xᵢ)) ≠ Σ xᵢ` for
more than one channel, and the deviation grows with how many channels are
contributing and how hard.

### FX_BUS — stereo, before the FX chain

Placed on the **send sum**, before the chain — not after.

Reasoning, since the brief asks for it rather than a guess: on a real desk the
aux send is derived from the channel and summed on its own bus, and that send bus
is a physical summing amplifier with the same character as any other. The signal
arriving at an outboard unit has already been through it. Placing it after the
chain would instead model a *return* amplifier, which exists, but then the effects
would be receiving mathematically clean signal — losing exactly the thing being
modelled.

It also has to be before the chain because of the return subtraction: the return
is `stage − send`, so a stage inserted after the chain would be applied to the
wet-plus-dry sum and then have the untouched dry subtracted from it, which is not
a meaningful operation.

The FX bus uses a **lighter** setting than the dry bus. The send is a subset of
the sources at reduced level, so the same drive would push it further up the
curve than the dry bus for the same musical input.

### MASTER — stereo

Runs the profile's **output-stage** behaviour on `dry + fx`, before
`applyOutputCeiling`. Ordering matters: the ceiling is the instrument's
guarantee that nothing can clip, so it must be last.

The master stage is **not** another copy of the bus stage. It has its own
headroom (the most of any stage), its own transfer weighting, and it is the only
stage that does mid/side rather than left/right — a master bus amplifier sums,
and stereo behaviour at that point is a property of the summing, not of two
independent channels.

---

## 3. Profile × context

A profile is a set of constants. A context selects which of them apply and in
which direction.

|  | CHANNEL | DRY_BUS | FX_BUS | MASTER |
|---|---|---|---|---|
| transfer direction | forward | inverse | inverse | forward, light |
| channels | mono ×4 | stereo | stereo | stereo (M/S) |
| drive | `channelDrive` | `busDrive` | `busDrive × fxBusTrim` | `masterDrive` |
| slew | yes (enhance) | yes (cut back) | no | yes (cut back) |
| even-harmonic bias | yes | no | no | yes, half |
| LF behaviour | HP + level trim | level trim | no | HP only |
| HF behaviour | level-dependent rolloff | fixed rolloff | fixed rolloff | level-dependent |
| DC block | yes | yes | yes | yes |

The contexts are not gain scalings of one another. `CHANNEL` and `DRY_BUS` run
mathematically opposite transfers; `FX_BUS` omits slew entirely; `MASTER` is the
only one operating on mid/side.

---

## 4. Aliasing strategy

Decided by measurement, not by preference. The measurement lives in the
`analog` test suite and compares 1×, 2× and 4× on the same material.

The prior expectation, stated before measuring so it can be wrong: the transfer
pair is smooth and bounded, and the instrument's sources are already trimmed to
−4 dB of headroom, so the curve is not being driven hard. Aliasing should be
modest, and the cost of oversampling — new plugin latency where there currently
is none — is real.

Antiderivative antialiasing was considered as the cheaper alternative and is
noted in the research document. It is not used in this implementation: the first
antiderivative of the chosen pair is closed-form, but ADAA introduces its own
half-sample delay and degenerates near stationary input, and the measured 1×
aliasing did not justify the complexity. That decision is recorded with its
numbers in the tuning document.

---

## 5. VibeEngine interaction

Signal order is fixed by the existing architecture and is not a choice:

```
per voice:   sources → VIBE → filters → amp env
                                          │
                                          ▼  summed into 4 mono source channels
                          AnalogEngine CHANNEL → mixer → buses → MASTER
```

Vibe is upstream and per-voice; AnalogEngine is downstream and per-channel. They
model different objects — Vibe the instrument's own analog imperfection,
AnalogEngine the desk it is plugged into — and the test suite checks that each
contributes something the other does not.

---

## 6. State management

Per the brief, and enforced by test:

| Thing | Serialised? |
|---|---|
| `analogProfile` (user-facing archetype choice) | **yes**, a normal parameter |
| `analogEnabled` (user-facing) | **yes**, a normal parameter |
| every tuning constant | **no** |

Tuning constants are compiled-in defaults, reachable at runtime only through the
debug console, and reset to their compiled values on every construction. They are
never written to preset files, never written to DAW state, and never appear in
`UIConfig.json`.

A future `AnalogEngineConfig.json` is described in the tuning document. It is
deliberately not implemented.

---

## 7. Attribution

The architectural idea — that a console is an invertible transfer pair split
across channel and bus, so that character emerges from summing rather than from
per-channel distortion — is taken from studying the Airwindows Console family
(MIT licensed, <https://github.com/airwindows/airwindows>). No Airwindows source
is copied. Transfer functions, profiles, filtering, slew, contexts and the tuning
surface are independently written for this project.
