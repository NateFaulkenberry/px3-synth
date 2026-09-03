#pragma once

// Everything DOOM needs for one block. See docs/DOOM_DSP_DESIGN.md for what
// each control does and why.
struct DoomSettings
{
    bool enabled { true };

    float mix { 0.35f };

    // CLOCK sets the engine's sample rate, which is what ties the two channels
    // together: it is the loop's length and pitch and the wet channel's time
    // and bandwidth, all at once.
    float clock { 1.0f };
    bool clockSmooth { false };   // bypass the harmonised quantiser

    int routingIndex { 0 };       // 0=INPUT, 1=INPUT+LOOP, 2=LOOP

    // ---- micro-looper channel -------------------------------------------
    bool loopActive { false };    // false = always-listening (recording)
    int loopModeIndex { 1 };      // 0=BURST, 1=RADIO, 2=MASK
    float loopLength { 0.45f };
    float loopModify { 0.50f };
    bool loopHalf { false };
    float overdub { 0.0f };
    float fade { 1.0f };          // loop retention per lap while overdubbing

    // ---- wet channel -----------------------------------------------------
    bool wetActive { true };
    int wetModeIndex { 0 };       // 0=SOUP, 1=RELAY, 2=FLIP
    float wetTime { 0.45f };
    float wetModify { 0.40f };
    bool freeze { false };

    // ---- global ----------------------------------------------------------
    float cross { 0.0f };
    int crossSourceIndex { 0 };   // 0=INPUT, 1=CHANNEL (each modulates the other)
    float glue { 0.15f };
    float eq { 0.0f };            // tilt, -1 darker .. +1 brighter
    float balance { 0.5f };       // micro-looper <-> wet channel
    float blend { 0.0f };         // clean micro-loop past the wet channel
    float spread { 0.5f };
};
