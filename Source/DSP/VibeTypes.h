#pragma once

struct VibeSharedState
{
    float oscillatorDrift { 0.0f };
    float psu { 0.0f };
    float temperature { 0.0f };
    float chaos { 0.0f };
};

struct VibeVoiceVariation
{
    float pitchCents { 0.0f };
    float cutoffOffset { 0.0f };
    float resonanceOffset { 0.0f };
    float gainOffset { 0.0f };
    float asymmetryBias { 0.0f };
    float saturationBias { 0.0f };
};

struct VibeTuning
{
    float oscillatorDrift { 0.55f };
    float voiceVariation { 0.55f };
    float filterVariation { 0.45f };
    float saturation { 0.40f };
    float noise { 0.25f };
    float psuMovement { 0.38f };
    float vcaNonlinearity { 0.42f };
    float waveformAsymmetry { 0.32f };
    float temperatureDrift { 0.40f };
    float correlatedChaos { 0.50f };
};

struct VibeSettings
{
    float globalAmount { 0.0f };
    bool enabled { true };
    int typeIndex { 0 };
};
