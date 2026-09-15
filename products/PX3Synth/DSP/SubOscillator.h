#pragma once

#include "OscillatorDsp.h"
#include "SubOscMode.h"
#include "SubOscTypes.h"

// The sub oscillator: a sine or a band-limited square, tuned like the main
// oscillators and on the same conventions (docs/OSCILLATOR_DSP_DESIGN.md) - a
// double phase in cycles, the common output latency, the same PolyBLEP line.
//
// It returns its signal BEFORE the voice's soft clip and without its level.
// The level used to be applied here and again by the voice after the clip,
// which put the sub 4 dB further down than every other source.
class SubOscillator
{
public:
    // The sub's level into the voice's soft clip: where it sat before the
    // double trim was removed, now applied once, like an oscillator's mode trim.
    static constexpr double kSourceTrim = 0.75;
    static constexpr double kWaveformCrossfadeSeconds = 0.005;

    void prepare(double newSampleRateHz);
    // Tuning ramps to its new ratio across `rampSamples`; the waveform crossfades.
    void setSettings(const SubOscSettings& newSettings, int rampSamples = 0);
    // `startPhase` in cycles.
    void resetForNote(double startPhase = 0.0);

    double renderSample(double baseFrequencyHz);

    double currentPhase() const noexcept { return phase; }

private:
    double renderWaveform(int waveform, double increment, bool wrapped, double tau);

    double sampleRateHz { 48000.0 };
    SubOscSettings settings;
    double phase { 0.0 };

    double ratioStart { 0.5 };
    double ratioTarget { 0.5 };
    double ratioCurrent { 0.5 };
    int rampPosition { 1 };
    int rampLength { 1 };
    bool configured { false };

    int activeWaveform { 1 };
    int fadingWaveform { -1 };
    int fadeRemaining { 0 };
    int fadeLength { 240 };

    px3::dsp::BlepLine squareLine;
    px3::dsp::LatencyDelay sineDelay;
};
