#pragma once

// Everything CHORUS needs for one block. See docs/CHORUS_DSP_DESIGN.md.
struct ChorusSettings
{
    bool enabled { true };

    // INTENSITY: fades from bypass (0) to the hardware's own wet/dry balance
    // and stereo routing (1). Zero by default: adding an effect must not
    // change what existing patches sound like.
    float amount { 0.0f };

    // 0..3 = DIM 1..4, 4..6 = DIM 1+4 / 2+4 / 3+4, 7 = ENSEMBLE, 8 = CE-1,
    // 9..11 = JUNO-60 I / II / I+II.
    int modeIndex { 1 };

    // At these defaults every mode runs at its hardware's figures: RATE 0.35 is
    // the original rate (+/- octaves around it), DEPTH 0.5 the original swing,
    // WIDTH 0.75 the original stereo routing, SPREAD 0.5 the original LFO
    // phase between the lines, CHARACTER 0.5 the original BBD drive, and the
    // two user extras (LOW CUT, FEEDBACK) off.
    float rate { 0.35f };
    float depth { 0.5f };
    float width { 0.75f };
    float spread { 0.5f };
    float tone { 0.0f };        // -1 warm .. +1 clear, wet only
    float lowCut { 0.0f };      // extra wet-path high-pass, 20 .. 420 Hz
    float feedback { 0.0f };
    float character { 0.5f };   // BBD drive: 0 linear, 0.5 hardware, 1 hot
    float mix { 1.0f };
};
