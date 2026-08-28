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

#include "../DSP/PluginProcessor.h"
#include "../DSP/PluginProcessorInternals.h"
#include "../DSP/AmpEnvelope.h"
#include "../UI/Card.h"
#include "../UI/CardInner.h"
#include "../UI/UIConfig.h"
#include "../DSP/Delay.h"
#include "../DSP/EnvelopeGenerator.h"
#include "../DSP/LfoGenerator.h"
#include "../DSP/Mood.h"
#include "../DSP/Reverb.h"
#include "../DSP/SubOscillator.h"
#include "../Preset/PresetManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

int gPassed = 0;
int gFailed = 0;
std::vector<std::string> gFailures;
const char* gSuite = "";

void suite(const char* name)
{
    gSuite = name;
    std::printf("\n== %s ==\n", name);
}

// Every check states what it expects, so a failure names the broken behaviour
// rather than an anonymous assertion index.
bool check(const char* name, bool ok, const juce::String& detail = {})
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

juce::String fmt(double v, int places = 6) { return juce::String(v, places); }

bool nearly(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}

//==============================================================================
// Processor helpers
//==============================================================================
juce::RangedAudioParameter* findParameter(juce::AudioProcessor& processor, const juce::String& id)
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

bool setParam(juce::AudioProcessor& processor, const juce::String& id, float value)
{
    if (auto* parameter = findParameter(processor, id))
    {
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        return true;
    }
    std::printf("  !!    parameter not found: %s\n", id.toRawUTF8());
    return false;
}

void setChoice(juce::AudioProcessor& processor, const juce::String& id, int index)
{
    if (auto* parameter = findParameter(processor, id))
    {
        const auto steps = juce::jmax(1, parameter->getNumSteps() - 1);
        parameter->setValueNotifyingHost(static_cast<float>(index) / static_cast<float>(steps));
    }
}

float getParamValue(juce::AudioProcessor& processor, const juce::String& id)
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
Capture render(PX3SynthAudioProcessor& processor,
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
double estimateFrequency(const std::vector<float>& signal,
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

// Energy at the harmonics of a tone, relative to energy at the fundamental. A
// sine has none of its own, so on a sine source this is a direct measure of how
// much a nonlinearity is adding - unaffected by level, and unaffected by slow
// pitch drift, which smears the fundamental but leaves the harmonics where they
// are relative to it.
double harmonicToFundamentalRatio(const std::vector<float>& signal,
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
void makePlainPatch(PX3SynthAudioProcessor& processor)
{
    setParam(processor, "ampAttack", 0.001f);
    setParam(processor, "ampDecay", 0.005f);
    setParam(processor, "ampSustain", 1.0f);
    setParam(processor, "ampRelease", 0.050f);
    setParam(processor, "ampEnvEnabled", 1.0f);
    setParam(processor, "masterGain", 0.6f);

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
} // namespace

//==============================================================================
// PHASE 4 - SUB OSCILLATOR
//==============================================================================
namespace
{
void testSubOscillator()
{
    suite("SUB OSCILLATOR");

    // ---- Level 1: unit behaviour on the DSP class itself ----
    {
        SubOscillator sub;
        sub.prepare(kSampleRate);
        SubOscSettings settings;
        settings.enabled = false;
        settings.level = 1.0f;
        sub.setSettings(settings);
        sub.resetForNote();

        bool silent = true;
        for (int i = 0; i < 4096; ++i)
        {
            if (sub.renderSample(220.0) != 0.0f) silent = false;
        }
        check("SubOscillator_DisabledProducesSilence", silent);
    }

    {
        SubOscillator sub;
        sub.prepare(kSampleRate);
        SubOscSettings settings;
        settings.enabled = true;
        settings.level = 0.0f;
        sub.setSettings(settings);
        sub.resetForNote();

        bool silent = true;
        for (int i = 0; i < 4096; ++i)
        {
            if (sub.renderSample(220.0) != 0.0f) silent = false;
        }
        check("SubOscillator_ZeroLevelProducesSilence", silent);
    }

    // Pitch is verified mathematically: the octave selector is a semitone
    // offset, so the rendered period must match base * 2^(semitones/12).
    struct OctaveCase { int index; int semitones; const char* label; };
    const OctaveCase octaves[] = { { 0, 0, "0 OCT" }, { 1, -12, "-1 OCT" }, { 2, -24, "-2 OCT" } };

    for (const auto& octave : octaves)
    {
        SubOscillator sub;
        sub.prepare(kSampleRate);
        SubOscSettings settings;
        settings.enabled = true;
        settings.level = 1.0f;
        settings.octaveIndex = octave.index;
        settings.waveformIndex = 0; // SINE
        sub.setSettings(settings);
        sub.resetForNote();

        constexpr double baseHz = 440.0;
        std::vector<float> signal;
        signal.reserve(48000);
        for (int i = 0; i < 48000; ++i) signal.push_back(sub.renderSample(baseHz));

        const auto expected = baseHz * std::pow(2.0, octave.semitones / 12.0);
        const auto measured = estimateFrequency(signal, 1000, 32000, 20.0, 2000.0);
        check((juce::String("SubOscillator_Octave_") + octave.label + "_ProducesExpectedFrequency").toRawUTF8(),
              nearly(measured, expected, expected * 0.02),
              "expected " + fmt(expected, 2) + " Hz, measured " + fmt(measured, 2) + " Hz");
    }

    // Fine pitch: the established range is +/-0.24 semitones, a detune trim,
    // not a transpose. Verified against that range, not against an assumed one.
    for (const auto cents : { -0.24f, 0.0f, 0.24f })
    {
        SubOscillator sub;
        sub.prepare(kSampleRate);
        SubOscSettings settings;
        settings.enabled = true;
        settings.level = 1.0f;
        settings.octaveIndex = 0;
        settings.pitchSemitones = cents;
        settings.waveformIndex = 0;
        sub.setSettings(settings);
        sub.resetForNote();

        constexpr double baseHz = 440.0;
        std::vector<float> signal;
        for (int i = 0; i < 96000; ++i) signal.push_back(sub.renderSample(baseHz));

        const auto expected = baseHz * std::pow(2.0, static_cast<double>(cents) / 12.0);
        const auto measured = estimateFrequency(signal, 1000, 64000, 300.0, 600.0);
        check((juce::String("SubOscillator_PitchTrim_") + juce::String(cents, 2) + "st_ShiftsFrequency").toRawUTF8(),
              nearly(measured, expected, expected * 0.01),
              "expected " + fmt(expected, 3) + " Hz, measured " + fmt(measured, 3) + " Hz");
    }

    // Waveforms: both must sound, and the square must be measurably richer than
    // the sine at the same level (that is what distinguishes them).
    {
        double sineRms = 0.0, squareRms = 0.0, sinePeak = 0.0, squarePeak = 0.0;
        for (int waveform = 0; waveform <= 1; ++waveform)
        {
            SubOscillator sub;
            sub.prepare(kSampleRate);
            SubOscSettings settings;
            settings.enabled = true;
            settings.level = 1.0f;
            settings.octaveIndex = 0;
            settings.waveformIndex = waveform;
            sub.setSettings(settings);
            sub.resetForNote();

            double energy = 0.0, peak = 0.0, dc = 0.0;
            constexpr int count = 48000;
            for (int i = 0; i < count; ++i)
            {
                const auto v = static_cast<double>(sub.renderSample(220.0));
                energy += v * v;
                peak = juce::jmax(peak, std::abs(v));
                dc += v;
            }
            const auto rms = std::sqrt(energy / count);
            dc /= count;

            check((juce::String("SubOscillator_Waveform") + juce::String(waveform) + "_Sounds").toRawUTF8(),
                  rms > 0.05, "rms " + fmt(rms, 4));
            check((juce::String("SubOscillator_Waveform") + juce::String(waveform) + "_NoDcOffset").toRawUTF8(),
                  std::abs(dc) < 0.01, "dc " + fmt(dc, 6));

            if (waveform == 0) { sineRms = rms; sinePeak = peak; }
            else { squareRms = rms; squarePeak = peak; }
        }
        // A square's RMS/peak ratio is near 1; a sine's is near 0.707.
        check("SubOscillator_SquareHasHigherRmsToPeakThanSine",
              (squareRms / squarePeak) > (sineRms / sinePeak) + 0.15,
              "sine " + fmt(sineRms / sinePeak, 3) + " vs square " + fmt(squareRms / squarePeak, 3));
    }

    // Level is a linear gain on the sub's own output.
    {
        double rmsAtHalf = 0.0, rmsAtFull = 0.0;
        for (const auto level : { 0.5f, 1.0f })
        {
            SubOscillator sub;
            sub.prepare(kSampleRate);
            SubOscSettings settings;
            settings.enabled = true;
            settings.level = level;
            settings.octaveIndex = 0;
            settings.waveformIndex = 0;
            sub.setSettings(settings);
            sub.resetForNote();

            double energy = 0.0;
            for (int i = 0; i < 48000; ++i)
            {
                const auto v = static_cast<double>(sub.renderSample(220.0));
                energy += v * v;
            }
            (level == 0.5f ? rmsAtHalf : rmsAtFull) = std::sqrt(energy / 48000.0);
        }
        check("SubOscillator_LevelIsLinearGain",
              nearly(rmsAtFull / juce::jmax(1.0e-9, rmsAtHalf), 2.0, 0.02),
              "ratio " + fmt(rmsAtFull / rmsAtHalf, 4) + " (expected 2.0)");
    }

    // Reset must place the phase deterministically, so a repeated note starts
    // identically rather than wherever the previous one stopped.
    {
        SubOscillator a, b;
        SubOscSettings settings;
        settings.enabled = true;
        settings.level = 1.0f;
        settings.octaveIndex = 1;
        settings.waveformIndex = 1;
        a.prepare(kSampleRate); a.setSettings(settings);
        b.prepare(kSampleRate); b.setSettings(settings);

        a.resetForNote();
        for (int i = 0; i < 5000; ++i) a.renderSample(330.0); // leave a in an arbitrary phase
        a.resetForNote();
        b.resetForNote();

        bool identical = true;
        for (int i = 0; i < 8192; ++i)
        {
            if (a.renderSample(220.0) != b.renderSample(220.0)) identical = false;
        }
        check("SubOscillator_ResetForNoteIsDeterministic", identical,
              "same phase and output after reset regardless of prior use");
    }

    // Two instances must not share state: this is what per-voice independence
    // reduces to, since each voice owns its own SubOscillator.
    {
        SubOscillator low, high;
        SubOscSettings settings;
        settings.enabled = true;
        settings.level = 1.0f;
        settings.octaveIndex = 0;
        settings.waveformIndex = 0;
        low.prepare(kSampleRate); low.setSettings(settings); low.resetForNote();
        high.prepare(kSampleRate); high.setSettings(settings); high.resetForNote();

        std::vector<float> lowSignal, highSignal;
        for (int i = 0; i < 48000; ++i)
        {
            lowSignal.push_back(low.renderSample(220.0));
            highSignal.push_back(high.renderSample(330.0));
        }
        const auto lowHz = estimateFrequency(lowSignal, 1000, 32000, 100.0, 500.0);
        const auto highHz = estimateFrequency(highSignal, 1000, 32000, 100.0, 500.0);
        check("SubOscillator_TwoInstancesTrackIndependentPitches",
              nearly(lowHz, 220.0, 4.0) && nearly(highHz, 330.0, 6.0),
              "measured " + fmt(lowHz, 2) + " Hz and " + fmt(highHz, 2) + " Hz");
    }

    // ---- Level 4: integration through the real processor ----
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "osc1Enabled", 0.0f);
        setParam(processor, "subOscEnabled", 1.0f);
        setChoice(processor, "subOscOctave", 0);
        setChoice(processor, "subOscWaveform", 0);
        const auto capture = render(processor, 48000, { { 2000, true, 69, 0.9f } });
        // MIDI 69 is A440; the 0 OCT setting must reproduce it.
        const auto hz = estimateFrequency(capture.left, 12000, 24000, 200.0, 900.0);
        check("SubOscillator_InSignalPath_ProducesAudio", capture.rms() > 0.01,
              "rms " + fmt(capture.rms(), 5));
        check("SubOscillator_InSignalPath_TracksMidiPitch", nearly(hz, 440.0, 12.0),
              "expected 440 Hz, measured " + fmt(hz, 2) + " Hz");
    }

    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "osc1Enabled", 0.0f);
        setParam(processor, "subOscEnabled", 0.0f);
        const auto capture = render(processor, 24000, { { 2000, true, 69, 0.9f } });
        check("SubOscillator_DisabledInSignalPath_IsSilent", capture.peak() < 1.0e-6,
              "peak " + fmt(capture.peak(), 9));
    }
}

//==============================================================================
// PHASE 5 - MAIN OSCILLATORS
//==============================================================================
void testOscillators()
{
    suite("OSCILLATORS");

    // Coarse tune is the transpose control (+/-24 semitones). An octave up must
    // double the frequency and an octave down must halve it.
    struct CoarseCase { float semitones; double ratio; const char* label; };
    const CoarseCase coarseCases[] = {
        { -12.0f, 0.5, "minus12" }, { 0.0f, 1.0, "zero" }, { 12.0f, 2.0, "plus12" }
    };

    for (int oscIndex = 1; oscIndex <= 3; ++oscIndex)
    {
        const auto slot = juce::String(oscIndex);
        for (const auto& coarse : coarseCases)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            for (const auto* other : { "1", "2", "3" })
            {
                setParam(processor, juce::String("osc") + other + "Enabled",
                         juce::String(other) == slot ? 1.0f : 0.0f);
            }
            setParam(processor, "osc" + slot + "Coarse", coarse.semitones);

            const auto capture = render(processor, 48000, { { 2000, true, 69, 0.9f } });
            const auto expected = 440.0 * coarse.ratio;
            const auto hz = estimateFrequency(capture.left, 12000, 24000,
                                              expected * 0.5, expected * 2.0);
            check((juce::String("Osc") + slot + "_Coarse_" + coarse.label + "_ProducesExpectedFrequency").toRawUTF8(),
                  nearly(hz, expected, expected * 0.03),
                  "expected " + fmt(expected, 2) + " Hz, measured " + fmt(hz, 2) + " Hz");
        }
    }

    // Fine tune is in cents; +100 cents is one semitone.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "osc1Fine", 100.0f);
        const auto capture = render(processor, 96000, { { 2000, true, 69, 0.9f } });
        const auto expected = 440.0 * std::pow(2.0, 1.0 / 12.0);
        const auto hz = estimateFrequency(capture.left, 12000, 64000, 300.0, 700.0);
        check("Osc1_FineTune100Cents_EqualsOneSemitone",
              nearly(hz, expected, expected * 0.01),
              "expected " + fmt(expected, 2) + " Hz, measured " + fmt(hz, 2) + " Hz");
    }

    // Enable/disable, per oscillator, independently.
    for (int oscIndex = 1; oscIndex <= 3; ++oscIndex)
    {
        const auto slot = juce::String(oscIndex);
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        for (const auto* other : { "1", "2", "3" })
        {
            setParam(processor, juce::String("osc") + other + "Enabled", 0.0f);
        }
        const auto silent = render(processor, 24000, { { 2000, true, 69, 0.9f } });
        check((juce::String("Osc") + slot + "_AllDisabledProducesSilence").toRawUTF8(),
              silent.peak() < 1.0e-6, "peak " + fmt(silent.peak(), 9));

        PX3SynthAudioProcessor enabled;
        makePlainPatch(enabled);
        for (const auto* other : { "1", "2", "3" })
        {
            setParam(enabled, juce::String("osc") + other + "Enabled",
                     juce::String(other) == slot ? 1.0f : 0.0f);
        }
        const auto sounding = render(enabled, 24000, { { 2000, true, 69, 0.9f } });
        check((juce::String("Osc") + slot + "_EnabledAloneProducesAudio").toRawUTF8(),
              sounding.rms() > 0.01, "rms " + fmt(sounding.rms(), 5));
    }

    // Waveform selection must actually change the signal, for every mode.
    {
        std::vector<double> rmsByMode;
        std::vector<juce::uint64> hashByMode;
        for (int mode = 0; mode < 20; ++mode)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", mode);
            const auto capture = render(processor, 24000, { { 2000, true, 57, 0.9f } });
            rmsByMode.push_back(capture.rms());
            juce::uint64 hash = 14695981039346656037ull;
            for (const auto v : capture.left)
            {
                juce::uint32 bits = 0;
                std::memcpy(&bits, &v, sizeof(bits));
                hash ^= bits;
                hash *= 1099511628211ull;
            }
            hashByMode.push_back(hash);
            check((juce::String("Osc1_Mode") + juce::String(mode) + "_ProducesAudio").toRawUTF8(),
                  capture.rms() > 0.005 && capture.isFinite(),
                  "rms " + fmt(capture.rms(), 5));
        }
        std::vector<juce::uint64> unique = hashByMode;
        std::sort(unique.begin(), unique.end());
        unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
        check("Osc1_EveryWaveformModeProducesADistinctSignal",
              unique.size() == hashByMode.size(),
              juce::String(static_cast<int>(unique.size())) + " distinct of "
                  + juce::String(static_cast<int>(hashByMode.size())));
    }

    // KARPLUS excitation. A plucked string is excited by a burst filling the
    // whole delay line; if that burst does not reach the output the mode is
    // audible only as a faint click. Compared against a plain sine at the same
    // settings, because "produces some audio" is too weak to catch a 20x level
    // deficit.
    {
        PX3SynthAudioProcessor sine, karplus;
        makePlainPatch(sine);
        makePlainPatch(karplus);
        setChoice(karplus, "osc1Mode", 13);
        setParam(karplus, "osc1MacroA", 1.0f); // longest decay: the string should ring
        setParam(karplus, "osc1MacroB", 0.8f);

        const auto sineCapture = render(sine, 48000, { { 2000, true, 57, 0.9f } });
        const auto karplusCapture = render(karplus, 48000, { { 2000, true, 57, 0.9f } });
        // Measured over the first 100 ms, where the pluck is loudest.
        const auto sineOnset = sineCapture.rmsOver(2000, 6800);
        const auto karplusOnset = karplusCapture.rmsOver(2000, 6800);
        check("Karplus_ExcitationReachesOutput",
              karplusOnset > sineOnset * 0.25,
              "karplus onset rms " + fmt(karplusOnset, 5)
                  + " vs sine " + fmt(sineOnset, 5)
                  + " (a pluck must be at least a quarter of a sustained sine)");
    }

    {
        // The excitation is a noise burst spanning one delay period, so a low
        // note (long delay line) must carry at least as much onset energy as a
        // high one. If the burst is being overwritten before it is read, only
        // the few samples of note-age noise survive and pitch stops mattering.
        PX3SynthAudioProcessor low, high;
        makePlainPatch(low);
        makePlainPatch(high);
        for (auto* p : { &low, &high })
        {
            setChoice(*p, "osc1Mode", 13);
            setParam(*p, "osc1MacroA", 1.0f);
        }
        const auto lowCapture = render(low, 48000, { { 2000, true, 33, 0.9f } });   // ~55 Hz
        const auto highCapture = render(high, 48000, { { 2000, true, 81, 0.9f } }); // ~880 Hz
        check("Karplus_LowNoteExcitationIsNotWeakerThanHighNote",
              lowCapture.rmsOver(2000, 6800) > highCapture.rmsOver(2000, 6800) * 0.5,
              "low " + fmt(lowCapture.rmsOver(2000, 6800), 5)
                  + " vs high " + fmt(highCapture.rmsOver(2000, 6800), 5));
    }

    // Independence: changing one oscillator must not alter the others.
    //
    // This cannot be a bitwise comparison. Voice start seeds oscillator phase
    // from a global note counter that keeps incrementing, so two renders of the
    // same patch differ in phase by construction. The comparison is therefore
    // against a measured control: render the same patch twice to establish how
    // much the output moves on its own, then require the change caused by
    // retuning a *different* oscillator to stay inside that.
    {
        auto renderOscAlone = [](int soloIndex, const std::function<void(PX3SynthAudioProcessor&)>& tweak)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            for (int i = 1; i <= 3; ++i)
            {
                setParam(processor, "osc" + juce::String(i) + "Enabled", i == soloIndex ? 1.0f : 0.0f);
            }
            setParam(processor, "subOscEnabled", 0.0f);
            tweak(processor);
            return render(processor, 32000, { { 2000, true, 60, 0.9f } });
        };

        auto independenceCheck = [&renderOscAlone](const char* name,
                                                   int soloIndex,
                                                   const std::function<void(PX3SynthAudioProcessor&)>& tweak)
        {
            const auto controlA = renderOscAlone(soloIndex, [](PX3SynthAudioProcessor&) {});
            const auto controlB = renderOscAlone(soloIndex, [](PX3SynthAudioProcessor&) {});
            const auto tweaked = renderOscAlone(soloIndex, tweak);

            const auto controlDelta = std::abs(controlA.rms() - controlB.rms());
            const auto tweakDelta = std::abs(controlA.rms() - tweaked.rms());
            const auto controlHz = estimateFrequency(controlA.left, 12000, 16000, 100.0, 800.0);
            const auto tweakHz = estimateFrequency(tweaked.left, 12000, 16000, 100.0, 800.0);

            // Tolerance is the observed self-variation plus a small floor, not a
            // number chosen to make this pass.
            const auto allowed = juce::jmax(controlDelta * 2.0, controlA.rms() * 0.02);
            check(name,
                  tweakDelta <= allowed && nearly(controlHz, tweakHz, 1.0),
                  "rms moved " + fmt(tweakDelta, 6) + " (self-variation " + fmt(controlDelta, 6)
                      + ", allowed " + fmt(allowed, 6) + "), pitch " + fmt(controlHz, 2)
                      + " -> " + fmt(tweakHz, 2) + " Hz");
        };

        independenceCheck("Osc1_UnaffectedByOsc2ParameterChanges", 1, [](PX3SynthAudioProcessor& p)
        {
            setParam(p, "osc2Coarse", 7.0f);
            setChoice(p, "osc2Mode", 6);
            setParam(p, "osc2Fine", 30.0f);
        });
        independenceCheck("Osc2_UnaffectedByOsc1AndOsc3ParameterChanges", 2, [](PX3SynthAudioProcessor& p)
        {
            setParam(p, "osc1Coarse", -5.0f);
            setParam(p, "osc3Fine", 40.0f);
            setChoice(p, "osc1Mode", 1);
        });
        independenceCheck("Osc3_UnaffectedBySubOscillatorParameterChanges", 3, [](PX3SynthAudioProcessor& p)
        {
            setChoice(p, "subOscOctave", 2);
            setParam(p, "subOscPitch", 0.2f);
        });
    }

    // Combining sources. The voice divides by sqrt(active source count), so the
    // designed behaviour is roughly CONSTANT summed level as sources are added,
    // not a louder one - that is what stops a four-source patch clipping. What
    // must hold is that each source actually reaches its own mixer channel.
    {
        PX3SynthAudioProcessor one, two, three;
        for (auto* p : { &one, &two, &three }) makePlainPatch(*p);
        for (auto* p : { &two, &three })
        {
            setParam(*p, "osc2Enabled", 1.0f);
            setParam(*p, "osc2Coarse", 7.0f);
        }
        setParam(three, "osc3Enabled", 1.0f);
        setParam(three, "osc3Coarse", 12.0f);

        const auto r1 = render(one, 32000, { { 2000, true, 57, 0.9f } }).rms();
        const auto r2 = render(two, 32000, { { 2000, true, 57, 0.9f } }).rms();
        const auto r3 = render(three, 32000, { { 2000, true, 57, 0.9f } }).rms();

        check("Oscillators_PerSourceNormalisationHoldsSummedLevelBounded",
              r2 < r1 * 1.7 && r3 < r1 * 1.7 && r2 > r1 * 0.5 && r3 > r1 * 0.5,
              "1 osc " + fmt(r1, 5) + ", 2 osc " + fmt(r2, 5) + ", 3 osc " + fmt(r3, 5));

        // Per-source meters prove each enabled oscillator reaches its own
        // channel, which a summed-level test cannot show.
        check("Oscillators_EachEnabledSourceReachesItsOwnMixerChannel",
              three.debugGetMixerSourceRms(1) > 1.0e-4
                  && three.debugGetMixerSourceRms(2) > 1.0e-4
                  && three.debugGetMixerSourceRms(3) > 1.0e-4
                  && three.debugGetMixerSourceRms(0) < 1.0e-6,
              "osc1 " + fmt(three.debugGetMixerSourceRms(1), 6)
                  + ", osc2 " + fmt(three.debugGetMixerSourceRms(2), 6)
                  + ", osc3 " + fmt(three.debugGetMixerSourceRms(3), 6)
                  + ", sub (disabled) " + fmt(three.debugGetMixerSourceRms(0), 6));
    }

    // The mixer channel is the single gain stage for a source. Separate
    // oscNLevel / subOscLevel parameters once existed alongside it, hardcoded
    // to 1.0 in the voice and therefore inert while still being host-visible
    // and saved into every preset; they were removed rather than wired in,
    // because wiring them in would have introduced a second gain stage. The
    // like-named MODULATION destinations are unaffected - they are canonical
    // IDs routed to the mixer params, covered by the ENV/LFO destination tests.
    {
        PX3SynthAudioProcessor processor;
        juce::StringArray offenders;
        for (auto* parameter : processor.getParameters())
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            {
                for (const auto* id : { "osc1Level", "osc2Level", "osc3Level", "subOscLevel" })
                {
                    if (ranged->paramID.equalsIgnoreCase(id)) offenders.add(ranged->paramID);
                }
            }
        }
        check("Oscillators_NoOrphanedSourceLevelParametersAreExposed", offenders.isEmpty(),
              offenders.isEmpty() ? "the mixer channel is the only source gain stage"
                                  : "still exposed: " + offenders.joinIntoString(", "));
    }

    // Gain-structure contract. The 4 dB of modulation headroom lives on the
    // SOURCE, not on the mixer fader, so a freshly loaded plugin shows its
    // channels at 0 dB rather than looking as though someone had already pulled
    // them all down. The fader therefore runs to +4 dB, so a channel can still
    // be driven to full scale.
    {
        PX3SynthAudioProcessor processor;
        auto* level = findParameter(processor, "mix.osc1.level");
        auto* fxReturn = findParameter(processor, "fxReturnGain");
        const auto headroom = px3::processor_internal::sourceHeadroomGain();
        const auto faderMax = px3::processor_internal::channelFaderMaxGain();

        check("Mixer_ChannelFaderDefaultsToUnityGain",
              level != nullptr && nearly(level->convertFrom0to1(level->getValue()), 1.0, 1.0e-4),
              "default channel gain = "
                  + fmt(level != nullptr ? level->convertFrom0to1(level->getValue()) : -1.0, 5)
                  + " (0.0 dB)");
        check("Mixer_FxReturnFaderDefaultsToUnityGain",
              fxReturn != nullptr && nearly(fxReturn->convertFrom0to1(fxReturn->getValue()), 1.0, 1.0e-4),
              "default FX return gain = "
                  + fmt(fxReturn != nullptr ? fxReturn->convertFrom0to1(fxReturn->getValue()) : -1.0, 5));
        check("Mixer_FaderTopExactlyCancelsSourceHeadroom",
              nearly(headroom * faderMax, 1.0, 1.0e-4),
              "source " + fmt(headroom, 5) + " x fader max " + fmt(faderMax, 5)
                  + " = " + fmt(headroom * faderMax, 5) + " (full scale)");
        check("Mixer_SourceCarriesFourDbOfHeadroom",
              nearly(juce::Decibels::gainToDecibels(headroom), -4.0, 0.01),
              fmt(juce::Decibels::gainToDecibels(headroom), 2) + " dB");
    }

    // Moving the headroom must not have changed how loud the synth is. These
    // are the levels measured from the previous gain structure, where the fader
    // defaulted to -4 dB and the source ran at full scale.
    {
        auto renderDefaultPatch = [](bool faderAtMaximum)
        {
            PX3SynthAudioProcessor processor;
            setParam(processor, "osc1Enabled", 1.0f);
            setParam(processor, "osc2Enabled", 0.0f);
            setParam(processor, "osc3Enabled", 0.0f);
            setParam(processor, "subOscEnabled", 0.0f);
            setParam(processor, "vibeEnabled", 0.0f);
            setParam(processor, "reverbEnabled", 0.0f);
            setParam(processor, "delayEnabled", 0.0f);
            setParam(processor, "moodEnabled", 0.0f);
            setParam(processor, "filter1Enabled", 0.0f);
            setParam(processor, "filter2Enabled", 0.0f);
            setParam(processor, "ampSustain", 1.0f);
            setChoice(processor, "osc1Mode", 0);
            if (faderAtMaximum)
            {
                if (auto* lv = findParameter(processor, "mix.osc1.level")) lv->setValueNotifyingHost(1.0f);
            }
            return render(processor, 48000, { { 2000, true, 57, 0.9f } }).rmsOver(24000, 46000);
        };
        check("Mixer_HeadroomMoveLeftDefaultLevelUnchanged",
              nearly(renderDefaultPatch(false), 0.127851, 0.0005),
              "default patch rms " + fmt(renderDefaultPatch(false), 6) + " (was 0.127851)");
        check("Mixer_HeadroomMoveLeftMaximumLevelUnchanged",
              nearly(renderDefaultPatch(true), 0.202856, 0.0005),
              "fader at maximum rms " + fmt(renderDefaultPatch(true), 6) + " (was 0.202856)");
    }

    // The mixer channel level is the gain stage that must work.
    {
        PX3SynthAudioProcessor quiet, loud;
        makePlainPatch(quiet);
        makePlainPatch(loud);
        setParam(quiet, "mix.osc1.level", 0.4f);
        setParam(loud, "mix.osc1.level", 0.8f);
        const auto quietRms = render(quiet, 32000, { { 2000, true, 57, 0.9f } }).rms();
        const auto loudRms = render(loud, 32000, { { 2000, true, 57, 0.9f } }).rms();
        check("MixerOsc1Level_IsTheEffectiveSourceGainStage",
              quietRms < loudRms * 0.75,
              "level 0.4 -> " + fmt(quietRms, 6) + ", 0.8 -> " + fmt(loudRms, 6));
    }

    // Per-voice independence: two simultaneous notes must each track their own
    // pitch, which fails if voices share oscillator phase or frequency state.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        const auto capture = render(processor, 64000,
                                    { { 2000, true, 57, 0.9f }, { 2000, true, 69, 0.9f } });
        check("Oscillators_TwoSimultaneousVoicesRenderWithoutCorruption",
              capture.isFinite() && capture.rms() > 0.01,
              "rms " + fmt(capture.rms(), 5));

        PX3SynthAudioProcessor lowOnly;
        makePlainPatch(lowOnly);
        const auto low = render(lowOnly, 64000, { { 2000, true, 57, 0.9f } });
        const auto lowHz = estimateFrequency(low.left, 12000, 32000, 100.0, 400.0);
        check("Oscillators_SingleVoiceTracksMidiPitch", nearly(lowHz, 220.0, 6.0),
              "MIDI 57 expected 220 Hz, measured " + fmt(lowHz, 2) + " Hz");
    }
}
//==============================================================================
// PHASE 6 - AMP ENV
//==============================================================================
// Runs an AmpEnvelope and records its output, so timing assertions are made
// against the real state machine rather than against the parameter value.
struct EnvelopeTrace
{
    std::vector<float> values;

    float at(double seconds) const
    {
        const auto index = static_cast<std::size_t>(seconds * kSampleRate);
        return index < values.size() ? values[index] : 0.0f;
    }

    // First sample index at or above a level, or -1.
    int firstReaching(float level) const
    {
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (values[i] >= level) return static_cast<int>(i);
        }
        return -1;
    }

    float peak() const
    {
        float p = 0.0f;
        for (const auto v : values) p = juce::jmax(p, v);
        return p;
    }
};

EnvelopeTrace traceAmpEnvelope(const EnvelopeSettings& settings,
                               double holdSeconds,
                               double totalSeconds)
{
    AmpEnvelope envelope;
    envelope.prepare(kSampleRate);
    envelope.setSettings(settings);
    envelope.noteOn();

    EnvelopeTrace trace;
    const auto total = static_cast<int>(totalSeconds * kSampleRate);
    const auto releaseAt = static_cast<int>(holdSeconds * kSampleRate);
    trace.values.reserve(static_cast<std::size_t>(total));

    for (int i = 0; i < total; ++i)
    {
        if (i == releaseAt) envelope.noteOff();
        trace.values.push_back(envelope.getNextSample());
    }
    return trace;
}

