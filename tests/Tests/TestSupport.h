#pragma once

// Shared scaffolding for the component test modules.
//
// This suite was one 32,000-line translation unit holding every area's tests.
// It is one file per area now, and this is what they have in common: the
// counters, the pass/fail reporting, and the fixtures for driving a processor
// and measuring what comes out.
//
// The namespace is `px3tests` at global scope, not `px3::tests`. Nested inside px3,
// every `px3::Something` in the tests would resolve against px3::tests::px3 and
// find nothing. It is named rather than anonymous because an anonymous
// namespace gives each translation unit its own counters, and the total would
// have meant nothing.
//
// The modules are contiguous slices of the original file rather than a re-sort
// by theme: helpers sit ahead of the tests that use them, so a contiguous slice
// cannot leave one behind. The fixtures below are the exception - each is
// reached from a suite in another slice, or from the sweep reports beside
// main. The measurement namespaces move as whole units: their members sit at
// column 0, so lifting one out on its own tears it from its namespace.

// Component regression, correctness and independence tests.
//
// Built in the SHIPPING configuration (PX3_DIAGNOSTICS=0) so it exercises the
// code the plugin actually runs.
//
// Structure: every component is tested at four levels - unit behaviour on the
// DSP class in isolation, parameter mapping, generated signal, and integration
// through the real processor. A component can pass its unit test and still be
// broken in the signal path, so the integration level is not optional.
//
// Determinism: fixed sample rate, fixed block size, fixed MIDI, fixed
// parameters. Where the synth draws from juce::Random::getSystemRandom() - which
// JUCE refuses to reseed - the affected assertions are stated as bounds rather
// than exact values, and that is called out at the assertion.

#include "PluginProcessor.h"
#include "PluginProcessorInternals.h"
#include "AmpEnvelope.h"
#include "GlobalSettings.h"
#include "Card.h"
#include "MacroLook.h"
#include "CardInner.h"
#include "Doom.h"
#include "FxChain.h"
#include "AnalogEngine.h"
#include "Chorus.h"
#include "FactoryPresets.h"
#include "Wavetable.h"
#include "BreakpointEnvelope.h"
#include "LfoMode.h"
#include "AmpEnvelope.h"
#include "EnvelopeGenerator.h"
#include "WavetableReader.h"
#include "WavetableSlot.h"
#include "WavetableFactory.h"
#include "WavetableImporter.h"
#include "WavetableLibrary.h"
#include "AmpEnvelopeComponent.h"
#include "WavetableGraph.h"
#include "Wavetable3DRenderer.h"
#include "OscillatorComponent.h"
#include "BreakpointEnvelopeEditor.h"
#include "EnvelopeComponent.h"
#include "Lucy.h"
#include "StereoSpread.h"
#include "FxCardComponent.h"
#include "FxChainLayout.h"
#include "ChipLabel.h"
#include "ParameterKnob.h"
#include "PluginEditor.h"
#include "ModPanel.h"
#include "PianoKeyboard.h"
#include "TopMenuBar.h"
#include "FilterComponent.h"
#include <chrono>
#include <set>

#include "BusInsertChain.h"
#include "FilterResponse.h"
#include "VoiceFilter.h"
#include "OscillatorComponent.h"
#include "FxPanel.h"
#include "OscPanel.h"
#include "AmpPanel.h"
#include "FltPanel.h"
#include "MixPanel.h"
#include "SettingsPanel.h"
#include "FxSignalFlow.h"
#include "UIConfigManager.h"
#include "BusEqGraph.h"
#include "BusInsertOverlay.h"
#include "MixerControls.h"
#include "MixerChannelComponent.h"
#include "FetPanelStyle.h"
#include "PerformanceControls.h"
#include "ModalBackdrop.h"
#include "RoundedRect.h"
#include "VuBallistics.h"
#include "VuMeterComponent.h"
#include "UIConfig.h"
#include "Delay.h"
#include "EnvelopeGenerator.h"
#include "LfoGenerator.h"
#include "Mood.h"
#include "Reverb.h"
#include "SubOscillator.h"
#include "PresetManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace px3tests
{

// The shipping UIConfig.json, in one place.
//
// It was spelled out in twenty tests as a path relative to the repository
// root, so moving the file into shared/UI/Style made every one of them load
// nothing - and the first test to use the result dereferenced it and took the
// whole suite down with a segfault rather than failing.
//
// Returning the File rather than the parsed config keeps the callers that want
// the raw text working, and gives the ones that parse it a single place to
// change the next time the tree moves.
inline juce::File shippingUiConfigFile()
{
    return juce::File::getCurrentWorkingDirectory()
        .getChildFile("shared/UI/Style/UIConfig.json");
}


constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

inline int gPassed = 0;
inline int gFailed = 0;
inline std::vector<std::string> gFailures;
inline const char* gSuite = "";

inline void suite(const char* name)
{
    gSuite = name;
    std::printf("\n== %s ==\n", name);
}

// Every check states what it expects, so a failure names the broken behaviour
// rather than an anonymous assertion index.
inline bool check(const char* name, bool ok, const juce::String& detail = {})
{
    if (ok)
    {
        ++gPassed;
        std::printf("  ok    %-58s %s\n", name, detail.toRawUTF8());
    }
    else
    {
        ++gFailed;
        gFailures.push_back(std::string(gSuite) + " / " + name + "  " + detail.toStdString());
        std::printf("  FAIL  %-58s %s\n", name, detail.toRawUTF8());
    }
    std::fflush(stdout);
    return ok;
}

inline juce::String fmt(double v, int places = 6) { return juce::String(v, places); }

inline bool nearly(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}

//==============================================================================
// Processor helpers
//==============================================================================
inline juce::RangedAudioParameter* findParameter(juce::AudioProcessor& processor, const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            if (ranged->paramID == id)
            {
                return ranged;
            }
        }
    }
    return nullptr;
}

inline bool setParam(juce::AudioProcessor& processor, const juce::String& id, float value)
{
    if (auto* parameter = findParameter(processor, id))
    {
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        return true;
    }
    std::printf("  !!    parameter not found: %s\n", id.toRawUTF8());
    return false;
}

inline void setChoice(juce::AudioProcessor& processor, const juce::String& id, int index)
{
    if (auto* parameter = findParameter(processor, id))
    {
        const auto steps = juce::jmax(1, parameter->getNumSteps() - 1);
        parameter->setValueNotifyingHost(static_cast<float>(index) / static_cast<float>(steps));
    }
}

inline float getParamValue(juce::AudioProcessor& processor, const juce::String& id)
{
    if (auto* parameter = findParameter(processor, id))
    {
        return parameter->convertFrom0to1(parameter->getValue());
    }
    return 0.0f;
}

// A rendered capture of the plugin output plus the measurements every test
// wants from it, so no test re-implements peak/RMS/DC.
struct Capture
{
    std::vector<float> left;
    std::vector<float> right;

    double peak() const
    {
        double p = 0.0;
        for (const auto v : left) p = juce::jmax(p, static_cast<double>(std::abs(v)));
        for (const auto v : right) p = juce::jmax(p, static_cast<double>(std::abs(v)));
        return p;
    }

