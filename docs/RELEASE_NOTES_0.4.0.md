# PX3 v0.4.0

83 commits since v0.3.0. Three substantial additions — bus inserts, a wavetable
oscillator, and breakpoint envelopes — plus a GPU renderer for the wavetable
display and a long run of UI corrections.

---

## New: bus inserts (EQ and FET compressor)

An EQ and an 1176-style FET compressor, insertable on the dry bus and the FX
bus independently.

- Four-band EQ with a graph you can play directly.
- FET compressor with the ratio, attack and release behaviour of the hardware
  it is modelled on, and an antialiased gain stage.
- Both are bypassable per bus, and an insert engages its processor on the
  first press rather than on every strip-button click.
- A VU meter with physically derived ballistics: an averaging detector and a
  vector-drawn needle, not a bitmap swept by a gain value.

## New: wavetable oscillator

A fourth oscillator mode, band-limited and importable.

- Eight factory tables. Each is mipmapped by harmonic count, giving 68–98 dB of
  alias rejection where a naive implementation measured 45–55 dB.
- Import from audio files or images; a user library alongside the factory one.
- Position is a modulation destination, so an LFO or envelope can sweep it.
- Tables are handed to the audio thread by RCU — a raw pointer and an epoch,
  retired on the message thread — so loading one never blocks or allocates in
  `processBlock`.

## New: breakpoint envelopes

All four envelopes (AMP and ENV 1–3) are now editable breakpoint curves rather
than fixed ADSR. Each has four separate controls — ATTACK, DECAY, SUSTAIN,
RELEASE — where sustain has a handle of its own on the held stretch, so decay
time and sustain level are never the same control.

- Up to 16 points, each with an adjustable curve to the next.
- Four handles - ATTACK, DECAY, SUSTAIN, RELEASE - each naming what it changes
  on hover. Sustain sits on the held stretch and moves in level only; the
  others move in time only, so no handle changes two things at once.
- A time axis labelled in seconds, to a maximum of 8.
- The curve is a rational (Möbius) function, chosen by measurement: monotone and
  bounded by construction, symmetric to 1.8e-15, and 26x cheaper than a
  constrained cubic Bezier, which needs a cubic inverted per sample per voice.
- ADSR is a special case of the model, not a separate path, so existing presets
  load and sound exactly as they did.
- All four graphs fill in the area under the part of the envelope the sounding
  note has already been through, following the shape exactly, bends included.
  Each reads its own envelope's position from the playing voice rather than a
  UI clock counting alongside it, so it cannot drift; the fill stops at the
  sustain for as long as the note is held, and the release runs on from there.

## New: GPU wavetable renderer

The wavetable display is drawn by OpenGL rather than by JUCE primitives.

- The stack is real perspective geometry, not a fixed shear — a shear does not
  converge, so it reads as stripes rather than as depth.
- Every curve is a triangle-strip ribbon rather than a line primitive, because
  `glLineWidth > 1` is unsupported in a macOS core profile, and because a ribbon
  gives constant pixel width with depth and analytical antialiasing.
- A wireframe floor box, lit by the selected waveform as it scans.
- A procedural environment: ambient lift, a light pool behind the subject, a
  vertical gradient, a vignette, a key light and depth haze.
- The camera orbits, zooms and re-frames itself for whichever table is loaded
  and whichever orientation it is currently pointing.

## Modulation

- Every modulation source now scales to the headroom its destination's base
  value leaves, instead of being summed and clamped. Clamping turned a sine into
  a square with rounded shoulders: at base 0.5 and full amount the value sat
  pinned at an end for 65.6% of every cycle, in stalls of 661 ms.
- Every knob with a modulated parameter draws its modulation as an arc, so an
  LFO on the filter cutoff is visible on the control it is moving.

## Fixed

- **A click on every note.** `SynthVoice::startNote` rebuilt the amp envelope
  from the four ADSR parameters even when the envelope had been drawn past what
  they can describe. Those parameters stop being written back at that point, so
  every note began on a stale envelope and was corrected one block later.
  Captured from a running standalone: the voice held a 12 ms attack while the
  drawn envelope had a 4 second one, so each note opened at 77% level and then
  collapsed to where the real attack had reached. It scaled with the buffer
  size - 492 samples of 512, ~960 of 1024 - because it was always exactly one
  block.
- **Allocation on the audio thread at note-on.** `startNote` prepared every
  filter on every note, and `CombResonator::prepare` sizes its delay line with
  `vector::assign` - a 3856 byte `malloc` inside the audio callback. Filters are
  prepared in `prepareToPlay` now. The real-time safety check had never caught
  it because it measured only blocks with an empty MIDI buffer.
- **A lock on the audio thread.** The on-screen keyboard handed its notes over
  through a `MidiBuffer` behind a `CriticalSection` that `processBlock` locked
  every block. Replaced with a lock-free single-producer ring.
- **A retriggered note dived to silence** before its attack began - 0.4934 to
  0.0052 in 5 ms under a long attack. The attack now starts from the level the
  envelope had reached, mirroring what the design document already specified for
  release.

- **Wavetable stacking order.** The frames were drawn nearest-first with the
  depth test off and straight alpha, so all 47 other translucent ribbons were
  composited on top of the curve you were looking at. The depth fade and haze
  ran the same way backwards — the near end of the stack measured at exactly the
  background level, invisible.
- **EQ analyser low end**, flattened by the resampler feeding it.
- **Card corner arcs**, every one drawn a quarter-turn out.
- **VU needle calibration** at any pivot offset; and its background, which cost
  27.6 ms a frame against the needle's 0.67.
- **The mode selector moving when it was used** — choosing WAVETABLE jumped it
  from a centred cell to the full row width.
- **The wavetable panel's square corners.** An attached GL context fills its own
  rectangle, corners included, and that native layer hides anything painted
  underneath, so the rounded panel was square exactly where the rounding showed.
- **The view not re-fitting on a table change** unless the camera happened to be
  at its default orientation.
- Particles left on screen; sparks clipped at the keyboard edge; the modal
  backdrop; preset subtitle alignment.

## Tooling

- Release signing and notarization are now tested end to end — app, installer
  and uninstaller — by `scripts/test-signing.sh`.
- The uninstaller's icon is generated from the app's.
- The debug console scrolls, and the preset dump moved up the section list.
- New GPU diagnostics: `glcheck` (does it draw), `envcheck` (the environment,
  measured off against on), `sharpcheck` (line sharpness, by edge profile).

## Known limitations

- **Apple Silicon only.** arm64; no Intel or universal build.
- **No CI.** Everything is measured on one machine.
- **Oscillator aliasing** in the non-wavetable modes is unchanged; only the
  wavetable oscillator is band-limited.
- **Worst-case CPU at extreme polyphony.** Sustained load is comfortable, but
  isolated blocks at 40+ voices with every effect enabled can exceed the
  real-time budget. See the audit below.
- **EQ spectrum visualiser** items from its design brief remain open: FFT size
  as a user setting, decoupled hop, screen-space curve interpolation.