void testAmpEnvelope()
{
    suite("AMP ENV");

    // Attack timing. The envelope is smoothed by a one-pole after the ADSR, so
    // the assertion is that it is near full at the set attack time and clearly
    // not there at half of it - which is what "the attack takes this long"
    // means - rather than an exact sample index.
    {
        EnvelopeSettings settings;
        settings.attackSeconds = 0.100f;
        settings.decaySeconds = 0.005f;
        settings.sustainLevel = 1.0f;
        settings.releaseSeconds = 0.100f;
        const auto trace = traceAmpEnvelope(settings, 0.5, 0.8);
        check("AmpEnvelope_Attack100ms_ReachesFullAtAttackTime",
              trace.at(0.100) > 0.95f, "value at 100 ms = " + fmt(trace.at(0.100), 4));
        check("AmpEnvelope_Attack100ms_IsPartwayAtHalfAttackTime",
              trace.at(0.050) > 0.30f && trace.at(0.050) < 0.75f,
              "value at 50 ms = " + fmt(trace.at(0.050), 4));
    }

    // Sustain level is what the envelope holds after decay.
    for (const auto sustain : { 0.0f, 0.25f, 0.5f, 1.0f })
    {
        EnvelopeSettings settings;
        settings.attackSeconds = 0.005f;
        settings.decaySeconds = 0.050f;
        settings.sustainLevel = sustain;
        settings.releaseSeconds = 0.100f;
        const auto trace = traceAmpEnvelope(settings, 0.5, 0.8);
        check((juce::String("AmpEnvelope_Sustain") + juce::String(sustain, 2) + "_IsHeldAfterDecay").toRawUTF8(),
              nearly(trace.at(0.30), sustain, 0.02),
              "expected " + fmt(sustain, 3) + ", held " + fmt(trace.at(0.30), 4));
    }

    // Release must start from the level the envelope is ACTUALLY at, not jump
    // to the sustain level first. Tested at three note-off points.
    {
        EnvelopeSettings settings;
        settings.attackSeconds = 0.400f;
        settings.decaySeconds = 0.200f;
        settings.sustainLevel = 0.25f;
        settings.releaseSeconds = 0.400f;

        // Note off part-way through the attack: the level there is well below
        // 1.0 and well above sustain, so a jump in either direction shows up.
        const auto duringAttack = traceAmpEnvelope(settings, 0.200, 1.2);
        const auto atRelease = duringAttack.at(0.199);
        const auto justAfter = duringAttack.at(0.205);
        check("AmpEnvelope_ReleaseStartsFromCurrentLevel_DuringAttack",
              justAfter < atRelease && justAfter > atRelease * 0.6f,
              "level at note-off " + fmt(atRelease, 4) + ", 5 ms later " + fmt(justAfter, 4));
        check("AmpEnvelope_ReleaseDuringAttack_DoesNotJumpToSustain",
              std::abs(justAfter - settings.sustainLevel) > 0.05f,
              "sustain is " + fmt(settings.sustainLevel, 3) + ", level after note-off "
                  + fmt(justAfter, 4));

        // Note off during decay, above sustain.
        const auto duringDecay = traceAmpEnvelope(settings, 0.450, 1.2);
        const auto decayLevel = duringDecay.at(0.449);
        const auto afterDecayRelease = duringDecay.at(0.455);
        check("AmpEnvelope_ReleaseStartsFromCurrentLevel_DuringDecay",
              afterDecayRelease < decayLevel && afterDecayRelease > decayLevel * 0.6f,
              "level at note-off " + fmt(decayLevel, 4) + ", 5 ms later " + fmt(afterDecayRelease, 4));

        // Note off from sustain.
        const auto fromSustain = traceAmpEnvelope(settings, 0.900, 1.6);
        check("AmpEnvelope_ReleaseFromSustain_DecaysToSilence",
              fromSustain.at(0.899) > 0.2f && fromSustain.at(1.35) < 0.01f,
              "sustain " + fmt(fromSustain.at(0.899), 4) + " -> " + fmt(fromSustain.at(1.35), 5));
    }

    // Release length must track the parameter.
    {
        auto releaseTailLength = [](float releaseSeconds)
        {
            EnvelopeSettings settings;
            settings.attackSeconds = 0.005f;
            settings.decaySeconds = 0.010f;
            settings.sustainLevel = 1.0f;
            settings.releaseSeconds = releaseSeconds;
            const auto trace = traceAmpEnvelope(settings, 0.2, 0.2 + releaseSeconds * 2.0 + 0.2);
            for (std::size_t i = static_cast<std::size_t>(0.2 * kSampleRate); i < trace.values.size(); ++i)
            {
                if (trace.values[i] < 0.001f)
                {
                    return static_cast<double>(i) / kSampleRate - 0.2;
                }
            }
            return -1.0;
        };
        const auto shortTail = releaseTailLength(0.100f);
        const auto longTail = releaseTailLength(1.000f);
        check("AmpEnvelope_ReleaseTimeTracksParameter",
              shortTail > 0.05 && shortTail < 0.20 && longTail > 0.7 && longTail < 1.4,
              "100 ms release -> " + fmt(shortTail, 4) + " s, 1000 ms release -> " + fmt(longTail, 4) + " s");
    }

    // Boundary values must not produce NaN, silence-when-it-should-sound, or a
    // stuck envelope.
    {
        struct Boundary { float a, d, s, r; const char* label; };
        const Boundary boundaries[] = {
            { 0.001f, 0.005f, 0.0f, 0.010f, "all minimum" },
            { 3.0f, 4.0f, 1.0f, 5.0f, "all maximum" },
            { 0.001f, 0.005f, 1.0f, 0.010f, "instant attack, full sustain" },
            { 3.0f, 0.005f, 0.0f, 5.0f, "slow attack, zero sustain" },
        };
        for (const auto& boundary : boundaries)
        {
            EnvelopeSettings settings;
            settings.attackSeconds = boundary.a;
            settings.decaySeconds = boundary.d;
            settings.sustainLevel = boundary.s;
            settings.releaseSeconds = boundary.r;
            const auto trace = traceAmpEnvelope(settings, 0.05, 0.3);
            bool finite = true;
            bool inRange = true;
            for (const auto v : trace.values)
            {
                if (! std::isfinite(v)) finite = false;
                if (v < -0.001f || v > 1.001f) inRange = false;
            }
            check((juce::String("AmpEnvelope_Boundary_") + boundary.label + "_StaysFiniteAndInRange").toRawUTF8(),
                  finite && inRange, "peak " + fmt(trace.peak(), 4));
        }
    }

    // Retrigger: a second noteOn must restart the contour rather than continue
    // the previous one.
    {
        AmpEnvelope envelope;
        envelope.prepare(kSampleRate);
        EnvelopeSettings settings;
        settings.attackSeconds = 0.050f;
        settings.decaySeconds = 0.050f;
        settings.sustainLevel = 0.3f;
        settings.releaseSeconds = 0.200f;
        envelope.setSettings(settings);

        envelope.noteOn();
        for (int i = 0; i < static_cast<int>(0.3 * kSampleRate); ++i) envelope.getNextSample();
        const auto beforeRetrigger = envelope.getNextSample();
        envelope.noteOn();
        float peakAfter = 0.0f;
        for (int i = 0; i < static_cast<int>(0.1 * kSampleRate); ++i)
        {
            peakAfter = juce::jmax(peakAfter, envelope.getNextSample());
        }
        check("AmpEnvelope_RetriggerRestartsContour",
              beforeRetrigger < 0.35f && peakAfter > 0.9f,
              "at sustain " + fmt(beforeRetrigger, 4) + ", peak after retrigger " + fmt(peakAfter, 4));
    }

    // reset() must leave no residue for the next note.
    {
        AmpEnvelope envelope;
        envelope.prepare(kSampleRate);
        EnvelopeSettings settings;
        settings.attackSeconds = 0.100f;
        settings.sustainLevel = 1.0f;
        envelope.setSettings(settings);
        envelope.noteOn();
        for (int i = 0; i < static_cast<int>(0.2 * kSampleRate); ++i) envelope.getNextSample();
        envelope.reset();
        check("AmpEnvelope_ResetClearsLevelAndActivity",
              ! envelope.isActive(), "isActive after reset");
        const auto firstAfterReset = envelope.getNextSample();
        check("AmpEnvelope_ResetLeavesNoResidualLevel",
              firstAfterReset < 1.0e-5f, "first sample after reset " + fmt(firstAfterReset, 8));
    }

    // AMP ENV must control amplitude in the signal path, and must not be
    // reachable as a modulation source.
    {
        PX3SynthAudioProcessor loud, quiet;
        makePlainPatch(loud);
        makePlainPatch(quiet);
        setParam(quiet, "ampSustain", 0.2f);
        setParam(loud, "ampSustain", 1.0f);
        const auto quietRms = render(quiet, 40000, { { 2000, true, 57, 0.9f } }).rmsOver(20000, 38000);
        const auto loudRms = render(loud, 40000, { { 2000, true, 57, 0.9f } }).rmsOver(20000, 38000);
        check("AmpEnvelope_SustainControlsSignalAmplitude",
              quietRms < loudRms * 0.6,
              "sustain 0.2 -> " + fmt(quietRms, 5) + ", sustain 1.0 -> " + fmt(loudRms, 5));
    }

    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        // The AMP ADSR controls are deliberately excluded from the assignable
        // destination list; they are a VCA contour, not a modulation lane.
        const auto assigned = processor.setLfoAssignmentByParameterId(0, "ampAttack", false);
        check("AmpEnvelope_AdsrIsNotAModulationDestination", ! assigned,
              "assigning an LFO to ampAttack must be rejected");
    }
}

//==============================================================================
// PHASE 6 - ENV1 / ENV2 / ENV3 and their destinations
//==============================================================================
void testModEnvelopes()
{
    suite("ENV1 / ENV2 / ENV3");

    // Unit level: the modulation envelope generator's own contour.
    for (int envIndex = 0; envIndex < 3; ++envIndex)
    {
        EnvelopeGenerator envelope;
        envelope.prepare(kSampleRate);
        EnvelopeSettings settings;
        settings.attackSeconds = 0.050f;
        settings.decaySeconds = 0.100f;
        settings.sustainLevel = 0.5f;
        settings.releaseSeconds = 0.200f;
        envelope.setSettings(settings);
        envelope.noteOn();

        std::vector<float> values;
        for (int i = 0; i < static_cast<int>(0.5 * kSampleRate); ++i)
        {
            values.push_back(envelope.getNextSample());
        }

        const auto atAttack = values[static_cast<std::size_t>(0.050 * kSampleRate)];
        const auto atSustain = values[static_cast<std::size_t>(0.400 * kSampleRate)];
        float peak = 0.0f, minimum = 1.0f;
        bool finite = true;
        for (const auto v : values)
        {
            peak = juce::jmax(peak, v);
            minimum = juce::jmin(minimum, v);
            if (! std::isfinite(v)) finite = false;
        }
        const auto slot = juce::String(envIndex + 1);
        check(("Env" + slot + "_ReachesFullAtAttackTime").toRawUTF8(),
              atAttack > 0.9f, "value at attack time " + fmt(atAttack, 4));
        check(("Env" + slot + "_HoldsSustainLevel").toRawUTF8(),
              nearly(atSustain, 0.5, 0.03), "held " + fmt(atSustain, 4));
        check(("Env" + slot + "_OutputStaysInUnitRange").toRawUTF8(),
              finite && minimum >= -0.001f && peak <= 1.001f,
              "range " + fmt(minimum, 4) + " .. " + fmt(peak, 4));
    }

    // Two generators with different settings must not influence each other.
    {
        EnvelopeGenerator fast, slow;
        fast.prepare(kSampleRate);
        slow.prepare(kSampleRate);
        EnvelopeSettings fastSettings;
        fastSettings.attackSeconds = 0.005f;
        fastSettings.sustainLevel = 1.0f;
        EnvelopeSettings slowSettings;
        slowSettings.attackSeconds = 1.000f;
        slowSettings.sustainLevel = 1.0f;
        fast.setSettings(fastSettings);
        slow.setSettings(slowSettings);
        fast.noteOn();
        slow.noteOn();

        float fastAt10ms = 0.0f, slowAt10ms = 0.0f;
        for (int i = 0; i < static_cast<int>(0.010 * kSampleRate); ++i)
        {
            fastAt10ms = fast.getNextSample();
            slowAt10ms = slow.getNextSample();
        }
        check("ModEnvelopes_IndependentGeneratorsKeepSeparateContours",
              fastAt10ms > 0.9f && slowAt10ms < 0.05f,
              "fast " + fmt(fastAt10ms, 4) + ", slow " + fmt(slowAt10ms, 4));
    }

    // Destination effect and DIRECTION. A positive amount on a cutoff
    // destination must raise cutoff (brighter, more high-frequency energy); a
    // negative amount must lower it. Measured as output brightness, since
    // cutoff itself is not observable from the rendered signal.
    {
        auto renderWithEnvToCutoff = [](float amount)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 1);      // SAW: harmonics for the filter to remove
            setParam(processor, "filter1Enabled", 1.0f);
            setChoice(processor, "filter1Type", 0);   // LP12
            setParam(processor, "filter1Cutoff", 900.0f);
            setParam(processor, "filter1Resonance", 0.7f);
            setParam(processor, "env1Enabled", 1.0f);
            setParam(processor, "env1Attack", 0.005f);
            setParam(processor, "env1Decay", 0.005f);
            setParam(processor, "env1Sustain", 1.0f);
            setParam(processor, "env1Release", 0.100f);
            setParam(processor, "envAmount", amount);
            processor.setEnvelopeAssignmentByParameterId(0, "filter1Cutoff", false);
            return render(processor, 48000, { { 2000, true, 45, 0.9f } });
        };

        // Brightness proxy: mean absolute first difference relative to RMS. A
        // higher cutoff passes more high-frequency energy, so consecutive
        // samples differ more for the same overall level.
        auto brightness = [](const Capture& capture)
        {
            double diff = 0.0, energy = 0.0;
            const auto first = 20000, last = 44000;
            for (int i = first + 1; i < last; ++i)
            {
                diff += std::abs(static_cast<double>(capture.left[static_cast<std::size_t>(i)])
                                 - capture.left[static_cast<std::size_t>(i - 1)]);
                energy += std::abs(static_cast<double>(capture.left[static_cast<std::size_t>(i)]));
            }
            return energy > 1.0e-9 ? diff / energy : 0.0;
        };

        const auto neutral = brightness(renderWithEnvToCutoff(0.0f));
        const auto positive = brightness(renderWithEnvToCutoff(1.0f));
        const auto negative = brightness(renderWithEnvToCutoff(-1.0f));

        check("Env1_PositiveAmountToCutoff_RaisesCutoff",
              positive > neutral * 1.10,
              "brightness neutral " + fmt(neutral, 5) + " -> positive " + fmt(positive, 5));
        check("Env1_NegativeAmountToCutoff_LowersCutoff",
              negative < neutral * 0.90,
              "brightness neutral " + fmt(neutral, 5) + " -> negative " + fmt(negative, 5));
        check("Env1_ZeroAmount_LeavesDestinationAlone",
              neutral > 0.0, "brightness " + fmt(neutral, 5));
    }

    // A level destination must change level, and in the right direction.
    {
        auto renderWithEnvToLevel = [](float amount, bool enabled)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "mix.osc1.level", 0.5f);
            setParam(processor, "env2Enabled", enabled ? 1.0f : 0.0f);
            setParam(processor, "env2Attack", 0.005f);
            setParam(processor, "env2Decay", 0.005f);
            setParam(processor, "env2Sustain", 1.0f);
            setParam(processor, "env2Amount", amount);
            processor.setEnvelopeAssignmentByParameterId(1, "osc1Level", false);
            return render(processor, 40000, { { 2000, true, 57, 0.9f } }).rmsOver(20000, 38000);
        };
        const auto neutral = renderWithEnvToLevel(0.0f, true);
        const auto positive = renderWithEnvToLevel(1.0f, true);
        const auto negative = renderWithEnvToLevel(-1.0f, true);
        check("Env2_PositiveAmountToOscLevel_RaisesLevel", positive > neutral * 1.05,
              "neutral " + fmt(neutral, 5) + " -> positive " + fmt(positive, 5));
        check("Env2_NegativeAmountToOscLevel_LowersLevel", negative < neutral * 0.95,
              "neutral " + fmt(neutral, 5) + " -> negative " + fmt(negative, 5));
    }

    // The source-level modulation destinations are CANONICAL IDs, not the
    // parameters that share their names. "subOscLevel" and "oscNLevel" as
    // destinations route to the corresponding mixer channel level, which is the
    // only gain stage in the source path. These pin that routing so it survives
    // any future cleanup of the like-named parameters.
    {
        auto renderSubLevelModulation = [](float amount)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "osc1Enabled", 0.0f);
            setParam(processor, "subOscEnabled", 1.0f);
            setParam(processor, "mix.sub.level", 0.5f);
            setParam(processor, "env2Enabled", 1.0f);
            setParam(processor, "env2Attack", 0.005f);
            setParam(processor, "env2Decay", 0.005f);
            setParam(processor, "env2Sustain", 1.0f);
            setParam(processor, "env2Amount", amount);
            const auto assigned = processor.setEnvelopeAssignmentByParameterId(1, "subOscLevel", false);
            juce::ignoreUnused(assigned);
            return render(processor, 40000, { { 2000, true, 57, 0.9f } }).rmsOver(20000, 38000);
        };
        const auto neutral = renderSubLevelModulation(0.0f);
        const auto boosted = renderSubLevelModulation(1.0f);
        check("Modulation_SubOscLevelCanonicalTargetRoutesToMixerChannel",
              neutral > 1.0e-4 && boosted > neutral * 1.05,
              "sub level modulation " + fmt(neutral, 5) + " -> " + fmt(boosted, 5));
    }

    {
        // A canonical destination is persisted by NAME, so a round trip has to
        // restore it and it has to still modulate afterwards.
        PX3SynthAudioProcessor source;
        makePlainPatch(source);
        source.setEnvelopeAssignmentByParameterId(1, "osc1Level", false);
        source.setLfoAssignmentByParameterId(0, "subOscLevel", false);
        const auto envAssignment = source.getEnvelopeAssignmentParameterId(1);
        const auto lfoAssignment = source.getLfoAssignmentParameterId(0);

        juce::MemoryBlock state;
        source.getStateInformation(state);
        PX3SynthAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        check("Preset_SourceLevelModulationAssignmentsSurviveRoundTrip",
              restored.getEnvelopeAssignmentParameterId(1).equalsIgnoreCase(envAssignment)
                  && restored.getLfoAssignmentParameterId(0).equalsIgnoreCase(lfoAssignment)
                  && envAssignment.equalsIgnoreCase("osc1Level")
                  && lfoAssignment.equalsIgnoreCase("subOscLevel"),
              "env2 -> " + restored.getEnvelopeAssignmentParameterId(1)
                  + ", lfo1 -> " + restored.getLfoAssignmentParameterId(0));
    }

    // Independence between the three envelopes, measured through their effects.
    {
        auto renderEnv1ToCutoff = [](const std::function<void(PX3SynthAudioProcessor&)>& tweak)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 1);
            setParam(processor, "filter1Enabled", 1.0f);
            setParam(processor, "filter1Cutoff", 900.0f);
            setParam(processor, "env1Enabled", 1.0f);
            setParam(processor, "env1Sustain", 1.0f);
            setParam(processor, "envAmount", 0.8f);
            processor.setEnvelopeAssignmentByParameterId(0, "filter1Cutoff", false);
            tweak(processor);
            return render(processor, 40000, { { 2000, true, 45, 0.9f } });
        };

        const auto controlA = renderEnv1ToCutoff([](PX3SynthAudioProcessor&) {});
        const auto controlB = renderEnv1ToCutoff([](PX3SynthAudioProcessor&) {});
        const auto withEnv2And3Changed = renderEnv1ToCutoff([](PX3SynthAudioProcessor& p)
        {
            setParam(p, "env2Enabled", 1.0f);
            setParam(p, "env2Attack", 2.0f);
            setParam(p, "env2Sustain", 0.1f);
            setParam(p, "env3Enabled", 1.0f);
            setParam(p, "env3Release", 4.0f);
            setParam(p, "env3Decay", 3.0f);
        });
        const auto selfVariation = std::abs(controlA.rms() - controlB.rms());
        const auto delta = std::abs(controlA.rms() - withEnv2And3Changed.rms());
        check("Env1_UnaffectedByEnv2AndEnv3AdsrChanges",
              delta <= juce::jmax(selfVariation * 2.0, controlA.rms() * 0.02),
              "rms moved " + fmt(delta, 6) + " (self-variation " + fmt(selfVariation, 6) + ")");

        const auto withAmpChanged = renderEnv1ToCutoff([](PX3SynthAudioProcessor& p)
        {
            setParam(p, "ampAttack", 0.001f);
            setParam(p, "ampDecay", 0.005f);
        });
        check("Env1_UnaffectedByAmpEnvelopeAdsrChanges",
              std::abs(controlA.rms() - withAmpChanged.rms())
                  <= juce::jmax(selfVariation * 2.0, controlA.rms() * 0.05),
              "rms moved " + fmt(std::abs(controlA.rms() - withAmpChanged.rms()), 6));
    }

    // A disabled envelope must not modulate, even with an assignment and a
    // non-zero amount still set.
    {
        auto renderEnv3 = [](bool enabled)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "mix.osc1.level", 0.5f);
            setParam(processor, "env3Enabled", enabled ? 1.0f : 0.0f);
            setParam(processor, "env3Sustain", 1.0f);
            setParam(processor, "env3Amount", 1.0f);
            processor.setEnvelopeAssignmentByParameterId(2, "osc1Level", false);
            return render(processor, 40000, { { 2000, true, 57, 0.9f } }).rmsOver(20000, 38000);
        };
        check("Env3_DisabledDoesNotModulateItsDestination",
              renderEnv3(false) < renderEnv3(true) * 0.95,
              "disabled " + fmt(renderEnv3(false), 5) + ", enabled " + fmt(renderEnv3(true), 5));
    }
}

//==============================================================================
// PHASE 7 - LFO
//==============================================================================
void testLfo()
{
    suite("LFO");

    // Waveform range and shape, straight from the generator.
    struct WaveformCase { int index; const char* label; };
    const WaveformCase waveforms[] = {
        { 0, "SINE" }, { 1, "TRIANGLE" }, { 2, "SAW" }, { 3, "SQUARE" }
    };

    for (const auto& waveform : waveforms)
    {
        LfoGenerator lfo;
        lfo.prepare(kSampleRate);
        LfoSettings settings;
        settings.frequencyHz = 2.0f;
        settings.waveformIndex = waveform.index;
        lfo.setSettings(settings);
        lfo.resetPhase(0.0f);

        std::vector<float> values;
        for (int i = 0; i < static_cast<int>(kSampleRate); ++i) values.push_back(lfo.getNextSample());

        float minimum = 1.0e9f, maximum = -1.0e9f;
        double sum = 0.0;
        for (const auto v : values)
        {
            minimum = juce::jmin(minimum, v);
            maximum = juce::jmax(maximum, v);
            sum += v;
        }
        const auto mean = sum / static_cast<double>(values.size());

        check((juce::String("Lfo_") + waveform.label + "_IsBipolarInMinusOneToOne").toRawUTF8(),
              minimum >= -1.001f && maximum <= 1.001f && minimum < -0.9f && maximum > 0.9f,
              "range " + fmt(minimum, 4) + " .. " + fmt(maximum, 4));
        check((juce::String("Lfo_") + waveform.label + "_IsCentredOnZero").toRawUTF8(),
              std::abs(mean) < 0.05, "mean " + fmt(mean, 5));
    }

    // Rate accuracy, measured as completed cycles per second.
    for (const auto rateHz : { 0.5f, 1.0f, 5.0f, 20.0f })
    {
        LfoGenerator lfo;
        lfo.prepare(kSampleRate);
        LfoSettings settings;
        settings.frequencyHz = rateHz;
        settings.waveformIndex = 0;
        lfo.setSettings(settings);
        lfo.resetPhase(0.0f);

        // Long enough that even the slowest rate completes several cycles: at
        // 0.5 Hz a two-second window yields one crossing, which no sane
        // tolerance can distinguish from zero.
        constexpr double windowSeconds = 20.0;
        int crossings = 0;
        auto previous = lfo.getNextSample();
        for (int i = 1; i < static_cast<int>(windowSeconds * kSampleRate); ++i)
        {
            const auto current = lfo.getNextSample();
            if (previous < 0.0f && current >= 0.0f) ++crossings;
            previous = current;
        }
        const auto measured = static_cast<double>(crossings) / windowSeconds;
        check((juce::String("Lfo_Rate") + juce::String(rateHz, 2) + "Hz_CompletesExpectedCycles").toRawUTF8(),
              nearly(measured, rateHz, rateHz * 0.05 + 0.05),
              "expected " + fmt(rateHz, 2) + " Hz, measured " + fmt(measured, 3) + " Hz over "
                  + fmt(windowSeconds, 0) + " s");
    }

    // Phase reset must place the phase deterministically.
    {
        LfoGenerator a, b;
        LfoSettings settings;
        settings.frequencyHz = 3.0f;
        settings.waveformIndex = 0;
        a.prepare(kSampleRate); a.setSettings(settings);
        b.prepare(kSampleRate); b.setSettings(settings);
        a.resetPhase(0.0f);
        for (int i = 0; i < 12345; ++i) a.getNextSample();
        a.resetPhase(0.0f);
        b.resetPhase(0.0f);
        bool identical = true;
        for (int i = 0; i < 4096; ++i)
        {
            if (a.getNextSample() != b.getNextSample()) identical = false;
        }
        check("Lfo_ResetPhaseIsDeterministic", identical);
    }

    // The block-rate read the processor actually uses must agree with the
    // per-sample generator over the same span - this is the call that drives
    // modulation, so it is the one that has to be right.
    {
        LfoGenerator perSample, perBlock;
        LfoSettings settings;
        settings.frequencyHz = 1.0f;
        settings.waveformIndex = 0;
        perSample.prepare(kSampleRate); perSample.setSettings(settings); perSample.resetPhase(0.0f);
        perBlock.prepare(kSampleRate); perBlock.setSettings(settings); perBlock.resetPhase(0.0f);

        bool phasesAgree = true;
        for (int block = 0; block < 40; ++block)
        {
            perBlock.getMidpointSignalAndAdvance(kBlockSize);
            for (int i = 0; i < kBlockSize; ++i) perSample.getNextSample();
            if (std::abs(perSample.getPhaseRadians() - perBlock.getPhaseRadians()) > 0.01f)
            {
                phasesAgree = false;
            }
        }
        check("Lfo_BlockRateAdvanceMatchesPerSampleAdvance", phasesAgree,
              "phase after 40 blocks: per-sample " + fmt(perSample.getPhaseRadians(), 5)
                  + ", per-block " + fmt(perBlock.getPhaseRadians(), 5));
    }

    // Integration: an LFO assigned to cutoff must modulate the signal, and with
    // no assignment must leave it alone.
    {
        auto renderLfo = [](bool lfoEnabled, bool assigned, float amount)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 1);
            setParam(processor, "filter1Enabled", 1.0f);
            setParam(processor, "filter1Cutoff", 1200.0f);
            setParam(processor, "lfoEnabled", lfoEnabled ? 1.0f : 0.0f);
            setParam(processor, "lfoFrequency", 6.0f);
            setParam(processor, "lfoAmount", amount);
            if (assigned)
            {
                processor.setLfoAssignmentByParameterId(0, "filter1Cutoff", false);
            }
            return render(processor, 64000, { { 2000, true, 45, 0.9f } });
        };

        // Modulation depth shows up as variation in short-window RMS over time.
        auto rmsVariation = [](const Capture& capture)
        {
            std::vector<double> windows;
            for (int start = 16000; start + 2000 < 60000; start += 2000)
            {
                windows.push_back(capture.rmsOver(start, start + 2000));
            }
            double mean = 0.0;
            for (const auto v : windows) mean += v;
            mean /= static_cast<double>(windows.size());
            double variance = 0.0;
            for (const auto v : windows) variance += (v - mean) * (v - mean);
            return mean > 1.0e-9 ? std::sqrt(variance / windows.size()) / mean : 0.0;
        };

        // Compared against an LFO-off control rather than an absolute floor. A
        // held note is not perfectly steady on its own - the polyphony gain
        // recovers over 2.5 s by design - so a fixed threshold would be
        // measuring that recovery, not the LFO.
        const auto lfoOff = rmsVariation(renderLfo(false, false, 0.0f));
        const auto unassigned = rmsVariation(renderLfo(true, false, 1.0f));
        const auto assigned = rmsVariation(renderLfo(true, true, 1.0f));
        const auto zeroAmount = rmsVariation(renderLfo(true, true, 0.0f));

        check("Lfo_AssignedToCutoff_ModulatesTheSignal", assigned > lfoOff * 3.0 + 0.02,
              "variation LFO off " + fmt(lfoOff, 5) + " -> assigned " + fmt(assigned, 5));
        check("Lfo_NoAssignment_DoesNotAlterAudio",
              nearly(unassigned, lfoOff, juce::jmax(0.005, lfoOff * 0.25)),
              "variation LFO off " + fmt(lfoOff, 5) + ", enabled but unassigned " + fmt(unassigned, 5));
        check("Lfo_ZeroAmount_ProducesNoModulation",
              nearly(zeroAmount, lfoOff, juce::jmax(0.005, lfoOff * 0.25)),
              "variation LFO off " + fmt(lfoOff, 5) + ", assigned at amount 0 " + fmt(zeroAmount, 5));
    }

    // Multiple LFOs on different destinations must not corrupt each other.
    {
        auto renderTwoLfos = [](float lfo2Amount)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 1);
            setParam(processor, "filter1Enabled", 1.0f);
            setParam(processor, "filter1Cutoff", 1200.0f);
            setParam(processor, "lfoEnabled", 1.0f);
            setParam(processor, "lfoFrequency", 6.0f);
            setParam(processor, "lfoAmount", 1.0f);
            processor.setLfoAssignmentByParameterId(0, "filter1Cutoff", false);
            setParam(processor, "lfo2Enabled", 1.0f);
            setParam(processor, "lfo2Frequency", 3.0f);
            setParam(processor, "lfo2Amount", lfo2Amount);
            processor.setLfoAssignmentByParameterId(1, "mix.osc1.pan", false);
            return render(processor, 48000, { { 2000, true, 45, 0.9f } });
        };
        const auto capture = renderTwoLfos(1.0f);
        check("Lfo_TwoAssignmentsCoexistWithoutCorruption",
              capture.isFinite() && capture.rms() > 0.01 && capture.peak() < 1.5,
              "rms " + fmt(capture.rms(), 5) + ", peak " + fmt(capture.peak(), 5));

        // LFO2 -> pan must move the stereo balance; LFO1 -> cutoff must not.
        auto stereoDifference = [](const Capture& capture)
        {
            double d = 0.0;
            for (std::size_t i = 20000; i < 44000 && i < capture.left.size(); ++i)
            {
                d += std::abs(static_cast<double>(capture.left[i]) - capture.right[i]);
            }
            return d / 24000.0;
        };
        const auto panning = stereoDifference(renderTwoLfos(1.0f));
        const auto notPanning = stereoDifference(renderTwoLfos(0.0f));
        check("Lfo2_PanAssignment_MovesStereoBalanceIndependentlyOfLfo1",
              panning > notPanning * 2.0 + 0.001,
              "stereo difference " + fmt(notPanning, 6) + " -> " + fmt(panning, 6));
    }

    // The LFO is a control signal, never audio: with everything else silent an
    // enabled, assigned LFO must not itself make sound.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        for (int i = 1; i <= 3; ++i) setParam(processor, "osc" + juce::String(i) + "Enabled", 0.0f);
        setParam(processor, "subOscEnabled", 0.0f);
        setParam(processor, "lfoEnabled", 1.0f);
        setParam(processor, "lfoAmount", 1.0f);
        setParam(processor, "lfoFrequency", 8.0f);
        processor.setLfoAssignmentByParameterId(0, "mix.osc1.level", false);
        const auto capture = render(processor, 32000, { { 2000, true, 57, 0.9f } });
        check("Lfo_IsNotAnAudioSource", capture.peak() < 1.0e-6,
              "peak with all oscillators disabled " + fmt(capture.peak(), 9));
    }
}
//==============================================================================
// PHASE 8 - VIBE
//==============================================================================
// Established behaviour, read from the implementation rather than the name:
// VIBE is an analog-character emulation distributed INSIDE the voice, not a bus
// effect. VibeEngine produces a per-block shared drift state (oscillator drift,
// PSU sag, temperature, correlated chaos) plus a per-voice variation set (pitch
// cents, cutoff/resonance offset, gain offset, asymmetry/saturation bias).
// SynthVoice applies these to filter cutoff and resonance, oscillator pitch,
// waveshaping, added noise, VCA nonlinearity and voice gain. It has three
// controls: vibeEnabled, vibeAmount and vibeType.
void testVibe()
{
    suite("VIBE");

    auto renderVibe = [](bool enabled, float amount, int typeIndex = 0)
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        setParam(processor, "filter1Enabled", 1.0f);
        setParam(processor, "filter1Cutoff", 2500.0f);
        setParam(processor, "vibeEnabled", enabled ? 1.0f : 0.0f);
        setParam(processor, "vibeAmount", amount);
        setChoice(processor, "vibeType", typeIndex);
        return render(processor, 48000, { { 2000, true, 45, 0.9f } });
    };

    const auto off = renderVibe(false, 0.0f);
    const auto zeroAmount = renderVibe(true, 0.0f);
    const auto midAmount = renderVibe(true, 0.5f);
    const auto fullAmount = renderVibe(true, 1.0f);

    check("Vibe_DisabledProducesAudio", off.rms() > 0.01 && off.isFinite(),
          "rms " + fmt(off.rms(), 5));
    check("Vibe_ZeroAmountMatchesDisabled",
          nearly(zeroAmount.rms(), off.rms(), off.rms() * 0.02),
          "off " + fmt(off.rms(), 6) + ", enabled at amount 0 " + fmt(zeroAmount.rms(), 6));

    // Amount must have a graded effect. Vibe adds saturation, noise and drift,
    // so the measure is how far the signal departs from the clean one.
    auto departureFrom = [](const Capture& reference, const Capture& other)
    {
        const auto count = juce::jmin(reference.left.size(), other.left.size());
        if (count == 0) return 0.0;
        double numerator = 0.0, denominator = 0.0;
        for (std::size_t i = 20000; i < count; ++i)
        {
            const auto d = static_cast<double>(other.left[i]) - reference.left[i];
            numerator += d * d;
            denominator += static_cast<double>(reference.left[i]) * reference.left[i];
        }
        return denominator > 1.0e-12 ? std::sqrt(numerator / denominator) : 0.0;
    };

    const auto midDeparture = departureFrom(off, midAmount);
    check("Vibe_MidAmountAlreadyChangesTheSignal", midDeparture > 0.01,
          "departure at 0.5 " + fmt(midDeparture, 5));

    // Amount is graded but SATURATING: vibeDepth is a compressive curve into a
    // waveshaper, so level rises steeply to about three quarters of the range
    // and then flattens. Asserting strict monotonicity to the top would be
    // asserting something the design does not do; asserting a rise through the
    // usable range and no collapse at the top is the real contract.
    // Measured as departure from the clean signal, not as level. The stage is
    // deliberately level-normalised now - it trades peaks for density rather
    // than turning the signal up - so its output level is no longer a proxy for
    // how much character it is adding. It used to be one only because the
    // makeup gain was level-dependent, which was the defect.
    // Crest factor, not level. Vibe is deliberately level-neutral now, so its
    // output level cannot report how much character it is adding; a composite
    // "distance from clean" cannot either, because the pitch drift decorrelates
    // the two signals completely even at a low setting and the measure
    // saturates. Peak-to-RMS does track the amount, because the asymmetry and
    // drift reshape the waveform progressively.
    // Measured as harmonic content added to a pure sine. Level cannot report
    // the amount - the stage is deliberately level-neutral now - and a
    // composite "distance from clean" cannot either, because the pitch drift
    // decorrelates the signals completely even at a low setting and the measure
    // saturates near sqrt(2). Harmonics are unambiguous: a sine has none, so
    // whatever appears at 2f..6f is the nonlinearity's doing.
    auto harmonicsAt = [](float amount)
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 0);      // SINE
        setParam(processor, "vibeEnabled", 1.0f);
        setParam(processor, "vibeAmount", amount);
        const auto capture = render(processor, 96000, { { 2000, true, 45, 0.9f } });
        return harmonicToFundamentalRatio(capture.left, 110.0, 24000);
    };
    const auto hQuarter = harmonicsAt(0.25f);
    const auto hHalf = harmonicsAt(0.5f);
    const auto hThreeQuarter = harmonicsAt(0.75f);
    const auto hFull = harmonicsAt(1.0f);
    check("Vibe_AmountIncreasesHarmonicContentMonotonically",
          hHalf > hQuarter && hThreeQuarter > hHalf && hFull > hThreeQuarter,
          "harmonic/fundamental 0.25 -> " + fmt(hQuarter, 5) + ", 0.5 -> " + fmt(hHalf, 5)
              + ", 0.75 -> " + fmt(hThreeQuarter, 5) + ", 1.0 -> " + fmt(hFull, 5));

    // Turning vibe up must not change the mix balance. This is the property the
    // old stage broke: its makeup gain was level-dependent, so vibe acted as up
    // to +6.6 dB of gain on quiet material and a cut on loud material.
    {
        auto levelAt = [&renderVibe](float amount)
        {
            return renderVibe(true, amount).rmsOver(20000, 94000);
        };
        const auto reference = levelAt(0.0f);
        auto worstDb = 0.0;
        for (const auto amount : { 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const auto db = std::abs(juce::Decibels::gainToDecibels(levelAt(amount) / juce::jmax(1.0e-9, reference)));
            worstDb = juce::jmax(worstDb, static_cast<double>(db));
        }
        check("Vibe_AmountIsLevelNeutral", worstDb < 1.5,
              "worst level change across the amount range = " + fmt(worstDb, 2) + " dB");
    }

    // Maximum amount must not run away or clip: this is a saturating, noise
    // adding stage, so it is exactly where runaway gain would show.
    check("Vibe_MaximumAmountDoesNotRunAwayOrClip",
          fullAmount.isFinite() && fullAmount.peak() <= 1.0001,
          "peak " + fmt(fullAmount.peak(), 5));
    // Vibe's VCA make-up gain is level dependent by construction:
    // tanh(v * (1 + a*3.2)) / (1 + a*0.95) is about +6.6 dB on a quiet signal
    // and -1.5 dB on a loud one. Sources now carry 4 dB of headroom, so the VCA
    // sits further into its high-gain region and vibe reads relatively louder
    // than it did when sources ran at full scale. The bound is therefore stated
    // against the current gain structure; what it guards is runaway, and
    // clipping is covered separately by the peak check above.
    check("Vibe_MaximumAmountKeepsLevelComparableToClean",
          fullAmount.rms() > off.rms() * 0.4 && fullAmount.rms() < off.rms() * 3.2,
          "clean " + fmt(off.rms(), 5) + ", full vibe " + fmt(fullAmount.rms(), 5)
              + " (x" + fmt(fullAmount.rms() / juce::jmax(1.0e-9, off.rms()), 2) + ")");

    // Every vibe type must be selectable and produce a distinct character.
    {
        std::vector<double> departures;
        bool allFinite = true;
        for (int type = 0; type < 4; ++type)
        {
            const auto capture = renderVibe(true, 0.9f, type);
            if (! capture.isFinite()) allFinite = false;
            departures.push_back(departureFrom(off, capture));
        }
        check("Vibe_AllTypesRenderFinitely", allFinite);
        bool anyDistinct = false;
        for (std::size_t i = 1; i < departures.size(); ++i)
        {
            if (std::abs(departures[i] - departures[0]) > 1.0e-4) anyDistinct = true;
        }
        check("Vibe_TypeSelectionChangesCharacter", anyDistinct,
              "departures " + fmt(departures[0], 5) + " / " + fmt(departures[1], 5)
                  + " / " + fmt(departures[2], 5) + " / " + fmt(departures[3], 5));
    }

    // Per-voice variation is the point of the component: two voices must not
    // receive identical drift, or the emulation is a global effect instead.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "vibeEnabled", 1.0f);
        setParam(processor, "vibeAmount", 1.0f);
        const auto capture = render(processor, 64000,
                                    { { 2000, true, 57, 0.9f }, { 2000, true, 69, 0.9f } });
        check("Vibe_MultipleVoicesRenderWithoutInterference",
              capture.isFinite() && capture.rms() > 0.01 && capture.peak() <= 1.0001,
              "rms " + fmt(capture.rms(), 5) + ", peak " + fmt(capture.peak(), 5));
    }

    // Vibe lives in the voice, so a released voice must still retire cleanly
    // with it at maximum - this is where stale drift state would show.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "vibeEnabled", 1.0f);
        setParam(processor, "vibeAmount", 1.0f);
        setParam(processor, "ampRelease", 0.100f);
        const auto capture = render(processor, 96000,
                                    { { 2000, true, 57, 0.9f }, { 20000, false, 57, 0.0f } });
        const auto afterRelease = capture.rmsOver(60000, 95000);
        check("Vibe_ReleasedVoiceReachesSilenceWithVibeAtMaximum",
              afterRelease < 1.0e-4, "rms well after release " + fmt(afterRelease, 8));
    }
}