    double rms() const
    {
        if (left.empty()) return 0.0;
        double e = 0.0;
        for (const auto v : left) e += static_cast<double>(v) * v;
        for (const auto v : right) e += static_cast<double>(v) * v;
        return std::sqrt(e / static_cast<double>(left.size() + right.size()));
    }

    double dcOffset() const
    {
        if (left.empty()) return 0.0;
        double s = 0.0;
        for (const auto v : left) s += v;
        return s / static_cast<double>(left.size());
    }

    bool isFinite() const
    {
        for (const auto v : left) if (! std::isfinite(v)) return false;
        for (const auto v : right) if (! std::isfinite(v)) return false;
        return true;
    }

    // Largest sample-to-sample step. A note-on or note-off transient is
    // expected; a mid-sustain jump is not.
    double maxStep(int fromSample, int toSample) const
    {
        double worst = 0.0;
        const auto last = juce::jmin(static_cast<int>(left.size()), toSample);
        for (int i = juce::jmax(1, fromSample); i < last; ++i)
        {
            worst = juce::jmax(worst, static_cast<double>(std::abs(left[static_cast<std::size_t>(i)]
                                                                  - left[static_cast<std::size_t>(i - 1)])));
        }
        return worst;
    }

    double rmsOver(int fromSample, int toSample) const
    {
        const auto last = juce::jmin(static_cast<int>(left.size()), toSample);
        const auto first = juce::jmax(0, fromSample);
        if (last <= first) return 0.0;
        double e = 0.0;
        for (int i = first; i < last; ++i)
        {
            const auto v = static_cast<double>(left[static_cast<std::size_t>(i)]);
            e += v * v;
        }
        return std::sqrt(e / static_cast<double>(last - first));
    }
};

struct NoteEvent
{
    int sample { 0 };
    bool on { true };
    int note { 60 };
    float velocity { 0.9f };
};

// Renders the real processor headlessly. Everything integration-level goes
// through this so tests share one definition of "run the plugin".
inline Capture render(PX3SynthAudioProcessor& processor,
               int totalSamples,
               const std::vector<NoteEvent>& events,
               const std::function<void(int)>& perBlock = {})
{
    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);

    Capture capture;
    capture.left.reserve(static_cast<std::size_t>(totalSamples));
    capture.right.reserve(static_cast<std::size_t>(totalSamples));

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    std::size_t nextEvent = 0;
    auto sorted = events;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const NoteEvent& a, const NoteEvent& b) { return a.sample < b.sample; });

    for (int position = 0; position < totalSamples; position += kBlockSize)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        while (nextEvent < sorted.size() && sorted[nextEvent].sample < position + kBlockSize)
        {
            const auto& e = sorted[nextEvent];
            midi.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, e.velocity)
                               : juce::MidiMessage::noteOff(1, e.note),
                          juce::jmax(0, e.sample - position));
            ++nextEvent;
        }

        if (perBlock)
        {
            perBlock(position / kBlockSize);
        }

        processor.processBlock(buffer, midi);

        const auto count = juce::jmin(kBlockSize, totalSamples - position);
        for (int i = 0; i < count; ++i)
        {
            capture.left.push_back(buffer.getSample(0, i));
            capture.right.push_back(buffer.getSample(1, i));
        }
    }

    return capture;
}

// Frequency estimate by autocorrelation. Zero-crossing counting is unreliable
// on the shapes this synth makes (a saw folded through tanh crosses cleanly,
// but a detuned stack or a filtered square does not), so the dominant period is
// found instead and reported in Hz.
inline double estimateFrequency(const std::vector<float>& signal,
                         int fromSample,
                         int windowSamples,
                         double minHz = 20.0,
                         double maxHz = 4000.0)
{
    const auto first = juce::jmax(0, fromSample);
    const auto available = static_cast<int>(signal.size()) - first;
    const auto window = juce::jmin(windowSamples, available);
    if (window < 512) return 0.0;

    const auto minLag = juce::jmax(2, static_cast<int>(kSampleRate / maxHz));
    const auto maxLag = juce::jmin(window / 2, static_cast<int>(kSampleRate / minHz));
    if (maxLag <= minLag) return 0.0;

    // Remove DC first: an offset biases every correlation toward long lags.
    double mean = 0.0;
    for (int i = 0; i < window; ++i) mean += signal[static_cast<std::size_t>(first + i)];
    mean /= static_cast<double>(window);

    // Properly normalised autocorrelation: divided by the energy of BOTH
    // windows. Dividing by one side only makes the score grow with lag, which
    // reported a 440 Hz sine as 20 Hz - the longest lag searched.
    std::vector<double> score(static_cast<std::size_t>(maxLag + 1), 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const auto count = window - lag;
        for (int i = 0; i < count; ++i)
        {
            const auto a = static_cast<double>(signal[static_cast<std::size_t>(first + i)]) - mean;
            const auto b = static_cast<double>(signal[static_cast<std::size_t>(first + i + lag)]) - mean;
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const auto denominator = std::sqrt(energyA * energyB);
        score[static_cast<std::size_t>(lag)] = denominator > 1.0e-12 ? correlation / denominator : 0.0;
    }

    double bestScore = -1.0e30;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        bestScore = juce::jmax(bestScore, score[static_cast<std::size_t>(lag)]);
    }

    if (bestScore <= 0.0) return 0.0;

    // A periodic signal correlates just as well at every multiple of its
    // period, so the global peak is not the period - taking it reported a
    // 440 Hz sine as 20 Hz, the longest lag searched, because 2400 samples is
    // almost exactly 22 cycles. The fundamental is the SHORTEST lag that scores
    // close to the best, and it must be a local maximum so that the rising
    // shoulder of the first peak is not mistaken for it.
    for (int lag = minLag + 1; lag < maxLag; ++lag)
    {
        const auto here = score[static_cast<std::size_t>(lag)];
        if (here >= bestScore * 0.90
            && here >= score[static_cast<std::size_t>(lag - 1)]
            && here >= score[static_cast<std::size_t>(lag + 1)])
        {
            return kSampleRate / static_cast<double>(lag);
        }
    }

    return 0.0;
}

// Broadband energy in a band, as dBFS. A hiss meter: pink noise lives up here,
// and a 110 Hz tone through a saturator does not - by 8 kHz it is past its 70th
// harmonic and the odd-harmonic series has long since died away.
inline double bandRmsDb(const std::vector<float>& signal,
                 double lowHz,
                 double highHz,
                 int fromSample,
                 int fftOrder = 14)
{
    const auto size = 1 << fftOrder;
    if (fromSample + size > static_cast<int>(signal.size())) return -200.0;

    juce::dsp::FFT fft(fftOrder);
    std::vector<float> data(static_cast<std::size_t>(size) * 2, 0.0f);
    for (int i = 0; i < size; ++i)
    {
        const auto w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
                                              * static_cast<float>(i) / static_cast<float>(size - 1));
        data[static_cast<std::size_t>(i)] = signal[static_cast<std::size_t>(fromSample + i)] * w;
    }
    fft.performFrequencyOnlyForwardTransform(data.data());

    const auto binsPerHz = static_cast<double>(size) / kSampleRate;
    double sum = 0.0;
    for (int b = static_cast<int>(lowHz * binsPerHz); b <= static_cast<int>(highHz * binsPerHz); ++b)
    {
        if (b <= 0 || b >= size / 2) continue;
        const auto m = static_cast<double>(data[static_cast<std::size_t>(b)]);
        sum += m * m;
    }
    return juce::Decibels::gainToDecibels(std::sqrt(sum) / (size * 0.5), -200.0);
}

