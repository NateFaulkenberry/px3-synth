#include "FxPluginProcessor.h"

namespace px3::fx
{
namespace
{
const juce::Identifier kStateRootId("PX3FxState");
const juce::Identifier kParameterId("param");
const juce::Identifier kIdId("id");
const juce::Identifier kValueId("value");
} // namespace

FxPluginProcessor::FxPluginProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool FxPluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    // Stereo in, stereo out, and mono for hosts that offer it. Matching in and
    // out, because an effect that changed channel count would be a router
    // rather than an insert.
    if (in != out) { return false; }

    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void FxPluginProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    prepareFx(sampleRate, maximumExpectedSamplesPerBlock);
}

void FxPluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Any output channel the host gave us beyond the input's width holds
    // whatever was last there.
    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
    {
        buffer.clear(channel, 0, buffer.getNumSamples());
    }

    // Host tempo, for the effects that sync. Read here rather than in each
    // product so a tempo-syncing effect and a free-running one ask the same
    // way, and so a host that reports nothing gives 120 rather than zero.
    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto tempo = position->getBpm())
            {
                bpm = *tempo > 0.0 ? *tempo : 120.0;
            }
        }
    }

    if (buffer.getNumSamples() > 0 && buffer.getNumChannels() > 0)
    {
        processFxBlock(buffer);
    }
}

void FxPluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state(kStateRootId);

    for (auto* parameter : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            juce::ValueTree node(kParameterId);
            node.setProperty(kIdId, ranged->getParameterID(), nullptr);
            // Stored NORMALISED, so a later change to a parameter's range
            // reinterprets an old session rather than reading a raw value that
            // no longer means what it did.
            node.setProperty(kValueId, ranged->getValue(), nullptr);
            state.appendChild(node, nullptr);
        }
    }

    if (auto xml = state.createXml()) { copyXmlToBinary(*xml, destData); }
}

void FxPluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr) { return; }

    const auto state = juce::ValueTree::fromXml(*xml);
    if (! state.hasType(kStateRootId)) { return; }

    for (const auto& node : state)
    {
        const auto id = node.getProperty(kIdId, juce::var()).toString();
        if (id.isEmpty()) { continue; }

        for (auto* parameter : getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            if (ranged == nullptr || ranged->getParameterID() != id) { continue; }

            // A parameter the state does not mention keeps its default, and a
            // parameter the state mentions that this build does not have is
            // ignored - so a session from either direction still loads.
            ranged->setValueNotifyingHost(
                juce::jlimit(0.0f, 1.0f, static_cast<float>(node.getProperty(kValueId, 0.0))));
            break;
        }
    }
}

} // namespace px3::fx
