#pragma once

#include <JuceHeader.h>

#include "SmoothedGain.h"

#include <array>
#include <complex>

namespace px3
{
struct UniVibeSettings
{
    bool enabled { true };
    float speed { 0.4f };       // 0..1 -> 0.5 to 8 Hz, exponential (0.4 = 1.5 Hz)
    float intensity { 0.6f };   // 0..1 lamp drive. 0 = the lamp idles (static colour), not a bypass
    int mode { 0 };             // 0 CHORUS (dry + phased, equal), 1 VIBRATO (phased only)
    float levelDb { 0.0f };     // output trim, -12..+12 dB
    int stereo { 0 };           // 0 LINKED, 1 INVERTED (right takes the last stage's opposite phase)
};

// VIBE: a model of the Shin-ei / Univox Uni-Vibe.
//
// Not a phaser with a Uni-Vibe label. What makes the pedal sound the way it
// does is modelled from the circuit, and each part is pinned by a test that a
// generic phaser fails (docs/VIBE_DSP_DESIGN.md has the research and every
// constant's source):
//
//  * FOUR UNMATCHED STAGES. The phase capacitors are 15 nF, 220 nF, 470 pF and
//    4.7 nF - no progression at all - against one shared lamp, so at every
//    instant the stage frequencies keep fixed ratios set by those capacitors
//    (f3 ~ 10 f4 ~ 32 f1 ~ 470 f2). Only two stages move a notch through the
//    mid band at a time.
//  * STAGES THAT ARE NOT ALL-PASSES. Each is a transistor phase splitter whose
//    inverting and non-inverting gains differ (alpha, beta) feeding the LDR
//    through a 1 uF coupling capacitor; the result is a moving shelf rather
//    than a flat all-pass (Darabundit, Wedelich & Bischoff, DAFx-19, eq. 9).
//    The 220 nF stage's large kappa gives a sweeping bass shelf: the throb.
//  * LAMP AND PHOTOCELLS, NOT AN LFO. A diode-limited sine drives an
//    incandescent filament (heats fast, cools slower), whose light reaches the
//    four LDRs through a power law; the cells brighten fast and darken slowly.
//    The resistance sweep is therefore lopsided - it snaps bright and drifts
//    dark - and it gets more lopsided the harder the lamp is driven.
//  * TRANSISTOR CHARACTER. The preamp and the three Darlington splitters clip
//    softly and asymmetrically (a biased tanh-shaped curve per stage, as in
//    the DAFx model).
//  * THE OUTPUT SWITCH IS THE PEDAL'S. CHORUS sums the (preamp-coloured) dry
//    signal and the phase-shifted one equally; VIBRATO passes the phase-shifted
//    signal alone, heard as pitch wobble. No mix knob - the hardware has none.
//
// One lamp, so one modulation state shared by both channels. Real time safe:
// no allocation, the lamp/LDR model and the stage coefficients run every
// kControlInterval samples and the coefficients are interpolated per sample.
class UniVibe
{
public:
    static juce::StringArray modeNames() { return { "CHORUS", "VIBRATO" }; }
    static juce::StringArray stereoNames() { return { "LINKED", "INVERTED" }; }

    static constexpr int kStageCount = 4;
    static constexpr int kControlInterval = 16;

    // Call updateForBlock with the initial settings BEFORE prepare: prepare
    // starts the enable, mode and stereo fades already at those settings, so
    // an instance created switched off is bit-transparent from its first sample.
    void prepare(double sampleRate);
    void reset();
    void updateForBlock(const UniVibeSettings& settings);
    void processSampleFrame(float inL, float inR, float& outL, float& outR) noexcept;

    // ---- the model, exposed for tests ----------------------------------------
    // Rate of the lamp sweep for a SPEED value, in Hz.
    static float speedToHz(float speed) noexcept;
    // Stage centre frequency (Hz) for a stage and an LDR resistance.
    static double stageCentreHz(int stage, double ldrOhms) noexcept;
    // The LDR resistance of a cell at a normalised conductance (0 dark, 1 bright).
    static double cellResistance(int stage, double conductance) noexcept;
    // Complex response of one stage at a frequency, for a given LDR resistance.
    static std::complex<double> stageResponse(int stage, double ldrOhms, double hz, double sampleRate) noexcept;
    // The four cells' current resistances and the shared normalised conductance.
    std::array<double, kStageCount> debugCellResistances() const noexcept;
    float debugConductance() const noexcept { return ldrLogConductance > -30.0f ? std::exp(ldrLogConductance) : 0.0f; }
    float debugLampTemperature() const noexcept { return filament; }
    bool debugIsIdle() const noexcept { return idle; }

private:
    struct Channel
    {
        std::array<float, kStageCount> x1 {};
        std::array<float, kStageCount> y1 {};
        float wetDcX1 { 0.0f }, wetDcY1 { 0.0f };
        float dryDcX1 { 0.0f }, dryDcY1 { 0.0f };
    };

    struct Coefficients
    {
        float b0 { 0.0f }, b1 { 0.0f }, a1 { 0.0f };
    };

    void controlTick() noexcept;
    void computeStageCoefficients(std::array<Coefficients, kStageCount>& out) const noexcept;
    void settleLampAtIdle() noexcept;
    float processChannel(Channel& c, float input, float& dryOut) noexcept;

    double sampleRateHz { 48000.0 };
    UniVibeSettings current;

    // Modulation (shared by both channels: there is one lamp).
    float lfoPhase { 0.0f };
    float lfoHz { 1.5f };
    float driveAmplitude { 0.0f };       // smoothed at control rate
    float driveAmplitudeTarget { 0.0f };
    float filament { 0.0f };             // normalised filament temperature
    float ldrLogConductance { 0.0f };    // ln of the shared normalised LDR conductance
    float heatCoeff { 0.0f }, coolCoeff { 0.0f }, ampCoeff { 0.0f };
    float controlDt { 0.0f };
    float levelTarget { 1.0f };
    int controlCountdown { 0 };

    std::array<Coefficients, kStageCount> coeff {};
    std::array<Coefficients, kStageCount> coeffStep {};

    std::array<Channel, 2> channels {};
    float dcCoeff { 0.999f };

    SmoothedGate enabledGate;
    SmoothedGate vibratoGate;
    SmoothedGate invertGate;
    SmoothedGain levelGain;
    bool idle { true };
};
} // namespace px3