// Energy at the harmonics of a tone, relative to energy at the fundamental. A
// sine has none of its own, so on a sine source this is a direct measure of how
// much a nonlinearity is adding - unaffected by level, and unaffected by slow
// pitch drift, which smears the fundamental but leaves the harmonics where they
// are relative to it.
inline double harmonicToFundamentalRatio(const std::vector<float>& signal,
                                  double fundamentalHz,
                                  int fromSample,
                                  int fftOrder = 14)
{
    const auto size = 1 << fftOrder;
    if (fromSample + size > static_cast<int>(signal.size())) return 0.0;

    juce::dsp::FFT fft(fftOrder);
    std::vector<float> data(static_cast<std::size_t>(size) * 2, 0.0f);
    for (int i = 0; i < size; ++i)
    {
        const auto w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
                                              * static_cast<float>(i) / static_cast<float>(size - 1));
        data[static_cast<std::size_t>(i)] = signal[static_cast<std::size_t>(fromSample + i)] * w;
    }
    fft.performFrequencyOnlyForwardTransform(data.data());

    const auto binsPerHz = static_cast<double>(size) / kSampleRate;
    auto energyAround = [&](double hz, int spread)
    {
        const auto centre = static_cast<int>(hz * binsPerHz + 0.5);
        double sum = 0.0;
        for (int b = centre - spread; b <= centre + spread; ++b)
        {
            if (b <= 0 || b >= size / 2) continue;
            const auto m = static_cast<double>(data[static_cast<std::size_t>(b)]);
            sum += m * m;
        }
        return sum;
    };

    const auto fundamental = energyAround(fundamentalHz, 24);
    double harmonics = 0.0;
    for (int h = 2; h <= 6; ++h)
    {
        harmonics += energyAround(fundamentalHz * h, 24);
    }
    return fundamental > 1.0e-18 ? harmonics / fundamental : 0.0;
}

// Configures a processor into a plain, predictable state: one oscillator, no
// filters, no FX, no modulation, full sustain. Tests then change exactly the
// one thing they are measuring.
// The animation preference is process-wide now, so a test that changes it
// changes it for every test that runs afterwards. This puts it back.
struct ScopedAnimationPreference
{
    explicit ScopedAnimationPreference(bool value)
        : previous(px3::GlobalSettings::getInstance().areAnimationsEnabled())
    {
        px3::GlobalSettings::getInstance().setAnimationsEnabled(value);
    }

    ~ScopedAnimationPreference()
    {
        px3::GlobalSettings::getInstance().setAnimationsEnabled(previous);
    }

    bool previous;
};

inline void makePlainPatch(PX3SynthAudioProcessor& processor)
{
    setParam(processor, "ampAttack", 0.001f);
    setParam(processor, "ampDecay", 0.005f);
    setParam(processor, "ampSustain", 1.0f);
    setParam(processor, "ampRelease", 0.050f);
    setParam(processor, "ampEnvEnabled", 1.0f);
    setParam(processor, "masterGain", 0.6f);

    // The analog console is a colour stage, and "plain" means no colour. It
    // ships ON now that SETTINGS can turn it off, so a plain patch has to say
    // so rather than relying on a default.
    setParam(processor, "analogEnabled", 0.0f);

    setParam(processor, "osc1Enabled", 1.0f);
    setParam(processor, "osc2Enabled", 0.0f);
    setParam(processor, "osc3Enabled", 0.0f);
    setParam(processor, "subOscEnabled", 0.0f);
    for (const auto* slot : { "1", "2", "3" })
    {
        setChoice(processor, juce::String("osc") + slot + "Mode", 0); // SINE
        setParam(processor, juce::String("osc") + slot + "Coarse", 0.0f);
        setParam(processor, juce::String("osc") + slot + "Fine", 0.0f);
        setParam(processor, juce::String("osc") + slot + "Pitch", 0.0f);
    }

    setParam(processor, "filter1Enabled", 0.0f);
    setParam(processor, "filter2Enabled", 0.0f);

    setParam(processor, "vibeEnabled", 0.0f);
    setParam(processor, "vibeAmount", 0.0f);
    setParam(processor, "delayEnabled", 0.0f);
    setParam(processor, "reverbEnabled", 0.0f);
    setParam(processor, "moodEnabled", 0.0f);

    for (int i = 0; i < 3; ++i)
    {
        const auto slot = juce::String(i + 1);
        setParam(processor, "env" + slot + "Enabled", 0.0f);
        setParam(processor, i == 0 ? juce::String("envAmount") : "env" + slot + "Amount", 0.0f);
        const auto lfoPrefix = i == 0 ? juce::String("lfo") : "lfo" + slot;
        setParam(processor, i == 0 ? juce::String("lfoEnabled") : lfoPrefix + "Enabled", 0.0f);
        setParam(processor, i == 0 ? juce::String("lfoAmount") : lfoPrefix + "Amount", 0.0f);
        processor.setLfoAssignmentIndex(i, 0, false);
        processor.setEnvelopeAssignmentIndex(i, 0, false);
    }

    for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
    {
        setParam(processor, juce::String("mix.") + id + ".level", 0.8f);
        setParam(processor, juce::String("mix.") + id + ".pan", 0.0f);
        setParam(processor, juce::String("mix.") + id + ".fxSend", 0.0f);
        setParam(processor, juce::String("mix.") + id + ".mute", 0.0f);
        setParam(processor, juce::String("mix.") + id + ".solo", 0.0f);
    }
    setParam(processor, "mix.fx.mute", 0.0f);
    setParam(processor, "mix.fx.solo", 0.0f);
}

//==============================================================================
// BREAKPOINT ENVELOPE
//==============================================================================

// ---- fixtures reached from more than one slice --------------------------
struct ReverbMetrics
{
    double echoDensityAt20ms { 0.0 };
    double echoDensityAt50ms { 0.0 };
    double echoDensityAt150ms { 0.0 };
    double spectralFlatness { 0.0 };
    double decayRippleDb { 0.0 };
    double lateRippleDb { 0.0 };
    double interChannelCorrelation { 0.0 };
    double rt60Seconds { 0.0 };
    double peak { 0.0 };
    double channelBalanceDb { 0.0 };
    bool finite { true };
};

