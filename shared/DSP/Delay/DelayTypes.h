#pragma once

struct DelaySettings
{
    float amount { 0.0f };
    float timeControl { 0.35f };
    float feedbackControl { 0.38f };
    int syncDivisionIndex { 0 };
    int algorithmIndex { 0 };
    int granularModeIndex { 0 };
    bool enabled { true };
    double bpm { 120.0 };
    // TAPE: how far the speed wanders (wow, flutter, scrape, drift). The
    // default reproduces the depth the tape mode always had.
    float wobble { 0.275f };
    // TAPE: worn (0) to pristine (1) - darker and more saturated as it wears.
    // The default is the head the tape mode always had.
    float tapeQuality { 0.71f };
    // MODULATED: chorus-like depth of the modulated taps. Default 1.8 ms.
    float modDepth { 0.3f };
};