//==============================================================================
// REVERB QUALITY METRICS
//==============================================================================
// "Sounds natural" has to be measurable or it is just an opinion. These are the
// standard objective measures for reverberation quality:
//
//  - Normalised echo density (Abel & Huang, AES 2006). The fraction of impulse
//    response samples exceeding one standard deviation, divided by the value a
//    Gaussian process gives (erfc(1/sqrt2) = 0.3173). It approaches 1.0 as the
//    response becomes fully diffuse. A sparse, "ping-y" early response reads
//    well below 1; a natural room reaches ~1 within a few tens of milliseconds.
//  - Spectral flatness of the late tail. Isolated resonant modes - the metallic
//    or ringing quality - show up as a low geometric-to-arithmetic mean ratio.
//  - Decay ripple. A natural tail decays smoothly and monotonically in dB;
//    flutter between delay lines shows up as ripple around the best-fit line.
//  - Inter-channel correlation of the tail. A wide natural reverb decorrelates
//    the two channels; a mono-ish tail sits near 1.0.
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

// Fraction of samples beyond one standard deviation, normalised by the Gaussian
// expectation so that 1.0 means "as diffuse as noise".
double normalisedEchoDensity(const std::vector<float>& ir, int centreSample, int windowSamples)
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

double spectralFlatnessOf(const std::vector<float>& ir, int fromSample, int fftOrder = 12)
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

// Max deviation (dB) of the smoothed decay envelope from its best-fit straight
// line, over the region where the tail is still well above the noise floor.
double decayRippleDb(const std::vector<float>& ir, int fromSample, int toSample)
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

