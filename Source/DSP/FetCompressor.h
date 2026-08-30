#pragma once

#include "BusInsertTypes.h"

#include <JuceHeader.h>

#include <array>
#include <atomic>

namespace px3
{

// An 1176-inspired FET compressor for bus duty.
//
// The three things that make the original recognisable, and which a generic
// "envelope + gain computer + ratio" cannot produce:
//
// 1. It is a FEEDBACK compressor. The detector senses the signal AFTER the gain
//    element, not before. The loop is self-limiting, so the effective ratio is
//    softer than the nominal one at light reduction and approaches it as
//    reduction deepens - and the threshold becomes program dependent rather
//    than a number. That is why the hardware has no threshold control at all:
//    INPUT drives the signal into a fixed threshold, which is why "turn input
//    up until it sounds right" is the actual workflow.
//
// 2. The ratio buttons are not switch positions. Each applies a bias level to
//    the detector diodes, so ratio and threshold are the same control. All four
//    engaged puts four bias networks in parallel - a state no single button
//    produces, with a threshold none of them has.
//
// 3. ALL BUTTONS IN is therefore a different circuit, not ratio = 20. The
//    documented behaviour is that the unit compresses at the selected ratio ON
//    the transient and the ratio then RISES afterwards, the knee hardens, and
//    distortion increases. The rising ratio has its own time constant, and that
//    lag is the sound.
//
// See docs/V3_1_EQ_COMP_RESEARCH.md for sources.
class FetCompressor
{
public:
    void prepare(double sampleRate);
    void reset();

    void setSettings(const CompressorSettings& settings);
    void processSample(float& left, float& right);

    // Gain reduction in decibels, positive, with VU-like ballistics. Published
    // through an atomic and written once per sample - the UI reads it whenever
    // it likes. Never a lock and never a queue on the audio thread.
    float gainReductionDb() const { return meterDb.load(std::memory_order_relaxed); }

    // Level either side of the unit, in dBFS, with the same ballistics as the
    // gain-reduction readout so the needle behaves the same whichever source
    // the meter is switched to. Input is measured AFTER the input control,
    // because that is the signal the unit is actually working on.
    float inputLevelDb() const { return inputMeterDb.load(std::memory_order_relaxed); }
    float outputLevelDb() const { return outputMeterDb.load(std::memory_order_relaxed); }

    // Attack 20 us to 800 us and release 50 ms to 1.1 s, the hardware ranges.
    static constexpr float kAttackFastUs = 20.0f;
    static constexpr float kAttackSlowUs = 800.0f;
    static constexpr float kReleaseFastMs = 50.0f;
    static constexpr float kReleaseSlowMs = 1100.0f;

    struct RatioPoint
    {
        float slope;        // the compression slope
        float thresholdDb;  // the bias the ratio buttons apply to the detector
        float knee;         // knee width in dB - harder at higher ratios
    };

    static RatioPoint ratioPointFor(CompRatio ratio);

private:
    float detectorKnee(float overDb, float knee) const;
    // The FET curve and its antiderivative. The antiderivative is what makes
    // antialiasing possible without oversampling - see fetGainElement.
    static float fetCurve(float x, float drive, float bias);
    static float fetIntegral(float x, float drive, float bias);
    float fetGainElement(int channel, float x, float reductionDb);
    static float outputTransformer(float x, float& state, float coeff,
                                   float& dcX1, float& dcY1, float dcCoeff);

    double sampleRateHz { 48000.0 };
    CompressorSettings settings;

    // Envelope state. Two release constants: the hardware's release is program
    // dependent, fast for short excursions and slower for sustained ones, which
    // is what stops it pumping on dense material.
    std::array<float, 2> envelopeDb { { 0.0f, 0.0f } };
    std::array<float, 2> slowEnvelopeDb { { 0.0f, 0.0f } };
    // The feedback path: last block's gain reduction, which is what the
    // detector actually sees.
    std::array<float, 2> feedbackGain { { 1.0f, 1.0f } };
    // All-buttons: the ratio that rises after the transient.
    float ratioCreep { 0.0f };

    std::array<float, 2> transformerState { { 0.0f, 0.0f } };
    std::array<float, 2> transformerDcX1 { { 0.0f, 0.0f } };
    std::array<float, 2> transformerDcY1 { { 0.0f, 0.0f } };
    std::array<float, 2> detectorHpState { { 0.0f, 0.0f } };
    // Antiderivative antialiasing needs one sample of history per channel, and
    // the integral evaluated at it. Storing the integral as well as the input
    // avoids recomputing a log every sample.
    std::array<float, 2> fetPrevInput { { 0.0f, 0.0f } };
    std::array<float, 2> fetPrevIntegral { { 0.0f, 0.0f } };
    std::array<float, 2> fetPrevDrive { { 1.0f, 1.0f } };
    std::array<float, 2> fetPrevBias { { 0.0f, 0.0f } };

    float attackCoeff { 0.5f };
    float releaseCoeff { 0.01f };
    float slowReleaseCoeff { 0.002f };
    float meterCoeff { 0.01f };
    float transformerCoeff { 0.01f };
    float transformerDcCoeff { 0.999f };
    float detectorHpCoeff { 0.01f };
    float creepCoeff { 0.001f };

    std::atomic<float> meterDb { 0.0f };
    std::atomic<float> inputMeterDb { -60.0f };
    std::atomic<float> outputMeterDb { -60.0f };
    float meterSmoothed { 0.0f };
    float inputMeterSmoothed { -60.0f };
    float outputMeterSmoothed { -60.0f };
};

} // namespace px3
