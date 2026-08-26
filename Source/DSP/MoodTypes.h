#pragma once

struct MoodSettings
{
    bool enabled { true };
    bool trueBypass { false };
    bool freeze { false };

    float mix { 0.35f };
    float clock { 1.0f };
    float routing { 0.0f };

    int wetModeIndex { 0 };   // 0=REVERB, 1=DELAY, 2=SLIP
    float wetTime { 0.40f };
    float wetModify { 0.45f };

    int loopModeIndex { 0 };  // 0=ENV, 1=TAPE, 2=STRETCH
    float loopLength { 0.28f };
    float loopModify { 0.50f };

    float feedback { 0.35f };
    float spread { 0.50f };
    float degrade { 0.20f };

    double bpm { 120.0 };
};