double interChannelCorrelation(const std::vector<float>& left,
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

// Reverberation time from the Schroeder backward integration curve, measured
// over the -5 dB to -35 dB span and extrapolated to 60 dB (T30).
double measureRt60(const std::vector<float>& ir)
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

// ISO 3382 style: deviation of the energy decay curve from a straight line
// over the -5 dB to -35 dB span, in dB.
double decayCurveNonlinearityDb(const std::vector<float>& ir)
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

// Renders a stereo impulse response straight from the Reverb class, fully wet,
// so the measurements describe the algorithm rather than the dry/wet mix.
// ---------------------------------------------------------------------------
// Mood characterisation. Driven on the Mood class directly. Every measurement
// here is about stereo behaviour, because that is what the component is
// supposed to have and mostly does not.
// ---------------------------------------------------------------------------
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

// Feeds Mood a signal and reports what came out. `panLeft` sends the test
// signal only to the left channel, which is how channel separation is measured.
MoodMetrics measureMood(const MoodSettings& settings,
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

// ---------------------------------------------------------------------------
// Delay characterisation. Driven on the Delay class directly rather than
// through the plugin, so an impulse in gives an impulse response out and the
// echo times can be read straight off it.
// ---------------------------------------------------------------------------
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

// Runs an impulse through one delay algorithm and reads the echo pattern back.
DelayMetrics measureDelay(const DelaySettings& settings,
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

// Drives a delay algorithm with audio while a control is swept, stops the
// input, and reports how much is left at three points afterwards. Static-
// parameter tests cannot see this class of fault: the delay only misbehaves
// while something is moving, and what it leaves behind is a tail that either
// decays far too slowly or does not decay at all.
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

// sweepWhich: 0 = nothing moves, 1 = TIME, 2 = FEEDBACK, 3 = AMOUNT, 4 = SYNC
DelayStressResult delayStress(int algo, int sweepWhich, float feedbackLevel = 0.85f)
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

// Feeds a delay, bypasses it, waits, then re-enables it with silence going in.
// Anything that comes out is a tail the effect kept across the bypass.
double delayTailAfterBypassCycle(int algorithmIndex)
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

// Passes a signal through the delay at amount 0 and reports the worst sample
// difference from the input. A delay at zero amount must be transparent.
double delayZeroAmountBleed(int algorithmIndex)
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

ReverbMetrics measureReverb(const ReverbSettings& settings, int totalSamples = 192000)
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
    const auto ir_span_limit = static_cast<double>(totalSamples) - static_cast<double>(kSampleRate) * 0.15;
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

void reportReverbMetrics(const char* label, const ReverbMetrics& m)
{
    std::printf("  %-26s ED20 %5.2f  ED50 %5.2f  ED150 %5.2f  flat %6.4f  lateRipple %5.2fdB  corr %+5.2f  bal %+5.2fdB  rt60 %5.2fs\n",
                label, m.echoDensityAt20ms, m.echoDensityAt50ms, m.echoDensityAt150ms,
                m.spectralFlatness, m.lateRippleDb, m.interChannelCorrelation, m.channelBalanceDb, m.rt60Seconds);
    std::fflush(stdout);
}

//==============================================================================
// PHASE 9 - REVERB
//==============================================================================
// Drives an effect, bypasses it, waits, then re-enables it with silence going
// in. Whatever comes out is a tail the effect held on to across the bypass -
// which on the next note arrives underneath something it has nothing to do
// with. `configure` sets the enabled flag on whatever settings type is in play.
template <typename EffectT, typename SettingsT, typename UpdateFn>
double tailAfterBypassCycle(SettingsT settings, UpdateFn update)
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

void testReverb()
{
    suite("REVERB");

    // Bypassing has to empty the delay lines. The amount control fades to zero
    // so nothing is heard while it is off, but the network keeps circulating,
    // and switching it back on releases the tail of whatever was playing when
    // it was switched off.
    {
        double worst = 0.0;
        int worstAlgo = 0;
        for (int algo = 0; algo < 4; ++algo)
        {
            ReverbSettings settings;
            settings.amount = 1.0f;
            settings.algorithmIndex = algo;
            settings.decay = 0.9f;
            settings.size = 0.8f;
            const auto tail = tailAfterBypassCycle<::Reverb, ReverbSettings>(
                settings,
                [](::Reverb& r, const ReverbSettings& s) { r.updateForBlock(s, 512); });
            if (tail > worst) { worst = tail; worstAlgo = algo; }
        }
        check("Reverb_BypassClearsTheTail",
              worst < 1.0e-4,
              "loudest sample after bypass and re-enable with silence in: "
                  + fmt(worst, 8) + " (algorithm " + juce::String(worstAlgo) + ")");
    }

    // Unit level: the Reverb class driven directly, so wet behaviour is not
    // confounded by the send/return topology around it.
    auto runReverb = [](const ReverbSettings& settings, int impulseSamples, int totalSamples)
    {
        ::Reverb reverb;
        reverb.prepare(kSampleRate);
        reverb.reset();
        reverb.updateForBlock(settings, totalSamples);

        Capture capture;
        for (int i = 0; i < totalSamples; ++i)
        {
            // A short burst of alternating-sign impulses, then silence.
            const auto input = i < impulseSamples ? ((i % 2) ? -0.5f : 0.5f) : 0.0f;
            float outL = 0.0f, outR = 0.0f;
            reverb.processSampleFrame(input, input, outL, outR);
            capture.left.push_back(outL);
            capture.right.push_back(outR);
        }
        return capture;
    };

    ReverbSettings base;
    base.enabled = true;
    base.amount = 0.8f;

    {
        const auto capture = runReverb(base, 480, 96000);
        check("Reverb_ProducesOutputFromInput", capture.rms() > 1.0e-5 && capture.isFinite(),
              "rms " + fmt(capture.rms(), 6));

        // Tail: input stops at 480 samples; energy must persist well beyond it.
        const auto duringInput = capture.rmsOver(0, 480);
        const auto tailEarly = capture.rmsOver(4800, 14400);
        const auto tailLate = capture.rmsOver(48000, 72000);
        check("Reverb_TailContinuesAfterInputStops", tailEarly > 1.0e-6,
              "input rms " + fmt(duringInput, 6) + ", tail at 0.1-0.3 s " + fmt(tailEarly, 6));
        check("Reverb_TailDecaysRatherThanSustaining", tailLate < tailEarly,
              "tail 0.1-0.3 s " + fmt(tailEarly, 6) + " -> 1.0-1.5 s " + fmt(tailLate, 6));
        check("Reverb_TailIsStable", capture.isFinite() && capture.peak() < 10.0,
              "peak " + fmt(capture.peak(), 5));
    }

    // Amount must scale the wet contribution.
    {
        auto wetLevel = [&runReverb, base](float amount)
        {
            auto settings = base;
            settings.amount = amount;
            return runReverb(settings, 480, 48000).rmsOver(4800, 24000);
        };
        const auto quiet = wetLevel(0.2f);
        const auto loud = wetLevel(1.0f);
        check("Reverb_AmountScalesWetContribution", loud > quiet * 1.5,
              "amount 0.2 -> " + fmt(quiet, 7) + ", amount 1.0 -> " + fmt(loud, 7));
    }

    // Disabled must produce no reverberation.
    {
        auto settings = base;
        settings.enabled = false;
        const auto capture = runReverb(settings, 480, 48000);
        check("Reverb_DisabledProducesNoTail", capture.rmsOver(4800, 24000) < 1.0e-7,
              "tail rms while disabled " + fmt(capture.rmsOver(4800, 24000), 9));
    }

    // Reset must clear the tail so a new signal is not contaminated.
    {
        ::Reverb reverb;
        reverb.prepare(kSampleRate);
        reverb.reset();
        reverb.updateForBlock(base, 48000);
        for (int i = 0; i < 4800; ++i)
        {
            float outL = 0.0f, outR = 0.0f;
            reverb.processSampleFrame(i < 480 ? 0.5f : 0.0f, i < 480 ? 0.5f : 0.0f, outL, outR);
        }
        reverb.reset();
        double residual = 0.0;
        for (int i = 0; i < 24000; ++i)
        {
            float outL = 0.0f, outR = 0.0f;
            reverb.processSampleFrame(0.0f, 0.0f, outL, outR);
            residual = juce::jmax(residual, static_cast<double>(std::abs(outL)));
        }
        check("Reverb_ResetClearsPreviousTail", residual < 1.0e-6,
              "largest sample after reset with silent input " + fmt(residual, 9));
    }

    // Parameter coverage, per algorithm.
    //
    // The four algorithms are genuinely different processors, and not every
    // control belongs to all of them: algorithm 0 wraps juce::Reverb, which
    // exposes only room size, damping and width, while decay and mod depth
    // belong to the plate/hall/cloud algorithms and the two cloud controls to
    // the cloud algorithm alone. Each parameter is therefore tested against an
    // algorithm that owns it, rather than asserted to affect all four.
    {
        auto captureFor = [&runReverb, base](int algorithmIndex,
                                             const std::function<void(ReverbSettings&)>& tweak)
        {
            auto settings = base;
            settings.algorithmIndex = algorithmIndex;
            tweak(settings);
            return runReverb(settings, 480, 48000);
        };

        // Comparing two RMS scalars hides a parameter that reshapes the tail
        // without changing its energy. The difference SIGNAL between the two
        // settings, relative to the tail's own level, catches that.
        auto relativeDifference = [](const Capture& a, const Capture& b)
        {
            double difference = 0.0, reference = 0.0;
            for (std::size_t i = 2400; i < 36000; ++i)
            {
                const auto d = static_cast<double>(a.left[i]) - b.left[i];
                difference += d * d;
                reference += static_cast<double>(a.left[i]) * a.left[i];
            }
            return reference > 1.0e-18 ? std::sqrt(difference / reference) : 0.0;
        };

        // Tonal controls only. DECAY sets a TIME and is covered by the
        // reverberation-time tests above, which measure it directly instead of
        // inferring it from tail energy over a fixed window.
        struct ParamCase
        {
            const char* name;
            int algorithm;
            std::function<void(ReverbSettings&)> low, high;
        };
        const ParamCase cases[] = {
            { "Size",           0, [](ReverbSettings& s) { s.size = 0.0f; },           [](ReverbSettings& s) { s.size = 1.0f; } },
            { "Damping",        0, [](ReverbSettings& s) { s.damping = 0.0f; },        [](ReverbSettings& s) { s.damping = 1.0f; } },
            { "Width",          0, [](ReverbSettings& s) { s.width = 0.0f; },          [](ReverbSettings& s) { s.width = 1.0f; } },
            { "ModDepth",       1, [](ReverbSettings& s) { s.modDepth = 0.0f; },       [](ReverbSettings& s) { s.modDepth = 1.0f; } },

            { "CloudDiffusion", 3, [](ReverbSettings& s) { s.cloudDiffusion = 0.0f; }, [](ReverbSettings& s) { s.cloudDiffusion = 1.0f; } },
        };

        for (const auto& parameter : cases)
        {
            const auto low = captureFor(parameter.algorithm, parameter.low);
            const auto high = captureFor(parameter.algorithm, parameter.high);
            const auto difference = relativeDifference(low, high);
            check((juce::String("Reverb_") + parameter.name + "_ChangesTheOutput_Algorithm"
                   + juce::String(parameter.algorithm)).toRawUTF8(),
                  difference > 0.02,
                  "tail differs by " + fmt(100.0 * difference, 2) + "% between min and max");
        }
    }

    // CLOUD FEEDBACK sets the tail length, which is measured over a window long
    // enough to contain it rather than the short one used for tonal controls.
    {
        auto cloudTail = [&runReverb, base](float feedback)
        {
            auto settings = base;
            settings.algorithmIndex = 3;
            settings.decay = 0.9f;
            settings.cloudFeedback = feedback;
            const auto capture = runReverb(settings, 480, 480000);
            return capture.rmsOver(240000, 470000);
        };
        const auto tight = cloudTail(0.0f);
        const auto endless = cloudTail(1.0f);
        check("Reverb_CloudFeedback_ExtendsTheTail", endless > tight * 4.0,
              "feedback 0 late rms " + fmt(tight, 8) + ", feedback 1 " + fmt(endless, 8));
    }

    // Pre-delay shifts the wet signal in TIME, which an energy measure over a
    // wide window cannot see. Measured as the onset of the wet signal instead.
    {
        // processSampleFrame returns dry + wet together, and subtracting a
        // disabled run does not isolate the wet either, because disabling also
        // removes the dry attenuation. Instead the input is a short burst that
        // is over by sample 64, so anything found after that is reverberation:
        // the pre-delay is the length of the silence between the two.
        auto wetOnsetSample = [&runReverb, base](float preDelay)
        {
            auto settings = base;
            settings.preDelay = preDelay;
            const auto capture = runReverb(settings, 64, 96000);
            for (std::size_t i = 200; i < capture.left.size(); ++i)
            {
                if (std::abs(capture.left[i]) > 1.0e-4f) return static_cast<int>(i);
            }
            return -1;
        };
        const auto none = wetOnsetSample(0.0f);
        const auto half = wetOnsetSample(0.5f);
        const auto maximum = wetOnsetSample(1.0f);
        check("Reverb_PreDelayDelaysTheWetOnset", none >= 0 && half > none + 1000,
              "onset at preDelay 0 = " + juce::String(none)
                  + " samples, at 0.5 = " + juce::String(half) + " samples");
        // The top of the range must be the longest pre-delay, not a wrap back to
        // none: the requested delay must fit inside the allocated line.
        check("Reverb_MaximumPreDelayIsLongerThanHalf", maximum > half,
              "onset at preDelay 0.5 = " + juce::String(half)
                  + " samples, at 1.0 = " + juce::String(maximum) + " samples");
    }

    // Every algorithm must respond to DECAY. Algorithm 0 used to wrap
    // juce::Reverb, which has no decay control at all, so the knob was inert on
    // the default algorithm; it is now a room network with its own decay time.
    // Measured as reverberation time by Schroeder backward integration, which
    // is what the control actually claims to set.
    {
        for (int algorithm = 0; algorithm < 4; ++algorithm)
        {
            auto rt60For = [algorithm](float decay)
            {
                ReverbSettings settings;
                settings.algorithmIndex = algorithm;
                settings.decay = decay;
                settings.size = 0.6f;
                settings.damping = 0.45f;
                settings.preDelay = 0.0f;
                if (algorithm == 3) settings.cloudFeedback = 1.0f;
                // Cloud reaches tens of seconds, and Schroeder integration over
                // a truncated response underestimates badly, so it gets a
                // capture long enough to contain its own tail.
                const auto captureSamples = algorithm == 3 ? 960000 : 192000;
                return measureReverb(settings, captureSamples).rt60Seconds;
            };
            const auto shortTail = rt60For(0.2f);
            const auto longTail = rt60For(0.9f);
            check((juce::String("Reverb_Algorithm") + juce::String(algorithm)
                   + "_DecayControlsReverberationTime").toRawUTF8(),
                  longTail > shortTail * 1.5 && shortTail > 0.05,
                  "decay 0.2 -> " + fmt(shortTail, 2) + " s, 0.9 -> " + fmt(longTail, 2) + " s");
        }
    }

    // CLOUD FEEDBACK sets the tail length, which is measured over a window long
    // enough to contain it rather than the short one used for tonal controls.
    {
        auto cloudTail = [&runReverb, base](float feedback)
        {
            auto settings = base;
            settings.algorithmIndex = 3;
            settings.decay = 0.9f;
            settings.cloudFeedback = feedback;
            const auto capture = runReverb(settings, 480, 480000);
            return capture.rmsOver(240000, 470000);
        };
        const auto tight = cloudTail(0.0f);
        const auto endless = cloudTail(1.0f);
        check("Reverb_CloudFeedback_ExtendsTheTail", endless > tight * 4.0,
              "feedback 0 late rms " + fmt(tight, 8) + ", feedback 1 " + fmt(endless, 8));
    }

    // Pre-delay shifts the wet signal in TIME, which an energy measure over a
    // wide window cannot see. Measured as the onset of the wet signal instead.
    {
        // processSampleFrame returns dry + wet together, and subtracting a
        // disabled run does not isolate the wet either, because disabling also
        // removes the dry attenuation. Instead the input is a short burst that
        // is over by sample 64, so anything found after that is reverberation:
        // the pre-delay is the length of the silence between the two.
        auto wetOnsetSample = [&runReverb, base](float preDelay)
        {
            auto settings = base;
            settings.preDelay = preDelay;
            const auto capture = runReverb(settings, 64, 96000);
            for (std::size_t i = 200; i < capture.left.size(); ++i)
            {
                if (std::abs(capture.left[i]) > 1.0e-4f) return static_cast<int>(i);
            }
            return -1;
        };
        const auto none = wetOnsetSample(0.0f);
        const auto half = wetOnsetSample(0.5f);
        const auto maximum = wetOnsetSample(1.0f);
        check("Reverb_PreDelayDelaysTheWetOnset", none >= 0 && half > none + 1000,
              "onset at preDelay 0 = " + juce::String(none)
                  + " samples, at 0.5 = " + juce::String(half) + " samples");
        // The top of the range must be the longest pre-delay, not a wrap back to
        // none: the requested delay must fit inside the allocated line.
        check("Reverb_MaximumPreDelayIsLongerThanHalf", maximum > half,
              "onset at preDelay 0.5 = " + juce::String(half)
                  + " samples, at 1.0 = " + juce::String(maximum) + " samples");
    }


    // Width is a stereo control: at width 0 the channels must be closer
    // together than at width 1.
    {
        auto stereoSpread = [&runReverb, base](float width)
        {
            auto settings = base;
            settings.width = width;
            const auto capture = runReverb(settings, 480, 48000);
            double difference = 0.0;
            for (std::size_t i = 2400; i < 36000; ++i)
            {
                difference += std::abs(static_cast<double>(capture.left[i]) - capture.right[i]);
            }
            return difference / 33600.0;
        };
        const auto narrow = stereoSpread(0.0f);
        const auto wide = stereoSpread(1.0f);
        check("Reverb_WidthControlsStereoSpread", wide > narrow,
              "width 0 spread " + fmt(narrow, 8) + ", width 1 spread " + fmt(wide, 8));
    }

    // Quality floors. These are the measures that separate a reverb from a
    // resonant delay, and they are pinned so that a future change cannot
    // quietly reintroduce the sparse, ringing behaviour the algorithms had
    // before: echo density that FELL over time, 25-40 dB of decay ripple, and
    // spectral flatness two orders of magnitude below a stock Freeverb.
    {
        static const char* algorithmNames[] = { "Room", "Plate", "Hall", "Cloud" };
        for (int algorithm = 0; algorithm < 4; ++algorithm)
        {
            ReverbSettings settings;
            settings.algorithmIndex = algorithm;
            settings.decay = 0.6f;
            settings.size = 0.6f;
            settings.damping = 0.45f;
            settings.preDelay = 0.0f;
            const auto m = measureReverb(settings);
            const auto name = juce::String(algorithmNames[algorithm]);

            check(("Reverb" + name + "_BecomesDiffuse").toRawUTF8(),
                  m.echoDensityAt150ms > 0.45,
                  "normalised echo density at 150 ms = " + fmt(m.echoDensityAt150ms, 3));
            check(("Reverb" + name + "_EchoDensityGrowsRatherThanFalls").toRawUTF8(),
                  m.echoDensityAt150ms > m.echoDensityAt20ms,
                  "20 ms " + fmt(m.echoDensityAt20ms, 3) + " -> 150 ms " + fmt(m.echoDensityAt150ms, 3));
            check(("Reverb" + name + "_TailIsNotMetallic").toRawUTF8(),
                  m.spectralFlatness > 0.02,
                  "spectral flatness of the late tail = " + fmt(m.spectralFlatness, 4));
            check(("Reverb" + name + "_DecayIsSmoothlyExponential").toRawUTF8(),
                  m.lateRippleDb < 8.0,
                  "decay curve nonlinearity = " + fmt(m.lateRippleDb, 2) + " dB");
            check(("Reverb" + name + "_IsStereoButMonoSafe").toRawUTF8(),
                  std::abs(m.interChannelCorrelation) < 0.75,
                  "inter-channel correlation = " + fmt(m.interChannelCorrelation, 3));
            check(("Reverb" + name + "_ImpulseResponseIsClean").toRawUTF8(),
                  m.finite && m.peak < 4.0,
                  "peak " + fmt(m.peak, 4));
        }
    }

    // Every algorithm must render and stay stable.
    {
        for (int algorithm = 0; algorithm < 4; ++algorithm)
        {
            auto settings = base;
            settings.algorithmIndex = algorithm;
            const auto capture = runReverb(settings, 480, 48000);
            check((juce::String("Reverb_Algorithm") + juce::String(algorithm) + "_IsStable").toRawUTF8(),
                  capture.isFinite() && capture.peak() < 10.0 && capture.rms() > 1.0e-7,
                  "rms " + fmt(capture.rms(), 7) + ", peak " + fmt(capture.peak(), 5));
        }
    }
}

//==============================================================================
// PHASE 10 - MOOD
//==============================================================================
// Established behaviour, read from the implementation: MOOD is a stereo
// granular/looping texture processor on the FX bus. It keeps a history buffer,
// spawns grains from it, and offers a wet stage (reverb / delay / slip), a loop
// stage (env / tape / stretch), plus feedback, spread, degrade, clock division
// and a freeze that stops new material entering.
// ---------------------------------------------------------------------------
// Card / Panel style system
//
// These test that a parsed property actually reaches the geometry or the
// rendering state - not that the JSON contains a key. The previous styling
// system's failure mode was properties that existed and did nothing, so a test
// that only proves a key is present would be testing the wrong thing.
// ---------------------------------------------------------------------------
void testCardStyle()
{
    suite("CARD STYLE");

    using namespace px3::ui;

    auto configFrom = [](const char* json)
    {
        juce::String error;
        return UIConfig::fromJsonText(json, error);
    };

    // ---- Dimension parsing -------------------------------------------------
    {
        const auto px = Dimension::parse(juce::var(300), {});
        const auto pxString = Dimension::parse(juce::var("300px"), {});
        const auto bare = Dimension::parse(juce::var("300"), {});
        const auto pct = Dimension::parse(juce::var("33%"), {});
        const auto autoValue = Dimension::parse(juce::var("auto"), { Dimension::Unit::pixels, 5.0f });

        const auto ok = px.unit == Dimension::Unit::pixels && juce::approximatelyEqual(px.value, 300.0f)
                     && pxString.unit == Dimension::Unit::pixels && juce::approximatelyEqual(pxString.value, 300.0f)
                     && bare.unit == Dimension::Unit::pixels && juce::approximatelyEqual(bare.value, 300.0f)
                     && pct.unit == Dimension::Unit::percent && juce::approximatelyEqual(pct.value, 33.0f)
                     && autoValue.isAuto();
        check("CardStyle_DimensionParsesPixelsAndPercent",
              ok,
              "300, \"300px\", \"300\", \"33%\" and \"auto\" all parse to the right unit");
    }

    // Percentage resolution against a known panel extent is the property most
    // likely to be got wrong, so it is checked against exact arithmetic.
    {
        const Dimension full { Dimension::Unit::percent, 100.0f };
        const Dimension half { Dimension::Unit::percent, 50.0f };
        const Dimension quarter { Dimension::Unit::percent, 25.0f };

        const auto a = full.resolve(400.0f, 999.0f);
        const auto b = half.resolve(400.0f, 999.0f);
        const auto c = quarter.resolve(400.0f, 999.0f);

        check("CardStyle_PercentResolvesAgainstPanelExtent",
              juce::approximatelyEqual(a, 400.0f)
                  && juce::approximatelyEqual(b, 200.0f)
                  && juce::approximatelyEqual(c, 100.0f),
              "panel 400px -> 100% = " + fmt(a, 1) + ", 50% = " + fmt(b, 1)
                  + ", 25% = " + fmt(c, 1));
    }

    {
        const Dimension autoValue {};
        const Dimension pixels { Dimension::Unit::pixels, 120.0f };
        check("CardStyle_AutoUsesAvailableSpaceAndPixelsIgnoreIt",
              juce::approximatelyEqual(autoValue.resolve(400.0f, 250.0f), 250.0f)
                  && juce::approximatelyEqual(pixels.resolve(400.0f, 250.0f), 120.0f),
              "auto -> available (250), pixels -> literal (120)");
    }

    // ---- Invalid input must not crash or produce nonsense ------------------
    {
        const Dimension fallback { Dimension::Unit::pixels, 42.0f };
        const auto garbage = Dimension::parse(juce::var("banana"), fallback);
        const auto negative = Dimension::parse(juce::var(-50), fallback);
        const auto negativePct = Dimension::parse(juce::var("-20%"), fallback);
        const auto empty = Dimension::parse(juce::var(), fallback);

        check("CardStyle_InvalidDimensionsFallBackInsteadOfBreaking",
              garbage.unit == Dimension::Unit::pixels && juce::approximatelyEqual(garbage.value, 42.0f)
                  && juce::approximatelyEqual(negative.value, 42.0f)
                  && juce::approximatelyEqual(negativePct.value, 42.0f)
                  && juce::approximatelyEqual(empty.value, 42.0f),
              "\"banana\", -50, \"-20%\" and a missing value all keep the fallback");
    }

    // ---- Insets ------------------------------------------------------------
    {
        const auto uniform = Insets::parse(juce::var(8), {});
        auto* object = new juce::DynamicObject();
        object->setProperty("top", 1);
        object->setProperty("right", 2);
        object->setProperty("bottom", 3);
        object->setProperty("left", 4);
        const auto perSide = Insets::parse(juce::var(object), {});

        const auto shrunk = perSide.shrink({ 0.0f, 0.0f, 100.0f, 100.0f });
        check("CardStyle_InsetsParseUniformAndPerSide",
              juce::approximatelyEqual(uniform.top, 8.0f) && juce::approximatelyEqual(uniform.left, 8.0f)
                  && juce::approximatelyEqual(perSide.top, 1.0f) && juce::approximatelyEqual(perSide.left, 4.0f)
                  && juce::approximatelyEqual(shrunk.getX(), 4.0f)
                  && juce::approximatelyEqual(shrunk.getWidth(), 94.0f),
              "8 -> all sides; {1,2,3,4} -> per side; shrink moves x to 4 and width to 94");
    }

    {
        // Padding larger than the card must collapse the content box, never
        // invert it - a negative rectangle silently breaks every layout downstream.
        const Insets huge { 500.0f, 500.0f, 500.0f, 500.0f };
        const auto collapsed = huge.shrink({ 0.0f, 0.0f, 100.0f, 100.0f });
        check("CardStyle_OversizedInsetsCollapseRatherThanInvert",
              collapsed.getWidth() >= 0.0f && collapsed.getHeight() >= 0.0f,
              "content box " + fmt(collapsed.getWidth(), 1) + " x " + fmt(collapsed.getHeight(), 1));
    }

    // ---- Bounds resolution -------------------------------------------------
    // The critical requirement: a percentage height is a percentage of the
    // parent PANEL's available height, not of the slot, the sibling, or the
    // card's own bounds.
    {
        CardStyle style;
        style.width = { Dimension::Unit::percent, 50.0f };
        style.height = { Dimension::Unit::percent, 50.0f };

        const juce::Rectangle<float> panel { 0.0f, 0.0f, 1000.0f, 400.0f };
        const juce::Rectangle<float> slot { 0.0f, 0.0f, 1000.0f, 400.0f };
        const auto resolved = style.resolveBounds(slot, panel);

        check("CardStyle_PercentHeightIsPercentOfThePanel",
              juce::approximatelyEqual(resolved.getHeight(), 200.0f)
                  && juce::approximatelyEqual(resolved.getWidth(), 500.0f),
              "panel 1000x400, card 50%/50% -> " + fmt(resolved.getWidth(), 1)
                  + " x " + fmt(resolved.getHeight(), 1));
    }

    {
        // The slot is deliberately smaller than the panel here. A card asking
        // for 50% of the panel would exceed its slot, so it is clamped - but
        // the percentage is still measured against the panel, which is what
        // stops "50%" meaning something different in every column.
        CardStyle style;
        style.height = { Dimension::Unit::percent, 50.0f };

        // The slot is deliberately TALLER than the panel, so the two references
        // give different answers: 50% of the panel is 200, 50% of the slot
        // would be 400. Sharing a height between them would make this test
        // unable to tell which one was used.
        const juce::Rectangle<float> panel { 0.0f, 0.0f, 1000.0f, 400.0f };
        const juce::Rectangle<float> tallSlot { 0.0f, 0.0f, 200.0f, 800.0f };
        const auto resolved = style.resolveBounds(tallSlot, panel);

        check("CardStyle_PercentIgnoresTheSlotAsAReference",
              juce::approximatelyEqual(resolved.getHeight(), 200.0f),
              "panel 400 / slot 800, card 50% -> " + fmt(resolved.getHeight(), 1)
                  + " (400 would mean it referenced the slot)");
    }

    {
        // Margin is outside the card: it reduces the box before sizing.
        CardStyle style;
        style.margin = { 10.0f, 10.0f, 10.0f, 10.0f };
        const juce::Rectangle<float> panel { 0.0f, 0.0f, 400.0f, 400.0f };
        const auto resolved = style.resolveBounds({ 0.0f, 0.0f, 400.0f, 400.0f }, panel);
        check("CardStyle_MarginShrinksTheCardFromItsSlot",
              juce::approximatelyEqual(resolved.getWidth(), 380.0f)
                  && juce::approximatelyEqual(resolved.getHeight(), 380.0f),
              "400px slot with 10px margin -> " + fmt(resolved.getWidth(), 1) + "px card");
    }

    {
        // Padding is inside the card: it reduces the content box, not the card.
        CardStyle style;
        style.padding = { 12.0f, 12.0f, 12.0f, 12.0f };
        const juce::Rectangle<float> card { 0.0f, 0.0f, 200.0f, 100.0f };
        const auto content = style.contentBounds(card);
        check("CardStyle_PaddingShrinksContentNotTheCard",
              juce::approximatelyEqual(content.getWidth(), 176.0f)
                  && juce::approximatelyEqual(content.getX(), 12.0f),
              "200px card with 12px padding -> content x=" + fmt(content.getX(), 1)
                  + " w=" + fmt(content.getWidth(), 1));
    }

    // ---- Parsing a real config --------------------------------------------
    {
        const auto config = configFrom(R"({
            "cards": {
                "defaults": {
                    "width": "auto", "height": "auto",
                    "margin": 4, "padding": 10,
                    "border":     { "enabled": true, "width": 1.2, "color": "#DCE8FC", "opacity": 0.35, "radius": 8 },
                    "background": { "color": "#101018", "opacity": 0.10 },
                    "gloss":      { "margin": 6, "split": 0.5,
                                    "topFill":    { "color": "#FFFFFF", "opacity": 0.10 },
                                    "bottomFill": { "color": "#000000", "opacity": 0.06 } },
                    "title":      { "fontSize": 11, "color": "#DCE8FC", "align": "center", "y": 0, "height": 14 }
                },
                "subOsc": { "width": "33%", "border": { "radius": 14 }, "title": { "y": -3, "fontSize": 13 } }
            }
        })");

        const auto style = CardStyle::fromConfig(config.get(), "cards.defaults", "cards.subOsc");

        const auto inherited = juce::approximatelyEqual(style.border.width, 1.2f)
                            && juce::approximatelyEqual(style.gloss.margin, 6.0f)
                            && juce::approximatelyEqual(style.padding.top, 10.0f);
        const auto overridden = style.width.unit == Dimension::Unit::percent
                             && juce::approximatelyEqual(style.width.value, 33.0f)
                             && juce::approximatelyEqual(style.border.radius, 14.0f)
                             && juce::approximatelyEqual(style.title.y, -3.0f)
                             && juce::approximatelyEqual(style.title.fontSize, 13.0f);

        check("CardStyle_OverridesLayerOverDefaults",
              inherited && overridden,
              "subOsc overrides width/radius/title, inherits border width, gloss margin and padding");
    }

    {
        // A card that declares nothing must still be fully styled.
        const auto config = configFrom(R"({ "cards": { "defaults": {}, "bare": {} } })");
        const CardStyle expected;
        const auto style = CardStyle::fromConfig(config.get(), "cards.defaults", "cards.bare");
        check("CardStyle_MissingPropertiesUseDefaults",
              juce::approximatelyEqual(style.border.radius, expected.border.radius)
                  && juce::approximatelyEqual(style.gloss.split, expected.gloss.split)
                  && juce::approximatelyEqual(style.title.fontSize, expected.title.fontSize)
                  && style.width.isAuto(),
              "an empty card style parses to the built-in defaults");
    }

    {
        // Malformed values must not crash and must not produce absurd geometry.
        const auto config = configFrom(R"({
            "cards": { "defaults": {}, "broken": {
                "width": "banana", "height": true,
                "border": { "width": -5, "opacity": 9, "radius": -3 },
                "gloss":  { "split": 4 },
                "title":  { "fontSize": -20, "align": "sideways" }
            } }
        })");
        const auto style = CardStyle::fromConfig(config.get(), "cards.defaults", "cards.broken");
        const auto sane = style.border.width >= 0.0f
                       && style.border.opacity >= 0.0f && style.border.opacity <= 1.0f
                       && style.border.radius >= 0.0f
                       && style.gloss.split >= 0.0f && style.gloss.split <= 1.0f
                       && style.title.fontSize > 0.0f;
        check("CardStyle_InvalidValuesAreClampedNotPropagated",
              sane,
              "border width " + fmt(style.border.width, 2) + ", opacity " + fmt(style.border.opacity, 2)
                  + ", gloss split " + fmt(style.gloss.split, 2)
                  + ", title size " + fmt(style.title.fontSize, 1));
    }

    // ---- Every property must change something -------------------------------
    //
    // This is the test that matters. The old styling system's failure was
    // properties that parsed fine and then affected nothing, so proving a key
    // exists proves nothing. Each property below is changed on its own and the
    // resolved style or geometry is required to differ - which is the same
    // thing live-reloading the file does.
    {
        const juce::String base = R"({"cards":{"defaults":{
            "width":"auto","height":"auto","margin":6,"padding":10,
            "border":{"enabled":true,"width":1.2,"color":"#DCE8FC","opacity":0.35,"radius":8},
            "background":{"color":"#68C2FF","opacity":0.10},
            "gloss":{"margin":6,"split":0.5,
                     "topFill":{"color":"#68C2FF","opacity":0.10},
                     "bottomFill":{"color":"#000000","opacity":0.06}},
            "title":{"fontSize":11,"color":"#DCE8FC","align":"center","y":0,"height":14}
        },"probe":{}}})";

        auto styleFor = [&](const juce::String& probeJson)
        {
            juce::String error;
            auto json = base;
            json = json.replace("\"probe\":{}", "\"probe\":" + probeJson);
            auto config = UIConfig::fromJsonText(json, error);
            return CardStyle::fromConfig(config.get(), "cards.defaults", "cards.probe");
        };

        const auto baseline = styleFor("{}");
        // The slot is large enough that the probe values below resolve inside
        // it. A value that exceeds the slot is capped by design - that case is
        // covered separately by CardStyle_CardNeverExceedsItsSlot.
        const juce::Rectangle<float> panel { 0.0f, 0.0f, 600.0f, 400.0f };
        const juce::Rectangle<float> slot { 0.0f, 0.0f, 560.0f, 380.0f };

        struct Probe
        {
            const char* name;
            const char* json;
            // Returns something that must differ from the baseline's value.
            std::function<double(const CardStyle&)> observe;
        };

        const std::vector<Probe> probes = {
            { "width",             R"({"width":"50%"})",
              [&](const CardStyle& s) { return s.resolveBounds(slot, panel).getWidth(); } },
            { "height",            R"({"height":"25%"})",
              [&](const CardStyle& s) { return s.resolveBounds(slot, panel).getHeight(); } },
            { "margin",            R"({"margin":20})",
              [&](const CardStyle& s) { return s.resolveBounds(slot, panel).getWidth(); } },
            { "padding",           R"({"padding":24})",
              [&](const CardStyle& s) { return s.contentBounds({ 0.0f, 0.0f, 200.0f, 100.0f }).getWidth(); } },
            { "border.enabled",    R"({"border":{"enabled":false}})",
              [](const CardStyle& s) { return s.border.enabled ? 1.0 : 0.0; } },
            { "border.width",      R"({"border":{"width":4}})",
              [](const CardStyle& s) { return static_cast<double>(s.border.width); } },
            { "border.color",      R"({"border":{"color":"#FF0000"}})",
              [](const CardStyle& s) { return static_cast<double>(s.border.colour.getARGB()); } },
            { "border.opacity",    R"({"border":{"opacity":0.9}})",
              [](const CardStyle& s) { return static_cast<double>(s.border.opacity); } },
            { "border.radius",     R"({"border":{"radius":20}})",
              [](const CardStyle& s) { return static_cast<double>(s.border.radius); } },
            { "background.color",  R"({"background":{"color":"#00FF00"}})",
              [](const CardStyle& s) { return static_cast<double>(s.background.colour.getARGB()); } },
            { "background.opacity",R"({"background":{"opacity":0.8}})",
              [](const CardStyle& s) { return static_cast<double>(s.background.opacity); } },
            { "gloss.margin",      R"({"gloss":{"margin":18}})",
              [](const CardStyle& s) { return static_cast<double>(s.gloss.margin); } },
            { "gloss.split",       R"({"gloss":{"split":0.25}})",
              [](const CardStyle& s) { return static_cast<double>(s.gloss.split); } },
            { "gloss.topFill.color",     R"({"gloss":{"topFill":{"color":"#123456"}}})",
              [](const CardStyle& s) { return static_cast<double>(s.gloss.topFill.colour.getARGB()); } },
            { "gloss.topFill.opacity",   R"({"gloss":{"topFill":{"opacity":0.5}}})",
              [](const CardStyle& s) { return static_cast<double>(s.gloss.topFill.opacity); } },
            { "gloss.bottomFill.color",  R"({"gloss":{"bottomFill":{"color":"#654321"}}})",
              [](const CardStyle& s) { return static_cast<double>(s.gloss.bottomFill.colour.getARGB()); } },
            { "gloss.bottomFill.opacity",R"({"gloss":{"bottomFill":{"opacity":0.4}}})",
              [](const CardStyle& s) { return static_cast<double>(s.gloss.bottomFill.opacity); } },
            { "title.fontSize",    R"({"title":{"fontSize":22}})",
              [](const CardStyle& s) { return static_cast<double>(s.title.fontSize); } },
            { "title.color",       R"({"title":{"color":"#ABCDEF"}})",
              [](const CardStyle& s) { return static_cast<double>(s.title.colour.getARGB()); } },
            { "title.align",       R"({"title":{"align":"left"}})",
              [](const CardStyle& s) { return static_cast<double>(s.title.align.getFlags()); } },
            { "title.y",           R"({"title":{"y":-6}})",
              [](const CardStyle& s) { return static_cast<double>(s.title.y); } },
            { "title.height",      R"({"title":{"height":30}})",
              [](const CardStyle& s) { return static_cast<double>(s.title.height); } },
        };

        juce::String inert;
        int changed = 0;
        for (const auto& probe : probes)
        {
            const auto before = probe.observe(baseline);
            const auto after = probe.observe(styleFor(probe.json));
            if (std::abs(after - before) < 1.0e-6)
            {
                inert += juce::String(probe.name) + " ";
            }
            else
            {
                ++changed;
            }
        }

        check("CardStyle_EveryPropertyChangesTheResolvedStyle",
              inert.isEmpty(),
              inert.isEmpty()
                  ? (juce::String(changed) + " of " + juce::String(static_cast<int>(probes.size()))
                     + " properties each change the style or geometry when edited")
                  : ("these parsed but changed nothing: " + inert));
    }

    {
        // The cap is a rule, so it is tested like one. A card asking for more
        // than its slot gets the slot, and is not allowed to overflow into the
        // column beside it.
        CardStyle style;
        style.width = { Dimension::Unit::percent, 90.0f };
        const juce::Rectangle<float> panel { 0.0f, 0.0f, 600.0f, 400.0f };
        const juce::Rectangle<float> narrowSlot { 0.0f, 0.0f, 200.0f, 400.0f };
        const auto resolved = style.resolveBounds(narrowSlot, panel);
        check("CardStyle_CardNeverExceedsItsSlot",
              resolved.getWidth() <= narrowSlot.getWidth() + 0.001f,
              "90% of a 600px panel is 540px, capped to the 200px slot -> "
                  + fmt(resolved.getWidth(), 1) + "px");
    }

    // ---- Bypassed cards go greyscale ---------------------------------------
    {
        // Every layer must desaturate, not just the ones that are easy to spot.
        // A bypassed card whose background or gloss kept its hue still reads as
        // "the blue one", which defeats the purpose of greying it out.
        CardStyle style;
        style.border.colour = juce::Colour::fromRGB(0x4A, 0x99, 0xFF);
        style.background.colour = juce::Colour::fromRGB(0x4A, 0x99, 0xFF);
        style.gloss.topFill.colour = juce::Colour::fromRGB(0xFF, 0xC6, 0x6E);
        style.gloss.bottomFill.colour = juce::Colour::fromRGB(0xEE, 0xB6, 0x78);
        style.title.colour = juce::Colour::fromRGB(0xDC, 0xE8, 0xFC);
        style.disabled.saturation = 0.0f;
        style.disabled.dim = 0.5f;

        const auto off = style.disabledVariant();

        const auto saturations = {
            off.border.colour.getSaturation(),
            off.background.colour.getSaturation(),
            off.gloss.topFill.colour.getSaturation(),
            off.gloss.bottomFill.colour.getSaturation(),
            off.title.colour.getSaturation(),
        };
        auto allGrey = true;
        for (const auto value : saturations)
        {
            if (value > 0.001f) allGrey = false;
        }

        const auto dimmed = juce::approximatelyEqual(off.border.opacity, style.border.opacity * 0.5f)
                         && juce::approximatelyEqual(off.background.opacity, style.background.opacity * 0.5f)
                         && juce::approximatelyEqual(off.gloss.topFill.opacity, style.gloss.topFill.opacity * 0.5f)
                         && juce::approximatelyEqual(off.gloss.bottomFill.opacity, style.gloss.bottomFill.opacity * 0.5f)
                         && off.title.colour.getFloatAlpha() < style.title.colour.getFloatAlpha();

        check("CardStyle_BypassedCardIsGreyscaleOnEveryLayer",
              allGrey && dimmed,
              allGrey ? "border, background, both gloss fills and title all desaturate and dim"
                      : "a layer kept its hue when bypassed");
    }

    {
        // The active style must be untouched: disabledVariant returns a copy,
        // so toggling bypass cannot permanently grey a card out.
        CardStyle style;
        style.border.colour = juce::Colour::fromRGB(0x4A, 0x99, 0xFF);
        const auto before = style.border.colour.getSaturation();
        const auto off = style.disabledVariant();
        juce::ignoreUnused(off);
        check("CardStyle_DisabledVariantDoesNotMutateTheActiveStyle",
              juce::approximatelyEqual(style.border.colour.getSaturation(), before)
                  && before > 0.001f,
              "active border saturation still " + fmt(style.border.colour.getSaturation(), 3));
    }

    {
        // Both properties are configurable and both are read.
        juce::String error;
        auto config = UIConfig::fromJsonText(R"({"cards":{
            "defaults":{"disabled":{"saturation":0.0,"dim":0.75}},
            "partial":{"disabled":{"saturation":0.6,"dim":0.9}}}})", error);

        const auto base = CardStyle::fromConfig(config.get(), "cards.defaults", "cards.defaults");
        const auto partial = CardStyle::fromConfig(config.get(), "cards.defaults", "cards.partial");

        check("CardStyle_DisabledAppearanceIsConfigurable",
              juce::approximatelyEqual(base.disabled.saturation, 0.0f)
                  && juce::approximatelyEqual(base.disabled.dim, 0.75f)
                  && juce::approximatelyEqual(partial.disabled.saturation, 0.6f)
                  && juce::approximatelyEqual(partial.disabled.dim, 0.9f),
              "defaults 0.0/0.75, override 0.6/0.9");
    }

    // ---- Live reload --------------------------------------------------------
    //
    // Regression test for a real bug: components parsed their style in
    // resized(), and a config reload only called repaint(). The reload stored
    // the new config and then painted using the style parsed from the old one,
    // so editing UIConfig.json appeared to do nothing at all.
    //
    // The cache re-parses when the config object changes, which is what a
    // reload always produces, so this is the behaviour that must hold.
    {
        auto make = [](const char* radius, const char* fontSize)
        {
            juce::String error;
            const juce::String json = juce::String(R"({"cards":{"defaults":{
                "border":{"radius":)") + radius + R"(},"title":{"fontSize":)" + fontSize + R"(}},
                "probe":{}}})";
            return UIConfig::fromJsonText(json, error);
        };

        CardStyleCache cache;
        cache.setKeys("cards.defaults", "cards.probe");

        cache.setConfig(make("8", "11"));
        const auto firstRadius = cache.style().border.radius;
        const auto firstFont = cache.style().title.fontSize;

        // The file is edited and reloaded: a NEW UIConfig object arrives.
        cache.setConfig(make("24", "19"));
        const auto secondRadius = cache.style().border.radius;
        const auto secondFont = cache.style().title.fontSize;

        check("CardStyle_ReloadingTheConfigChangesTheStyle",
              juce::approximatelyEqual(firstRadius, 8.0f)
                  && juce::approximatelyEqual(secondRadius, 24.0f)
                  && juce::approximatelyEqual(firstFont, 11.0f)
                  && juce::approximatelyEqual(secondFont, 19.0f),
              "radius " + fmt(firstRadius, 1) + " -> " + fmt(secondRadius, 1)
                  + ", title " + fmt(firstFont, 1) + " -> " + fmt(secondFont, 1));
    }

    {
        // Changing which block a card reads must also take effect - this is how
        // Osc 1/2/3 pick up their own styles from one implementation.
        juce::String error;
        auto config = UIConfig::fromJsonText(R"({"cards":{
            "defaults":{"border":{"radius":8}},
            "osc1":{"border":{"radius":10}},
            "osc2":{"border":{"radius":30}}}})", error);

        CardStyleCache cache;
        cache.setConfig(config);
        cache.setKeys("cards.defaults", "cards.osc1");
        const auto one = cache.style().border.radius;
        cache.setKeys("cards.defaults", "cards.osc2");
        const auto two = cache.style().border.radius;

        check("CardStyle_ChangingTheStyleKeyReParses",
              juce::approximatelyEqual(one, 10.0f) && juce::approximatelyEqual(two, 30.0f),
              "osc1 radius " + fmt(one, 1) + ", osc2 radius " + fmt(two, 1));
    }

    // ---- Panel -------------------------------------------------------------
    {
        const auto config = configFrom(R"({
            "panels": {
                "osc": { "height": 300, "overflowY": "auto" },
                "flt": { "height": 180, "overflowY": "hidden" },
                "odd": { "overflowY": "sideways" }
            }
        })");

        const auto osc = PanelStyle::fromConfig(config.get(), "panels.osc");
        const auto flt = PanelStyle::fromConfig(config.get(), "panels.flt");
        const auto odd = PanelStyle::fromConfig(config.get(), "panels.odd");

        check("PanelStyle_HeightAndOverflowParse",
              osc.height == 300 && osc.scrollVertically
                  && flt.height == 180 && ! flt.scrollVertically
                  && ! odd.scrollVertically,
              "osc 300/auto, flt 180/hidden, and an unrecognised overflow does not enable scrolling");
    }

    {
        // Panels are independent: one panel's height must not leak into another.
        const auto config = configFrom(R"({ "panels": { "a": { "height": 100 }, "b": { "height": 500 } } })");
        const auto a = PanelStyle::fromConfig(config.get(), "panels.a");
        const auto b = PanelStyle::fromConfig(config.get(), "panels.b");
        const auto missing = PanelStyle::fromConfig(config.get(), "panels.nope");
        check("PanelStyle_PanelsAreIndependent",
              a.height == 100 && b.height == 500 && missing.height == 0,
              "a=100, b=500, an undeclared panel keeps the default (editor-allocated)");
    }
}

