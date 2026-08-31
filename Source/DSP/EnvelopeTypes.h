#pragma once

// Generic ADSR envelope parameter lane shared between processor state and
// runtime DSP components.
struct EnvelopeSettings
{
    float attackSeconds { 0.005f };

    float decaySeconds { 0.050f };
    float sustainLevel { 0.8f };
    float releaseSeconds { 0.100f };
};