inline double normalisedEchoDensity(const std::vector<float>& ir, int centreSample, int windowSamples)
{
    const auto half = windowSamples / 2;
    const auto first = juce::jmax(0, centreSample - half);
    const auto last = juce::jmin(static_cast<int>(ir.size()), centreSample + half);
    if (last - first < 32) return 0.0;

    double mean = 0.0;
    for (int i = first; i < last; ++i) mean += ir[static_cast<std::size_t>(i)];
    mean /= static_cast<double>(last - first);

    double variance = 0.0;
    for (int i = first; i < last; ++i)
    {
        const auto d = ir[static_cast<std::size_t>(i)] - mean;
        variance += d * d;
    }
    const auto sigma = std::sqrt(variance / static_cast<double>(last - first));
    if (sigma < 1.0e-12) return 0.0;

    int beyond = 0;
    for (int i = first; i < last; ++i)
    {
        if (std::abs(ir[static_cast<std::size_t>(i)] - mean) > sigma) ++beyond;
    }
    constexpr double gaussianExpectation = 0.3173;
    return static_cast<double>(beyond) / (gaussianExpectation * static_cast<double>(last - first));
}

inline double spectralFlatnessOf(const std::vector<float>& ir, int fromSample, int fftOrder = 12)
{
    const auto size = 1 << fftOrder;
    if (fromSample + size > static_cast<int>(ir.size())) return 0.0;

    juce::dsp::FFT fft(fftOrder);
    std::vector<float> data(static_cast<std::size_t>(size) * 2, 0.0f);
    for (int i = 0; i < size; ++i)
    {
        // Hann window so leakage does not masquerade as flatness.
        const auto w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi
                                              * static_cast<float>(i) / static_cast<float>(size - 1));
        data[static_cast<std::size_t>(i)] = ir[static_cast<std::size_t>(fromSample + i)] * w;
    }
    fft.performFrequencyOnlyForwardTransform(data.data());

    double logSum = 0.0, linSum = 0.0;
    int count = 0;
    // Ignore DC and the very top of the band, where windowing dominates.
    for (int bin = 4; bin < size / 2 - 4; ++bin)
    {
        const auto power = static_cast<double>(data[static_cast<std::size_t>(bin)]) * data[static_cast<std::size_t>(bin)]
                           + 1.0e-20;
        logSum += std::log(power);
        linSum += power;
        ++count;
    }
    if (count == 0) return 0.0;
    const auto geometric = std::exp(logSum / count);
    const auto arithmetic = linSum / count;
    return arithmetic > 0.0 ? geometric / arithmetic : 0.0;
}

inline double decayRippleDb(const std::vector<float>& ir, int fromSample, int toSample)
{
    constexpr int windowSamples = 512;
    std::vector<double> times, levels;
    for (int start = fromSample; start + windowSamples < toSample; start += windowSamples)
    {
        double energy = 0.0;
        for (int i = 0; i < windowSamples; ++i)
        {
            const auto v = static_cast<double>(ir[static_cast<std::size_t>(start + i)]);
            energy += v * v;
        }
        const auto rms = std::sqrt(energy / windowSamples);
        if (rms < 1.0e-9) break;
        times.push_back(static_cast<double>(start));
        levels.push_back(20.0 * std::log10(rms));
    }
    if (times.size() < 6) return 1.0e9;

    // Least-squares line through the dB envelope.
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const auto n = static_cast<double>(times.size());
    for (std::size_t i = 0; i < times.size(); ++i)
    {
        sx += times[i]; sy += levels[i];
        sxx += times[i] * times[i]; sxy += times[i] * levels[i];
    }
    const auto denom = n * sxx - sx * sx;
    if (std::abs(denom) < 1.0e-12) return 1.0e9;
    const auto slope = (n * sxy - sx * sy) / denom;
    const auto intercept = (sy - slope * sx) / n;

    double worst = 0.0;
    for (std::size_t i = 0; i < times.size(); ++i)
    {
        worst = juce::jmax(worst, std::abs(levels[i] - (slope * times[i] + intercept)));
    }
    return worst;
}

inline double interChannelCorrelation(const std::vector<float>& left,
                              const std::vector<float>& right,
                              int fromSample,
                              int toSample)
{
    double sl = 0, sr = 0, sll = 0, srr = 0, slr = 0;
    const auto last = juce::jmin(toSample, juce::jmin(static_cast<int>(left.size()), static_cast<int>(right.size())));
    const auto count = last - fromSample;
    if (count <= 0) return 0.0;
    for (int i = fromSample; i < last; ++i)
    {
        const auto a = static_cast<double>(left[static_cast<std::size_t>(i)]);
        const auto b = static_cast<double>(right[static_cast<std::size_t>(i)]);
        sl += a; sr += b; sll += a * a; srr += b * b; slr += a * b;
    }
    const auto n = static_cast<double>(count);
    const auto cov = slr / n - (sl / n) * (sr / n);
    const auto vl = sll / n - (sl / n) * (sl / n);
    const auto vr = srr / n - (sr / n) * (sr / n);
    const auto denom = std::sqrt(juce::jmax(1.0e-24, vl * vr));
    return cov / denom;
}

inline double measureRt60(const std::vector<float>& ir)
{
    std::vector<double> schroeder(ir.size(), 0.0);
    double running = 0.0;
    for (int i = static_cast<int>(ir.size()) - 1; i >= 0; --i)
    {
        const auto v = static_cast<double>(ir[static_cast<std::size_t>(i)]);
        running += v * v;
        schroeder[static_cast<std::size_t>(i)] = running;
    }
    if (schroeder[0] <= 0.0) return 0.0;
    const auto ref = 10.0 * std::log10(schroeder[0]);

    int at5 = -1, at35 = -1;
    for (std::size_t i = 0; i < schroeder.size(); ++i)
    {
        if (schroeder[i] <= 0.0) break;
        const auto db = 10.0 * std::log10(schroeder[i]) - ref;
        if (at5 < 0 && db <= -5.0) at5 = static_cast<int>(i);
        if (at35 < 0 && db <= -35.0) { at35 = static_cast<int>(i); break; }
    }
    if (at5 < 0 || at35 <= at5) return 0.0;
    const auto seconds = static_cast<double>(at35 - at5) / kSampleRate;
    return seconds * 2.0; // -30 dB span extrapolated to -60 dB
}

inline double decayCurveNonlinearityDb(const std::vector<float>& ir)
{
    std::vector<double> schroeder(ir.size(), 0.0);
    double running = 0.0;
    for (int i = static_cast<int>(ir.size()) - 1; i >= 0; --i)
    {
        const auto v = static_cast<double>(ir[static_cast<std::size_t>(i)]);
        running += v * v;
        schroeder[static_cast<std::size_t>(i)] = running;
    }
    if (schroeder[0] <= 0.0) return 1.0e9;
    const auto ref = 10.0 * std::log10(schroeder[0]);

    std::vector<double> xs, ys;
    for (std::size_t i = 0; i < schroeder.size(); i += 64)
    {
        if (schroeder[i] <= 0.0) break;
        const auto db = 10.0 * std::log10(schroeder[i]) - ref;
        if (db > -5.0) continue;
        if (db < -35.0) break;
        xs.push_back(static_cast<double>(i));
        ys.push_back(db);
    }
    if (xs.size() < 8) return 1.0e9;

    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const auto n = static_cast<double>(xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i)
    {
        sx += xs[i]; sy += ys[i]; sxx += xs[i] * xs[i]; sxy += xs[i] * ys[i];
    }
    const auto denom = n * sxx - sx * sx;
    if (std::abs(denom) < 1.0e-12) return 1.0e9;
    const auto slope = (n * sxy - sx * sy) / denom;
    const auto intercept = (sy - slope * sx) / n;

    double worst = 0.0;
    for (std::size_t i = 0; i < xs.size(); ++i)
    {
        worst = juce::jmax(worst, std::abs(ys[i] - (slope * xs[i] + intercept)));
    }
    return worst;
}