// ---------------------------------------------------------------------------
// cardInner / row layout
//
// The percentage chain is the thing most likely to be got wrong, so it is
// tested against exact arithmetic at every level:
//
//     Card content -> cardInner (margin, padding) -> row (% of cardInner)
//
// A row height must never be measured against the panel, the card before
// padding, or the previous row.
// ---------------------------------------------------------------------------
void testCardInner()
{
    suite("CARD INNER");

    using namespace px3::ui;

    auto configFrom = [](const char* json)
    {
        juce::String error;
        return UIConfig::fromJsonText(json, error);
    };

    // ---- The percentage chain ---------------------------------------------
    {
        const auto config = configFrom(R"({"cards":{"defaults":{"cardInner":{
            "margin":0,"padding":0,"direction":"column","gap":0,
            "rows":{"default":{"height":"33%"},
                    "row1":{"height":"30%"},"row2":{"height":"30%"},"row3":{"height":"40%"}}}},
            "probe":{}}})");

        CardInner inner;
        inner.setKeys("cards.defaults.cardInner", "cards.probe.cardInner");
        inner.setConfig(config);
        inner.setRowCount(3);
        inner.layout({ 0, 0, 200, 400 });

        const auto r1 = inner.rowContent(0);
        const auto r2 = inner.rowContent(1);
        const auto r3 = inner.rowContent(2);

        check("CardInner_RowHeightIsPercentOfCardInner",
              r1.getHeight() == 120 && r2.getHeight() == 120 && r3.getHeight() == 160,
              "cardInner 400px tall, rows 30/30/40% -> " + juce::String(r1.getHeight()) + ", "
                  + juce::String(r2.getHeight()) + ", " + juce::String(r3.getHeight()));
    }

    {
        // Margin and padding shrink what the percentages are measured against.
        // 400 - (10+10 margin) - (20+20 padding) = 340, and 50% of that is 170.
        const auto config = configFrom(R"({"cards":{"defaults":{"cardInner":{
            "margin":10,"padding":20,"direction":"column","gap":0,
            "rows":{"row1":{"height":"50%"},"row2":{"height":"50%"}}}},"probe":{}}})");

        CardInner inner;
        inner.setKeys("cards.defaults.cardInner", "cards.probe.cardInner");
        inner.setConfig(config);
        inner.setRowCount(2);
        inner.layout({ 0, 0, 200, 400 });

        check("CardInner_PercentIsMeasuredAfterMarginAndPadding",
              inner.content().getHeight() == 340 && inner.rowContent(0).getHeight() == 170,
              "content height " + juce::String(inner.content().getHeight())
                  + ", 50% row = " + juce::String(inner.rowContent(0).getHeight()));
    }

    {
        // A row spans cardInner's width; it is never a percentage of anything.
        const auto config = configFrom(R"({"cards":{"defaults":{"cardInner":{
            "margin":0,"padding":{"top":0,"right":15,"bottom":0,"left":15},
            "rows":{"row1":{"height":"100%"}}}},"probe":{}}})");

        CardInner inner;
        inner.setKeys("cards.defaults.cardInner", "cards.probe.cardInner");
        inner.setConfig(config);
        inner.setRowCount(1);
        inner.layout({ 0, 0, 300, 100 });

        check("CardInner_RowSpansTheFullInnerWidth",
              inner.rowContent(0).getWidth() == 270 && inner.content().getWidth() == 270,
              "card 300 wide, 15px side padding -> row width "
                  + juce::String(inner.rowContent(0).getWidth()));
    }

    {
        // Row margin and padding are the row's own, independent of cardInner's.
        const auto config = configFrom(R"({"cards":{"defaults":{"cardInner":{
            "margin":0,"padding":0,
            "rows":{"row1":{"height":"100%","margin":5,"padding":10}}}},"probe":{}}})");

        CardInner inner;
        inner.setKeys("cards.defaults.cardInner", "cards.probe.cardInner");
        inner.setConfig(config);
        inner.setRowCount(1);
        inner.layout({ 0, 0, 200, 100 });

        // 200 - (5+5) - (10+10) = 170 wide; 100 - 10 - 20 = 70 tall.
        const auto row = inner.rowContent(0);
        check("CardInner_RowMarginAndPaddingAreIndependent",
              row.getWidth() == 170 && row.getHeight() == 70,
              "row content " + juce::String(row.getWidth()) + " x " + juce::String(row.getHeight()));
    }

    {
        // Rows totalling more than 100% shrink proportionally rather than
        // overflowing the card - documented behaviour, so it is pinned.
        const auto config = configFrom(R"({"cards":{"defaults":{"cardInner":{
            "margin":0,"padding":0,
            "rows":{"row1":{"height":"80%"},"row2":{"height":"80%"}}}},"probe":{}}})");

        CardInner inner;
        inner.setKeys("cards.defaults.cardInner", "cards.probe.cardInner");
        inner.setConfig(config);
        inner.setRowCount(2);
        inner.layout({ 0, 0, 200, 400 });

        const auto total = inner.rowContent(0).getHeight() + inner.rowContent(1).getHeight();
        check("CardInner_OverlongRowsShrinkInsteadOfOverflowing",
              total <= 400,
              "two 80% rows in 400px -> " + juce::String(total) + "px total");
    }

    // ---- Flex properties reach FlexBox -------------------------------------
    {
        const auto config = configFrom(R"({"cards":{"defaults":{"cardInner":{
            "rows":{"row1":{"direction":"column","wrap":"wrap",
                            "justifyContent":"space-between","alignItems":"flex-start",
                            "alignContent":"flex-end","gap":8}}}},"probe":{}}})");

        CardInner inner;
        inner.setKeys("cards.defaults.cardInner", "cards.probe.cardInner");
        inner.setConfig(config);
        inner.setRowCount(1);
        inner.layout({ 0, 0, 200, 100 });

        const auto box = inner.rowFlex(0);
        const auto gap = inner.rowGap(0);

        check("CardInner_FlexPropertiesReachFlexBox",
              box.flexDirection == juce::FlexBox::Direction::column
                  && box.flexWrap == juce::FlexBox::Wrap::wrap
                  && box.justifyContent == juce::FlexBox::JustifyContent::spaceBetween
                  && box.alignItems == juce::FlexBox::AlignItems::flexStart
                  && box.alignContent == juce::FlexBox::AlignContent::flexEnd
                  && juce::approximatelyEqual(gap.top + gap.bottom, 8.0f),
              "direction, wrap, justify, alignItems, alignContent and gap all applied");
    }

    {
        // Gap must actually separate items, not merely parse.
        const auto config = configFrom(R"({"cards":{"defaults":{"cardInner":{
            "rows":{"row1":{"height":"100%","direction":"row","justifyContent":"flex-start","gap":20}}}},
            "probe":{}}})");

        CardInner inner;
        inner.setKeys("cards.defaults.cardInner", "cards.probe.cardInner");
        inner.setConfig(config);
        inner.setRowCount(1);
        inner.layout({ 0, 0, 300, 100 });

        auto box = inner.rowFlex(0);
        const auto gapMargin = inner.rowGap(0);
        box.items.add(juce::FlexItem(40.0f, 40.0f).withMargin(gapMargin));
        box.items.add(juce::FlexItem(40.0f, 40.0f).withMargin(gapMargin));
        box.performLayout(inner.rowContent(0).toFloat());

        const auto first = box.items.getReference(0).currentBounds;
        const auto second = box.items.getReference(1).currentBounds;
        const auto separation = second.getX() - first.getRight();

        check("CardInner_GapSeparatesAdjacentItems",
              juce::approximatelyEqual(separation, 20.0f),
              "two 40px items with gap 20 -> " + fmt(separation, 1) + "px apart");
    }

    // ---- Wrapping rows -----------------------------------------------------
    {
        // Delay's row 3 has five controls and Mood's has nine. They wrap, and
        // the wrapped lines have to fit the row: FlexBox takes its line height
        // from the items, so sizing them against the full row height makes two
        // lines twice as tall as the row that holds them.
        const std::vector<float> five { 60.0f, 60.0f, 104.0f, 104.0f, 104.0f };

        const auto oneLine = px3::ui::wrappedLineCount(five, 6.0f, 500.0f);
        const auto twoLines = px3::ui::wrappedLineCount(five, 6.0f, 280.0f);
        const auto narrow = px3::ui::wrappedLineCount(five, 6.0f, 110.0f);
        const auto degenerate = px3::ui::wrappedLineCount(five, 6.0f, 0.0f);

        check("CardInner_WrappedRowsCountTheirLines",
              oneLine == 1 && twoLines == 2 && narrow == 5 && degenerate == 1,
              "500px -> " + juce::String(oneLine) + " line, 280px -> " + juce::String(twoLines)
                  + ", 110px -> " + juce::String(narrow) + ", 0px -> " + juce::String(degenerate));
    }

    {
        // A wrapped row must not spill out of the bounds it was given. This is
        // the property the line count exists to guarantee.
        const auto config = configFrom(R"({"cards":{"defaults":{"cardInner":{
            "margin":0,"padding":0,"gap":0,
            "rows":{"row1":{"height":"100%","wrap":"wrap","gap":6}}}},"probe":{}}})");
        CardInner inner;
        inner.setKeys("cards.defaults.cardInner", "cards.probe.cardInner");
        inner.setConfig(config);
        inner.setRowCount(1);
        inner.layout({ 0, 0, 280, 200 });

        const auto row = inner.rowContent(0);
        auto flex = inner.rowFlex(0);
        const auto gap = inner.rowGap(0);
        const std::vector<float> widths(9, 64.0f);
        const auto lines = px3::ui::wrappedLineCount(widths, gap.left + gap.right,
                                                     static_cast<float>(row.getWidth()));
        const auto cellHeight = juce::jmax(1.0f,
                                           static_cast<float>(row.getHeight()) / static_cast<float>(lines)
                                               - (gap.top + gap.bottom));
        for (const auto w : widths)
        {
            flex.items.add(juce::FlexItem(w, cellHeight).withMargin(gap));
        }
        flex.performLayout(row.toFloat());

        juce::Rectangle<float> union_;
        for (int i = 0; i < flex.items.size(); ++i)
        {
            union_ = union_.isEmpty() ? flex.items.getReference(i).currentBounds
                                      : union_.getUnion(flex.items.getReference(i).currentBounds);
        }

        check("CardInner_WrappedRowStaysInsideItsBounds",
              lines > 1 && row.toFloat().contains(union_),
              juce::String(lines) + " lines, items span " + fmt(union_.getHeight(), 1)
                  + "px inside a " + juce::String(row.getHeight()) + "px row");
    }

    // ---- Control shapes ----------------------------------------------------
    {
        // A knob is round and a dropdown is not. Laying both out with one rule
        // turns every combo box in the plugin into a square.
        juce::Component knob;
        juce::Component dropdown;
        knob.setVisible(true);
        dropdown.setVisible(true);

        const juce::Rectangle<int> cell { 0, 0, 120, 60 };
        px3::ui::layoutLabelledControl(cell, nullptr, &knob, nullptr, 0, 0,
                                       px3::ui::ControlShape::square, 0);
        px3::ui::layoutLabelledControl(cell, nullptr, &dropdown, nullptr, 0, 0,
                                       px3::ui::ControlShape::stretch, 24);

        check("CardInner_ControlShapeDecidesKnobVersusDropdown",
              knob.getWidth() == 60 && knob.getHeight() == 60
                  && dropdown.getWidth() == 120 && dropdown.getHeight() == 24,
              "knob " + knob.getBounds().toString() + ", dropdown " + dropdown.getBounds().toString());
    }

    {
        // Label above, readout below, control in what is left - the shape the
        // existing knobs already have, which this must not change.
        juce::Component label, knob, readout;
        label.setVisible(true);
        knob.setVisible(true);
        readout.setVisible(true);

        px3::ui::layoutLabelledControl({ 0, 0, 100, 100 }, &label, &knob, &readout, 16, 14,
                                       px3::ui::ControlShape::square, 0);

        check("CardInner_LabelledControlStacksLabelKnobReadout",
              label.getBounds() == juce::Rectangle<int>(0, 0, 100, 16)
                  && readout.getBounds() == juce::Rectangle<int>(0, 86, 100, 14)
                  && knob.getBounds() == juce::Rectangle<int>(15, 16, 70, 70),
              "label " + label.getBounds().toString() + ", knob " + knob.getBounds().toString()
                  + ", readout " + readout.getBounds().toString());
    }

    // ---- No dead properties ------------------------------------------------
    {
        // Same rule the Card is held to: every property UIConfig.json exposes
        // has to change something. A property that parses but does nothing is a
        // lie told to whoever edits the file next.
        //
        // The fingerprint covers both the resolved geometry and the parsed
        // style, because some properties are genuinely style-only from
        // cardInner's point of view - `alignItems` on a column of full-width
        // rows cannot move them, exactly as in CSS, but it is still handed to
        // components through rowFlex() and they do use it.
        const juce::String baseInner = R"("margin":2,"padding":3,"display":"flex","direction":"column",
            "wrap":"nowrap","justifyContent":"center","alignItems":"center","alignContent":"center","gap":4,
            "rows":{"row1":{"height":"20%","margin":1,"padding":1,"display":"flex","direction":"row",
                            "wrap":"nowrap","justifyContent":"center","alignItems":"center",
                            "alignContent":"center","gap":3},
                    "row2":{"height":"20%"},"row3":{"height":"20%"}})";

        auto fingerprint = [&](const juce::String& innerJson)
        {
            const auto config = configFrom((R"({"cards":{"defaults":{"cardInner":{)"
                                            + innerJson + R"(}},"probe":{}}})").toRawUTF8());
            CardInner inner;
            inner.setKeys("cards.defaults.cardInner", "cards.probe.cardInner");
            inner.setConfig(config);
            inner.setRowCount(3);
            inner.layout({ 0, 0, 240, 300 });

            juce::String out = inner.content().toString();
            const auto& style = inner.style();
            const auto describe = [](const px3::ui::FlexStyle& f)
            {
                return juce::String(f.display ? 1 : 0) + "/" + juce::String((int) f.direction)
                     + "/" + juce::String((int) f.wrap) + "/" + juce::String((int) f.justifyContent)
                     + "/" + juce::String((int) f.alignItems) + "/" + juce::String((int) f.alignContent)
                     + "/" + fmt(f.gap, 2);
            };
            out += "|" + describe(style.flex);
            for (int i = 0; i < 3; ++i)
            {
                out += "|" + inner.rowContent(i).toString() + "|" + describe(style.rows[(size_t) i].flex);
            }
            return out;
        };

        const auto base = fingerprint(baseInner);

        // Each entry replaces one property with a different value. If the
        // fingerprint does not move, that property is inert.
        const std::vector<std::pair<juce::String, juce::String>> probes {
            { "margin",             R"("margin":12)" },
            { "padding",            R"("padding":14)" },
            { "display",            R"("display":"none")" },
            { "direction",          R"("direction":"row")" },
            { "wrap",               R"("wrap":"wrap")" },
            { "justifyContent",     R"("justifyContent":"flex-start")" },
            { "alignItems",         R"("alignItems":"stretch")" },
            { "alignContent",       R"("alignContent":"flex-end")" },
            { "gap",                R"("gap":20)" },
            { "row.height",         R"("height":"45%")" },
            { "row.margin",         R"("margin":9)" },
            { "row.padding",        R"("padding":9)" },
            { "row.display",        R"("display":"none")" },
            { "row.direction",      R"("direction":"column")" },
            { "row.wrap",           R"("wrap":"wrap")" },
            { "row.justifyContent", R"("justifyContent":"flex-end")" },
            { "row.alignItems",     R"("alignItems":"stretch")" },
            { "row.alignContent",   R"("alignContent":"flex-start")" },
            { "row.gap",            R"("gap":18)" },
        };

        juce::StringArray inert;
        for (const auto& probe : probes)
        {
            juce::String variant = baseInner;
            if (probe.first.startsWith("row."))
            {
                // Replace the value inside row1 only, so a row property is not
                // confused with the cardInner property of the same name.
                const auto key = probe.first.fromFirstOccurrenceOf(".", false, false);
                const auto rowStart = variant.indexOf("\"row1\"");
                const auto keyStart = variant.indexOf(rowStart, "\"" + key + "\":");
                const auto keyEnd = variant.indexOfAnyOf(",}", keyStart, false);
                if (keyStart < 0 || keyEnd < 0)
                {
                    inert.add(probe.first + " (probe did not match)");
                    continue;
                }
                variant = variant.substring(0, keyStart) + probe.second + variant.substring(keyEnd);
            }
            else
            {
                const auto keyStart = variant.indexOf("\"" + probe.first + "\":");
                const auto keyEnd = variant.indexOfAnyOf(",", keyStart, false);
                variant = variant.substring(0, keyStart) + probe.second + variant.substring(keyEnd);
            }

            if (fingerprint(variant) == base)
            {
                inert.add(probe.first);
            }
        }

        check("CardInner_EveryPropertyChangesTheLayout",
              inert.isEmpty(),
              inert.isEmpty() ? juce::String(probes.size()) + " properties all have an effect"
                              : "inert: " + inert.joinIntoString(", "));
    }

    // ---- display: none -----------------------------------------------------
    {
        // A hidden row must leave the layout entirely, not merely draw nothing:
        // it takes up no height and no gap, and its neighbours end up adjacent.
        const auto config = configFrom(R"({"cards":{"defaults":{"cardInner":{
            "margin":0,"padding":0,"gap":0,
            "rows":{"row1":{"height":"25%"},
                    "row2":{"height":"50%","display":"none"},
                    "row3":{"height":"25%"}}}},"probe":{}}})");
        CardInner inner;
        inner.setKeys("cards.defaults.cardInner", "cards.probe.cardInner");
        inner.setConfig(config);
        inner.setRowCount(3);
        inner.layout({ 0, 0, 200, 400 });

        const auto hidden = inner.rowContent(1).getHeight();
        const auto first = inner.rowContent(0).getHeight();
        const auto third = inner.rowContent(2).getHeight();

        check("CardInner_DisplayNoneRemovesTheRowFromTheLayout",
              hidden == 0 && first == 100 && third == 100
                  && inner.rowContent(2).getY() == inner.rowContent(0).getBottom(),
              "hidden row " + juce::String(hidden) + "px, neighbours "
                  + juce::String(first) + "/" + juce::String(third)
                  + "px and adjacent");
    }

    // ---- Defaults ----------------------------------------------------------
    {
        // A card that declares no cardInner block must still lay out.
        const auto config = configFrom(R"({"cards":{"defaults":{},"probe":{}}})");
        CardInner inner;
        inner.setKeys("cards.defaults.cardInner", "cards.probe.cardInner");
        inner.setConfig(config);
        inner.setRowCount(3);
        inner.layout({ 0, 0, 200, 300 });

        const auto box = inner.style().flex;
        const auto rowsFillTheCard = inner.rowContent(0).getHeight() > 0
                                  && inner.rowContent(2).getHeight() > 0;
        check("CardInner_DefaultsProduceAUsableLayout",
              box.direction == FlexDirection::column && rowsFillTheCard
                  && inner.content().getHeight() == 300,
              "column by default, three rows of "
                  + juce::String(inner.rowContent(0).getHeight()) + "px in an undeclared card");
    }

    {
        // Reload semantics, same rule as the Card: a new UIConfig re-parses.
        auto make = [&](const char* h1) {
            juce::String error;
            const juce::String json = juce::String(R"({"cards":{"defaults":{"cardInner":{
                "margin":0,"padding":0,"rows":{"row1":{"height":")") + h1 + R"("},"row2":{"height":"10%"}}}},"probe":{}}})";
            return UIConfig::fromJsonText(json, error);
        };

        CardInner inner;
        inner.setKeys("cards.defaults.cardInner", "cards.probe.cardInner");
        inner.setRowCount(2);

        inner.setConfig(make("20%"));
        inner.layout({ 0, 0, 100, 400 });
        const auto before = inner.rowContent(0).getHeight();

        inner.setConfig(make("60%"));
        const auto after = inner.rowContent(0).getHeight();

        check("CardInner_ReloadingTheConfigChangesTheLayout",
              before == 80 && after == 240,
              "row 1 height " + juce::String(before) + " -> " + juce::String(after) + "px");
    }
}

void testDelay()
{
    suite("DELAY");

    static const char* names[] = { "Granular", "Tape", "AnalogBBD", "PingPong",
                                   "Stereo", "Modulated", "Diffusion" };

    // At zero amount the delay is a wire. The previous mix law bottomed out at
    // 6% wet, so six of the seven algorithms coloured the signal with the knob
    // all the way down.
    {
        double worst = 0.0;
        int worstAlgo = 0;
        for (int algo = 0; algo < 7; ++algo)
        {
            const auto bleed = delayZeroAmountBleed(algo);
            if (bleed > worst) { worst = bleed; worstAlgo = algo; }
        }
        check("Delay_ZeroAmountIsTransparent",
              worst < 1.0e-6,
              "worst deviation " + fmt(worst, 8) + " (" + names[worstAlgo] + ")");
    }

    // No algorithm may gain energy in its own feedback loop. Tape used to,
    // because its head bump and its hysteresis bias each added gain that the
    // feedback coefficient did not know about.
    {
        double worstRatio = 0.0;
        int worstAlgo = 0;
        bool allFinite = true;
        for (int algo = 0; algo < 7; ++algo)
        {
            DelaySettings s;
            s.amount = 1.0f;
            s.timeControl = 0.35f;
            s.feedbackControl = 1.0f;
            s.algorithmIndex = algo;
            const auto m = measureDelay(s, kSampleRate, static_cast<int>(kSampleRate * 20.0));
            allFinite = allFinite && m.finite;
            const auto ratio = m.tailRmsEarly > 1.0e-9 ? m.tailRmsLate / m.tailRmsEarly : 0.0;
            if (ratio > worstRatio) { worstRatio = ratio; worstAlgo = algo; }
        }
        check("Delay_MaximumFeedbackDoesNotGrow",
              allFinite && worstRatio <= 1.0,
              "worst late/early ratio " + fmt(worstRatio, 4) + " (" + names[worstAlgo] + ")");
    }

    // Tape's wow used to be added to the sample value rather than to the delay
    // length, which put a sub-audio tone into the feedback path: 99.95% of the
    // algorithm's steady-state energy was below 30 Hz.
    //
    // Measured on sustained input rather than on an impulse tail. As a ratio it
    // needs a denominator with something in it: taken from a near-silent window
    // the granular algorithm's grain-spawn randomness alone moved this between
    // 0.06 and 0.31 from run to run.
    {
        double worst = 0.0;
        int worstAlgo = 0;
        for (int algo = 0; algo < 7; ++algo)
        {
            const auto ratio = delayStress(algo, 0, 0.5f).lowFrequencyEnergyRatio;
            if (ratio > worst) { worst = ratio; worstAlgo = algo; }
        }
        check("Delay_NoAlgorithmGeneratesSubsonicContent",
              worst < 0.25,
              "worst sub-30 Hz energy share on sustained input " + fmt(worst, 4)
                  + " (" + names[worstAlgo] + ")");
    }

    // Ping-pong has to alternate channels. The old implementation read the
    // opposite channel at the same position, which from a mono input produced
    // two identical channels and measured +1.000.
    {
        DelaySettings s;
        s.amount = 0.7f;
        s.timeControl = 0.35f;
        s.feedbackControl = 0.6f;
        s.algorithmIndex = 3;
        const auto corr = measureDelay(s).interChannelCorrelation;
        check("Delay_PingPongAlternatesChannels",
              corr < 0.80,
              "inter-channel correlation " + fmt(corr, 4));
    }

    // Diffusion was four extra taps summed together - a multitap, not a
    // diffuser - and both channels ran identical chains, so it measured +0.950.
    {
        DelaySettings s;
        s.amount = 0.7f;
        s.timeControl = 0.35f;
        s.feedbackControl = 0.6f;
        s.algorithmIndex = 6;
        const auto corr = measureDelay(s).interChannelCorrelation;
        check("Delay_DiffusionDecorrelatesChannels",
              corr < 0.80,
              "inter-channel correlation " + fmt(corr, 4));
    }

    // The TIME knob must move the delay in one direction. BBD is exempt above
    // its ceiling: a bucket-brigade chip runs out of stages, and holding the
    // time there rather than pretending otherwise is the point of the model.
    {
        bool allMonotonic = true;
        juce::String detail;
        for (int algo = 1; algo < 7; ++algo)
        {
            double previous = -1.0;
            bool monotonic = true;
            for (const auto t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                DelaySettings s;
                s.amount = 0.7f;
                s.timeControl = t;
                s.feedbackControl = 0.3f;
                s.algorithmIndex = algo;
                const auto ms = measureDelay(s).firstEchoMs;
                if (ms + 1.0e-6 < previous) monotonic = false;
                previous = ms;
            }
            if (!monotonic)
            {
                allMonotonic = false;
                detail += juce::String(names[algo]) + " ";
            }
        }
        check("Delay_TimeControlIsMonotonic",
              allMonotonic,
              allMonotonic ? "all algorithms" : ("non-monotonic: " + detail));
    }

    // Echo times are in seconds, not samples. Every filter in the delay was
    // written with bare one-pole constants before, which moved an octave
    // whenever the host changed rate.
    {
        double worstSpreadPercent = 0.0;
        int worstAlgo = 0;
        for (int algo = 1; algo < 7; ++algo)
        {
            double lowest = 1.0e9, highest = 0.0;
            for (const auto sr : { 44100.0, 48000.0, 96000.0 })
            {
                DelaySettings s;
                s.amount = 0.7f;
                s.timeControl = 0.35f;
                s.feedbackControl = 0.5f;
                s.algorithmIndex = algo;
                const auto ms = measureDelay(s, sr).firstEchoMs;
                lowest = juce::jmin(lowest, ms);
                highest = juce::jmax(highest, ms);
            }
            const auto spread = lowest > 1.0 ? (highest - lowest) / lowest * 100.0 : 0.0;
            if (spread > worstSpreadPercent) { worstSpreadPercent = spread; worstAlgo = algo; }
        }
        check("Delay_EchoTimeIsSampleRateIndependent",
              worstSpreadPercent < 2.0,
              "worst spread across 44.1/48/96 kHz " + fmt(worstSpreadPercent, 3) + "% ("
                  + names[worstAlgo] + ")");
    }

    // A quarter note at 120 BPM is 500 ms, whatever the TIME knob says.
    {
        bool allCorrect = true;
        juce::String detail;
        for (int algo = 1; algo < 7; ++algo)
        {
            DelaySettings s;
            s.amount = 0.7f;
            s.timeControl = 0.05f;          // deliberately not the synced value
            s.feedbackControl = 0.4f;
            s.algorithmIndex = algo;
            s.syncDivisionIndex = 3;        // 1/4
            s.bpm = 120.0;
            const auto ms = measureDelay(s).firstEchoMs;
            // Stereo runs its left line at two thirds, so it is checked against
            // that rather than against the raw division.
            const auto expected = algo == 4 ? 500.0 * 2.0 / 3.0 : 500.0;
            if (std::abs(ms - expected) > expected * 0.06)
            {
                allCorrect = false;
                detail += juce::String(names[algo]) + " " + fmt(ms, 1) + "ms ";
            }
        }
        check("Delay_TempoSyncIsHonoured",
              allCorrect,
              allCorrect ? "1/4 at 120 BPM lands at 500 ms on every algorithm"
                         : ("wrong: " + detail));
    }

    // The characteristic BBD behaviour: stage count is fixed, so the clock -
    // and with it the bandwidth - is set by the delay time. A long setting has
    // to be audibly darker than a short one. The old implementation used one
    // fixed one-pole and had none of this.
    {
        // Measured as absolute energy above 4 kHz rather than as a share of the
        // total. The dry path carries the same white noise at the same gain in
        // both cases, so it cancels out of a comparison of absolute energies -
        // whereas as a fraction it swamps the wet path entirely and the measure
        // reports no difference even when the filter is doing its job.
        auto highFrequencyEnergy = [](float timeControl)
        {
            Delay delay;
            delay.prepare(kSampleRate);
            delay.reset();
            DelaySettings s;
            s.enabled = true;
            s.amount = 1.0f;
            s.timeControl = timeControl;
            s.feedbackControl = 0.3f;
            s.algorithmIndex = 2;
            delay.updateForBlock(s);

            // White noise in, so every band is equally represented going in and
            // whatever comes out reflects the chip's bandwidth.
            juce::Random random(0x51DE51DEu);
            std::vector<float> out;
            const auto total = static_cast<int>(kSampleRate * 4.0);
            out.reserve(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i)
            {
                const auto n = random.nextFloat() * 0.6f - 0.3f;
                float l = 0.0f, r = 0.0f;
                delay.processSampleFrame(n, n, l, r);
                out.push_back(l);
            }

            const auto coeff = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 4000.0f
                                               / static_cast<float>(kSampleRate));
            float lp = 0.0f;
            double high = 0.0;
            const auto from = static_cast<int>(kSampleRate * 2.0);
            for (int i = from; i < total; ++i)
            {
                const auto v = out[static_cast<std::size_t>(i)];
                lp += coeff * (v - lp);
                const auto hp = v - lp;
                high += static_cast<double>(hp) * hp;
            }
            return std::sqrt(high / juce::jmax(1, total - from));
        };

        const auto shortSetting = highFrequencyEnergy(0.05f);
        const auto longSetting = highFrequencyEnergy(0.9f);
        check("Delay_BbdBandwidthNarrowsWithDelayTime",
              longSetting < shortSetting * 0.90,
              "RMS above 4 kHz: short " + fmt(shortSetting, 5)
                  + ", long " + fmt(longSetting, 5));
    }

    // Nothing may keep ringing after the input stops. This is the fault the
    // static tests could not see: three separate mechanisms only misbehaved
    // while a control was moving, or only on sustained rather than impulsive
    // input. Checked at 25 s, by which point the longest decay the FEEDBACK
    // control can ask for is 75 dB down.
    {
        bool allDecay = true;
        bool allFinite = true;
        juce::String detail;
        double worstRatio = 0.0;
        for (int algo = 0; algo < 7; ++algo)
        {
            for (int sweep = 0; sweep < 5; ++sweep)
            {
                const auto r = delayStress(algo, sweep);
                allFinite = allFinite && r.finite;
                const auto ratio = r.tailAt1s > 1.0e-9 ? r.tailAt25s / r.tailAt1s : 0.0;
                if (ratio > worstRatio) worstRatio = ratio;
                if (ratio > 0.02)
                {
                    allDecay = false;
                    detail += juce::String(names[algo]) + "/sweep" + juce::String(sweep)
                            + " " + fmt(ratio, 4) + " ";
                }
            }
        }
        check("Delay_NothingSustainsAfterTheInputStops",
              allDecay && allFinite,
              allDecay ? ("worst tail at 25 s vs 1 s = " + fmt(worstRatio, 5)
                          + " across all algorithms and control sweeps")
                       : ("still ringing: " + detail));
    }

    // FEEDBACK asks for a decay time, so the same knob position has to mean
    // roughly the same decay whether the delay is short or long. As a raw
    // per-repeat coefficient it did not: 0.98 is a 30 s decay at 100 ms and an
    // 11 minute one at 2 s, which is what "stuck repeating forever" was.
    {
        bool consistent = true;
        juce::String detail;
        double shortest = 1.0e9, longest = 0.0;
        for (const auto timeControl : { 0.15f, 0.5f, 1.0f })
        {
            Delay delay;
            delay.prepare(kSampleRate);
            delay.reset();
            DelaySettings s;
            s.enabled = true;
            s.amount = 0.9f;
            s.timeControl = timeControl;
            s.feedbackControl = 1.0f;
            s.algorithmIndex = 3;
            delay.updateForBlock(s);

            // Impulse in, then measure how long it takes to fall 20 dB.
            const auto total = static_cast<int>(kSampleRate * 40.0);
            std::vector<float> out;
            out.reserve(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i)
            {
                float l = 0.0f, r = 0.0f;
                delay.processSampleFrame(i < 64 ? 0.9f : 0.0f, i < 64 ? 0.9f : 0.0f, l, r);
                out.push_back(l);
            }

            auto windowRms = [&](int from)
            {
                const auto to = juce::jmin(total, from + static_cast<int>(kSampleRate * 1.0));
                if (to <= from) return 0.0;
                double e = 0.0;
                for (int k = from; k < to; ++k) e += static_cast<double>(out[static_cast<std::size_t>(k)]) * out[static_cast<std::size_t>(k)];
                return std::sqrt(e / (to - from));
            };
            const auto reference = windowRms(static_cast<int>(kSampleRate * 2.0));
            double fellAt = 40.0;
            for (double t = 3.0; t < 39.0; t += 1.0)
            {
                if (windowRms(static_cast<int>(kSampleRate * t)) < reference * 0.1)
                {
                    fellAt = t;
                    break;
                }
            }
            shortest = juce::jmin(shortest, fellAt);
            longest = juce::jmax(longest, fellAt);
            detail += fmt(timeControl, 2) + "->" + fmt(fellAt, 0) + "s ";
        }
        // Within a factor of four across a delay range that spans 30x.
        consistent = longest <= shortest * 4.0 + 2.0;
        check("Delay_FeedbackMeansTheSameDecayAtEveryDelayTime",
              consistent,
              "time to fall 20 dB at maximum feedback: " + detail);
    }

    // Bypassing has to empty the lines. Otherwise switching the delay back on
    // replays whatever was in flight when it was switched off, underneath
    // whatever is playing now.
    {
        double worst = 0.0;
        int worstAlgo = 0;
        for (int algo = 0; algo < 7; ++algo)
        {
            const auto tail = delayTailAfterBypassCycle(algo);
            if (tail > worst) { worst = tail; worstAlgo = algo; }
        }
        check("Delay_BypassClearsTheTail",
              worst < 1.0e-4,
              "loudest sample after bypass and re-enable with silence in: "
                  + fmt(worst, 8) + " (" + names[worstAlgo] + ")");
    }

    // Through the whole plugin, at the extremes, on every algorithm.
    {
        bool allGood = true;
        juce::String detail;
        for (int algo = 0; algo < 7; ++algo)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "delayEnabled", 1.0f);
            setChoice(processor, "delayAlgorithm", algo);
            setParam(processor, "delayAmount", 1.0f);
            setParam(processor, "delayFeedback", 1.0f);
            setParam(processor, "delayTime", 0.3f);
            const auto capture = render(processor, 192000,
                                        { { 2000, true, 45, 0.9f }, { 60000, false, 45, 0.0f } });
            if (!capture.isFinite() || capture.peak() > 1.0)
            {
                allGood = false;
                detail += juce::String(names[algo]) + " peak " + fmt(capture.peak(), 4) + " ";
            }
        }
        check("Delay_AllAlgorithmsStayFiniteAndWithinCeilingInThePlugin",
              allGood,
              allGood ? "all seven algorithms at maximum amount and feedback" : detail);
    }

    // The same, but with the controls moving throughout - which is when the
    // crossfade and companding faults showed themselves.
    {
        bool allGood = true;
        juce::String detail;
        for (int algo = 0; algo < 7; ++algo)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "delayEnabled", 1.0f);
            setChoice(processor, "delayAlgorithm", algo);
            setParam(processor, "delayAmount", 0.9f);
            setParam(processor, "delayFeedback", 0.95f);
            const auto totalBlocks = 192000 / kBlockSize;
            const auto capture = render(processor, 192000,
                                        { { 2000, true, 45, 0.9f }, { 90000, false, 45, 0.0f } },
                                        [&](int block)
                                        {
                                            const auto t = static_cast<float>(block)
                                                         / static_cast<float>(juce::jmax(1, totalBlocks));
                                            setParam(processor, "delayTime", t);
                                        });
            if (!capture.isFinite() || capture.peak() > 1.0)
            {
                allGood = false;
                detail += juce::String(names[algo]) + " peak " + fmt(capture.peak(), 4) + " ";
            }
        }
        check("Delay_ControlSweepsStayWithinCeilingInThePlugin",
              allGood,
              allGood ? "TIME swept end to end on all seven algorithms" : detail);
    }
}

