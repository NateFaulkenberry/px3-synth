#pragma once

// Everything CHORUS needs for one block. See docs/CHORUS_DSP_DESIGN.md.
struct ChorusSettings
{
    bool enabled { true };

    // The macro. Zero by default: adding an effect must not change what
    // existing patches sound like.
    float amount { 0.0f };

    // 0..3 = DIM 1..4, 4..6 = DIM 1+4 / 2+4 / 3+4, 7 = ENSEMBLE, 8 = CE WARM.
    int modeIndex { 1 };

    float rate { 0.35f };
    float depth { 0.5f };
    float width { 0.75f };
    float spread { 0.5f };
    float tone { 0.0f };        // -1 warm .. +1 clear
    float lowCut { 0.3f };      // wet-path high-pass: the bass anchor
    float feedback { 0.0f };
    float character { 0.5f };   // BBD group depth
    float mix { 1.0f };
};