struct MoodMetrics
{
    bool finite { true };
    double peak { 0.0 };
    double rms { 0.0 };
    double interChannelCorrelation { 1.0 };
    // Energy that leaks to the silent channel when only one channel is fed. A
    // true stereo effect keeps a hard-panned source mostly where it was put;
    // a mono-summing one splits it evenly and reports 0 dB.
    double channelSeparationDb { 0.0 };
    // How much the two channels differ over time, as RMS of (L-R) against RMS
    // of (L+R)/2. Zero means the output is mono no matter how it was fed.
    double sideToMidRatio { 0.0 };
};

inline MoodMetrics measureMood(const px3::MoodUserParameters& settings,
                        bool panLeft = false,
                        double sampleRate = kSampleRate,
                        int totalSamples = 0)
{
    if (totalSamples <= 0) totalSamples = static_cast<int>(sampleRate * 6.0);

    Mood mood;
    mood.prepare(sampleRate);
    mood.reset();
    auto s = settings;
    s.enabled = true;
    mood.updateForBlock(s);

    juce::Random random(0x0FEEDBACu);
    std::vector<float> left, right;
    left.reserve(static_cast<std::size_t>(totalSamples));
    right.reserve(static_cast<std::size_t>(totalSamples));

    // A repeating pluck: broadband enough to excite everything, and gated so
    // envelope-driven modes have something to trigger on.
    const auto period = static_cast<int>(sampleRate * 0.5);
    for (int i = 0; i < totalSamples; ++i)
    {
        const auto inPeriod = i % period;
        const auto envelope = std::exp(-4.0f * static_cast<float>(inPeriod) / static_cast<float>(period));
        const auto tone = std::sin(juce::MathConstants<float>::twoPi * 220.0f
                                   * static_cast<float>(i) / static_cast<float>(sampleRate));
        const auto noise = random.nextFloat() * 0.2f - 0.1f;
        const auto in = (tone * 0.5f + noise) * envelope;

        float l = 0.0f, r = 0.0f;
        mood.processSampleFrame(in, panLeft ? 0.0f : in, l, r);
        left.push_back(l);
        right.push_back(r);
    }

    MoodMetrics m;
    for (const auto v : left)  { if (! std::isfinite(v)) m.finite = false; m.peak = juce::jmax(m.peak, std::abs(static_cast<double>(v))); }
    for (const auto v : right) { if (! std::isfinite(v)) m.finite = false; m.peak = juce::jmax(m.peak, std::abs(static_cast<double>(v))); }

    // Everything below ignores the first second, so start-up transients and
    // the buffer filling up do not count.
    const auto from = static_cast<int>(sampleRate);
    double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0, sumSide = 0.0, sumMid = 0.0, energy = 0.0;
    for (int i = from; i < totalSamples; ++i)
    {
        const auto l = static_cast<double>(left[static_cast<std::size_t>(i)]);
        const auto r = static_cast<double>(right[static_cast<std::size_t>(i)]);
        sumLR += l * r; sumLL += l * l; sumRR += r * r;
        const auto side = 0.5 * (l - r);
        const auto mid = 0.5 * (l + r);
        sumSide += side * side;
        sumMid += mid * mid;
        energy += l * l + r * r;
    }
    const auto count = juce::jmax(1, totalSamples - from);
    m.rms = std::sqrt(energy / (2.0 * count));
    m.interChannelCorrelation = sumLR / std::sqrt(juce::jmax(1.0e-20, sumLL * sumRR));
    m.sideToMidRatio = std::sqrt(sumSide / juce::jmax(1.0e-20, sumMid));
    m.channelSeparationDb = 10.0 * std::log10(juce::jmax(1.0e-20, sumLL) / juce::jmax(1.0e-20, sumRR));
    return m;
}

struct DelayMetrics
{
    bool finite { true };
    double peak { 0.0 };
    double firstEchoMs { 0.0 };      // when the first repeat arrives
    double secondEchoMs { 0.0 };     // and the second, so spacing is checkable
    double tailRmsEarly { 0.0 };     // RMS over the first second
    double tailRmsLate { 0.0 };      // RMS over the last second: growing = unstable
    double interChannelCorrelation { 1.0 };
    double lowFrequencyEnergyRatio { 0.0 };  // energy below 30 Hz vs total
};

