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

// Where an envelope currently is, for drawing it. Read-only: the visualisation
// consumes this and never writes anything back, so there is one envelope and
// one timeline rather than a DSP one and a UI copy that drift apart. Both
// envelope classes report the same shape of answer, so the graph that draws
// AMP ENV and the one that draws ENV 1-3 are the same graph.
struct EnvelopePosition
{
    bool active { false };
    bool inRelease { false };
    double heldSeconds { 0.0 };       // clamped at the sustain point
    double releasedSeconds { 0.0 };
    double sustainSeconds { 0.0 };
};
