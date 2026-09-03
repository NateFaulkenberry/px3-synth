#pragma once

#include <JuceHeader.h>

#include <vector>

namespace px3
{

// Settings for one comb resonator, in musical terms rather than DSP terms.
//
// The user sets a pitch and a decay time; the resonator works out the delay
// length and the loop gain that produce them. That mapping is the point of the
// class - a raw "feedback = 0.873" control behaves completely differently at
// 80 Hz than at 4 kHz, because the loop runs 50x more often per second.
struct CombSettings
{
    // Resonant frequency. The delay line is fs / tuneHz samples long, so this
    // is the fundamental of the resonance and the spacing of its harmonics.
    float tuneHz { 220.0f };
    // Approximate time for the resonance to fall 60 dB, in seconds.
    float decaySeconds { 0.6f };
    // 0 = bright, the loop passes everything. 1 = strongly damped, high
    // frequencies die well before the fundamental does.
    float damping { 0.25f };
    // Departure from a harmonic series, via allpass phase delay in the loop.
    // 0 = harmonic. 1 = clangorous/bell-like.
    float dispersion { 0.0f };
    // Drive into the loop's saturator. Also what makes self-oscillation settle
    // at a bounded level instead of running away.
    float drive { 0.0f };
    // Wet/dry. 0 = dry only, 1 = resonator only.
    float mix { 1.0f };
    // Negative feedback moves the resonance to odd harmonics of half the tune
    // frequency, which is a genuinely different timbre rather than a polarity
    // detail.
    bool invertPolarity { false };
};

// A tuned feedback comb resonator.
//
//        in ->(+)-> [ delay D ] -+-> out
//              ^                 |
//              |                 v
//              +-- [ x g ] <- [ saturate ] <- [ dispersion allpass ] <- [ damping LP ]
//
// The loop order is deliberate. Damping and dispersion shape what is fed back,
// so they change how the resonance decays rather than merely equalising the
// output - a damping control placed after the delay's output tap would just be
// a tone knob. Saturation sits last before the gain so that everything the loop
// gain multiplies has already been bounded.
class CombResonator
{
public:
    // The lowest note the resonator can be tuned to. This sets the delay
    // line's length, and therefore its memory: the synth runs one resonator
    // per source per filter slot per voice, so a lower limit here is paid 512
    // times over. 50 Hz reaches below the bass staff while keeping each line
    // to a kilobyte or two.
    static constexpr float kMinTuneHz = 50.0f;
    static constexpr float kMaxTuneHz = 8000.0f;
    static constexpr float kMinDecaySeconds = 0.02f;
    static constexpr float kMaxDecaySeconds = 12.0f;

    void prepare(double sampleRate);
    void reset();

    void setTargetSettings(const CombSettings& settings);
    // Used when a voice starts: the resonator should begin at the requested
    // settings rather than gliding to them from whatever the last note left.
    void setCurrentSettingsImmediate(const CombSettings& settings);

    float processSample(float inputSample);

private:
    float readDelay(float delaySamples) const;
    void updateLoopCoefficients();

    std::vector<float> line;
    int lineSize { 0 };
    int writePos { 0 };

    double currentSampleRate { 44100.0 };

    CombSettings target;
    // Everything that can move is smoothed per sample. Delay length especially:
    // stepping it would jump the read pointer to an unrelated part of the line,
    // which is a click, and modulating Tune is an explicitly supported use.
    float delaySamples { 200.0f };
    float targetDelaySamples { 200.0f };
    float feedbackGain { 0.0f };
    float targetFeedbackGain { 0.0f };
    float dampingCoeff { 0.0f };
    float targetDampingCoeff { 0.0f };
    float dispersionCoeff { 0.0f };
    float targetDispersionCoeff { 0.0f };
    float driveAmount { 0.0f };
    float targetDriveAmount { 0.0f };
    float mixAmount { 1.0f };
    float targetMixAmount { 1.0f };
    float polaritySign { 1.0f };
    float targetPolaritySign { 1.0f };

    float smoothingCoeff { 1.0f };

    // Loop state.
    float dampingState { 0.0f };
    std::array<float, 2> dispersionStateX { { 0.0f, 0.0f } };
    std::array<float, 2> dispersionStateY { { 0.0f, 0.0f } };
};

} // namespace px3