inline DelayMetrics measureDelay(const DelaySettings& settings,
                          double sampleRate = kSampleRate,
                          int totalSamples = 0)
{
    if (totalSamples <= 0) totalSamples = static_cast<int>(sampleRate * 8.0);

    Delay delay;
    delay.prepare(sampleRate);
    delay.reset();
    auto s = settings;
    s.enabled = true;
    delay.updateForBlock(s);

    std::vector<float> left, right;
    left.reserve(static_cast<std::size_t>(totalSamples));
    right.reserve(static_cast<std::size_t>(totalSamples));

    // Let the control smoothers settle before the impulse, or the measured
    // echo time is the smoother's trajectory rather than the algorithm's.
    for (int i = 0; i < static_cast<int>(sampleRate * 0.5); ++i)
    {
        float l = 0.0f, r = 0.0f;
        delay.processSampleFrame(0.0f, 0.0f, l, r);
    }

    for (int i = 0; i < totalSamples; ++i)
    {
        const auto input = i == 0 ? 1.0f : 0.0f;
        float l = 0.0f, r = 0.0f;
        delay.processSampleFrame(input, input, l, r);
        left.push_back(l);
        right.push_back(r);
    }

    DelayMetrics m;
    for (const auto v : left)  { if (! std::isfinite(v)) m.finite = false; m.peak = juce::jmax(m.peak, std::abs(static_cast<double>(v))); }
    for (const auto v : right) { if (! std::isfinite(v)) m.finite = false; m.peak = juce::jmax(m.peak, std::abs(static_cast<double>(v))); }

    // Echo arrivals: local energy maxima after the direct sound, found on a
    // short-window envelope so a single echo is one event rather than many.
    {
        const auto window = static_cast<int>(sampleRate * 0.005);
        std::vector<double> env;
        env.reserve(static_cast<std::size_t>(totalSamples / window + 1));
        for (int i = 0; i + window <= totalSamples; i += window)
        {
            double e = 0.0;
            for (int j = i; j < i + window; ++j) e += static_cast<double>(left[static_cast<std::size_t>(j)]) * left[static_cast<std::size_t>(j)];
            env.push_back(std::sqrt(e / window));
        }
        double envPeak = 0.0;
        for (std::size_t i = 1; i < env.size(); ++i) envPeak = juce::jmax(envPeak, env[i]);
        const auto threshold = envPeak * 0.25;
        int found = 0;
        for (std::size_t i = 2; i + 1 < env.size() && found < 2; ++i)
        {
            if (env[i] > threshold && env[i] >= env[i - 1] && env[i] > env[i + 1])
            {
                const auto ms = static_cast<double>(i) * window * 1000.0 / sampleRate;
                if (found == 0) m.firstEchoMs = ms;
                else            m.secondEchoMs = ms;
                ++found;
                i += 4;   // do not report the same echo twice
            }
        }
    }

    const auto oneSecond = static_cast<int>(sampleRate);
    auto rmsOver = [&](int from, int to)
    {
        from = juce::jmax(0, from);
        to = juce::jmin(totalSamples, to);
        if (to <= from) return 0.0;
        double e = 0.0;
        for (int i = from; i < to; ++i) e += static_cast<double>(left[static_cast<std::size_t>(i)]) * left[static_cast<std::size_t>(i)];
        return std::sqrt(e / (to - from));
    };
    m.tailRmsEarly = rmsOver(oneSecond / 4, oneSecond + oneSecond / 4);
    m.tailRmsLate = rmsOver(totalSamples - oneSecond, totalSamples);

    {
        double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
        for (int i = 0; i < totalSamples; ++i)
        {
            const auto l = static_cast<double>(left[static_cast<std::size_t>(i)]);
            const auto r = static_cast<double>(right[static_cast<std::size_t>(i)]);
            sumLR += l * r; sumLL += l * l; sumRR += r * r;
        }
        const auto d = std::sqrt(juce::jmax(1.0e-20, sumLL * sumRR));
        m.interChannelCorrelation = sumLR / d;
    }

    // Energy below 30 Hz. An impulse contains all frequencies, but by a second
    // in, a delay should be repeating what it was fed - not generating its own
    // subsonic content. A high number here means something in the algorithm is
    // adding a low-frequency signal of its own.
    {
        const auto from = oneSecond * 2;
        const auto count = juce::jmin(totalSamples - from, oneSecond * 2);
        if (count > 1024)
        {
            // One-pole lowpass at 30 Hz, energy ratio against the unfiltered band.
            const auto coeff = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 30.0f / static_cast<float>(sampleRate));
            float lp = 0.0f;
            double lowEnergy = 0.0, totalEnergy = 0.0;
            for (int i = from; i < from + count; ++i)
            {
                const auto v = left[static_cast<std::size_t>(i)];
                lp += coeff * (v - lp);
                lowEnergy += static_cast<double>(lp) * lp;
                totalEnergy += static_cast<double>(v) * v;
            }
            m.lowFrequencyEnergyRatio = totalEnergy > 1.0e-20 ? lowEnergy / totalEnergy : 0.0;
        }
    }

    return m;
}

struct DelayStressResult
{
    double tailAt1s { 0.0 };
    double tailAt10s { 0.0 };
    double tailAt25s { 0.0 };
    double peak { 0.0 };
    double tailHz { 0.0 };
    // Sub-30 Hz share, measured while the input is still playing. As a ratio
    // it needs a healthy denominator: taken from a near-silent window it
    // swings between 0.06 and 0.31 run to run on the granular algorithm purely
    // from grain-spawn randomness, and says nothing about the algorithm.
    double lowFrequencyEnergyRatio { 0.0 };
    bool finite { true };
};

inline DelayStressResult delayStress(int algo, int sweepWhich, float feedbackLevel = 0.85f)
{
    Delay delay;
    delay.prepare(kSampleRate);
    delay.reset();

    const auto driveSamples = static_cast<int>(kSampleRate * 6.0);
    const auto tailSamples = static_cast<int>(kSampleRate * 28.0);
    juce::Random random(0x0DE1A1u);

    std::vector<float> out;
    const auto total = driveSamples + tailSamples;
    out.reserve(static_cast<std::size_t>(total));

    constexpr int blockSize = 64;
    int i = 0;
    while (i < total)
    {
        const auto progress = juce::jlimit(0.0f, 1.0f,
                                           static_cast<float>(i) / static_cast<float>(driveSamples));
        DelaySettings s;
        s.enabled = true;
        s.algorithmIndex = algo;
        s.amount = sweepWhich == 3 ? progress : 0.8f;
        s.timeControl = sweepWhich == 1 ? progress : 0.4f;
        s.feedbackControl = sweepWhich == 2 ? progress : feedbackLevel;
        s.syncDivisionIndex = sweepWhich == 4 ? (1 + (i / static_cast<int>(kSampleRate)) % 7) : 0;
        delay.updateForBlock(s);

        for (int j = 0; j < blockSize && i < total; ++j, ++i)
        {
            float in = 0.0f;
            if (i < driveSamples)
            {
                const auto env = 0.5f + 0.5f * std::sin(static_cast<float>(i) * 0.0002f);
                in = (std::sin(juce::MathConstants<float>::twoPi * 196.0f
                               * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.4f
                      + (random.nextFloat() * 0.1f - 0.05f)) * env;
            }
            float l = 0.0f, r = 0.0f;
            delay.processSampleFrame(in, in, l, r);
            out.push_back(l);
        }
    }

    auto rmsAt = [&](double secondsAfterStop)
    {
        const auto from = driveSamples + static_cast<int>(kSampleRate * secondsAfterStop);
        const auto to = juce::jmin(static_cast<int>(out.size()), from + static_cast<int>(kSampleRate * 0.5));
        if (to <= from) return 0.0;
        double e = 0.0;
        for (int k = from; k < to; ++k)
        {
            e += static_cast<double>(out[static_cast<std::size_t>(k)]) * out[static_cast<std::size_t>(k)];
        }
        return std::sqrt(e / (to - from));
    };

    DelayStressResult result;
    for (const auto v : out)
    {
        if (! std::isfinite(v)) result.finite = false;
        result.peak = juce::jmax(result.peak, std::abs(static_cast<double>(v)));
    }
    result.tailAt1s = rmsAt(1.0);
    result.tailAt10s = rmsAt(10.0);
    result.tailAt25s = rmsAt(25.0);
    result.tailHz = estimateFrequency(out,
                                      driveSamples + static_cast<int>(kSampleRate * 24.0),
                                      static_cast<int>(kSampleRate * 2.0), 20.0, 8000.0);

    {
        const auto from = static_cast<int>(kSampleRate * 2.0);
        const auto to = driveSamples;
        const auto coeff = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 30.0f
                                           / static_cast<float>(kSampleRate));
        float lp = 0.0f;
        double lowEnergy = 0.0, totalEnergy = 0.0;
        for (int k = from; k < to; ++k)
        {
            const auto v = out[static_cast<std::size_t>(k)];
            lp += coeff * (v - lp);
            lowEnergy += static_cast<double>(lp) * lp;
            totalEnergy += static_cast<double>(v) * v;
        }
        result.lowFrequencyEnergyRatio = totalEnergy > 1.0e-12 ? lowEnergy / totalEnergy : 0.0;
    }
    return result;
}

