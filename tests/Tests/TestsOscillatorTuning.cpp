#include "TestSupport.h"
#include "../../products/PX3Synth/DSP/OscillatorTuning.h"

// testOscillatorTuning - one Coarse Tune (whole octaves) and one Fine Tune
// (cents) per oscillator, the main oscillators and the sub alike, combining with
// dynamic pitch modulation in the order OscillatorTuning.h documents.

namespace px3tests
{
namespace
{

// Frequency from interpolated rising zero crossings. Autocorrelation resolves a
// period only to a whole sample - about 8 cents at 440 Hz - which is far too
// coarse to check a one-cent step; crossings over a second resolve well under
// a tenth of a cent.
double zeroCrossingHz(const std::vector<float>& signal, int from, int to)
{
    auto first = -1.0, last = -1.0;
    auto crossings = 0;
    const auto end = juce::jmin(to, static_cast<int>(signal.size()));

    for (int i = juce::jmax(1, from); i < end; ++i)
    {
        const auto a = signal[static_cast<std::size_t>(i - 1)];
        const auto b = signal[static_cast<std::size_t>(i)];
        if (a < 0.0f && b >= 0.0f)
        {
            const auto t = static_cast<double>(i - 1) + static_cast<double>(-a) / static_cast<double>(b - a);
            if (first < 0.0) { first = t; }
            last = t;
            ++crossings;
        }
    }

    return crossings > 1 ? static_cast<double>(crossings - 1) * kSampleRate / (last - first) : 0.0;
}

double centsFrom(double hz, double referenceHz)
{
    return hz > 0.0 && referenceHz > 0.0 ? 1200.0 * std::log2(hz / referenceHz) : 1.0e9;
}

enum class Source { osc, sub };

// A4 held on one source alone - oscillator 1 or the sub, both a sine - and its
// pitch reported in cents from 440 Hz.
double measureCents(Source source,
                    float coarseOctaves,
                    float fineCents,
                    const std::function<void(PX3SynthAudioProcessor&)>& extra = {})
{
    PX3SynthAudioProcessor processor;
    makePlainPatch(processor);

    if (source == Source::sub)
    {
        setParam(processor, "osc1Enabled", 0.0f);
        setParam(processor, "subOscEnabled", 1.0f);
        setChoice(processor, "subOscWaveform", 0);
        setParam(processor, "subOscCoarse", coarseOctaves);
        setParam(processor, "subOscFine", fineCents);
    }
    else
    {
        setParam(processor, "osc1Coarse", coarseOctaves);
        setParam(processor, "osc1Fine", fineCents);
    }

    if (extra) { extra(processor); }

    const auto capture = render(processor, 72000, { { 0, true, 69, 0.9f } });
    return centsFrom(zeroCrossingHz(capture.left, 12000, 72000), 440.0);
}

const char* nameOf(Source source) { return source == Source::osc ? "Osc" : "Sub"; }

} // namespace

void testOscillatorTuning()
{
    suite("OSCILLATOR TUNING");

    // ---- exactly one coarse and one fine control per oscillator -----------------
    {
        PX3SynthAudioProcessor processor;
        juce::StringArray problems;

        const auto expect = [&](const juce::String& id, float start, float end, float defaultValue, const juce::String& name)
        {
            auto* parameter = findParameter(processor, id);
            if (parameter == nullptr) { problems.add(id + " missing"); return; }

            const auto& range = parameter->getNormalisableRange();
            if (range.start != start || range.end != end || range.interval != 1.0f)
            {
                problems.add(id + " spans " + fmt(range.start, 2) + ".." + fmt(range.end, 2)
                             + " in steps of " + fmt(range.interval, 2));
            }
            if (std::abs(parameter->convertFrom0to1(parameter->getDefaultValue()) - defaultValue) > 1.0e-4f)
            {
                problems.add(id + " defaults to " + fmt(parameter->convertFrom0to1(parameter->getDefaultValue()), 2));
            }
            if (parameter->getName(64) != name)
            {
                problems.add(id + " is named \"" + parameter->getName(64) + "\"");
            }
        };

        for (const auto* slot : { "1", "2", "3" })
        {
            expect(juce::String("osc") + slot + "Coarse", -2.0f, 2.0f, 0.0f, juce::String("Osc ") + slot + " Coarse Tune");
            expect(juce::String("osc") + slot + "Fine", -24.0f, 24.0f, 0.0f, juce::String("Osc ") + slot + " Fine Tune");
        }
        expect("subOscCoarse", -2.0f, 2.0f, -1.0f, "Sub Osc Coarse Tune");
        expect("subOscFine", -24.0f, 24.0f, 0.0f, "Sub Osc Fine Tune");

        for (const auto* retired : { "osc1Pitch", "osc2Pitch", "osc3Pitch", "subOscPitch", "subOscOctave" })
        {
            if (findParameter(processor, retired) != nullptr) { problems.add(juce::String(retired) + " still exists"); }
        }

        check("Tuning_OneCoarseAndOneFinePerOscillator", problems.isEmpty(),
              problems.isEmpty() ? juce::String("osc 1-3 and sub: coarse -2..+2 oct, fine -24..+24 ct, both stepped by 1; the old pitch and octave controls are gone")
                                 : problems.joinIntoString("; "));
    }

    // ---- the full ranges, on the main oscillator and the sub ----------------------
    for (const auto source : { Source::osc, Source::sub })
    {
        {
            juce::String detail;
            auto worst = 0.0;
            for (int octaves = -2; octaves <= 2; ++octaves)
            {
                const auto cents = measureCents(source, static_cast<float>(octaves), 0.0f);
                worst = juce::jmax(worst, std::abs(cents - 1200.0 * octaves));
                detail << octaves << " oct " << fmt(cents, 2) << " ct  ";
            }
            check((juce::String("Tuning_") + nameOf(source) + "_CoarseSpansTwoOctavesEachWay").toRawUTF8(),
                  worst < 1.0, detail + "(worst error " + fmt(worst, 3) + " ct)");
        }
        {
            juce::String detail;
            auto worst = 0.0;
            for (const auto fine : { -24, -1, 0, 1, 24 })
            {
                const auto cents = measureCents(source, 0.0f, static_cast<float>(fine));
                worst = juce::jmax(worst, std::abs(cents - fine));
                detail << fine << " ct -> " << fmt(cents, 2) << "  ";
            }
            check((juce::String("Tuning_") + nameOf(source) + "_FineMovesInCents").toRawUTF8(),
                  worst < 0.5, detail + "(worst error " + fmt(worst, 3) + " ct)");
        }
    }

    // ---- the same settings mean the same pitch on both --------------------------------
    {
        const std::array<std::pair<float, float>, 4> settings { {
            { -2.0f, 24.0f }, { -1.0f, -13.0f }, { 1.0f, 7.0f }, { 2.0f, -24.0f } } };
        juce::String detail;
        auto worst = 0.0;
        for (const auto& setting : settings)
        {
            const auto osc = measureCents(Source::osc, setting.first, setting.second);
            const auto sub = measureCents(Source::sub, setting.first, setting.second);
            worst = juce::jmax(worst, std::abs(osc - sub));
            detail << fmt(setting.first, 0) << " oct " << fmt(setting.second, 0) << " ct: osc "
                   << fmt(osc, 2) << ", sub " << fmt(sub, 2) << "  ";
        }
        check("Tuning_MainAndSubUseTheSameSemantics", worst < 0.5, detail + "(worst gap " + fmt(worst, 3) + " ct)");
    }

    // ---- static tuning and dynamic modulation combine predictably ---------------------
    {
        // +1 octave and +12 cents of static tuning, plus a full pitch bend over a
        // two-semitone range.
        const auto withBend = measureCents(Source::osc, 1.0f, 12.0f, [](PX3SynthAudioProcessor& processor)
        {
            setParam(processor, "pitchBendRange", 2.0f);
            processor.setPitchBendNormalizedFromUI(1.0f);
        });
        check("Tuning_StaticTuningAndPitchBendAdd", std::abs(withBend - 1412.0) < 1.0,
              "+1 oct +12 ct with +2 st of bend measures " + fmt(withBend, 2) + " ct (expected 1412)");
    }
    for (const auto source : { Source::osc, Source::sub })
    {
        // -1 octave of static tuning against +12 semitones of Pitch Mod from a
        // square LFO at 50%: the two cancel exactly.
        const auto cancelled = measureCents(source, -1.0f, 0.0f, [source](PX3SynthAudioProcessor& processor)
        {
            setParam(processor, "lfoEnabled", 1.0f);
            setParam(processor, "lfoFrequency", 0.01f);
            setParam(processor, "lfoAmount", 0.5f);
            setChoice(processor, "lfoWaveform", 3);
            processor.setLfoAssignmentByParameterId(0, source == Source::osc ? "osc1PitchMod" : "subOscPitchMod", false);
        });
        check((juce::String("Tuning_") + nameOf(source) + "_PitchModulationAddsToStaticTuning").toRawUTF8(),
              std::abs(cancelled) < 1.0,
              "-1 oct static with +12 st of Pitch Mod measures " + fmt(cancelled, 2) + " ct (expected 0)");
    }
    {
        // Pitch Mod's own value is not a tuning: only modulation moves pitch.
        const auto osc = measureCents(Source::osc, 0.0f, 0.0f, [](PX3SynthAudioProcessor& processor)
        {
            setParam(processor, "osc1PitchMod", 12.0f);
        });
        const auto sub = measureCents(Source::sub, 0.0f, 0.0f, [](PX3SynthAudioProcessor& processor)
        {
            setParam(processor, "subOscPitchMod", -12.0f);
        });
        check("Tuning_PitchModsOwnValueNeverOffsetsPitch", std::abs(osc) < 1.0 && std::abs(sub) < 1.0,
              "Pitch Mod parameter set to +12 / -12 st with no modulation: osc " + fmt(osc, 2)
                  + " ct, sub " + fmt(sub, 2) + " ct");
    }
    {
        // Coarse stays a stepped control however it is driven.
        const auto ok = px3::tuning::totalSemitones(1.4f, 0.0f, 0.0f) == 12.0
                     && px3::tuning::totalSemitones(-1.6f, 0.0f, 0.0f) == -24.0
                     && px3::tuning::totalSemitones(0.49f, 24.0f, 0.0f) == 0.24
                     && px3::tuning::totalSemitones(9.0f, 99.0f, 99.0f) == 24.0 + 0.24 + 24.0;
        check("Tuning_ModulatedCoarseLandsOnWholeOctaves", ok,
              "1.4 oct -> " + fmt(px3::tuning::totalSemitones(1.4f, 0.0f, 0.0f), 2) + " st, -1.6 oct -> "
                  + fmt(px3::tuning::totalSemitones(-1.6f, 0.0f, 0.0f), 2) + " st, and every term clamps to its range");
    }

    // ---- saved and restored --------------------------------------------------------------
    {
        PX3SynthAudioProcessor source;
        const std::array<std::pair<const char*, float>, 8> values { {
            { "osc1Coarse", -2.0f }, { "osc1Fine", 17.0f }, { "osc2Coarse", 1.0f }, { "osc2Fine", -9.0f },
            { "osc3Coarse", 2.0f }, { "osc3Fine", 24.0f }, { "subOscCoarse", -2.0f }, { "subOscFine", -24.0f } } };
        for (const auto& value : values) { setParam(source, value.first, value.second); }

        juce::MemoryBlock block;
        source.getStateInformation(block);
        PX3SynthAudioProcessor target;
        target.setStateInformation(block.getData(), static_cast<int>(block.getSize()));

        juce::StringArray wrong;
        for (const auto& value : values)
        {
            const auto restored = getParamValue(target, value.first);
            if (std::abs(restored - value.second) > 1.0e-3f)
            {
                wrong.add(juce::String(value.first) + " " + fmt(restored, 2) + " (saved " + fmt(value.second, 2) + ")");
            }
        }
        check("Tuning_ValuesSurviveAStateRoundTrip", wrong.isEmpty(),
              wrong.isEmpty() ? juce::String("coarse and fine for osc 1-3 and the sub restored exactly")
                              : wrong.joinIntoString(", "));
    }
}

} // namespace px3tests
