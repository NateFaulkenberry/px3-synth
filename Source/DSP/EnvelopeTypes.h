#pragma once

// Generic ADSR envelope parameter lane shared between processor state and
// runtime DSP components.
struct EnvelopeSettings
{
    float attackSeconds { 0.005f };

    // Held at full level between the attack and the decay. Zero by default, so
    // every envelope that predates it has exactly the shape it always had - the
    // hold point sits on top of the peak and the segment between them has no
    // length.
    float holdSeconds { 0.0f };
    float decaySeconds { 0.050f };
    float sustainLevel { 0.8f };
    float releaseSeconds { 0.100f };
};