inline double delayTailAfterBypassCycle(int algorithmIndex)
{
    Delay delay;
    delay.prepare(kSampleRate);
    delay.reset();

    DelaySettings s;
    s.enabled = true;
    s.amount = 0.9f;
    s.timeControl = 0.5f;
    s.feedbackControl = 0.9f;
    s.algorithmIndex = algorithmIndex;
    delay.updateForBlock(s);

    auto run = [&](int samples, bool feedAudio)
    {
        double peak = 0.0;
        for (int i = 0; i < samples; ++i)
        {
            const auto in = feedAudio
                ? std::sin(juce::MathConstants<float>::twoPi * 330.0f
                           * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.6f
                : 0.0f;
            float l = 0.0f, r = 0.0f;
            delay.processSampleFrame(in, in, l, r);
            peak = juce::jmax(peak, juce::jmax(std::abs(static_cast<double>(l)),
                                               std::abs(static_cast<double>(r))));
        }
        return peak;
    };

    run(static_cast<int>(kSampleRate * 2.0), true);      // fill it up

    s.enabled = false;
    delay.updateForBlock(s);
    run(static_cast<int>(kSampleRate * 1.0), false);     // bypassed

    s.enabled = true;
    delay.updateForBlock(s);
    return run(static_cast<int>(kSampleRate * 3.0), false);   // silence in: must stay silent
}

inline double delayZeroAmountBleed(int algorithmIndex)
{
    Delay delay;
    delay.prepare(kSampleRate);
    delay.reset();
    DelaySettings s;
    s.enabled = true;
    s.amount = 0.0f;
    s.algorithmIndex = algorithmIndex;
    delay.updateForBlock(s);

    double worst = 0.0;
    for (int i = 0; i < static_cast<int>(kSampleRate * 2.0); ++i)
    {
        const auto phase = juce::MathConstants<float>::twoPi * 220.0f * static_cast<float>(i) / static_cast<float>(kSampleRate);
        const auto in = 0.5f * std::sin(phase);
        float l = 0.0f, r = 0.0f;
        delay.processSampleFrame(in, in, l, r);
        if (i > static_cast<int>(kSampleRate))   // after the smoothers have settled
        {
            worst = juce::jmax(worst, std::abs(static_cast<double>(l) - in));
        }
    }
    return worst;
}

inline ReverbMetrics measureReverb(const ReverbSettings& settings, int totalSamples = 192000)
{
    ::Reverb reverb;
    reverb.prepare(kSampleRate);
    reverb.reset();
    auto wetSettings = settings;
    wetSettings.enabled = true;
    wetSettings.amount = 1.0f;
    reverb.updateForBlock(wetSettings, totalSamples);

    std::vector<float> left, right;
    left.reserve(static_cast<std::size_t>(totalSamples));
    right.reserve(static_cast<std::size_t>(totalSamples));

    // The amount smoother needs to reach unity before the impulse, or the
    // measured onset is a fade rather than the algorithm's own build-up.
    for (int i = 0; i < 4096; ++i)
    {
        float l = 0.0f, r = 0.0f;
        reverb.processSampleFrame(0.0f, 0.0f, l, r);
    }

    for (int i = 0; i < totalSamples; ++i)
    {
        const auto input = i == 0 ? 1.0f : 0.0f;
        float l = 0.0f, r = 0.0f;
        reverb.processSampleFrame(input, input, l, r);
        left.push_back(l);
        right.push_back(r);
    }

    ReverbMetrics m;
    for (const auto v : left) { if (! std::isfinite(v)) m.finite = false; m.peak = juce::jmax(m.peak, std::abs(static_cast<double>(v))); }
    for (const auto v : right) { if (! std::isfinite(v)) m.finite = false; m.peak = juce::jmax(m.peak, std::abs(static_cast<double>(v))); }

    const auto ms = [](double milliseconds) { return static_cast<int>(milliseconds * 0.001 * kSampleRate); };
    m.echoDensityAt20ms = normalisedEchoDensity(left, ms(20), ms(20));
    m.echoDensityAt50ms = normalisedEchoDensity(left, ms(50), ms(20));
    m.echoDensityAt150ms = normalisedEchoDensity(left, ms(150), ms(40));
    m.spectralFlatness = spectralFlatnessOf(left, ms(200));
    m.decayRippleDb = decayRippleDb(left, ms(60), ms(1200));
    m.interChannelCorrelation = interChannelCorrelation(left, right, ms(100), ms(1000));
    m.rt60Seconds = measureRt60(left);

    // Decay-curve nonlinearity, as ISO 3382 defines it for measured rooms: fit
    // a straight line to the Schroeder energy-decay curve over the -5 dB to
    // -35 dB span and report the worst deviation from it. A smooth exponential
    // decay is a straight line on that curve; flutter and lurching show up as
    // curvature. This replaced a hand-rolled ripple measure whose answer
    // depended on where the window happened to start relative to the onset.
    m.lateRippleDb = decayCurveNonlinearityDb(left);

    // Energy balance between the channels for an identical mono input. A
    // decorrelated reverb must still be level-balanced, or panning a source one
    // way makes its reverb appear on the other.
    {
        double energyL = 0.0, energyR = 0.0;
        for (const auto v : left) energyL += static_cast<double>(v) * v;
        for (const auto v : right) energyR += static_cast<double>(v) * v;
        m.channelBalanceDb = 10.0 * std::log10(juce::jmax(1.0e-20, energyL)
                                               / juce::jmax(1.0e-20, energyR));
    }

    return m;
}

inline void reportReverbMetrics(const char* label, const ReverbMetrics& m)
{
    std::printf("  %-26s ED20 %5.2f  ED50 %5.2f  ED150 %5.2f  flat %6.4f  lateRipple %5.2fdB  corr %+5.2f  bal %+5.2fdB  rt60 %5.2fs\n",
                label, m.echoDensityAt20ms, m.echoDensityAt50ms, m.echoDensityAt150ms,
                m.spectralFlatness, m.lateRippleDb, m.interChannelCorrelation, m.channelBalanceDb, m.rt60Seconds);
    std::fflush(stdout);
}

template <typename EffectT, typename SettingsT, typename UpdateFn>
inline double tailAfterBypassCycle(SettingsT settings, UpdateFn update)
{
    EffectT effect;
    effect.prepare(kSampleRate);
    effect.reset();

    auto run = [&](int samples, bool feedAudio)
    {
        double peak = 0.0;
        for (int i = 0; i < samples; ++i)
        {
            const auto in = feedAudio
                ? std::sin(juce::MathConstants<float>::twoPi * 330.0f
                           * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.6f
                : 0.0f;
            float l = 0.0f, r = 0.0f;
            effect.processSampleFrame(in, in, l, r);
            peak = juce::jmax(peak, juce::jmax(std::abs(static_cast<double>(l)),
                                               std::abs(static_cast<double>(r))));
        }
        return peak;
    };

    settings.enabled = true;
    update(effect, settings);
    run(static_cast<int>(kSampleRate * 2.0), true);

    settings.enabled = false;
    update(effect, settings);
    run(static_cast<int>(kSampleRate * 1.5), false);

    settings.enabled = true;
    update(effect, settings);
    return run(static_cast<int>(kSampleRate * 3.0), false);
}

inline double rmsOver(const std::vector<float>& signal, int from, int count)
{
    const auto first = juce::jlimit(0, static_cast<int>(signal.size()), from);
    const auto last = juce::jlimit(first, static_cast<int>(signal.size()), first + count);
    if (last <= first) return 0.0;

    double sum = 0.0;
    for (int i = first; i < last; ++i)
    {
        sum += static_cast<double>(signal[static_cast<std::size_t>(i)])
             * static_cast<double>(signal[static_cast<std::size_t>(i)]);
    }
    return std::sqrt(sum / static_cast<double>(last - first));
}

inline bool allFinite(const std::vector<float>& signal)
{
    for (const auto x : signal)
    {
        if (! std::isfinite(x)) return false;
    }
    return true;
}

namespace doomtest
{
struct Result
{
    std::vector<float> left;
    std::vector<float> right;

    bool finite() const
    {
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            if (! std::isfinite(left[i]) || ! std::isfinite(right[i]))
            {
                return false;
            }
        }
        return true;
    }

    float peak() const
    {
        auto p = 0.0f;
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            p = juce::jmax(p, std::abs(left[i]), std::abs(right[i]));
        }
        return p;
    }

    double rms() const
    {
        if (left.empty())
        {
            return 0.0;
        }
        auto sum = 0.0;
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            sum += static_cast<double>(left[i]) * left[i] + static_cast<double>(right[i]) * right[i];
        }
        return std::sqrt(sum / static_cast<double>(left.size() * 2));
    }

    double dc() const
    {
        if (left.empty())
        {
            return 0.0;
        }
        auto sum = 0.0;
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            sum += static_cast<double>(left[i]) + right[i];
        }
        return sum / static_cast<double>(left.size() * 2);
    }

    // Only the second half, so a measurement is not dominated by the engine
    // filling up.
    double tailRms() const
    {
        const auto half = left.size() / 2;
        if (half == 0)
        {
            return 0.0;
        }
        auto sum = 0.0;
        for (auto i = half; i < left.size(); ++i)
        {
            sum += static_cast<double>(left[i]) * left[i] + static_cast<double>(right[i]) * right[i];
        }
        return std::sqrt(sum / static_cast<double>((left.size() - half) * 2));
    }

    double correlation() const
    {
        auto sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            sxy += static_cast<double>(left[i]) * right[i];
            sxx += static_cast<double>(left[i]) * left[i];
            syy += static_cast<double>(right[i]) * right[i];
        }
        const auto denom = std::sqrt(sxx * syy);
        return denom > 1.0e-12 ? sxy / denom : 1.0;
    }
};

