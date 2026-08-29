#pragma once

#include <JuceHeader.h>

#include <array>
#include <cstdint>

namespace px3
{

// A distributed analog-console character engine.
//
// The idea, taken from studying the Airwindows Console family: a console is an
// INVERTIBLE transfer pair split across the channel and the bus. The channel
// runs a forward function, the bus runs its exact inverse, and for one channel
// the pair is mathematically transparent:
//
//     inverse(forward(x)) == x
//
// but for several channels summed together it is not:
//
//     inverse(forward(a) + forward(b)) != a + b
//
// So the nonlinearity lives in the SUMMING rather than on any channel, and it
// grows with how much is playing. That is what the summing literature actually
// describes - "nonlinear inter-channel crosstalk" that glues a mix - and it is
// why this is not a saturator on every stage. A saturator per stage distorts
// each channel identically and independently; nothing about that glues.
//
// Around that invertible core sit the stages that carry a profile's COLOUR -
// bandwidth, coupling, slew, a small even-harmonic bias. Those are deliberately
// not inverted: they are the desk's tone, not its summing behaviour.
//
// See docs/ANALOG_ENGINE_RESEARCH.md for the debate and the rejected
// alternatives, and docs/ANALOG_ENGINE_ARCHITECTURE.md for where each stage sits
// in the signal path.
class AnalogEngine
{
public:
    // Generic archetypes informed by research into broad console design
    // families. These are NOT emulations of specific hardware and make no such
    // claim.
    enum class Profile
    {
        clean = 0,     // modern large-format VCA: nearly transparent
        british,       // discrete transformer-coupled: 2nd harmonic, LF weight
        american,      // discrete op-amp: harder knee, odd harmonics, fast slew
        transformer,   // transformer-heavy: strongest even content, restricted band
        modern         // later clean VCA: tight, fast, most headroom
    };

    static constexpr int kProfileCount = 5;
    static juce::StringArray profileNames();

    // Where in the signal path this call sits. Not a gain scaling - the context
    // selects the transfer DIRECTION and which colour stages run at all.
    enum class Context
    {
        channel = 0,   // mono, forward transfer, pre-fader
        dryBus,        // stereo, inverse transfer, after the dry sum
        fxBus,         // stereo, inverse transfer, lighter, before the FX chain
        master         // stereo mid/side, output-stage transfer
    };

    // Every tuning constant. Compiled defaults, reachable at runtime ONLY
    // through the debug console, never serialised into presets or DAW state.
    struct Tuning
    {
        // ---- the invertible core -------------------------------------------
        // ONE drive for the channel and the buses that invert it.
        //
        // They have to be equal or the pair does not invert: the channel
        // computes g(d*x)/d and the bus computes g-1(b*y)/b, and those only
        // cancel when b == d. Giving them separate constants looked like extra
        // control and was actually a way to silently break the whole premise -
        // measured at 4.3% THD on a single channel where it should be zero.
        float pairDrive { 1.00f };
        float masterDrive { 0.85f };       // the master is forward, so it is free
        // Scales how much of the FX bus stage is mixed in, NOT its drive. The
        // send is a quieter subset of the sources and wants a lighter touch,
        // but trimming its drive would break invertibility on the FX path the
        // same way separate drives broke it on the dry path.
        float fxBusTrim { 0.70f };
        float curveBlend { 0.25f };        // 0 = pure sine pair, 1 = the |x|-weighted pair

        // ---- colour ---------------------------------------------------------
        float evenHarmonic { 0.06f };      // asymmetry -> 2nd harmonic. Small on purpose.
        float slewEnhance { 0.35f };       // arcsine slew on the channel, sine on the bus
        float hfRolloffHz { 19000.0f };    // bandwidth of the stage
        float hfLevelDependence { 0.30f }; // how much louder signal loses top
        float lfCornerHz { 12.0f };        // coupling capacitor
        float lfLevelTrim { 0.20f };       // transformer-style level-dependent LF

        // ---- safety ----------------------------------------------------------
        float dcBlockHz { 5.0f };
        float headroom { 1.00f };          // scales everything's distance from the knee
        // How much of the engine is mixed in. A tuning constant rather than a
        // user parameter: the brief allows only the profile choice to be
        // user-facing at this stage.
        float engineAmount { 1.00f };
        // Per-profile makeup, applied per STAGE. The colour stages lose level -
        // the bandwidth limit most of all - and an A/B is meaningless if one
        // side is quieter. Per stage rather than once at the end, because the
        // number of stages a signal passes through varies with the path.
        float outputTrim { 1.00f };
    };

    AnalogEngine();

    void prepare(double sampleRate, int channelCount);
    void reset();

    void setProfile(Profile profile);
    Profile getProfile() const noexcept { return activeProfile; }

    void setBypass(bool shouldBypass) noexcept { bypass = shouldBypass; }
    bool isBypassed() const noexcept { return bypass; }

    // 0 = no console, 1 = the profile as designed. Interpolates the wet result
    // against the input rather than scaling the drive, so zero is exactly
    // transparent and there is no curve position that only exists mid-sweep.
    void setAmount(float amount) noexcept;
    float getAmount() const noexcept { return amount; }

    // ---- tuning, for the debug console only --------------------------------
    void setTuningValue(const juce::String& key, float value);
    float getTuningValue(const juce::String& key) const;
    static juce::StringArray tuningKeys();
    // Re-reads engineAmount into the smoother. Called after a tuning edit.
    void refreshAmount() noexcept;
    // The compiled defaults for the active profile - what a reset returns to.
    void resetTuning();
    const Tuning& getTuning() const noexcept { return tuning; }

    // ---- processing ---------------------------------------------------------
    // Mono, one source channel. `slot` selects that channel's own state.
    float processChannelSample(int slot, float input);
    // Stereo, in place.
    void processBusSample(Context context, float& left, float& right);

    // The compiled tuning for a profile, before any debug edits.
    static Tuning defaultTuningFor(Profile profile);

    // The transfer pair, exposed so tests can assert invertibility directly
    // rather than inferring it from rendered audio.
    static float forwardTransfer(float x, float blend);
    static float inverseTransfer(float x, float blend);

private:
    static constexpr int kMaxChannels = 8;
    static constexpr int kMaxBusContexts = 4;

    struct StageState
    {
        // slew / DC servo
        float lastSample { 0.0f };
        float lastShaped { 0.0f };
        float servo { 0.0f };
        // filters
        float hfState { 0.0f };
        float lfState { 0.0f };
        float dcPrev { 0.0f };
        float dcState { 0.0f };
        // envelope, for the level-dependent stages
        float envelope { 0.0f };

        void clear() { *this = {}; }
    };

    static float sanitize(float v);
    static float onePoleCoeff(float hz, float rate);

    // One stage of the model. `forward` picks the transfer direction; the rest
    // of the flags come from the context.
    float processStage(StageState& state,
                       float input,
                       Context context,
                       bool forward);

    Profile activeProfile { Profile::clean };
    Tuning tuning;
    bool bypass { false };
    float amount { 0.0f };
    double sampleRateHz { 44100.0 };

    std::array<StageState, kMaxChannels> channelState {};
    // [context][L, R]
    std::array<std::array<StageState, 2>, kMaxBusContexts> busState {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> amountSmoothed;
};

} // namespace px3
