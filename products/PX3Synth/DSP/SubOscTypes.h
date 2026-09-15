#pragma once

struct SubOscSettings
{
    bool enabled { false };
    float level { 0.0f };
    // The same tuning model as the main oscillators - see OscillatorTuning.h.
    float coarseOctaves { -1.0f };
    float fineCents { 0.0f };
    float pitchModSemitones { 0.0f };
    int waveformIndex { 1 };
};