enum class Source { silence, impulse, sine, burst, noise };

// Runs DOOM for `seconds` and returns the wet path. Deterministic: the engine's
// stochastic parts run off a seeded generator.
inline Result runDoom(const px3::DoomUserParameters& settings,
               Source source,
               double seconds,
               double sampleRate = kSampleRate,
               float frequency = 220.0f,
               uint32_t seed = 12345u,
               double listenSeconds = 0.0)
{
    px3::Doom doom;
    doom.prepare(sampleRate);
    doom.setSeed(seed);

    // The looper is always listening, so engaging it captures what has already
    // happened. A test that engages it at t = 0 captures silence - correct
    // behaviour, but it measures nothing. Feed it material first, exactly as a
    // player would.
    if (listenSeconds > 0.0 && settings.loopActive)
    {
        auto listening = settings;
        listening.loopActive = false;
        doom.updateForBlock(listening);

        // Continuous, not bursts. A gapped warm-up ends on a silent bar, and a
        // short loop then correctly captures that silence - which measures
        // nothing and looks exactly like a broken looper.
        auto warmPhase = 0.0;
        const auto warmIncrement = juce::MathConstants<double>::twoPi * frequency / sampleRate;
        const auto warmTotal = static_cast<int>(sampleRate * listenSeconds);
        for (int i = 0; i < warmTotal; ++i)
        {
            const auto in = 0.5f * static_cast<float>(std::sin(warmPhase));
            warmPhase += warmIncrement;
            float l = 0.0f;
            float r = 0.0f;
            doom.processSampleFrame(in, in, l, r);
        }
    }

    doom.updateForBlock(settings);

    const auto total = static_cast<int>(sampleRate * seconds);
    Result result;
    result.left.reserve(static_cast<std::size_t>(total));
    result.right.reserve(static_cast<std::size_t>(total));

    auto phase = 0.0;
    const auto increment = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    for (int i = 0; i < total; ++i)
    {
        auto in = 0.0f;
        switch (source)
        {
            case Source::silence:
                break;
            case Source::impulse:
                // Past the smoothing ramps. At sample 64 the mix smoother is
                // still near zero, so the DRY impulse leaks through and is
                // louder than anything the effect produces.
                in = i == 4096 ? 1.0f : 0.0f;
                break;
            case Source::sine:
                in = 0.5f * static_cast<float>(std::sin(phase));
                break;
            case Source::burst:
                // Notes with gaps: the shape the looper, Burst's onset detector
                // and Cross's envelope follower all actually see in use.
                in = ((i / static_cast<int>(sampleRate * 0.25)) % 2 == 0)
                         ? 0.5f * static_cast<float>(std::sin(phase))
                         : 0.0f;
                break;
            case Source::noise:
                // Unsigned: signed overflow is undefined, and this multiply
                // overflows int by design - UBSan flagged it in both copies.
                in = 0.3f * (static_cast<float>((static_cast<uint32_t>(i) * 1103515245u + 12345u) & 0xFFFFu)
                             / 32768.0f - 1.0f);
                break;
        }
        phase += increment;

        float outL = 0.0f;
        float outR = 0.0f;
        doom.processSampleFrame(in, in, outL, outR);
        result.left.push_back(outL);
        result.right.push_back(outR);
    }

    return result;
}

inline px3::DoomUserParameters audible()
{
    px3::DoomUserParameters s;
    s.enabled = true;
    s.mix = 1.0f;
    return s;
}
} // namespace doomtest

// One entry point per suite. main() calls them in the order the output has
// always been in.
void testBreakpointEnvelope();
void testWavetable();
void testSubOscillator();
void testOscillators();
void testAmpEnvelope();
void testModEnvelopes();
void testLfo();
void testVibe();
void testReverb();
void testComb();
void testCardStyle();
void testCardInner();
void testDelay();
void testMood();
void testEffectIndependence();
void testPresets();
void testIntegration();
void testFxChain();
void testDoom();
void testLucy();
void testChorus();
void testStereoSpread();
void testEnvelopeModes();
void testMacroSystem();
void testMidiMapping();
void testFactoryPresets();
void testEditorLifecycle();
void testVuBallistics();
void testBusInserts();
void testMultiOutput();
void testUpdater();
void testEcosystem();
void testFxProducts();
void testUninstaller();
void testFilters();
void testOscillatorModeRichness();
void testAnalogEngine();
void testEditorLayout();

} // namespace px3tests
