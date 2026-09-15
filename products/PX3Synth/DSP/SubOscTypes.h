#pragma once

struct SubOscSettings
{
    bool enabled { false };
    float level { 0.0f };
    float pitchSemitones { 0.0f };
    // Separate from the fine tune, which setSettings clamps to +-0.24 st.
    float pitchModSemitones { 0.0f };
    int octaveIndex { 1 };
    int waveformIndex { 1 };
};
