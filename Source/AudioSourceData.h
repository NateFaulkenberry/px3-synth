#pragma once

#include <JuceHeader.h>

#include <vector>

struct AudioSourceData
{
    juce::AudioBuffer<float> samples;
    double sampleRate { 44100.0 };
    int numChannels { 0 };
    int numSamples { 0 };
    float peak { 1.0f };
    std::vector<float> waveformPreview;
};
