#pragma once

#include <JuceHeader.h>

#include <vector>

namespace px3::fx
{

// Everything a standalone PX3 effect plug-in needs that is not the effect.
//
// Bus layout, prepare, the block loop, host tempo, parameter state. Written
// once here so each product is the effect plus its parameters, and so seven
// products cannot drift into seven slightly different answers to "what does
// setStateInformation do".
//
// PERFORMANCE. The virtual the audio thread crosses is processFxBlock, called
// ONCE per block. The per-sample loop lives inside the product, where the
// concrete FX type is known and the call inlines - a virtual per sample would
// be 512 indirect calls a block for no benefit.
//
// The effects themselves are untouched by this: they are the same objects the
// Synth drives, from shared/DSP, with the same prepare/updateForBlock/
// processSampleFrame contract. This class is a host adapter, not a wrapper
// around the DSP.
class FxPluginProcessor : public juce::AudioProcessor
{
public:
    FxPluginProcessor();
    ~FxPluginProcessor() override = default;

    //---- the JUCE surface, answered once for every effect -----------------
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) final;
    void releaseResources() final {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const final;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) final;

    bool acceptsMidi() const final { return false; }
    bool producesMidi() const final { return false; }
    bool isMidiEffect() const final { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() final { return 1; }
    int getCurrentProgram() final { return 0; }
    void setCurrentProgram(int) final {}
    const juce::String getProgramName(int) final { return {}; }
    void changeProgramName(int, const juce::String&) final {}

    // Generic and complete: every parameter this plug-in declared, by ID.
    // An effect's state is its parameters, so there is nothing for a product
    // to add and nothing for it to forget.
    void getStateInformation(juce::MemoryBlock& destData) final;
    void setStateInformation(const void* data, int sizeInBytes) final;

    // The tempo the host reported for this block, or 120 if it reported none.
    double hostBpm() const noexcept { return bpm; }

protected:
    // Called from prepareToPlay, before any audio.
    virtual void prepareFx(double sampleRate, int maximumExpectedSamplesPerBlock) = 0;

    // One call per block. The product reads its parameters, builds its
    // settings, and runs its own per-sample loop - which is where the FX's
    // processSampleFrame inlines.
    virtual void processFxBlock(juce::AudioBuffer<float>& buffer) = 0;

private:
    double bpm { 120.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxPluginProcessor)
};

} // namespace px3::fx
