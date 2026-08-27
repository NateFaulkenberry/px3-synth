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
#include "../DSP/AmpEnvelope.h"
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
        setParam(processor, juce::String("osc") + slot + "Level", 1.0f);
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
        setParam(processor, "subOscLevel", 1.0f);
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

    // Contract pin: oscNLevel / subOscLevel are host-visible parameters that are
    // deliberately NOT in the DSP level path - currentOscillatorLayerSettings
    // and currentSubOscillatorSettings both hardcode level to 1.0, and the
    // mixer channel is the single gain stage. This test exists so that anyone
    // who "fixes" the orphan by wiring it back in has to confront the fact that
    // doing so introduces a second gain stage.
    {
        PX3SynthAudioProcessor quiet, loud;
        makePlainPatch(quiet);
        makePlainPatch(loud);
        setParam(quiet, "osc1Level", 0.1f);
        setParam(loud, "osc1Level", 1.0f);
        const auto quietRms = render(quiet, 32000, { { 2000, true, 57, 0.9f } }).rms();
        const auto loudRms = render(loud, 32000, { { 2000, true, 57, 0.9f } }).rms();
        check("Osc1Level_IsIntentionallyInertBecauseMixerIsTheGainStage",
              nearly(quietRms, loudRms, loudRms * 0.05),
              "osc1Level 0.1 -> " + fmt(quietRms, 6) + ", 1.0 -> " + fmt(loudRms, 6));
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
    const auto quarter = renderVibe(true, 0.25f).rms();
    const auto half = midAmount.rms();
    const auto threeQuarter = renderVibe(true, 0.75f).rms();
    const auto full = fullAmount.rms();
    check("Vibe_AmountIncreasesEffectThroughUsableRange",
          half > quarter * 1.1 && threeQuarter > half * 1.1,
          "rms 0.25 -> " + fmt(quarter, 5) + ", 0.5 -> " + fmt(half, 5)
              + ", 0.75 -> " + fmt(threeQuarter, 5));
    check("Vibe_AmountSaturatesRatherThanCollapsingAtMaximum",
          full > half * 1.1,
          "rms 0.5 -> " + fmt(half, 5) + ", 0.75 -> " + fmt(threeQuarter, 5)
              + ", 1.0 -> " + fmt(full, 5));

    // Maximum amount must not run away or clip: this is a saturating, noise
    // adding stage, so it is exactly where runaway gain would show.
    check("Vibe_MaximumAmountDoesNotRunAwayOrClip",
          fullAmount.isFinite() && fullAmount.peak() <= 1.0001,
          "peak " + fmt(fullAmount.peak(), 5));
    check("Vibe_MaximumAmountKeepsLevelComparableToClean",
          fullAmount.rms() > off.rms() * 0.4 && fullAmount.rms() < off.rms() * 2.5,
          "clean " + fmt(off.rms(), 5) + ", full vibe " + fmt(fullAmount.rms(), 5));

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
// PHASE 9 - REVERB
//==============================================================================
void testReverb()
{
    suite("REVERB");

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
            { "Decay",          1, [](ReverbSettings& s) { s.decay = 0.0f; },          [](ReverbSettings& s) { s.decay = 1.0f; } },
            { "ModDepth",       1, [](ReverbSettings& s) { s.modDepth = 0.0f; },       [](ReverbSettings& s) { s.modDepth = 1.0f; } },
            { "DecayOnHall",    2, [](ReverbSettings& s) { s.decay = 0.0f; },          [](ReverbSettings& s) { s.decay = 1.0f; } },
            { "CloudFeedback",  3, [](ReverbSettings& s) { s.cloudFeedback = 0.0f; },  [](ReverbSettings& s) { s.cloudFeedback = 1.0f; } },
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

    // Contract pin: with the default algorithm, four of the reverb's controls
    // are inert because juce::Reverb does not expose them. This is recorded so
    // the behaviour is a known property rather than a surprise.
    {
        auto signature = [&runReverb, base](const std::function<void(ReverbSettings&)>& tweak)
        {
            auto settings = base;
            settings.algorithmIndex = 0;
            tweak(settings);
            return runReverb(settings, 480, 48000).rmsOver(2400, 36000);
        };
        const auto decayLow = signature([](ReverbSettings& s) { s.decay = 0.0f; });
        const auto decayHigh = signature([](ReverbSettings& s) { s.decay = 1.0f; });
        check("Reverb_Algorithm0_DecayAndCloudControlsAreInertByDesign",
              nearly(decayLow, decayHigh, 1.0e-9),
              "algorithm 0 wraps juce::Reverb, which has no decay control");
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
void testMood()
{
    suite("MOOD");

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

    // Bypass. The bypass that works is `enabled`: MoodSettings also carries a
    // trueBypass flag, but Mood never reads it and the processor hardcodes it
    // to false, so it is an orphaned control rather than a second bypass. This
    // pins that, so a future reader is not misled into relying on it.
    {
        auto withFlag = base;
        withFlag.trueBypass = true;
        auto withoutFlag = base;
        withoutFlag.trueBypass = false;
        const auto flagged = runMood(withFlag, 4800, 24000);
        const auto plain = runMood(withoutFlag, 4800, 24000);
        bool identical = flagged.left.size() == plain.left.size();
        for (std::size_t i = 0; identical && i < flagged.left.size(); ++i)
        {
            if (flagged.left[i] != plain.left[i]) identical = false;
        }
        check("Mood_TrueBypassFlagIsNotImplementedAndHasNoEffect", identical,
              "output identical with trueBypass true and false; `enabled` is the bypass");
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

        // Listening to the FX return alone, with the source panned hard left.
        // The send is centred by design, so the return must NOT lean left.
        const auto wetWithLeftSource = balance(renderPanned(-1.0f, 0.0f, true, true));
        check("FxSend_IsNotSteeredBySourceDryPan", std::abs(wetWithLeftSource) < 0.15,
              "FX-return-only balance with source panned hard left " + fmt(wetWithLeftSource, 4));

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

    if (filter == "probe")
    {
        // Diagnostic probe, not an assertion: prints the level at each stage so
        // an unexpected measurement can be attributed to a stage rather than
        // guessed at.
        std::printf("\n  %-10s %12s %12s %12s %12s\n",
                    "osc1Level", "masterRms", "oscBusRms", "polyGain", "srcRms");
        for (const auto level : { 0.125f, 0.25f, 0.5f, 1.0f })
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "osc1Level", level);
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
