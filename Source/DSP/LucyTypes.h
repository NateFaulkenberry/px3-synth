#pragma once

// Everything LUCY needs for one block. See docs/LUCY_DSP_DESIGN.md for what
// each control does and which documented Lossy behaviour it reproduces.
struct LucySettings
{
    bool enabled { true };

    // GLOBAL is a macro over the whole effect rather than a wet/dry, which is
    // how the source describes it. Zero by default: adding an effect must not
    // change what existing patches sound like.
    float global { 0.0f };

    // LOSS sets both the strength of the degradation AND which frequencies it
    // reaches; SPEED sets how often the engine makes a new decision, for Loss,
    // Packets and Freeze alike.
    float loss { 0.55f };
    float speed { 0.5f };

    int modeIndex { 0 };        // 0=STANDARD, 1=INVERSE, 2=JITTER
    int packetIndex { 0 };      // 0=CLEAN, 1=PACKET LOSS, 2=PACKET REPEAT

    // A band-pass whose WIDTH is the primary control: at zero there is no
    // filtering at all.
    float filterWidth { 0.0f };
    float filterFreq { 0.5f };
    int slopeIndex { 1 };       // 0=6dB, 1=24dB, 2=96dB
    bool filterInvert { false };

    float verb { 0.0f };
    float verbDecay { 0.45f };
    bool verbPost { false };    // false = PRE, which feeds the loss

    bool freeze { false };
    bool freezeSlushy { false };
    float freezer { 1.0f };     // live <-> frozen balance

    bool gate { false };
    float gateCutoff { 0.25f };

    float threshold { 0.8f };   // limiter; lower means more limiting
    float autoGain { 0.75f };
    float weighting { 0.0f };   // -1 dark .. 0 psychoacoustic .. +1 bright
    float gainDb { 0.0f };      // -36 .. +36
    float spread { 0.5f };

    bool slow { false };        // bigger, darker, slower, more latency
};