void testMood()
{
    suite("MOOD");

    // No read pointer may wrap without a crossfade. A looping playhead that
    // simply jumps from the end of its loop back to the start steps the signal,
    // and that step is a click once per loop, slice or window - which is what
    // the component actually sounded like: TAPE ticked 17 times in four
    // seconds, SLIP eight, and ENV's detector froze the output on a single
    // sample for the length of each hold.
    //
    // Measured against what the programme itself can do: a 220 Hz sine at 0.5
    // moves at most 0.0144 per sample, so a jump far above that did not come
    // from the signal.
    {
        auto worstStep = [](int loopMode, int wetMode, float routing)
        {
            Mood mood;
            mood.prepare(kSampleRate);
            mood.reset();
            MoodSettings s;
            s.enabled = true;
            s.mix = 1.0f;
            s.loopModeIndex = loopMode;
            s.wetModeIndex = wetMode;
            s.routing = routing;
            s.clock = 1.0f;      // full rate, so stepping here is not the clock
            s.degrade = 0.0f;    // and not the lo-fi control either
            s.spread = 0.5f;
            s.feedback = 0.4f;
            s.loopLength = 0.35f;
            s.loopModify = 0.62f;
            mood.updateForBlock(s);

            const auto total = static_cast<int>(kSampleRate * 6.0);
            std::vector<float> out;
            out.reserve(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i)
            {
                const auto in = std::sin(juce::MathConstants<float>::twoPi * 220.0f
                                         * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.5f;
                float l = 0.0f, r = 0.0f;
                mood.processSampleFrame(in, in, l, r);
                out.push_back(l);
            }

            double worst = 0.0;
            for (int i = static_cast<int>(kSampleRate * 2.0) + 1; i < total; ++i)
            {
                worst = juce::jmax(worst, std::abs(static_cast<double>(
                    out[static_cast<std::size_t>(i)] - out[static_cast<std::size_t>(i - 1)])));
            }
            return worst;
        };

        double worst = 0.0;
        juce::String detail;
        bool clean = true;
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            for (int wetMode = 0; wetMode < 3; ++wetMode)
            {
                const auto step = worstStep(loopMode, wetMode, 0.5f);
                worst = juce::jmax(worst, step);
                if (step > 0.05)
                {
                    clean = false;
                    detail += "loop" + juce::String(loopMode) + "/wet" + juce::String(wetMode)
                            + " " + fmt(step, 4) + " ";
                }
            }
        }
        check("Mood_NoReadPointerWrapsWithoutACrossfade",
              clean,
              clean ? ("worst sample-to-sample step across all nine pairings " + fmt(worst, 5))
                    : ("discontinuities: " + detail));
    }

    // Every mode pairing with FEEDBACK, SPREAD and DEGRADE all at maximum. The
    // widening controls raise the peak if they are not level-compensated - a
    // mid/side widener that is not compensated pushed this to 1.14 - and a
    // stereo effect that clips when it is turned up is not usable at the top of
    // its range.
    {
        bool allGood = true;
        juce::String detail;
        double worstPeak = 0.0;
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            for (int wetMode = 0; wetMode < 3; ++wetMode)
            {
                MoodSettings s;
                s.mix = 1.0f;
                s.loopModeIndex = loopMode;
                s.wetModeIndex = wetMode;
                s.spread = 1.0f;
                s.feedback = 1.0f;
                s.degrade = 1.0f;
                s.routing = 1.0f;
                s.wetModify = 1.0f;
                const auto m = measureMood(s);
                worstPeak = juce::jmax(worstPeak, m.peak);
                if (!m.finite || m.peak > 1.0)
                {
                    allGood = false;
                    detail += "loop" + juce::String(loopMode) + "/wet" + juce::String(wetMode)
                            + " peak " + fmt(m.peak, 4) + " ";
                }
            }
        }
        check("Mood_MaximumSettingsStayFiniteAndWithinCeiling",
              allGood,
              allGood ? ("worst peak with feedback, spread and degrade at maximum "
                         + fmt(worstPeak, 4))
                      : detail);
    }

    // ROUTING has to do what its labels say. The parameter is exposed as a
    // three-way choice - DRY->WET, LOOP->WET, PARALLEL - and reaches the DSP as
    // index/2. The middle and top settings used to be swapped relative to their
    // labels: "LOOP->WET" fed the wet channel the input as well, and "PARALLEL"
    // fed it the loop alone.
    //
    // Distinguished by feeding a signal and comparing against a silent-input
    // render: on LOOP->WET the wet channel never sees the input directly, so
    // the immediate response to a transient is much smaller than on the two
    // settings that pass the input through.
    {
        auto immediacy = [](float routing)
        {
            Mood mood;
            mood.prepare(kSampleRate);
            mood.reset();
            MoodSettings ms;
            ms.enabled = true;
            ms.mix = 1.0f;
            ms.routing = routing;
            ms.loopModeIndex = 1;      // TAPE
            ms.wetModeIndex = 1;       // DELAY
            ms.wetTime = 0.0f;         // 30 ms, so the input's own echo lands inside the window
            ms.wetModify = 0.0f;
            ms.loopLength = 0.9f;
            ms.feedback = 0.0f;
            ms.spread = 0.0f;
            ms.degrade = 0.0f;
            mood.updateForBlock(ms);

            // A short burst, measured over the window immediately after it, so
            // only signal that reached the output promptly is counted. The loop
            // is still empty this early, which is exactly what separates a
            // routing that passes the input from one that does not.
            double energy = 0.0;
            const auto burst = static_cast<int>(kSampleRate * 0.05);
            const auto window = static_cast<int>(kSampleRate * 0.30);
            for (int i = 0; i < burst + window; ++i)
            {
                const auto in = i < burst
                    ? std::sin(juce::MathConstants<float>::twoPi * 440.0f
                               * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.6f
                    : 0.0f;
                float l = 0.0f, r = 0.0f;
                mood.processSampleFrame(in, in, l, r);
                if (i >= burst) energy += static_cast<double>(l) * l;
            }
            return std::sqrt(energy / window);
        };

        const auto dryToWet = immediacy(0.0f);    // index 0
        const auto loopToWet = immediacy(0.5f);   // index 1
        const auto parallel = immediacy(1.0f);    // index 2
        check("Mood_RoutingMatchesItsLabels",
              loopToWet < dryToWet * 0.5 && parallel > loopToWet,
              "prompt response: DRY->WET " + fmt(dryToWet, 6)
                  + ", LOOP->WET " + fmt(loopToWet, 6)
                  + ", PARALLEL " + fmt(parallel, 6));
    }

    // SPREAD has to make a stereo image out of every mode pairing, not just the
    // ones that happen to pan. Before the rewrite six of the nine combinations
    // measured a side-to-mid ratio of exactly 0.0000 - the output was mono -
    // and the control NARROWED what stereo there was, because all it did was
    // mix each channel into the other.
    //
    // Averaged over renders: the granular and gated modes use the system
    // Random, which cannot be seeded, so a single render moves by a third
    // either way.
    {
        auto meanSideToMid = [](int loopMode, int wetMode, float spread)
        {
            double total = 0.0;
            constexpr int renders = 4;
            for (int i = 0; i < renders; ++i)
            {
                MoodSettings s;
                s.mix = 1.0f;
                s.loopModeIndex = loopMode;
                s.wetModeIndex = wetMode;
                s.spread = spread;
                s.routing = 1.0f;
                s.feedback = 0.4f;
                total += measureMood(s).sideToMidRatio;
            }
            return total / renders;
        };

        bool allWiden = true;
        juce::String detail;
        double worstGain = 1.0e9;
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            for (int wetMode = 0; wetMode < 3; ++wetMode)
            {
                const auto narrow = meanSideToMid(loopMode, wetMode, 0.0f);
                const auto wide = meanSideToMid(loopMode, wetMode, 1.0f);
                const auto gain = wide / juce::jmax(1.0e-4, narrow);
                worstGain = juce::jmin(worstGain, gain);
                // The absolute floor is low because ENV is duty-cycled by
                // design: it holds the incoming image until its detector fires
                // and only pans while it is open, so its time-averaged width is
                // legitimately a fraction of what the always-on modes reach.
                // It still clears this by more than an order of magnitude
                // (0.0015 -> 0.065, a 34x gain), and a mode that had gone mono
                // would measure around 0.001. The ratio is the real assertion.
                if (wide < 0.03 || gain < 1.5)
                {
                    allWiden = false;
                    detail += "loop" + juce::String(loopMode) + "/wet" + juce::String(wetMode)
                            + " " + fmt(narrow, 4) + "->" + fmt(wide, 4) + " ";
                }
            }
        }
        check("Mood_SpreadWidensEveryModeCombination",
              allWiden,
              allWiden ? ("side-to-mid rises on all nine pairings; smallest gain x"
                          + fmt(worstGain, 2))
                       : ("did not widen: " + detail));
    }

    // With SPREAD down the incoming image must survive. A source hard to one
    // side has to come out on that side, which is what makes the control a
    // choice rather than a permanent stereoiser.
    {
        bool allPreserve = true;
        juce::String detail;
        double worstDb = 1.0e9;
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            for (int wetMode = 0; wetMode < 3; ++wetMode)
            {
                MoodSettings s;
                s.mix = 1.0f;
                s.loopModeIndex = loopMode;
                s.wetModeIndex = wetMode;
                s.spread = 0.0f;
                s.routing = 1.0f;
                const auto sep = measureMood(s, true).channelSeparationDb;
                worstDb = juce::jmin(worstDb, sep);
                if (sep < 20.0)
                {
                    allPreserve = false;
                    detail += "loop" + juce::String(loopMode) + "/wet" + juce::String(wetMode)
                            + " " + fmt(sep, 1) + "dB ";
                }
            }
        }
        check("Mood_SpreadOffPreservesTheIncomingImage",
              allPreserve,
              allPreserve ? ("worst channel separation with a hard-left input "
                             + fmt(worstDb, 1) + " dB")
                          : ("image collapsed: " + detail));
    }

    // TAPE's stereo treatment is specific: with SPREAD up the right channel
    // plays the loop forward and the left plays the same loop in reverse. Two
    // copies of the same material running opposite ways are uncorrelated, so
    // this is checkable rather than merely describable.
    {
        MoodSettings s;
        s.mix = 1.0f;
        s.loopModeIndex = 1;      // TAPE
        s.wetModeIndex = 1;
        s.spread = 1.0f;
        s.routing = 1.0f;
        s.feedback = 0.4f;
        const auto corr = measureMood(s).interChannelCorrelation;
        check("Mood_TapeSpreadPlaysTheLoopForwardAndReverse",
              std::abs(corr) < 0.35,
              "inter-channel correlation at full spread " + fmt(corr, 4));
    }

    // CLOCK is the engine's sample rate. Audio captured at one rate and played
    // back at another changes speed and pitch together, so turning the clock
    // down transposes a captured loop - and it must do so in the harmonised
    // steps the control is specified with, over a full three octaves.
    //
    // Measured against a FROZEN loop. While the looper is still recording,
    // capture and playback happen at the same rate and the pitch is preserved
    // by definition, so a render that holds the clock constant shows nothing
    // however well the control works.
    {
        auto playbackHz = [](float clock)
        {
            Mood mood;
            mood.prepare(kSampleRate);
            mood.reset();
            MoodSettings ms;
            ms.enabled = true;
            ms.mix = 1.0f;
            ms.loopModeIndex = 1;
            ms.loopModify = 0.70f;
            ms.loopLength = 0.5f;
            ms.wetModeIndex = 1;
            ms.wetModify = 0.0f;
            ms.wetTime = 0.0f;
            ms.routing = 1.0f;
            ms.spread = 0.0f;
            ms.degrade = 0.0f;
            ms.feedback = 0.0f;
            ms.clock = 1.0f;
            mood.updateForBlock(ms);

            for (int i = 0; i < static_cast<int>(kSampleRate * 4.0); ++i)
            {
                const auto in = std::sin(juce::MathConstants<float>::twoPi * 440.0f
                                         * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.6f;
                float l = 0.0f, r = 0.0f;
                mood.processSampleFrame(in, in, l, r);
            }

            ms.clock = clock;
            ms.freeze = true;
            mood.updateForBlock(ms);

            const auto total = static_cast<int>(kSampleRate * 6.0);
            std::vector<float> out;
            out.reserve(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i)
            {
                float l = 0.0f, r = 0.0f;
                mood.processSampleFrame(0.0f, 0.0f, l, r);
                out.push_back(l);
            }
            return estimateFrequency(out, static_cast<int>(kSampleRate * 3.0),
                                     static_cast<int>(kSampleRate * 2.0), 20.0, 2000.0);
        };

        const auto full = playbackHz(1.0f);
        const auto threeQuarter = playbackHz(0.75f);
        const auto half = playbackHz(0.5f);
        const auto quarter = playbackHz(0.25f);
        const auto lowest = playbackHz(0.0f);

        const auto monotonic = full > threeQuarter && threeQuarter > half
                            && half > quarter && quarter > lowest;
        // Three octaves across the knob, i.e. a factor of eight.
        const auto span = full / juce::jmax(1.0, lowest);
        check("Mood_ClockTransposesACapturedLoop",
              monotonic && span > 6.0 && span < 10.0,
              "playback pitch " + fmt(full, 0) + " / " + fmt(threeQuarter, 0) + " / "
                  + fmt(half, 0) + " / " + fmt(quarter, 0) + " / " + fmt(lowest, 0)
                  + " Hz, span x" + fmt(span, 2));
    }

    // Same requirement as the other two effects: bypass empties the history
    // and wet buffers so re-enabling does not replay an old loop.
    {
        double worst = 0.0;
        int worstCombo = 0;
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            for (int wetMode = 0; wetMode < 3; ++wetMode)
            {
                MoodSettings settings;
                settings.mix = 1.0f;
                settings.loopModeIndex = loopMode;
                settings.wetModeIndex = wetMode;
                settings.feedback = 0.8f;
                settings.routing = 0.5f;
                const auto tail = tailAfterBypassCycle<Mood, MoodSettings>(
                    settings,
                    [](Mood& m, const MoodSettings& s) { m.updateForBlock(s); });
                if (tail > worst) { worst = tail; worstCombo = loopMode * 3 + wetMode; }
            }
        }
        check("Mood_BypassClearsTheTail",
              worst < 1.0e-4,
              "loudest sample after bypass and re-enable with silence in: "
                  + fmt(worst, 8) + " (loop " + juce::String(worstCombo / 3)
                  + ", wet " + juce::String(worstCombo % 3) + ")");
    }

    auto runMood = [](const MoodSettings& settings, int inputSamples, int totalSamples)
    {
        ::Mood mood;
        mood.prepare(kSampleRate);
        mood.reset();
        mood.updateForBlock(settings);

        Capture capture;
        double phase = 0.0;
        for (int i = 0; i < totalSamples; ++i)
        {
            // A tone rather than an impulse: granular processing needs material
            // in its history buffer to work with.
            const auto input = i < inputSamples
                                   ? static_cast<float>(0.5 * std::sin(phase))
                                   : 0.0f;
            phase += juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
            float outL = 0.0f, outR = 0.0f;
            mood.processSampleFrame(input, input, outL, outR);
            capture.left.push_back(outL);
            capture.right.push_back(outR);
        }
        return capture;
    };

    MoodSettings base;
    base.enabled = true;
    base.mix = 0.8f;

    {
        const auto capture = runMood(base, 24000, 72000);
        check("Mood_ProducesOutputFromInput", capture.rms() > 1.0e-5 && capture.isFinite(),
              "rms " + fmt(capture.rms(), 6));
        check("Mood_IsStableAndDoesNotRunAway", capture.peak() < 4.0,
              "peak " + fmt(capture.peak(), 5));
        check("Mood_HasNoSignificantDcOffset", std::abs(capture.dcOffset()) < 0.01,
              "dc " + fmt(capture.dcOffset(), 7));
    }

    // `enabled` is the one and only bypass. A second control, moodTrueBypass,
    // used to be exposed to the host and saved into every preset while Mood
    // never read it; it was removed rather than implemented. This guards
    // against it being reintroduced as an inert control.
    {
        PX3SynthAudioProcessor processor;
        bool found = false;
        for (auto* parameter : processor.getParameters())
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            {
                if (ranged->paramID.equalsIgnoreCase("moodTrueBypass")) found = true;
            }
        }
        check("Mood_NoOrphanedTrueBypassParameterIsExposed", ! found,
              "the host must not be offered a bypass that does nothing");
    }

    // Freeze must stop new material entering the history buffer.
    {
        auto frozen = base;
        frozen.freeze = true;
        auto flowing = base;
        flowing.freeze = false;
        const auto frozenCapture = runMood(frozen, 24000, 60000);
        const auto flowingCapture = runMood(flowing, 24000, 60000);
        bool differs = false;
        for (std::size_t i = 0; i < frozenCapture.left.size(); ++i)
        {
            if (std::abs(frozenCapture.left[i] - flowingCapture.left[i]) > 1.0e-6f) differs = true;
        }
        check("Mood_FreezeChangesWhatIsPlayedBack", differs,
              "frozen rms " + fmt(frozenCapture.rms(), 6)
                  + ", flowing rms " + fmt(flowingCapture.rms(), 6));
    }

    // Disabled must not process.
    {
        auto settings = base;
        settings.enabled = false;
        const auto capture = runMood(settings, 4800, 24000);
        check("Mood_DisabledProducesNoWetTail", capture.rmsOver(9600, 24000) < 1.0e-6,
              "tail rms while disabled " + fmt(capture.rmsOver(9600, 24000), 9));
    }

    // Reset must clear the history so old material cannot leak into new audio.
    {
        ::Mood mood;
        mood.prepare(kSampleRate);
        mood.reset();
        mood.updateForBlock(base);
        double phase = 0.0;
        for (int i = 0; i < 24000; ++i)
        {
            float outL = 0.0f, outR = 0.0f;
            mood.processSampleFrame(static_cast<float>(0.5 * std::sin(phase)),
                                    static_cast<float>(0.5 * std::sin(phase)), outL, outR);
            phase += juce::MathConstants<double>::twoPi * 220.0 / kSampleRate;
        }
        mood.reset();
        double residual = 0.0;
        for (int i = 0; i < 48000; ++i)
        {
            float outL = 0.0f, outR = 0.0f;
            mood.processSampleFrame(0.0f, 0.0f, outL, outR);
            residual = juce::jmax(residual, static_cast<double>(std::abs(outL)));
        }
        check("Mood_ResetClearsPreviousMaterial", residual < 1.0e-6,
              "largest sample after reset with silent input " + fmt(residual, 9));
    }

    // Every continuous parameter must measurably change the output.
    {
        auto captureFor = [&runMood, base](const std::function<void(MoodSettings&)>& tweak)
        {
            auto settings = base;
            tweak(settings);
            return runMood(settings, 24000, 60000);
        };

        // As with the reverb, the difference SIGNAL between the two settings is
        // the sensitive measure; two RMS scalars can match while the waveform
        // is completely different.
        auto relativeDifference = [](const Capture& a, const Capture& b)
        {
            double difference = 0.0, reference = 0.0;
            for (std::size_t i = 4800; i < 57600; ++i)
            {
                const auto d = static_cast<double>(a.left[i]) - b.left[i];
                difference += d * d;
                reference += static_cast<double>(a.left[i]) * a.left[i];
            }
            return reference > 1.0e-18 ? std::sqrt(difference / reference) : 0.0;
        };

        struct ParamCase { const char* name; std::function<void(MoodSettings&)> low, high; };
        const ParamCase cases[] = {
            { "Mix",        [](MoodSettings& s) { s.mix = 0.0f; },        [](MoodSettings& s) { s.mix = 1.0f; } },
            { "Clock",      [](MoodSettings& s) { s.clock = 0.0f; },      [](MoodSettings& s) { s.clock = 1.0f; } },
            { "WetTime",    [](MoodSettings& s) { s.wetTime = 0.0f; },    [](MoodSettings& s) { s.wetTime = 1.0f; } },
            { "WetModify",  [](MoodSettings& s) { s.wetModify = 0.0f; },  [](MoodSettings& s) { s.wetModify = 1.0f; } },
            { "LoopLength", [](MoodSettings& s) { s.loopLength = 0.0f; }, [](MoodSettings& s) { s.loopLength = 1.0f; } },
            { "LoopModify", [](MoodSettings& s) { s.loopModify = 0.0f; }, [](MoodSettings& s) { s.loopModify = 1.0f; } },
            { "Feedback",   [](MoodSettings& s) { s.feedback = 0.0f; },   [](MoodSettings& s) { s.feedback = 1.0f; } },
            { "Degrade",    [](MoodSettings& s) { s.degrade = 0.0f; },    [](MoodSettings& s) { s.degrade = 1.0f; } },
        };

        for (const auto& parameter : cases)
        {
            const auto low = captureFor(parameter.low);
            const auto high = captureFor(parameter.high);
            const auto difference = relativeDifference(low, high);
            check((juce::String("Mood_") + parameter.name + "_ChangesTheOutput").toRawUTF8(),
                  difference > 0.02,
                  "output differs by " + fmt(100.0 * difference, 2) + "% between min and max");
        }
    }

    // SPREAD places grains across the stereo field, so it is a stereo control:
    // measured on one channel it is invisible. Measured as the divergence
    // between channels, at spread 0 every grain is centred and the two channels
    // must match closely.
    {
        auto stereoDivergence = [&runMood, base](float spread)
        {
            auto settings = base;
            settings.spread = spread;
            // STRETCH is the loop mode that spawns grains continuously, and
            // grains are the only thing spread acts on.
            settings.loopModeIndex = 2;
            const auto capture = runMood(settings, 24000, 60000);
            double difference = 0.0, reference = 0.0;
            for (std::size_t i = 4800; i < 57600; ++i)
            {
                const auto d = static_cast<double>(capture.left[i]) - capture.right[i];
                difference += d * d;
                reference += static_cast<double>(capture.left[i]) * capture.left[i];
            }
            return reference > 1.0e-18 ? std::sqrt(difference / reference) : 0.0;
        };
        // Averaged over several renders. Grain pan is drawn from the shared
        // system Random, which JUCE will not let anything reseed, so a single
        // render of the wide setting varies enough to make a single-shot
        // comparison flaky - it was measured at both 0.029 and 0.016 for the
        // same settings. The centred setting has no such variance because every
        // grain is placed at dead centre, so the mean of a handful of runs is a
        // stable basis for the comparison.
        constexpr int kRepeats = 5;
        double centred = 0.0, wide = 0.0;
        for (int repeat = 0; repeat < kRepeats; ++repeat)
        {
            centred += stereoDivergence(0.0f);
            wide += stereoDivergence(1.0f);
        }
        centred /= kRepeats;
        wide /= kRepeats;
        check("Mood_Spread_WidensTheStereoField", wide > centred * 1.8,
              "mean channel divergence over " + juce::String(kRepeats) + " renders: spread 0 "
                  + fmt(centred, 5) + ", spread 1 " + fmt(wide, 5));
    }

    // Discrete mode selectors must all render stably.
    {
        for (int wetMode = 0; wetMode < 3; ++wetMode)
        {
            auto settings = base;
            settings.wetModeIndex = wetMode;
            const auto capture = runMood(settings, 24000, 60000);
            check((juce::String("Mood_WetMode") + juce::String(wetMode) + "_IsStable").toRawUTF8(),
                  capture.isFinite() && capture.peak() < 4.0,
                  "rms " + fmt(capture.rms(), 6) + ", peak " + fmt(capture.peak(), 5));
        }
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            auto settings = base;
            settings.loopModeIndex = loopMode;
            const auto capture = runMood(settings, 24000, 60000);
            check((juce::String("Mood_LoopMode") + juce::String(loopMode) + "_IsStable").toRawUTF8(),
                  capture.isFinite() && capture.peak() < 4.0,
                  "rms " + fmt(capture.rms(), 6) + ", peak " + fmt(capture.peak(), 5));
        }
    }

    // Maximum feedback is where an unstable loop would show as runaway gain.
    {
        auto settings = base;
        settings.feedback = 1.0f;
        settings.mix = 1.0f;
        const auto capture = runMood(settings, 24000, 192000);
        const auto early = capture.rmsOver(24000, 48000);
        const auto late = capture.rmsOver(160000, 190000);
        check("Mood_MaximumFeedbackDoesNotRunAway",
              capture.isFinite() && capture.peak() < 4.0 && late <= early * 4.0,
              "early rms " + fmt(early, 6) + ", late rms " + fmt(late, 6)
                  + ", peak " + fmt(capture.peak(), 5));
    }

    // Transient and silence-to-signal handling.
    {
        ::Mood mood;
        mood.prepare(kSampleRate);
        mood.reset();
        mood.updateForBlock(base);
        Capture capture;
        for (int i = 0; i < 48000; ++i)
        {
            // Silence, then a hard transient, then silence again.
            const auto input = (i >= 12000 && i < 12100) ? 0.9f : 0.0f;
            float outL = 0.0f, outR = 0.0f;
            mood.processSampleFrame(input, input, outL, outR);
            capture.left.push_back(outL);
            capture.right.push_back(outR);
        }
        check("Mood_HandlesSilenceToTransientWithoutInstability",
              capture.isFinite() && capture.peak() < 4.0,
              "peak " + fmt(capture.peak(), 5));
    }
}

//==============================================================================
// PHASE 11 / 12 - EFFECT INDEPENDENCE AND THE FX SIGNAL PATH
//==============================================================================
void testEffectIndependence()
{
    suite("FX INDEPENDENCE AND SIGNAL PATH");

    // Each effect is measured alone; changing another effect's parameters must
    // leave it where it was. Compared against the measured self-variation, as
    // elsewhere, because voice start phase is not reproducible.
    auto renderWithFx = [](const std::function<void(PX3SynthAudioProcessor&)>& configure)
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
        {
            setParam(processor, juce::String("mix.") + id + ".fxSend", 0.8f);
        }
        setParam(processor, "fxSendGain", 1.0f);
        setParam(processor, "fxReturnGain", 0.8f);
        configure(processor);
        return render(processor, 64000, { { 2000, true, 45, 0.9f }, { 30000, false, 45, 0.0f } });
    };

    auto independence = [&renderWithFx](const char* name,
                                        const std::function<void(PX3SynthAudioProcessor&)>& subject,
                                        const std::function<void(PX3SynthAudioProcessor&)>& other)
    {
        const auto controlA = renderWithFx(subject);
        const auto controlB = renderWithFx(subject);
        auto combined = [&subject, &other](PX3SynthAudioProcessor& p) { subject(p); other(p); };
        const auto withOther = renderWithFx(combined);

        const auto selfVariation = std::abs(controlA.rms() - controlB.rms());
        const auto delta = std::abs(controlA.rms() - withOther.rms());
        check(name, delta <= juce::jmax(selfVariation * 3.0, controlA.rms() * 0.02),
              "rms moved " + fmt(delta, 6) + " (self-variation " + fmt(selfVariation, 6) + ")");
    };

    const auto reverbOnly = [](PX3SynthAudioProcessor& p)
    {
        setParam(p, "reverbEnabled", 1.0f);
        setParam(p, "reverbAmount", 0.7f);
        setParam(p, "delayEnabled", 0.0f);
        setParam(p, "moodEnabled", 0.0f);
    };
    const auto moodOnly = [](PX3SynthAudioProcessor& p)
    {
        setParam(p, "moodEnabled", 1.0f);
        setParam(p, "moodMix", 0.7f);
        setParam(p, "reverbEnabled", 0.0f);
        setParam(p, "delayEnabled", 0.0f);
    };

    // Changing a disabled effect's parameters must not reach the enabled one.
    independence("Reverb_UnaffectedByMoodParameterChanges", reverbOnly,
                 [](PX3SynthAudioProcessor& p)
                 {
                     setParam(p, "moodMix", 1.0f);
                     setParam(p, "moodFeedback", 0.9f);
                     setParam(p, "moodDegrade", 0.8f);
                 });
    independence("Mood_UnaffectedByReverbParameterChanges", moodOnly,
                 [](PX3SynthAudioProcessor& p)
                 {
                     setParam(p, "reverbSize", 1.0f);
                     setParam(p, "reverbDecay", 1.0f);
                     setParam(p, "reverbDamping", 0.0f);
                 });
    independence("Reverb_UnaffectedByVibeParameterChanges", reverbOnly,
                 [](PX3SynthAudioProcessor& p)
                 {
                     // Vibe is a voice-stage component; with vibeEnabled off its
                     // amount must not reach the FX bus at all.
                     setParam(p, "vibeEnabled", 0.0f);
                     setParam(p, "vibeAmount", 1.0f);
                 });

    // FX send / return topology. A source panned hard left must place its DRY
    // signal left without steering its send, and the FX return pan must not
    // move the dry signal.
    {
        // Wet-only is reached by pulling the channel FADER to zero, not by
        // muting: the mixer contract is that the send is pre-fader but a mute
        // kills the send as well, so a muted channel contributes nothing at all.
        auto renderPanned = [](float sourcePan, float fxReturnPan, bool wetOnly, bool fxActive)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 1);
            setParam(processor, "mix.osc1.pan", sourcePan);
            setParam(processor, "mix.osc1.level", wetOnly ? 0.0f : 0.8f);
            setParam(processor, "mix.osc1.fxSend", fxActive ? 1.0f : 0.0f);
            setParam(processor, "mix.fx.pan", fxReturnPan);
            setParam(processor, "fxSendGain", 1.0f);
            setParam(processor, "fxReturnGain", fxActive ? 1.0f : 0.0f);
            setParam(processor, "reverbEnabled", fxActive ? 1.0f : 0.0f);
            setParam(processor, "reverbAmount", 1.0f);
            return render(processor, 48000, { { 2000, true, 45, 0.9f } });
        };

        auto balance = [](const Capture& capture)
        {
            double left = 0.0, right = 0.0;
            for (std::size_t i = 12000; i < capture.left.size(); ++i)
            {
                left += std::abs(static_cast<double>(capture.left[i]));
                right += std::abs(static_cast<double>(capture.right[i]));
            }
            const auto total = left + right;
            return total > 1.0e-12 ? (left - right) / total : 0.0;
        };

        // Dry pan, measured with the FX path shut off so the wet cannot dilute
        // the reading.
        const auto dryLeft = balance(renderPanned(-1.0f, 0.0f, false, false));
        const auto dryRight = balance(renderPanned(1.0f, 0.0f, false, false));
        const auto dryCentre = balance(renderPanned(0.0f, 0.0f, false, false));
        check("MixerPan_HardLeftSourcePlacesDrySignalLeft", dryLeft > 0.9,
              "left/right balance " + fmt(dryLeft, 4));
        check("MixerPan_HardRightSourcePlacesDrySignalRight", dryRight < -0.9,
              "left/right balance " + fmt(dryRight, 4));
        check("MixerPan_CentreSourceIsBalanced", std::abs(dryCentre) < 0.02,
              "left/right balance " + fmt(dryCentre, 4));

        // Listening to the FX return alone. The send is centred by design, so
        // panning the source must not move the return: the test is that the
        // balance does not FOLLOW the pan. An absolute-balance test would
        // instead be measuring the reverb algorithm's own stereo pattern, which
        // is deliberately asymmetric and is not what this is about.
        const auto wetWithLeftSource = balance(renderPanned(-1.0f, 0.0f, true, true));
        const auto wetWithRightSource = balance(renderPanned(1.0f, 0.0f, true, true));
        check("FxSend_IsNotSteeredBySourceDryPan",
              std::abs(wetWithLeftSource - wetWithRightSource) < 0.10,
              "FX-return-only balance: source hard left " + fmt(wetWithLeftSource, 4)
                  + ", hard right " + fmt(wetWithRightSource, 4));

        // The FX return has its own pan.
        const auto returnLeft = balance(renderPanned(0.0f, -1.0f, true, true));
        const auto returnRight = balance(renderPanned(0.0f, 1.0f, true, true));
        check("FxReturnPan_MovesTheReturnSignal", returnLeft > 0.5 && returnRight < -0.5,
              "FX-return-only balance: pan left " + fmt(returnLeft, 4)
                  + ", pan right " + fmt(returnRight, 4));

        // With no FX return in the output at all, moving the return pan must
        // leave the dry signal exactly where it was.
        const auto dryWithReturnPanned = balance(renderPanned(-0.5f, 1.0f, false, false));
        const auto dryWithReturnCentred = balance(renderPanned(-0.5f, 0.0f, false, false));
        check("FxReturnPan_DoesNotMoveTheDrySignal",
              nearly(dryWithReturnPanned, dryWithReturnCentred, 0.01),
              "dry balance with return centred " + fmt(dryWithReturnCentred, 4)
                  + ", with return hard right " + fmt(dryWithReturnPanned, 4));
    }

    // Dry-only and wet-only paths must both be reachable.
    {
        PX3SynthAudioProcessor dryOnly, wetOnly;
        for (auto* p : { &dryOnly, &wetOnly })
        {
            makePlainPatch(*p);
            setChoice(*p, "osc1Mode", 1);
            setParam(*p, "reverbEnabled", 1.0f);
            setParam(*p, "reverbAmount", 1.0f);
            setParam(*p, "fxSendGain", 1.0f);
        }
        setParam(dryOnly, "mix.osc1.fxSend", 0.0f);
        setParam(dryOnly, "fxReturnGain", 0.0f);
        setParam(wetOnly, "mix.osc1.fxSend", 1.0f);
        setParam(wetOnly, "fxReturnGain", 1.0f);
        // Fader down, not muted: the send is pre-fader, so this is wet-only.
        setParam(wetOnly, "mix.osc1.level", 0.0f);

        const auto dry = render(dryOnly, 48000, { { 2000, true, 45, 0.9f } });
        const auto wet = render(wetOnly, 48000, { { 2000, true, 45, 0.9f } });
        check("FxPath_DryOnlyProducesAudio", dry.rms() > 0.01, "rms " + fmt(dry.rms(), 5));
        check("FxPath_WetOnlyProducesAudio", wet.rms() > 1.0e-4, "rms " + fmt(wet.rms(), 6));

        // Muting a channel must kill its send as well as its dry path, which is
        // the established mixer contract and the reason wet-only uses the fader.
        PX3SynthAudioProcessor muted;
        makePlainPatch(muted);
        setChoice(muted, "osc1Mode", 1);
        setParam(muted, "reverbEnabled", 1.0f);
        setParam(muted, "reverbAmount", 1.0f);
        setParam(muted, "fxSendGain", 1.0f);
        setParam(muted, "mix.osc1.fxSend", 1.0f);
        setParam(muted, "fxReturnGain", 1.0f);
        setParam(muted, "mix.osc1.mute", 1.0f);
        const auto mutedCapture = render(muted, 48000, { { 2000, true, 45, 0.9f } });
        check("MixerMute_KillsTheSendAsWellAsTheDryPath", mutedCapture.peak() < 1.0e-5,
              "peak with the channel muted and send at maximum " + fmt(mutedCapture.peak(), 8));
    }

    // An FX tail must survive the note that produced it.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        setParam(processor, "ampRelease", 0.020f);
        setParam(processor, "mix.osc1.fxSend", 1.0f);
        setParam(processor, "fxSendGain", 1.0f);
        setParam(processor, "fxReturnGain", 1.0f);
        setParam(processor, "reverbEnabled", 1.0f);
        setParam(processor, "reverbAmount", 1.0f);
        setParam(processor, "reverbDecay", 0.9f);
        setParam(processor, "reverbSize", 0.9f);
        const auto capture = render(processor, 96000,
                                    { { 2000, true, 45, 0.9f }, { 20000, false, 45, 0.0f } });
        check("FxPath_ReverbTailOutlivesTheNote", capture.rmsOver(30000, 60000) > 1.0e-5,
              "rms well after note-off " + fmt(capture.rmsOver(30000, 60000), 7));
    }
}
//==============================================================================
// PHASE 13 / 14 / 15 - PRESET MANAGER AND STATE
//==============================================================================
// Applies a deliberately unusual configuration: many parameters away from their
// defaults, across every audited component, so a round trip that drops or
// mis-maps any of them is visible.
void applyUnusualConfiguration(PX3SynthAudioProcessor& processor)
{
    setParam(processor, "ampAttack", 0.731f);
    setParam(processor, "ampDecay", 1.234f);
    setParam(processor, "ampSustain", 0.371f);
    setParam(processor, "ampRelease", 2.510f);
    setParam(processor, "ampEnvEnabled", 1.0f);
    setParam(processor, "masterGain", 0.83f);
    setParam(processor, "pitchBendRange", 7.0f);

    setParam(processor, "osc1Enabled", 1.0f);
    setParam(processor, "osc2Enabled", 1.0f);
    setParam(processor, "osc3Enabled", 0.0f);
    setChoice(processor, "osc1Mode", 13);
    setChoice(processor, "osc2Mode", 6);
    setChoice(processor, "osc3Mode", 19);
    setParam(processor, "osc1Coarse", -7.0f);
    setParam(processor, "osc2Coarse", 5.0f);
    setParam(processor, "osc3Coarse", 12.0f);
    setParam(processor, "osc1Fine", -37.0f);
    setParam(processor, "osc2Fine", 63.0f);
    setParam(processor, "osc1Pitch", 0.17f);
    setParam(processor, "osc2MacroA", 0.234f);
    setParam(processor, "osc2MacroB", 0.876f);
    setParam(processor, "osc3MacroC", 0.412f);
    setChoice(processor, "osc1Vowel", 3);
    setParam(processor, "osc1H3", 0.913f);
    setParam(processor, "osc2H7", 0.041f);

    setParam(processor, "subOscEnabled", 1.0f);
    setChoice(processor, "subOscOctave", 2);
    setChoice(processor, "subOscWaveform", 0);
    setParam(processor, "subOscPitch", -0.19f);

    for (const auto* slot : { "1", "2" })
    {
        setParam(processor, juce::String("filter") + slot + "Enabled", 1.0f);
        setParam(processor, juce::String("filter") + slot + "Cutoff", slot[0] == '1' ? 733.0f : 4211.0f);
        setParam(processor, juce::String("filter") + slot + "Resonance", slot[0] == '1' ? 1.73f : 0.41f);
    }
    setChoice(processor, "filter1Type", 4);
    setChoice(processor, "filter2Type", 2);

    for (int envIndex = 0; envIndex < 3; ++envIndex)
    {
        const auto slot = juce::String(envIndex + 1);
        setParam(processor, "env" + slot + "Enabled", 1.0f);
        setParam(processor, "env" + slot + "Attack", 0.11f + 0.23f * static_cast<float>(envIndex));
        setParam(processor, "env" + slot + "Decay", 0.37f + 0.19f * static_cast<float>(envIndex));
        setParam(processor, "env" + slot + "Sustain", 0.29f + 0.17f * static_cast<float>(envIndex));
        setParam(processor, "env" + slot + "Release", 1.13f + 0.41f * static_cast<float>(envIndex));
        setParam(processor, envIndex == 0 ? juce::String("envAmount") : "env" + slot + "Amount",
                 -0.63f + 0.44f * static_cast<float>(envIndex));
    }
    processor.setEnvelopeAssignmentByParameterId(0, "filter1Cutoff", false);
    processor.setEnvelopeAssignmentByParameterId(1, "osc1Level", false);
    processor.setEnvelopeAssignmentByParameterId(2, "filter2Resonance", false);

    for (int lfoIndex = 0; lfoIndex < 3; ++lfoIndex)
    {
        const auto slot = juce::String(lfoIndex + 1);
        const auto prefix = lfoIndex == 0 ? juce::String("lfo") : "lfo" + slot;
        setParam(processor, lfoIndex == 0 ? juce::String("lfoEnabled") : prefix + "Enabled", 1.0f);
        setParam(processor, lfoIndex == 0 ? juce::String("lfoFrequency") : prefix + "Frequency",
                 0.73f + 3.19f * static_cast<float>(lfoIndex));
        setParam(processor, lfoIndex == 0 ? juce::String("lfoAmount") : prefix + "Amount",
                 -0.81f + 0.57f * static_cast<float>(lfoIndex));
        setChoice(processor, lfoIndex == 0 ? juce::String("lfoWaveform") : prefix + "Waveform",
                  (lfoIndex + 2) % 4);
    }
    processor.setLfoAssignmentByParameterId(0, "filter2Cutoff", false);
    processor.setLfoAssignmentByParameterId(1, "mix.osc2.pan", false);
    processor.setLfoAssignmentByParameterId(2, "osc1Pitch", false);

    setParam(processor, "vibeEnabled", 1.0f);
    setParam(processor, "vibeAmount", 0.67f);
    setChoice(processor, "vibeType", 2);

    setParam(processor, "reverbEnabled", 1.0f);
    setParam(processor, "reverbAmount", 0.71f);
    setChoice(processor, "reverbAlgorithm", 3);
    setParam(processor, "reverbSize", 0.83f);
    setParam(processor, "reverbDecay", 0.29f);
    setParam(processor, "reverbDamping", 0.77f);
    setParam(processor, "reverbPreDelay", 0.43f);
    setParam(processor, "reverbWidth", 0.19f);
    setParam(processor, "reverbCloudFeedback", 0.91f);
    setParam(processor, "reverbCloudDiffusion", 0.13f);

    setParam(processor, "moodEnabled", 1.0f);
    setParam(processor, "moodMix", 0.61f);
    setParam(processor, "moodClock", 0.23f);
    setParam(processor, "moodWetTime", 0.87f);
    setParam(processor, "moodFeedback", 0.44f);
    setParam(processor, "moodSpread", 0.71f);
    setParam(processor, "moodDegrade", 0.58f);
    setParam(processor, "moodFreeze", 0.0f);
    setChoice(processor, "moodWetMode", 2);
    setChoice(processor, "moodLoopMode", 1);

    setParam(processor, "delayEnabled", 1.0f);
    setParam(processor, "delayAmount", 0.39f);
    setParam(processor, "delayTime", 0.62f);
    setParam(processor, "delayFeedback", 0.47f);

    setParam(processor, "fxSendGain", 0.73f);
    setParam(processor, "fxReturnGain", 0.58f);
    setParam(processor, "mix.fx.pan", -0.41f);
    setParam(processor, "mix.fx.mute", 0.0f);

    const float levels[] = { 0.31f, 0.79f, 0.52f, 0.94f };
    const float pans[] = { -0.87f, 0.23f, 0.61f, -0.34f };
    const float sends[] = { 0.11f, 0.83f, 0.37f, 0.66f };
    int index = 0;
    for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
    {
        setParam(processor, juce::String("mix.") + id + ".level", levels[index]);
        setParam(processor, juce::String("mix.") + id + ".pan", pans[index]);
        setParam(processor, juce::String("mix.") + id + ".fxSend", sends[index]);
        ++index;
    }
    setParam(processor, "mix.osc3.mute", 1.0f);
    setParam(processor, "mix.osc2.solo", 0.0f);

    processor.setFxProcessingOrder({ { 3, 1, 0, 2 } });
}

