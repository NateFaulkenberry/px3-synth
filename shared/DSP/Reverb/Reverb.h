#pragma once

#include <JuceHeader.h>

#include "ReverbTypes.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class Reverb
{
public:
    // IR mode. Loading happens off the audio thread: the file is checked here
    // (message thread) and juce::dsp::Convolution reads and swaps it on its own
    // background thread. Returns an empty string, or why the file was refused.
    juce::String loadImpulseResponse(const juce::File& file);
    void clearImpulseResponse();
    bool hasImpulseResponse() const noexcept { return irLoaded.load(); }
    juce::String impulseResponseName() const { return irName; }
    // Whether the IR engine (and its loader thread) has been built yet.
    bool hasConvolutionEngine() const noexcept { return convolution.load() != nullptr; }
    static constexpr int kIrBlock = 256;   // the IR path's block, and its extra latency

    void prepare(double sampleRate);
    void reset();

    void updateForBlock(const ReverbSettings& settings, int numSamples);
    void processSampleFrame(float inL, float inR, float& outL, float& outR);
    // The reverb's level match is applied inside processSampleFrame, to the
    // reverb stage's own output. It used to be a separate call that scaled the
    // whole master buffer - dry and every other effect included - AFTER the
    // output ceiling, in steps of a block: up to +3.2 dB past the ceiling's
    // guarantee, which is what made full patches peak above full scale.

private:
    struct DelayLine
    {
        std::vector<float> buffer;
        int writePos { 0 };
        float lpState { 0.0f };
        float modPhase { 0.0f };
    };

    static float clamp01(float v);
    static float lerp(float a, float b, float t);
    static float smoothstep(float x);
    static float sanitizeAudioSample(float x);

    static void resizeLine(DelayLine& line, int size);
    static void writeLine(DelayLine& line, float sample);
    static float readLine(const DelayLine& line, float delaySamples);
    static float processAllpass(DelayLine& line, float in, float delaySamples, float gain);
    static float processDelay(DelayLine& line, float in, float delaySamples);
    float processInputDiffusion(float in, float amount);

    // One feedback-delay-network implementation, shared by the room, hall and
    // cloud algorithms. They differ only in how long the delays are, how hard
    // the input is diffused and how the decay time is derived - not in
    // topology, so there is a single place where the network can be wrong.
    struct FdnConfig
    {
        float sizeScale { 1.0f };
        float rt60Seconds { 2.0f };
        float dampingCoeff { 0.1f };
        float modHz { 0.3f };
        float modSamples { 2.0f };
        float allpassGain { 0.55f };
        float inputGain { 0.35f };
    };

    void allocateFdn(std::array<DelayLine, 8>& delays,
                     std::array<DelayLine, 8>& allpasses,
                     float maxScale);

    void processFdn8(std::array<DelayLine, 8>& delays,
                     std::array<DelayLine, 8>& allpasses,
                     std::array<float, 8>& readCache,
                     const FdnConfig& config,
                     float input,
                     float& wetL,
                     float& wetR);

    void processCore(float inL, float inR, float amount, int algorithmIndex, float& outL, float& outR);

    std::array<DelayLine, 2> preDelayLines;

    // Room: a tapped early-reflection delay per channel feeding a compact
    // four-line network. A small space is defined by its early pattern far more
    // than by its tail, so the taps are the character here.
    std::array<DelayLine, 2> roomEarlyLines;
    std::array<DelayLine, 8> roomLines;
    std::array<DelayLine, 8> roomAllpassLines;
    // Dattorro plate: 4 input diffusion allpasses + 8 tank elements
    // (2 modulated allpasses, 2 decay-diffusion allpasses, 4 delays).
    std::array<DelayLine, 12> plateLines;
    float plateModPhase { 0.0f };
    // Shared input diffuser. An FDN with no diffusion in front of it answers an
    // impulse with a burst of discrete taps, which is the classic metallic
    // attack; four short allpasses smear that into noise before it enters the
    // network.
    std::array<DelayLine, 4> inputDiffusionLines;

    // Hall / cloud: an FDN delay line plus an allpass inside each loop, which
    // keeps building density on every circulation rather than only at input.
    std::array<DelayLine, 8> hallLines;
    std::array<DelayLine, 8> hallAllpassLines;
    std::array<DelayLine, 8> cloudLines;
    std::array<DelayLine, 8> cloudAllpassLines;

    std::array<float, 2> plateTankState { { 0.0f, 0.0f } };
    std::array<float, 8> roomReadCache { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };
    std::array<float, 8> hallReadCache { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };
    std::array<float, 8> cloudReadCache { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } };

    // SHIMMER: an octave-up delay-line pitch shifter on the CLOUD tail, fed
    // back into the network's input. Two taps half a window apart, each read
    // at twice the write speed and Hann-crossfaded, so their weights sum to 1.
    static constexpr int kShimmerBufferSize = 8192;   // at 48 kHz; scaled with the rate in prepare
    // Shimmer's constants, set per sample rate in prepare: tuned at 48 kHz as
    // per-sample values, they made the shift faster, the loop brighter and its
    // gain higher at 96 kHz.
    int shimmerWindow { 2048 };
    float shimmerLowpassCoeff { 0.35f };
    float shimmerDcPole { 0.995f };
    std::vector<float> shimmerBuffer;
    int shimmerWrite { 0 };
    float shimmerPhase { 0.0f };
    float shimmerReturn { 0.0f };
    float shimmerLowpass { 0.0f };
    float shimmerDcX1 { 0.0f }, shimmerDcY1 { 0.0f };
    float processShimmer(float input) noexcept;

    // The IR engine exists only once an impulse response has been loaded.
    // A juce::dsp::Convolution owns a background loader thread and ~0.4 MB of
    // prepared state, and most instances never use IR mode, so building it
    // up front cost every instance a thread and ~0.8 MB for nothing.
    //
    // Built on the loading thread (never the audio thread), prepared there,
    // then published through the atomic; the audio thread only reads the raw
    // pointer. Once built it lives as long as the Reverb, so the audio thread
    // can never see it freed. The mutex orders creation against prepare(), the
    // only other non-audio-thread user.
    //
    // Why not one ConvolutionMessageQueue shared by every instance instead:
    // JUCE's queue is single-producer (an AbstractFifo), and each Convolution
    // pushes to it from its own audio thread whenever it installs a new IR.
    // Hosts that process tracks in parallel would then push from several audio
    // threads at once - a data race on the queue's storage.
    std::unique_ptr<juce::dsp::Convolution> convolutionOwner;
    std::atomic<juce::dsp::Convolution*> convolution { nullptr };
    std::mutex convolutionLock;
    juce::dsp::Convolution& ensureConvolution();   // loading thread, lock held
    juce::AudioBuffer<float> irBlock;   // kIrBlock frames, allocated in prepare
    int irFill { 0 };
    std::atomic<bool> irLoaded { false };
    // Set by clearImpulseResponse (message thread); the audio thread does the
    // reset. Convolution::reset() is not safe against a concurrent process(),
    // and clearing used to call it directly while the audio thread might be
    // inside one.
    std::atomic<bool> irResetRequested { false };
    juce::String irName;

    std::array<float, 2> inputDcX1 { { 0.0f, 0.0f } };
    std::array<float, 2> inputDcY1 { { 0.0f, 0.0f } };
    std::array<float, 2> wetDcX1 { { 0.0f, 0.0f } };
    std::array<float, 2> wetDcY1 { { 0.0f, 0.0f } };
    std::array<float, 2> wetSlewState { { 0.0f, 0.0f } };

    ReverbSettings currentSettings;

    double sampleRateHz { 44100.0 };
    // The damping coefficient mapped to the current rate (see processFdn8),
    // recomputed only when the setting moves; prepare() invalidates it.
    float dampingCacheIn { -1.0f };
    float dampingCacheOut { 0.0f };
    int blockSampleCount { 0 };
    // Latches so the clear on bypass runs once, after the fade reaches zero.
    bool bypassCleared { false };
    float amountSmoothed { 0.0f };
    float amountSmoothingCoeff { 0.0f };
    float outputCompGain { 1.0f };      // this block's target, from the last block's energies
    float compGainCurrent { 1.0f };     // per-sample ramp toward it
    float compGainStep { 0.0f };
    double blockPreEnergy { 0.0 };
    double blockPostEnergy { 0.0 };
};