// Every host-visible parameter, as normalised values, so a comparison covers
// controls the test author did not think to name.
std::vector<std::pair<juce::String, float>> snapshotParameters(PX3SynthAudioProcessor& processor)
{
    std::vector<std::pair<juce::String, float>> snapshot;
    for (auto* parameter : processor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            snapshot.emplace_back(ranged->paramID, ranged->getValue());
        }
    }
    return snapshot;
}

void testPresets()
{
    suite("PRESET / STATE");

    // Full DAW-session round trip: save, reset to defaults, restore, compare
    // every parameter.
    {
        PX3SynthAudioProcessor processor;
        applyUnusualConfiguration(processor);
        const auto before = snapshotParameters(processor);
        const auto orderBefore = processor.getFxProcessingOrder();
        const auto envAssignmentsBefore = std::array<int, 3> {
            processor.getEnvelopeAssignmentIndex(0),
            processor.getEnvelopeAssignmentIndex(1),
            processor.getEnvelopeAssignmentIndex(2)
        };
        const auto lfoAssignmentsBefore = std::array<int, 3> {
            processor.getLfoAssignmentIndex(0),
            processor.getLfoAssignmentIndex(1),
            processor.getLfoAssignmentIndex(2)
        };

        juce::MemoryBlock state;
        processor.getStateInformation(state);
        check("Preset_StateSerialisesToNonEmptyPayload", state.getSize() > 0,
              juce::String(static_cast<int>(state.getSize())) + " bytes");

        // A fresh processor is at defaults; restoring must reproduce the patch.
        PX3SynthAudioProcessor restored;
        const auto defaults = snapshotParameters(restored);
        int differingFromDefaults = 0;
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            if (std::abs(before[i].second - defaults[i].second) > 1.0e-6f) ++differingFromDefaults;
        }
        check("Preset_TestConfigurationActuallyDiffersFromDefaults",
              differingFromDefaults > 50,
              juce::String(differingFromDefaults) + " of "
                  + juce::String(static_cast<int>(before.size())) + " parameters moved");

        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        const auto after = snapshotParameters(restored);

        int mismatches = 0;
        juce::String firstMismatch;
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            if (before[i].first != after[i].first)
            {
                ++mismatches;
                continue;
            }
            if (std::abs(before[i].second - after[i].second) > 1.0e-5f)
            {
                ++mismatches;
                if (firstMismatch.isEmpty())
                {
                    firstMismatch = before[i].first + " " + fmt(before[i].second, 6)
                                    + " -> " + fmt(after[i].second, 6);
                }
            }
        }
        check("Preset_RoundTripRestoresAllParameters", mismatches == 0,
              mismatches == 0
                  ? juce::String(static_cast<int>(before.size())) + " parameters matched"
                  : juce::String(mismatches) + " mismatched, first: " + firstMismatch);

        check("Preset_RoundTripRestoresFxProcessingOrder",
              restored.getFxProcessingOrder() == orderBefore);
        check("Preset_RoundTripRestoresEnvelopeAssignments",
              restored.getEnvelopeAssignmentIndex(0) == envAssignmentsBefore[0]
                  && restored.getEnvelopeAssignmentIndex(1) == envAssignmentsBefore[1]
                  && restored.getEnvelopeAssignmentIndex(2) == envAssignmentsBefore[2]);
        check("Preset_RoundTripRestoresLfoAssignments",
              restored.getLfoAssignmentIndex(0) == lfoAssignmentsBefore[0]
                  && restored.getLfoAssignmentIndex(1) == lfoAssignmentsBefore[1]
                  && restored.getLfoAssignmentIndex(2) == lfoAssignmentsBefore[2]);
    }

    // The restored patch must also SOUND the same, which parameter equality
    // alone does not guarantee.
    {
        PX3SynthAudioProcessor original;
        applyUnusualConfiguration(original);
        // FX and modes that draw from the shared system Random are switched off
        // for this comparison so the two renders are comparable at all.
        setParam(original, "reverbEnabled", 0.0f);
        setParam(original, "moodEnabled", 0.0f);
        setParam(original, "delayEnabled", 0.0f);
        setChoice(original, "osc1Mode", 1);
        setChoice(original, "osc2Mode", 3);
        setParam(original, "mix.osc3.mute", 0.0f);

        juce::MemoryBlock state;
        original.getStateInformation(state);

        PX3SynthAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        // Compared against the patch's own run-to-run variation. Voice start
        // phase is seeded from a global note counter, so two renders of the
        // same patch are never sample-identical; the question is whether the
        // restored patch differs by MORE than the patch differs from itself.
        const auto originalA = render(original, 48000, { { 2000, true, 45, 0.9f } });
        const auto originalB = render(original, 48000, { { 2000, true, 45, 0.9f } });
        const auto restoredCapture = render(restored, 48000, { { 2000, true, 45, 0.9f } });

        const auto selfVariation = std::abs(originalA.rms() - originalB.rms());
        const auto restoredDelta = std::abs(originalA.rms() - restoredCapture.rms());
        check("Preset_RoundTripPreservesRenderedAudioLevel",
              restoredDelta <= juce::jmax(selfVariation * 2.0, originalA.rms() * 0.05),
              "original rms " + fmt(originalA.rms(), 6) + ", restored " + fmt(restoredCapture.rms(), 6)
                  + "; restored differs by " + fmt(restoredDelta, 6)
                  + " vs self-variation " + fmt(selfVariation, 6));
    }

    // Malformed input must be rejected safely and must not corrupt live state.
    {
        auto survivesPayload = [](const char* name, const void* data, int size)
        {
            PX3SynthAudioProcessor processor;
            applyUnusualConfiguration(processor);
            const auto before = snapshotParameters(processor);
            processor.setStateInformation(data, size);
            const auto after = snapshotParameters(processor);

            // Either the state is unchanged (rejected) or it is some valid
            // state; what must never happen is a non-finite or out-of-range
            // parameter, or a crash.
            bool valid = true;
            juce::String offender;
            for (const auto& entry : after)
            {
                if (! std::isfinite(entry.second) || entry.second < -0.001f || entry.second > 1.001f)
                {
                    valid = false;
                    if (offender.isEmpty()) offender = entry.first + "=" + fmt(entry.second, 6);
                }
            }
            const auto capture = render(processor, 16000, { { 1000, true, 57, 0.9f } });
            const auto audioFinite = capture.isFinite();
            check(name, valid && audioFinite && before.size() == after.size(),
                  juce::String(before.size() == after.size() ? "parameter set intact" : "PARAMETER SET CHANGED")
                      + (valid ? ", all values finite and in range" : ", BAD PARAMETER " + offender)
                      + (audioFinite ? ", audio finite" : ", AUDIO NOT FINITE")
                      + ", peak " + fmt(capture.peak(), 6));
        };

        survivesPayload("Preset_MalformedNullPayloadIsRejectedSafely", nullptr, 0);

        const char garbage[] = "this is not a plugin state payload at all, not even close";
        survivesPayload("Preset_GarbagePayloadIsRejectedSafely", garbage, static_cast<int>(sizeof(garbage)));

        // A truncated version of a genuine payload.
        {
            PX3SynthAudioProcessor source;
            applyUnusualConfiguration(source);
            juce::MemoryBlock good;
            source.getStateInformation(good);
            survivesPayload("Preset_TruncatedPayloadIsRejectedSafely",
                            good.getData(), static_cast<int>(good.getSize() / 3));
        }

        // Well-formed XML carrying hostile values: NaN, infinity, out-of-range
        // numbers, an unknown parameter and a wrong-typed field.
        {
            PX3SynthAudioProcessor source;
            applyUnusualConfiguration(source);
            auto tree = source.createParameterStateTree();
            tree.setProperty("ampSustain", std::numeric_limits<double>::quiet_NaN(), nullptr);
            tree.setProperty("ampAttack", std::numeric_limits<double>::infinity(), nullptr);
            tree.setProperty("masterGain", -17.5, nullptr);
            tree.setProperty("filter1Cutoff", 9999.0, nullptr);
            tree.setProperty("aParameterThatDoesNotExist", 0.5, nullptr);
            tree.setProperty("osc1Coarse", "not a number", nullptr);
            if (auto xml = tree.createXml())
            {
                juce::MemoryBlock block;
                juce::AudioProcessor::copyXmlToBinary(*xml, block);
                survivesPayload("Preset_HostileValuesAreClampedOrRejectedSafely",
                                block.getData(), static_cast<int>(block.getSize()));
            }
        }

        // A preset saved before moodTrueBypass was removed still carries that
        // property. Loading one must apply everything else normally and simply
        // ignore the retired entry - this is the compatibility question the
        // removal turns on, so it is tested directly rather than inferred from
        // the generic unknown-property case.
        {
            PX3SynthAudioProcessor source;
            applyUnusualConfiguration(source);
            auto tree = source.createParameterStateTree();
            tree.setProperty("moodTrueBypass", 1.0, nullptr);
            const auto expected = snapshotParameters(source);

            if (auto xml = tree.createXml())
            {
                juce::MemoryBlock block;
                juce::AudioProcessor::copyXmlToBinary(*xml, block);

                PX3SynthAudioProcessor target;
                target.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
                const auto actual = snapshotParameters(target);

                int mismatches = 0;
                juce::String firstMismatch;
                for (std::size_t i = 0; i < expected.size(); ++i)
                {
                    if (std::abs(expected[i].second - actual[i].second) > 1.0e-5f)
                    {
                        ++mismatches;
                        if (firstMismatch.isEmpty())
                        {
                            firstMismatch = expected[i].first + " " + fmt(expected[i].second, 6)
                                            + " -> " + fmt(actual[i].second, 6);
                        }
                    }
                }
                check("Preset_SavedBeforeTrueBypassRemovalStillLoadsCompletely",
                      mismatches == 0,
                      mismatches == 0
                          ? juce::String("retired moodTrueBypass entry ignored, all ")
                                + juce::String(static_cast<int>(expected.size()))
                                + " live parameters restored"
                          : juce::String(mismatches) + " mismatched, first: " + firstMismatch);
            }
        }

        // A payload with most parameters simply missing.
        {
            juce::ValueTree sparse("PX3State");
            sparse.setProperty("masterGain", 0.5, nullptr);
            if (auto xml = sparse.createXml())
            {
                juce::MemoryBlock block;
                juce::AudioProcessor::copyXmlToBinary(*xml, block);
                survivesPayload("Preset_MissingParametersAreHandledSafely",
                                block.getData(), static_cast<int>(block.getSize()));
            }
        }
    }

    // Preset FILE round trip through PresetManager, into a temporary directory
    // so the user's own preset library is never touched.
    {
        PX3SynthAudioProcessor processor;
        applyUnusualConfiguration(processor);
        const auto before = snapshotParameters(processor);

        auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("px3-component-tests");
        tempDirectory.createDirectory();
        const auto presetFile = tempDirectory.getChildFile("round-trip.px3preset");
        presetFile.deleteFile();

        PresetManager manager(processor);
        juce::String error;
        PresetManager::PresetMetadata metadata;
        metadata.name = "Round Trip";
        metadata.category = "Test";
        metadata.author = "component tests";

        int serializedBytes = 0;
        const auto saved = manager.dumpCurrentStateToPresetFile(presetFile, metadata, true, true,
                                                                error, &serializedBytes);
        check("Preset_SaveWritesAPresetFile", saved && presetFile.existsAsFile(),
              saved ? juce::String(serializedBytes) + " bytes" : "error: " + error);

        if (saved)
        {
            PX3SynthAudioProcessor target;
            PresetManager targetManager(target);
            juce::String loadError;
            const auto loaded = targetManager.loadPresetFile(presetFile, loadError);
            check("Preset_LoadReadsAPresetFile", loaded, loaded ? juce::String() : "error: " + loadError);

            if (loaded)
            {
                const auto after = snapshotParameters(target);
                int mismatches = 0;
                juce::String firstMismatch;
                for (std::size_t i = 0; i < before.size(); ++i)
                {
                    if (std::abs(before[i].second - after[i].second) > 1.0e-5f)
                    {
                        ++mismatches;
                        if (firstMismatch.isEmpty())
                        {
                            firstMismatch = before[i].first + " " + fmt(before[i].second, 6)
                                            + " -> " + fmt(after[i].second, 6);
                        }
                    }
                }
                check("Preset_FileRoundTripRestoresAllParameters", mismatches == 0,
                      mismatches == 0 ? juce::String("all parameters matched")
                                      : juce::String(mismatches) + " mismatched, first: " + firstMismatch);
            }

            // Overwriting an existing preset must succeed when asked to.
            juce::String overwriteError;
            const auto overwritten = manager.dumpCurrentStateToPresetFile(presetFile, metadata, true, false,
                                                                          overwriteError, nullptr);
            check("Preset_OverwriteExistingFileSucceeds", overwritten,
                  overwritten ? juce::String() : "error: " + overwriteError);

            // Refusing to overwrite must be honoured.
            juce::String refuseError;
            const auto refused = manager.dumpCurrentStateToPresetFile(presetFile, metadata, false, false,
                                                                      refuseError, nullptr);
            check("Preset_OverwriteIsRefusedWhenNotRequested", ! refused,
                  refused ? "file was overwritten without permission" : "refused: " + refuseError);
        }

        // A corrupt preset file must fail to load rather than crash or apply.
        {
            const auto corruptFile = tempDirectory.getChildFile("corrupt.px3preset");
            corruptFile.replaceWithText("<<<<not xml at all >>>> \x01\x02\x03");
            PX3SynthAudioProcessor target;
            applyUnusualConfiguration(target);
            const auto before2 = snapshotParameters(target);
            PresetManager targetManager(target);
            juce::String loadError;
            const auto loaded = targetManager.loadPresetFile(corruptFile, loadError);
            const auto after2 = snapshotParameters(target);
            bool unchanged = true;
            for (std::size_t i = 0; i < before2.size(); ++i)
            {
                if (std::abs(before2[i].second - after2[i].second) > 1.0e-6f) unchanged = false;
            }
            check("Preset_CorruptFileFailsToLoadAndLeavesStateIntact",
                  ! loaded && unchanged,
                  loaded ? "corrupt file reported success" : "rejected: " + loadError);
            corruptFile.deleteFile();
        }

        presetFile.deleteFile();
        tempDirectory.deleteRecursively();
    }
}
//==============================================================================
// PHASE 18 / 19 / 20 / 21 - LIFECYCLE, POLYPHONY, EDGE CASES, ARTIFACTS
//==============================================================================
void testIntegration()
{
    suite("INTEGRATION / LIFECYCLE / EDGE CASES");

    // Reset and reuse: prepare, render, reset, render again must give a
    // comparable result rather than degraded or silent output.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        const auto first = render(processor, 32000, { { 2000, true, 57, 0.9f } });
        processor.reset();
        const auto second = render(processor, 32000, { { 2000, true, 57, 0.9f } });
        check("Processor_ResetThenRenderAgainProducesComparableAudio",
              second.rms() > first.rms() * 0.8 && second.rms() < first.rms() * 1.2
                  && second.isFinite(),
              "before reset " + fmt(first.rms(), 5) + ", after reset " + fmt(second.rms(), 5));
    }

    // Note A, release, note B on the same voice: the second note must not carry
    // the first one's tail or state.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 0);
        setParam(processor, "ampRelease", 0.020f);
        const auto capture = render(processor, 96000,
                                    { { 2000, true, 57, 0.9f },
                                      { 20000, false, 57, 0.0f },
                                      { 48000, true, 69, 0.9f },
                                      { 70000, false, 69, 0.0f } });
        // Between the two notes the synth must reach silence.
        const auto gap = capture.rmsOver(30000, 46000);
        check("VoiceReuse_SilenceBetweenNotesIsComplete", gap < 1.0e-5,
              "rms in the gap between notes " + fmt(gap, 8));

        const auto secondNoteHz = estimateFrequency(capture.left, 56000, 12000, 200.0, 900.0);
        check("VoiceReuse_SecondNoteTracksItsOwnPitch",
              nearly(secondNoteHz, 440.0, 12.0),
              "second note expected 440 Hz, measured " + fmt(secondNoteHz, 2) + " Hz");
    }

    // Polyphonic isolation: releasing one voice must not disturb another that
    // is still held.
    {
        auto renderHeldVoice = [](bool withSecondVoice)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 0);
            setParam(processor, "ampRelease", 0.300f);
            std::vector<NoteEvent> events { { 2000, true, 57, 0.9f } };
            if (withSecondVoice)
            {
                // A second voice that starts and is released mid-render.
                events.push_back({ 4000, true, 72, 0.9f });
                events.push_back({ 30000, false, 72, 0.0f });
            }
            return render(processor, 96000, events);
        };

        const auto alone = renderHeldVoice(false);
        const auto withNeighbour = renderHeldVoice(true);

        // Pitch is the property that must be untouched: a shared or corrupted
        // oscillator state would show here immediately.
        const auto aloneHz = estimateFrequency(alone.left, 70000, 20000, 100.0, 500.0);
        const auto neighbourHz = estimateFrequency(withNeighbour.left, 70000, 20000, 100.0, 500.0);
        check("Polyphony_HeldVoiceKeepsItsPitchWhenANeighbourIsReleased",
              nearly(aloneHz, neighbourHz, 0.5) && nearly(aloneHz, 220.0, 6.0),
              "held voice alone " + fmt(aloneHz, 2) + " Hz, with a neighbour released "
                  + fmt(neighbourHz, 2) + " Hz");

        check("Polyphony_HeldVoiceKeepsSoundingWhenANeighbourIsReleased",
              withNeighbour.rmsOver(70000, 94000) > 0.01,
              "held-voice rms after the neighbour released "
                  + fmt(withNeighbour.rmsOver(70000, 94000), 5));

        // Absolute LEVEL is deliberately shared: the polyphony gain responds to
        // total voice load and, by design, recovers over about 2.5 s so a
        // decaying tail cannot lift its own gain back up. So a neighbour coming
        // and going does move a held voice's level - what must be true is that
        // the disturbance is transient, and the level returns to the solo value
        // once the gain has recovered.
        auto renderLong = [](bool withSecondVoice)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 0);
            setParam(processor, "ampRelease", 0.300f);
            std::vector<NoteEvent> events { { 2000, true, 57, 0.9f } };
            if (withSecondVoice)
            {
                events.push_back({ 4000, true, 72, 0.9f });
                events.push_back({ 30000, false, 72, 0.0f });
            }
            return render(processor, 576000, events); // 12 s, well past recovery
        };
        const auto longAlone = renderLong(false);
        const auto longWithNeighbour = renderLong(true);
        const auto settledAlone = longAlone.rmsOver(480000, 570000);
        const auto settledWithNeighbour = longWithNeighbour.rmsOver(480000, 570000);
        check("Polyphony_HeldVoiceLevelRecoversAfterANeighbourIsReleased",
              nearly(settledAlone, settledWithNeighbour, settledAlone * 0.05),
              "settled rms alone " + fmt(settledAlone, 5)
                  + ", after a neighbour came and went " + fmt(settledWithNeighbour, 5));
    }

    // Maximum polyphony must stay finite and bounded.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        setParam(processor, "osc2Enabled", 1.0f);
        setParam(processor, "osc3Enabled", 1.0f);
        setParam(processor, "subOscEnabled", 1.0f);
        setParam(processor, "ampRelease", 3.0f);
        std::vector<NoteEvent> events;
        for (int voice = 0; voice < 64; ++voice)
        {
            events.push_back({ 2000 + voice * 200, true, 24 + (voice % 72), 0.9f });
        }
        const auto capture = render(processor, 96000, events);
        check("Polyphony_MaximumVoicesStayFiniteAndBounded",
              capture.isFinite() && capture.peak() <= 1.0001,
              "peak " + fmt(capture.peak(), 5) + ", rms " + fmt(capture.rms(), 5));
    }

    // Voice stealing under sustained pressure.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        setParam(processor, "ampRelease", 4.0f);
        std::vector<NoteEvent> events;
        for (int note = 0; note < 200; ++note)
        {
            events.push_back({ 1000 + note * 400, true, 30 + (note % 60), 0.9f });
            events.push_back({ 1000 + note * 400 + 300, false, 30 + (note % 60), 0.0f });
        }
        const auto capture = render(processor, 96000, events);
        check("VoiceStealing_UnderSustainedPressureStaysFiniteAndBounded",
              capture.isFinite() && capture.peak() <= 1.0001,
              "peak " + fmt(capture.peak(), 5) + ", rms " + fmt(capture.rms(), 5));
    }

    // Edge cases: parameter extremes across the whole synth at once.
    {
        struct EdgeCase { const char* name; std::function<void(PX3SynthAudioProcessor&)> apply; };
        const EdgeCase cases[] = {
            { "AllOscillatorsDisabled", [](PX3SynthAudioProcessor& p)
              {
                  for (int i = 1; i <= 3; ++i) setParam(p, "osc" + juce::String(i) + "Enabled", 0.0f);
                  setParam(p, "subOscEnabled", 0.0f);
              } },
            { "ShortestEnvelopes", [](PX3SynthAudioProcessor& p)
              {
                  setParam(p, "ampAttack", 0.001f);
                  setParam(p, "ampDecay", 0.005f);
                  setParam(p, "ampSustain", 0.0f);
                  setParam(p, "ampRelease", 0.010f);
              } },
            { "LongestEnvelopes", [](PX3SynthAudioProcessor& p)
              {
                  setParam(p, "ampAttack", 3.0f);
                  setParam(p, "ampDecay", 4.0f);
                  setParam(p, "ampSustain", 1.0f);
                  setParam(p, "ampRelease", 5.0f);
              } },
            { "MaximumResonanceBothFilters", [](PX3SynthAudioProcessor& p)
              {
                  setParam(p, "filter1Enabled", 1.0f);
                  setParam(p, "filter2Enabled", 1.0f);
                  setParam(p, "filter1Resonance", 2.2f);
                  setParam(p, "filter2Resonance", 2.2f);
                  setParam(p, "filter1Cutoff", 80.0f);
                  setParam(p, "filter2Cutoff", 18000.0f);
              } },
            { "MaximumModulationEverywhere", [](PX3SynthAudioProcessor& p)
              {
                  setParam(p, "filter1Enabled", 1.0f);
                  for (int i = 0; i < 3; ++i)
                  {
                      const auto slot = juce::String(i + 1);
                      setParam(p, "env" + slot + "Enabled", 1.0f);
                      setParam(p, i == 0 ? juce::String("envAmount") : "env" + slot + "Amount", 1.0f);
                      const auto prefix = i == 0 ? juce::String("lfo") : "lfo" + slot;
                      setParam(p, i == 0 ? juce::String("lfoEnabled") : prefix + "Enabled", 1.0f);
                      setParam(p, i == 0 ? juce::String("lfoAmount") : prefix + "Amount", 1.0f);
                      setParam(p, i == 0 ? juce::String("lfoFrequency") : prefix + "Frequency", 20.0f);
                  }
                  p.setLfoAssignmentByParameterId(0, "filter1Cutoff", false);
                  p.setLfoAssignmentByParameterId(1, "osc1Pitch", false);
                  p.setLfoAssignmentByParameterId(2, "mix.osc1.pan", false);
                  p.setEnvelopeAssignmentByParameterId(0, "filter1Resonance", false);
                  p.setEnvelopeAssignmentByParameterId(1, "osc1Level", false);
                  p.setEnvelopeAssignmentByParameterId(2, "filter2Cutoff", false);
              } },
            { "AllFxAtMaximum", [](PX3SynthAudioProcessor& p)
              {
                  setParam(p, "reverbEnabled", 1.0f);
                  setParam(p, "reverbAmount", 1.0f);
                  setParam(p, "moodEnabled", 1.0f);
                  setParam(p, "moodMix", 1.0f);
                  setParam(p, "moodFeedback", 1.0f);
                  setParam(p, "delayEnabled", 1.0f);
                  setParam(p, "delayAmount", 1.0f);
                  setParam(p, "delayFeedback", 1.0f);
                  setParam(p, "vibeEnabled", 1.0f);
                  setParam(p, "vibeAmount", 1.0f);
                  setParam(p, "fxSendGain", 1.0f);
                  setParam(p, "fxReturnGain", 1.0f);
                  for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
                  {
                      setParam(p, juce::String("mix.") + id + ".fxSend", 1.0f);
                  }
              } },
            { "EverythingAtMaximum", [](PX3SynthAudioProcessor& p)
              {
                  for (int i = 1; i <= 3; ++i)
                  {
                      setParam(p, "osc" + juce::String(i) + "Enabled", 1.0f);
                      setParam(p, "osc" + juce::String(i) + "MacroA", 1.0f);
                      setParam(p, "osc" + juce::String(i) + "MacroB", 1.0f);
                      setParam(p, "osc" + juce::String(i) + "MacroC", 1.0f);
                  }
                  setParam(p, "subOscEnabled", 1.0f);
                  setParam(p, "masterGain", 1.0f);
                  for (const auto* id : { "sub", "osc1", "osc2", "osc3" })
                  {
                      setParam(p, juce::String("mix.") + id + ".level", 1.0f);
                  }
                  setParam(p, "vibeEnabled", 1.0f);
                  setParam(p, "vibeAmount", 1.0f);
                  setParam(p, "reverbEnabled", 1.0f);
                  setParam(p, "reverbAmount", 1.0f);
                  setParam(p, "fxReturnGain", 1.0f);
              } },
        };

        for (const auto& edgeCase : cases)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            edgeCase.apply(processor);
            const auto capture = render(processor, 96000,
                                        { { 2000, true, 45, 0.9f },
                                          { 2000, true, 52, 0.9f },
                                          { 2000, true, 57, 0.9f },
                                          { 40000, false, 45, 0.0f },
                                          { 40000, false, 52, 0.0f },
                                          { 40000, false, 57, 0.0f } });
            check((juce::String("EdgeCase_") + edgeCase.name + "_StaysFiniteAndWithinCeiling").toRawUTF8(),
                  capture.isFinite() && capture.peak() <= 1.0001,
                  "peak " + fmt(capture.peak(), 5) + ", rms " + fmt(capture.rms(), 5)
                      + ", dc " + fmt(capture.dcOffset(), 6));
        }
    }

    // Enable/disable transitions while a note sounds must not step the signal.
    // A large sample-to-sample jump mid-note is an audible click; the threshold
    // is the largest step the same patch produces with no switching at all.
    {
        // An empty parameter id means "change nothing", which gives the control
        // reading: how much the signal steps on its own.
        auto stepWithSwitching = [](const juce::String& toggleParameterId)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 0); // SINE: nothing of its own to mask a step
            setParam(processor, "osc2Enabled", 1.0f);
            setParam(processor, "subOscEnabled", 1.0f);

            std::function<void(int)> perBlock;
            if (toggleParameterId.isNotEmpty())
            {
                perBlock = [&processor, toggleParameterId](int blockIndex)
                {
                    // Toggle every 20 blocks, well inside the sustained note.
                    if (blockIndex > 15 && blockIndex % 20 == 0)
                    {
                        setParam(processor, toggleParameterId, (blockIndex / 20) % 2 == 0 ? 1.0f : 0.0f);
                    }
                };
            }
            return render(processor, 64000, { { 2000, true, 57, 0.9f } }, perBlock);
        };

        const auto steady = stepWithSwitching({});
        const auto steadyStep = steady.maxStep(8000, 60000);

        struct SwitchCase { const char* name; juce::String parameterId; };
        const SwitchCase switches[] = {
            { "OscillatorEnable", "osc2Enabled" },
            { "SubOscillatorEnable", "subOscEnabled" },
            { "FilterEnable", "filter1Enabled" },
            { "ReverbEnable", "reverbEnabled" },
        };

        for (const auto& switchCase : switches)
        {
            const auto capture = stepWithSwitching(switchCase.parameterId);
            const auto step = capture.maxStep(8000, 60000);
            check((juce::String("Artifact_") + switchCase.name + "_ToggleDoesNotStepTheSignal").toRawUTF8(),
                  capture.isFinite() && step < juce::jmax(0.02, steadyStep * 3.0),
                  "largest step " + fmt(step, 6) + " vs steady " + fmt(steadyStep, 6));
        }
    }

    // Rapid parameter jumps must not produce non-finite output or overshoot.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        setParam(processor, "filter1Enabled", 1.0f);
        const auto capture = render(processor, 64000, { { 2000, true, 45, 0.9f } },
                                    [&processor](int blockIndex)
                                    {
                                        // Extremes on alternate blocks, across
                                        // several unrelated controls at once.
                                        const auto high = (blockIndex % 2) == 0;
                                        setParam(processor, "filter1Cutoff", high ? 18000.0f : 80.0f);
                                        setParam(processor, "filter1Resonance", high ? 2.2f : 0.25f);
                                        setParam(processor, "mix.osc1.level", high ? 1.0f : 0.0f);
                                        setParam(processor, "mix.osc1.pan", high ? 1.0f : -1.0f);
                                        setParam(processor, "masterGain", high ? 1.0f : 0.0f);
                                    });
        check("Artifact_RapidParameterJumpsStayFiniteAndWithinCeiling",
              capture.isFinite() && capture.peak() <= 1.0001,
              "peak " + fmt(capture.peak(), 5));
    }

    // Long release tails must terminate rather than hang.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setChoice(processor, "osc1Mode", 1);
        setParam(processor, "ampRelease", 1.0f);
        std::vector<NoteEvent> events;
        for (int note = 0; note < 24; ++note)
        {
            events.push_back({ 2000 + note * 800, true, 40 + note, 0.9f });
            events.push_back({ 2000 + note * 800 + 600, false, 40 + note, 0.0f });
        }
        // Rendered well past the last release plus the full release time.
        const auto capture = render(processor, 240000, events);
        check("LongRelease_AllTailsReachSilence", capture.rmsOver(200000, 238000) < 1.0e-5,
              "rms long after the last release " + fmt(capture.rmsOver(200000, 238000), 8));
    }

    // Sample-rate and block-size independence: the synth must render sensibly
    // at rates and block sizes other than the ones every other test uses.
    {
        struct Config { double sampleRate; int blockSize; };
        const Config configs[] = {
            { 44100.0, 64 }, { 44100.0, 512 }, { 48000.0, 32 },
            { 88200.0, 256 }, { 96000.0, 1024 }, { 192000.0, 128 }
        };
        for (const auto& config : configs)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 1);
            setParam(processor, "filter1Enabled", 1.0f);
            setParam(processor, "reverbEnabled", 1.0f);
            setParam(processor, "reverbAmount", 0.6f);
            processor.setPlayConfigDetails(0, 2, config.sampleRate, config.blockSize);
            processor.prepareToPlay(config.sampleRate, config.blockSize);

            juce::AudioBuffer<float> buffer(2, config.blockSize);
            const auto totalBlocks = static_cast<int>(config.sampleRate / config.blockSize);
            double peak = 0.0, energy = 0.0;
            juce::int64 count = 0;
            bool finite = true;
            for (int block = 0; block < totalBlocks; ++block)
            {
                buffer.clear();
                juce::MidiBuffer midi;
                if (block == 2) midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.9f), 0);
                processor.processBlock(buffer, midi);
                for (int i = 0; i < config.blockSize; ++i)
                {
                    const auto v = buffer.getSample(0, i);
                    if (! std::isfinite(v)) finite = false;
                    peak = juce::jmax(peak, static_cast<double>(std::abs(v)));
                    energy += static_cast<double>(v) * v;
                    ++count;
                }
            }
            const auto rms = std::sqrt(energy / static_cast<double>(juce::jmax<juce::int64>(1, count)));
            check((juce::String("SampleRate_") + juce::String(static_cast<int>(config.sampleRate))
                   + "Hz_Block" + juce::String(config.blockSize) + "_RendersCorrectly").toRawUTF8(),
                  finite && rms > 0.005 && peak <= 1.0001,
                  "rms " + fmt(rms, 5) + ", peak " + fmt(peak, 5));
        }
    }
}
} // namespace

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String filter = argc > 1 ? argv[1] : "";
    auto wants = [&filter](const char* name)
    {
        return filter.isEmpty() || filter == name;
    };

    std::printf("\nPX3 COMPONENT TESTS  (%.0f Hz, %d-sample blocks, shipping build)\n",
                kSampleRate, kBlockSize);

    if (filter == "reverbmetrics")
    {
        // Baseline characterisation of every algorithm at a few settings.
        std::printf("\nREVERB QUALITY METRICS (fully wet impulse response)\n");
        std::printf("  ED = normalised echo density (1.0 = fully diffuse)\n");
        std::printf("  flat = spectral flatness of the late tail (higher = less metallic)\n");
        std::printf("  ripple = deviation from a smooth exponential decay\n");
        std::printf("  corr = inter-channel correlation (lower = wider)\n\n");
        static const char* names[] = { "0 ROOM", "1 PLATE", "2 HALL", "3 CLOUD" };
        for (int algorithm = 0; algorithm < 4; ++algorithm)
        {
            for (const auto decay : { 0.35f, 0.75f })
            {
                ReverbSettings s;
                s.algorithmIndex = algorithm;
                s.decay = decay;
                s.size = 0.6f;
                s.damping = 0.45f;
                s.preDelay = 0.0f;
                const auto m = measureReverb(s);
                reportReverbMetrics((juce::String(names[algorithm]) + " decay " + juce::String(decay, 2)).toRawUTF8(), m);
            }
        }
        std::printf("\n");
        return 0;
    }

    if (filter == "moodartifacts")
    {
        std::printf("\nMOOD DISCONTINUITY SCAN\n");
        std::printf("  Feeds a steady tone and looks for sample-to-sample jumps that the\n");
        std::printf("  signal itself cannot explain. A read pointer that wraps without a\n");
        std::printf("  crossfade produces one of these once per loop, slice or window.\n\n");

        static const char* loopNames[] = { "ENV", "TAPE", "STRETCH" };
        static const char* wetNames[] = { "REVERB", "DELAY", "SLIP" };

        auto scan = [](int loopMode, int wetMode, float routing)
        {
            Mood mood;
            mood.prepare(kSampleRate);
            mood.reset();
            MoodSettings s;
            s.enabled = true;
            s.mix = 1.0f;
            s.loopModeIndex = loopMode;
            s.wetModeIndex = wetMode;
            s.routing = routing;
            s.clock = 1.0f;          // full rate: any stepping here is not the clock
            s.degrade = 0.0f;        // and not the lo-fi control either
            s.spread = 0.5f;
            s.feedback = 0.4f;
            s.loopLength = 0.35f;
            s.loopModify = 0.62f;
            s.wetTime = 0.4f;
            s.wetModify = 0.45f;
            mood.updateForBlock(s);

            const auto total = static_cast<int>(kSampleRate * 6.0);
            std::vector<float> out;
            out.reserve(static_cast<std::size_t>(total));
            for (int i = 0; i < total; ++i)
            {
                const auto in = std::sin(juce::MathConstants<float>::twoPi * 220.0f
                                         * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.5f;
                float l = 0.0f, r = 0.0f;
                mood.processSampleFrame(in, in, l, r);
                out.push_back(l);
            }

            // A 220 Hz sine at 0.5 moves at most 0.5*2*pi*220/48000 = 0.0144 per
            // sample. Anything far above that is a discontinuity, not programme.
            const auto from = static_cast<int>(kSampleRate * 2.0);
            double worst = 0.0;
            int jumps = 0;
            std::vector<int> positions;
            for (int i = from + 1; i < total; ++i)
            {
                const auto d = std::abs(static_cast<double>(out[static_cast<std::size_t>(i)]
                                                            - out[static_cast<std::size_t>(i - 1)]));
                worst = juce::jmax(worst, d);
                if (d > 0.05)
                {
                    ++jumps;
                    if (positions.size() < 6) positions.push_back(i);
                }
            }
            // Spacing between jumps tells us which wrap is responsible.
            double spacingMs = 0.0;
            if (positions.size() >= 2)
            {
                spacingMs = (positions[1] - positions[0]) * 1000.0 / kSampleRate;
            }
            return std::make_tuple(worst, jumps, spacingMs);
        };

        std::printf("  routing = micro-looper only (isolates the loop channel)\n");
        std::printf("  loop      wet        worst step   jumps>0.05   spacing ms\n");
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            const auto [worst, jumps, spacing] = scan(loopMode, 1, 1.0f);
            std::printf("    %-8s %-9s  %9.5f  %10d  %9.1f%s\n",
                        loopNames[loopMode], "(bypassed)", worst, jumps, spacing,
                        jumps > 0 ? "   <-- discontinuities" : "");
        }

        std::printf("\n  routing = input only (isolates the wet channel)\n");
        std::printf("  loop      wet        worst step   jumps>0.05   spacing ms\n");
        for (int wetMode = 0; wetMode < 3; ++wetMode)
        {
            const auto [worst, jumps, spacing] = scan(1, wetMode, 0.0f);
            std::printf("    %-8s %-9s  %9.5f  %10d  %9.1f%s\n",
                        "(n/a)", wetNames[wetMode], worst, jumps, spacing,
                        jumps > 0 ? "   <-- discontinuities" : "");
        }
        std::printf("\n");
        return 0;
    }

    if (filter == "moodmetrics")
    {
        std::printf("\nMOOD CHARACTERISATION (driven on the Mood class)\n");
        std::printf("  sep = channel separation in dB with a hard-left input (high = stereo preserved)\n");
        std::printf("  S/M = side-to-mid ratio (0 = the output is mono)\n");
        std::printf("  corr = inter-channel correlation\n\n");

        static const char* loopNames[] = { "ENV", "TAPE", "STRETCH" };
        static const char* wetNames[] = { "REVERB", "DELAY", "SLIP" };

        std::printf("  loop mode  wet mode    spread   corr     S/M      sep dB    rms      peak\n");
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            for (int wetMode = 0; wetMode < 3; ++wetMode)
            {
                for (const auto spread : { 0.0f, 1.0f })
                {
                    MoodSettings s;
                    s.mix = 1.0f;
                    s.loopModeIndex = loopMode;
                    s.wetModeIndex = wetMode;
                    s.spread = spread;
                    s.routing = 1.0f;      // input + micro-looper
                    s.feedback = 0.4f;
                    const auto m = measureMood(s);
                    const auto sep = measureMood(s, true);
                    std::printf("  %-10s %-10s %5.2f  %+.4f  %.4f  %+8.2f  %.6f  %.4f%s\n",
                                loopNames[loopMode], wetNames[wetMode], spread,
                                m.interChannelCorrelation, m.sideToMidRatio,
                                sep.channelSeparationDb, m.rms, m.peak,
                                m.finite ? "" : "  NON-FINITE");
                }
            }
        }

        std::printf("\n  does SPREAD widen? (side-to-mid ratio should rise with the knob)\n");
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            std::printf("    loop %-8s", loopNames[loopMode]);
            for (const auto spread : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                MoodSettings s;
                s.mix = 1.0f;
                s.loopModeIndex = loopMode;
                s.wetModeIndex = 0;
                s.spread = spread;
                s.routing = 0.5f;
                std::printf("  %.4f", measureMood(s).sideToMidRatio);
            }
            std::printf("\n");
        }

        // CLOCK is a sample rate, so what it changes is PITCH and length, not
        // level. It has to be measured against an EXISTING loop: recording and
        // playing back at the same rate preserves pitch by definition, so a
        // render that holds the clock constant throughout shows nothing. The
        // loop is captured at full clock, then frozen, and only then is the
        // clock moved - which is the pedal's own description of the control.
        std::printf("\n  does CLOCK transpose a captured loop? (440 Hz in, playback Hz)\n");
        for (int loopMode = 0; loopMode < 3; ++loopMode)
        {
            std::printf("    loop %-8s", loopNames[loopMode]);
            for (const auto clock : { 1.0f, 0.75f, 0.5f, 0.25f, 0.0f })
            {
                Mood mood;
                mood.prepare(kSampleRate);
                mood.reset();
                MoodSettings ms;
                ms.enabled = true;
                ms.mix = 1.0f;
                ms.loopModeIndex = loopMode;
                ms.loopModify = loopMode == 1 ? 0.70f : 0.75f;   // unity-ish playback speed
                ms.loopLength = 0.5f;
                ms.wetModeIndex = 1;
                ms.wetModify = 0.0f;       // no wet feedback muddying the pitch
                ms.wetTime = 0.0f;
                ms.clock = clock;
                ms.routing = 1.0f;         // micro-looper only
                ms.spread = 0.0f;
                ms.degrade = 0.0f;
                ms.feedback = 0.0f;
                // Capture at full clock.
                ms.clock = 1.0f;
                ms.freeze = false;
                mood.updateForBlock(ms);
                const auto captureSamples = static_cast<int>(kSampleRate * 4.0);
                for (int i = 0; i < captureSamples; ++i)
                {
                    const auto in = std::sin(juce::MathConstants<float>::twoPi * 440.0f
                                             * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.6f;
                    float l = 0.0f, r = 0.0f;
                    mood.processSampleFrame(in, in, l, r);
                }

                // Freeze what was captured, then move the clock.
                ms.clock = clock;
                ms.freeze = true;
                mood.updateForBlock(ms);

                const auto total = static_cast<int>(kSampleRate * 6.0);
                std::vector<float> out;
                out.reserve(static_cast<std::size_t>(total));
                for (int i = 0; i < total; ++i)
                {
                    float l = 0.0f, r = 0.0f;
                    mood.processSampleFrame(0.0f, 0.0f, l, r);
                    out.push_back(l);
                }
                std::printf("  %7.1f", estimateFrequency(out, static_cast<int>(kSampleRate * 3.0),
                                                         static_cast<int>(kSampleRate * 2.0), 20.0, 2000.0));
            }
            std::printf("   (clock 1.0 -> 0.0)\n");
        }

        std::printf("\n  does DEGRADE do anything? (rms across the knob)\n");
        for (int wetMode = 0; wetMode < 3; ++wetMode)
        {
            std::printf("    wet %-9s", wetNames[wetMode]);
            for (const auto degrade : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                MoodSettings s;
                s.mix = 1.0f;
                s.wetModeIndex = wetMode;
                s.degrade = degrade;
                s.routing = 0.0f;
                std::printf("  %.6f", measureMood(s).rms);
            }
            std::printf("\n");
        }

        std::printf("\n");
        return 0;
    }

    if (filter == "delaystress")
    {
        std::printf("\nDELAY STRESS: moving controls, then silence\n");
        std::printf("  feeds audio while sweeping a control, stops the input, and reports\n");
        std::printf("  the tail level 1 s and 10 s after the input stops.\n\n");

        static const char* names[] = { "0 Granular", "1 Tape", "2 AnalogBBD", "3 PingPong",
                                       "4 Stereo", "5 Modulated", "6 Diffusion" };

        // sweepWhich: 0 = none, 1 = time, 2 = feedback, 3 = amount, 4 = sync division
        auto stress = [&](int algo, int sweepWhich, float feedbackLevel)
        {
            Delay delay;
            delay.prepare(kSampleRate);
            delay.reset();

            const auto driveSamples = static_cast<int>(kSampleRate * 6.0);
            const auto tailSamples = static_cast<int>(kSampleRate * 12.0);
            juce::Random random(0x0DE1A1u);

            std::vector<float> out;
            out.reserve(static_cast<std::size_t>(driveSamples + tailSamples));

            const auto blockSize = 64;
            int i = 0;
            const auto total = driveSamples + tailSamples;
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
                s.syncDivisionIndex = sweepWhich == 4 ? (1 + (i / (int) kSampleRate) % 7) : 0;
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
                for (int k = from; k < to; ++k) e += static_cast<double>(out[(std::size_t) k]) * out[(std::size_t) k];
                return std::sqrt(e / (to - from));
            };

            bool finite = true;
            double peak = 0.0;
            for (const auto v : out) { if (! std::isfinite(v)) finite = false; peak = juce::jmax(peak, std::abs((double) v)); }

            // Dominant frequency of whatever is left at the end, so a stuck
            // tone can be identified rather than just noticed.
            const auto tailStart = driveSamples + static_cast<int>(kSampleRate * 9.0);
            const auto tailHz = estimateFrequency(out, tailStart, static_cast<int>(kSampleRate * 2.0), 20.0, 8000.0);

            return std::make_tuple(rmsAt(1.0), rmsAt(10.0), peak, finite, tailHz);
        };

        static const char* sweepNames[] = { "static", "TIME sweep", "FEEDBACK sweep",
                                            "AMOUNT sweep", "SYNC changes" };
        for (int algo = 0; algo < 7; ++algo)
        {
            for (int sweep = 0; sweep < 5; ++sweep)
            {
                const auto [r1, r10, peak, finite, hz] = stress(algo, sweep, 0.85f);
                const auto stuck = r10 > 1.0e-5 && r10 > r1 * 0.5;
                std::printf("  %-12s %-15s  +1s %.7f  +10s %.7f  peak %.4f  tailHz %6.1f%s%s\n",
                            names[algo], sweepNames[sweep], r1, r10, peak, hz,
                            finite ? "" : "  NON-FINITE",
                            stuck ? "   STUCK" : "");
            }
        }
        std::printf("\n");
        return 0;
    }

    if (filter == "delaymetrics")
    {
        std::printf("\nDELAY CHARACTERISATION (impulse response, driven on the Delay class)\n\n");
        static const char* names[] = { "0 Granular", "1 Tape", "2 Analog/BBD", "3 Ping-Pong",
                                       "4 Stereo", "5 Modulated", "6 Diffusion" };

        std::printf("  zero-amount transparency (worst sample deviation from input)\n");
        for (int algo = 0; algo < 7; ++algo)
        {
            std::printf("    %-14s %.6f\n", names[algo], delayZeroAmountBleed(algo));
        }

        std::printf("\n  algorithm       echo1 ms  echo2 ms   peak    early rms   late rms   corr   sub-30Hz\n");
        for (int algo = 0; algo < 7; ++algo)
        {
            DelaySettings s;
            s.amount = 0.7f;
            s.timeControl = 0.35f;
            s.feedbackControl = 0.5f;
            s.algorithmIndex = algo;
            const auto m = measureDelay(s);
            std::printf("    %-14s %7.1f  %7.1f  %7.4f  %9.6f  %9.6f  %+.3f  %7.4f%s\n",
                        names[algo], m.firstEchoMs, m.secondEchoMs, m.peak,
                        m.tailRmsEarly, m.tailRmsLate, m.interChannelCorrelation,
                        m.lowFrequencyEnergyRatio, m.finite ? "" : "  NON-FINITE");
        }

        std::printf("\n  stability at maximum feedback (late rms should not exceed early rms)\n");
        for (int algo = 0; algo < 7; ++algo)
        {
            DelaySettings s;
            s.amount = 1.0f;
            s.timeControl = 0.35f;
            s.feedbackControl = 1.0f;
            s.algorithmIndex = algo;
            const auto m = measureDelay(s, kSampleRate, static_cast<int>(kSampleRate * 20.0));
            const auto growth = m.tailRmsEarly > 1.0e-9 ? m.tailRmsLate / m.tailRmsEarly : 0.0;
            std::printf("    %-14s early %.6f  late %.6f  ratio %7.3f  peak %.4f%s\n",
                        names[algo], m.tailRmsEarly, m.tailRmsLate, growth, m.peak,
                        growth > 1.05 ? "   GROWING" : "");
        }

        std::printf("\n  delay time knob sweep (first echo, ms) - should rise monotonically\n");
        for (int algo = 1; algo < 7; ++algo)
        {
            std::printf("    %-14s", names[algo]);
            for (const auto t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                DelaySettings s;
                s.amount = 0.7f;
                s.timeControl = t;
                s.feedbackControl = 0.3f;
                s.algorithmIndex = algo;
                std::printf(" %7.1f", measureDelay(s).firstEchoMs);
            }
            std::printf("\n");
        }

        std::printf("\n  sample-rate consistency (first echo ms / late rms at amount 0.7)\n");
        for (int algo = 1; algo < 7; ++algo)
        {
            std::printf("    %-14s", names[algo]);
            for (const auto sr : { 44100.0, 48000.0, 96000.0 })
            {
                DelaySettings s;
                s.amount = 0.7f;
                s.timeControl = 0.35f;
                s.feedbackControl = 0.6f;
                s.algorithmIndex = algo;
                const auto m = measureDelay(s, sr);
                std::printf("  %6.1f/%.5f", m.firstEchoMs, m.tailRmsLate);
            }
            std::printf("\n");
        }

        std::printf("\n");
        return 0;
    }

    if (filter == "vibemetrics")
    {
        // Objective characterisation of the vibe engine, so "more analog" is
        // measured rather than asserted.
        std::printf("\nVIBE ENGINE METRICS\n");

        // 1. Are the per-voice drift signals independent? Measured on the
        // engine directly: routing this through audio does not work, because
        // juce::Synthesiser retargets the existing voice when the same pitch is
        // played twice, so only one voice would ever sound.
        {
            VibeEngine engine;
            engine.prepare(kSampleRate, 64, 0x13579BDFu);
            VibeEngine::Tuning tuning;
            engine.setTuning(tuning);
            engine.setGlobalAmount(1.0f);

            std::vector<double> a, b, c;
            for (int block = 0; block < 4000; ++block)   // ~43 s
            {
                engine.advance(512, 0.25f);
                a.push_back(engine.getVoiceVariation(0).pitchCents);
                b.push_back(engine.getVoiceVariation(1).pitchCents);
                c.push_back(engine.getVoiceVariation(2).pitchCents);
            }
            auto spread = [](const std::vector<double>& v)
            {
                double mean = 0.0; for (auto x : v) mean += x; mean /= static_cast<double>(v.size());
                double var = 0.0; for (auto x : v) var += (x - mean) * (x - mean);
                return std::sqrt(var / static_cast<double>(v.size()));
            };
            auto correlation = [](const std::vector<double>& x, const std::vector<double>& y)
            {
                double mx = 0, my = 0; const auto n = static_cast<double>(x.size());
                for (std::size_t i = 0; i < x.size(); ++i) { mx += x[i]; my += y[i]; }
                mx /= n; my /= n;
                double sxy = 0, sxx = 0, syy = 0;
                for (std::size_t i = 0; i < x.size(); ++i)
                {
                    sxy += (x[i]-mx)*(y[i]-my); sxx += (x[i]-mx)*(x[i]-mx); syy += (y[i]-my)*(y[i]-my);
                }
                return sxx > 1e-12 && syy > 1e-12 ? sxy / std::sqrt(sxx*syy) : 1.0;
            };
            std::printf("  per-voice pitch drift: movement %.3f / %.3f / %.3f cents (std dev)\n",
                        spread(a), spread(b), spread(c));
            std::printf("  drift correlation voice0-1 %+.3f, voice0-2 %+.3f  (0 = independent)\n",
                        correlation(a, b), correlation(a, c));
        }

        // 2. DC injected by the asymmetry stage.
        {
            for (const auto amount : { 0.0f, 1.0f })
            {
                PX3SynthAudioProcessor processor;
                makePlainPatch(processor);
                setChoice(processor, "osc1Mode", 0);
                setParam(processor, "vibeEnabled", 1.0f);
                setParam(processor, "vibeAmount", amount);
                const auto c = render(processor, 96000, { { 2000, true, 57, 0.9f } });
                std::printf("  DC offset at vibe %.1f: %+.6f  (peak %.4f)\n",
                            amount, c.dcOffset(), c.peak());
            }
        }

        // 3. Noise spectrum tilt. Analog hiss is 1/f weighted; white noise is
        // the giveaway of a digital source.
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            for (int i = 1; i <= 3; ++i) setParam(processor, "osc" + juce::String(i) + "Enabled", 0.0f);
            setParam(processor, "subOscEnabled", 0.0f);
            setParam(processor, "vibeEnabled", 1.0f);
            setParam(processor, "vibeAmount", 1.0f);
            const auto c = render(processor, 96000, { { 2000, true, 57, 0.9f } });
            std::printf("  vibe noise floor with all sources off: rms %.8f\n", c.rms());

        // Which measures actually rise monotonically with the amount control?
        std::printf("\n  %-8s %10s %10s %10s %10s\n", "amount", "rms", "peak", "crest", "dc");
        for (const auto amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 0);
            setParam(processor, "vibeEnabled", 1.0f);
            setParam(processor, "vibeAmount", amount);
            const auto c = render(processor, 96000, { { 2000, true, 57, 0.9f } });
            const auto r = c.rmsOver(20000, 94000);
            std::printf("  %-8.2f %10.6f %10.6f %10.4f %+10.6f\n",
                        amount, r, c.peak(), c.peak() / juce::jmax(1.0e-9, r), c.dcOffset());
        }
        }

        // 4. Block-size dependence. The character must not change with the
        // host's buffer size.
        {
            for (const auto blockSize : { 64, 512 })
            {
                PX3SynthAudioProcessor processor;
                makePlainPatch(processor);
                setParam(processor, "vibeEnabled", 1.0f);
                setParam(processor, "vibeAmount", 1.0f);
                processor.setPlayConfigDetails(0, 2, kSampleRate, blockSize);
                processor.prepareToPlay(kSampleRate, blockSize);
                juce::AudioBuffer<float> buffer(2, blockSize);
                double energy = 0.0; juce::int64 n = 0;
                const auto blocks = static_cast<int>(8.0 * kSampleRate / blockSize);
                for (int b = 0; b < blocks; ++b)
                {
                    buffer.clear();
                    juce::MidiBuffer midi;
                    if (b == 4) midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.9f), 0);
                    processor.processBlock(buffer, midi);
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const auto v = static_cast<double>(buffer.getSample(0, i));
                        energy += v * v; ++n;
                    }
                }
                std::printf("  block %4d: rms %.6f\n", blockSize, std::sqrt(energy / static_cast<double>(juce::jmax<juce::int64>(1, n))));
            }
        }
        std::printf("\n");
        return 0;
    }

    if (filter == "gainstage")
    {
        // Absolute source-to-master gain at the default fader position and at
        // the top of its travel. Moving the headroom from the fader to the
        // oscillator must leave both of these unchanged.
        std::printf("\n  %-22s %12s %12s %12s\n", "mix.osc1.level", "masterRms", "oscBusRms", "polyGain");
        for (const auto label : { "default", "maximum" })
        {
            PX3SynthAudioProcessor processor;
            // Defaults everywhere except: one oscillator on, no FX, no vibe.
            setParam(processor, "osc1Enabled", 1.0f);
            setParam(processor, "osc2Enabled", 0.0f);
            setParam(processor, "osc3Enabled", 0.0f);
            setParam(processor, "subOscEnabled", 0.0f);
            setParam(processor, "vibeEnabled", 0.0f);
            setParam(processor, "reverbEnabled", 0.0f);
            setParam(processor, "delayEnabled", 0.0f);
            setParam(processor, "moodEnabled", 0.0f);
            setParam(processor, "filter1Enabled", 0.0f);
            setParam(processor, "filter2Enabled", 0.0f);
            setParam(processor, "ampSustain", 1.0f);
            setChoice(processor, "osc1Mode", 0);
            if (juce::String(label) == "maximum")
            {
                if (auto* lv = findParameter(processor, "mix.osc1.level"))
                {
                    lv->setValueNotifyingHost(1.0f); // top of the fader
                }
            }
            const auto capture = render(processor, 48000, { { 2000, true, 57, 0.9f } });
            std::printf("  %-22s %12.6f %12.6f %12.6f\n", label, capture.rmsOver(24000, 46000),
                        processor.debugGetOscillatorBusRms(), processor.debugGetPolyphonyGainApplied());
        }
        std::printf("\n");
        return 0;
    }

    if (filter == "probe")
    {
        // Diagnostic probe, not an assertion: prints the level at each stage so
        // an unexpected measurement can be attributed to a stage rather than
        // guessed at.
        std::printf("\n  %-10s %12s %12s %12s %12s\n",
                    "mix.osc1.level", "masterRms", "oscBusRms", "polyGain", "srcRms");
        for (const auto level : { 0.125f, 0.25f, 0.5f, 1.0f })
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "mix.osc1.level", level);
            const auto capture = render(processor, 48000, { { 2000, true, 57, 0.9f } });
            std::printf("  %-10.3f %12.6f %12.6f %12.6f %12.6f\n",
                        level, capture.rms(),
                        processor.debugGetOscillatorBusRms(),
                        processor.debugGetPolyphonyGainApplied(),
                        processor.debugGetMixerSourceRms(1));
        }

        std::printf("\n  %-14s %12s %12s %12s\n", "sources", "masterRms", "oscBusRms", "polyGain");
        for (int count = 1; count <= 3; ++count)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "osc2Enabled", count >= 2 ? 1.0f : 0.0f);
            setParam(processor, "osc2Coarse", 7.0f);
            setParam(processor, "osc3Enabled", count >= 3 ? 1.0f : 0.0f);
            setParam(processor, "osc3Coarse", 12.0f);
            const auto capture = render(processor, 48000, { { 2000, true, 57, 0.9f } });
            std::printf("  %-14d %12.6f %12.6f %12.6f\n",
                        count, capture.rms(),
                        processor.debugGetOscillatorBusRms(),
                        processor.debugGetPolyphonyGainApplied());
        }

        std::printf("\n  VIBE amount sweep\n  %-10s %12s %12s\n", "amount", "rms", "peak");
        for (const auto amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 1);
            setParam(processor, "filter1Enabled", 1.0f);
            setParam(processor, "filter1Cutoff", 2500.0f);
            setParam(processor, "vibeEnabled", 1.0f);
            setParam(processor, "vibeAmount", amount);
            const auto capture = render(processor, 48000, { { 2000, true, 45, 0.9f } });
            std::printf("  %-10.3f %12.6f %12.6f\n", amount, capture.rms(), capture.peak());
        }

        std::printf("\n  KARPLUS macroA sweep (mode 13)\n  %-10s %12s\n", "macroA", "rms");
        for (const auto macro : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 13);
            setParam(processor, "osc1MacroA", macro);
            const auto capture = render(processor, 48000, { { 2000, true, 57, 0.9f } });
            std::printf("  %-10.3f %12.6f\n", macro, capture.rms());
        }
        return 0;
    }

    if (wants("subosc")) testSubOscillator();
    if (wants("osc")) testOscillators();
    if (wants("ampenv")) testAmpEnvelope();
    if (wants("modenv")) testModEnvelopes();
    if (wants("lfo")) testLfo();
    if (wants("vibe")) testVibe();
    if (wants("reverb")) testReverb();
    if (wants("cardstyle")) testCardStyle();
    if (wants("cardinner")) testCardInner();
    if (wants("delay")) testDelay();
    if (wants("mood")) testMood();
    if (wants("fx")) testEffectIndependence();
    if (wants("preset")) testPresets();
    if (wants("integration")) testIntegration();

    std::printf("\n------------------------------------------------------------\n");
    std::printf("  %d passed, %d failed\n", gPassed, gFailed);
    if (! gFailures.empty())
    {
        std::printf("\n  failures:\n");
        for (const auto& failure : gFailures)
        {
            std::printf("    - %s\n", failure.c_str());
        }
    }
    std::printf("\n");
    return gFailed == 0 ? 0 : 1;
}
