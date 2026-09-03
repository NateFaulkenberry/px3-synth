#include "TestSupport.h"

// testAmpEnvelope, testModEnvelopes, testLfo, testVibe, testReverb, testComb

namespace px3tests
{

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
            return mean > 1.0e-9
                       ? std::sqrt(variance / static_cast<double>(windows.size())) / mean
                       : 0.0;
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


    // Hiss has to scale with the amount rather than sit on a fixed floor. It
    // used to be "0.0035 + 0.0165 * amount", and the constant term dominated
    // everything below about three quarters of the range: measured in an
    // 8-16 kHz band on a sine source, hiss jumped from -161 dBFS with vibe off
    // to -75.4 dBFS at amount 0.05 - an 86 dB step into a level within 14 dB of
    // FULL amount. The floor also swamped the type profiles, so Clean (noise
    // 0.03) and LoFi (noise 0.84) measured identically.
    {
        auto hissAt = [](float amount, int typeIndex)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setChoice(processor, "osc1Mode", 0);          // SINE: no HF of its own
            setParam(processor, "vibeEnabled", 1.0f);
            setParam(processor, "vibeAmount", amount);
            setChoice(processor, "vibeType", typeIndex);
            const auto capture = render(processor, 96000, { { 2000, true, 45, 0.9f } });
            return bandRmsDb(capture.left, 8000.0, 16000.0, 40000);
        };

        const auto quiet = hissAt(0.05f, 0);
        const auto loud = hissAt(1.0f, 0);
        check("Vibe_HissScalesWithAmountRatherThanSittingOnAFloor",
              loud - quiet > 40.0,
              "amount 0.05 -> " + fmt(quiet, 1) + " dBFS, amount 1.0 -> " + fmt(loud, 1)
                  + " dBFS, range " + fmt(loud - quiet, 1) + " dB");

        const auto clean = hissAt(1.0f, 4);               // Clean, noise 0.03
        const auto lofi = hissAt(1.0f, 5);                // LoFi,  noise 0.84
        check("Vibe_TypeProfilesHaveDistinctNoiseFloors",
              lofi - clean > 15.0,
              "Clean " + fmt(clean, 1) + " dBFS vs LoFi " + fmt(lofi, 1) + " dBFS");
    }

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

// Fraction of samples beyond one standard deviation, normalised by the Gaussian
// expectation so that 1.0 means "as diffuse as noise".


// Max deviation (dB) of the smoothed decay envelope from its best-fit straight
// line, over the region where the tail is still well above the noise floor.


// Reverberation time from the Schroeder backward integration curve, measured
// over the -5 dB to -35 dB span and extrapolated to 60 dB (T30).

// ISO 3382 style: deviation of the energy decay curve from a straight line
// over the -5 dB to -35 dB span, in dB.

// Renders a stereo impulse response straight from the Reverb class, fully wet,
// so the measurements describe the algorithm rather than the dry/wet mix.
// ---------------------------------------------------------------------------
// Mood characterisation. Driven on the Mood class directly. Every measurement
// here is about stereo behaviour, because that is what the component is
// supposed to have and mostly does not.
// ---------------------------------------------------------------------------

// Feeds Mood a signal and reports what came out. `panLeft` sends the test
// signal only to the left channel, which is how channel separation is measured.

// ---------------------------------------------------------------------------
// Delay characterisation. Driven on the Delay class directly rather than
// through the plugin, so an impulse in gives an impulse response out and the
// echo times can be read straight off it.
// ---------------------------------------------------------------------------

// Runs an impulse through one delay algorithm and reads the echo pattern back.

// Drives a delay algorithm with audio while a control is swept, stops the
// input, and reports how much is left at three points afterwards. Static-
// parameter tests cannot see this class of fault: the delay only misbehaves
// while something is moving, and what it leaves behind is a tail that either
// decays far too slowly or does not decay at all.

// sweepWhich: 0 = nothing moves, 1 = TIME, 2 = FEEDBACK, 3 = AMOUNT, 4 = SYNC

// Feeds a delay, bypasses it, waits, then re-enables it with silence going in.
// Anything that comes out is a tail the effect kept across the bypass.

// Passes a signal through the delay at amount 0 and reports the worst sample
// difference from the input. A delay at zero amount must be transparent.



//==============================================================================
// PHASE 9 - REVERB
//==============================================================================
// Drives an effect, bypasses it, waits, then re-enables it with silence going
// in. Whatever comes out is a tail the effect held on to across the bypass -
// which on the next note arrives underneath something it has nothing to do
// with. `configure` sets the enabled flag on whatever settings type is in play.

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

// ---------------------------------------------------------------------------
// Comb resonator
// ---------------------------------------------------------------------------
//
// The comb is tested through px3::CombResonator directly rather than through a
// voice. Its contract is about pitch, decay time and stability, and measuring
// those through four sources, two filter slots and an amp envelope would mean
// measuring the envelope as much as the resonator.

// Rings the resonator with a short burst and returns the tail, which is what
// every measurement below is made on. A burst rather than a single impulse: one
// sample excites the loop so weakly at short delays that the tail is hard to
// measure without also measuring the noise floor.
std::vector<float> ringComb(const px3::CombSettings& settings,
                            int tailSamples,
                            double sampleRate = kSampleRate,
                            int burstSamples = 64)
{
    px3::CombResonator comb;
    comb.prepare(sampleRate);
    comb.setCurrentSettingsImmediate(settings);

    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(tailSamples));

    juce::Random random { 20260828 };
    for (int i = 0; i < tailSamples; ++i)
    {
        const auto excite = i < burstSamples ? (random.nextFloat() * 2.0f - 1.0f) * 0.7f : 0.0f;
        out.push_back(comb.processSample(excite));
    }
    return out;
}


// Energy above roughly a quarter of Nyquist, via a one-pole highpass. Used to
// show that damping removes highs faster than it removes the fundamental.
double highFrequencyRms(const std::vector<float>& signal, int from, int count)
{
    const auto first = juce::jlimit(0, static_cast<int>(signal.size()), from);
    const auto last = juce::jlimit(first, static_cast<int>(signal.size()), first + count);
    if (last <= first + 1) return 0.0;

    double sum = 0.0;
    float prev = signal[static_cast<std::size_t>(first)];
    for (int i = first + 1; i < last; ++i)
    {
        const auto x = signal[static_cast<std::size_t>(i)];
        const auto hp = static_cast<double>(x - prev);
        prev = x;
        sum += hp * hp;
    }
    return std::sqrt(sum / static_cast<double>(last - first - 1));
}

// Largest single-sample step relative to the signal's level around it.
//
// A raw step size cannot identify a click, because a loud passage legitimately
// steps further than a quiet one - comparing the two just finds whichever part
// of the tail is loudest. Dividing by the local RMS asks the question that
// actually matters: is this sample far out of line with its neighbours?
double worstLocalStepRatio(const std::vector<float>& signal, int from)
{
    constexpr int window = 256;
    const auto first = juce::jmax(from, window);
    double worst = 0.0;

    for (int i = first; i < static_cast<int>(signal.size()); ++i)
    {
        double sum = 0.0;
        for (int k = i - window; k < i; ++k)
        {
            sum += static_cast<double>(signal[static_cast<std::size_t>(k)])
                 * static_cast<double>(signal[static_cast<std::size_t>(k)]);
        }
        const auto localRms = std::sqrt(sum / static_cast<double>(window));
        if (localRms < 1.0e-5)
        {
            continue;   // silence: any step here is numerically meaningless
        }

        const auto step = std::abs(static_cast<double>(signal[static_cast<std::size_t>(i)])
                                   - static_cast<double>(signal[static_cast<std::size_t>(i - 1)]));
        worst = juce::jmax(worst, step / localRms);
    }
    return worst;
}


void testComb()
{
    suite("COMB");

    // ---- tuning ------------------------------------------------------------
    {
        // The resonance has to land on the requested pitch, and stay there
        // across the range. A comb whose delay is rounded to whole samples
        // drifts sharp as the pitch rises, because one sample is a larger
        // fraction of a shorter period - which is the whole reason for
        // interpolating the delay.
        struct Case { float tune; double tolerancePercent; };
        const std::array<Case, 5> cases { {
            { 55.0f, 2.0 }, { 110.0f, 2.0 }, { 220.0f, 2.0 }, { 440.0f, 2.0 }, { 880.0f, 3.0 },
        } };

        juce::StringArray problems;
        for (const auto& c : cases)
        {
            px3::CombSettings settings;
            settings.tuneHz = c.tune;
            settings.decaySeconds = 2.0f;
            settings.damping = 0.05f;
            settings.mix = 1.0f;

            const auto tail = ringComb(settings, 48000);
            const auto measured = estimateFrequency(tail, 4000, 32000,
                                                    c.tune * 0.5, c.tune * 2.0);
            const auto errorPercent = std::abs(measured - c.tune) / c.tune * 100.0;
            if (errorPercent > c.tolerancePercent)
            {
                problems.add(juce::String(c.tune, 0) + "Hz -> " + fmt((float) measured, 1)
                             + "Hz (" + fmt((float) errorPercent, 2) + "% off)");
            }
        }

        check("Comb_TracksRequestedPitch",
              problems.isEmpty(),
              problems.isEmpty() ? "55-880 Hz all within tolerance"
                                 : problems.joinIntoString("; "));
    }

    // ---- decay -------------------------------------------------------------
    {
        // Decay is a time, not a feedback number, so a longer setting must ring
        // longer - and the same setting must mean roughly the same duration at
        // different pitches. That second property is what a raw feedback
        // control cannot provide: the loop runs eight times more often at
        // 880 Hz than at 110 Hz.
        const auto measureDecay = [](float tune, float decay)
        {
            px3::CombSettings settings;
            settings.tuneHz = tune;
            settings.decaySeconds = decay;
            settings.damping = 0.0f;
            settings.mix = 1.0f;

            const auto tail = ringComb(settings, static_cast<int>(kSampleRate * 4.0));
            const auto reference = rmsOver(tail, 2000, 4000);
            if (reference <= 1.0e-6) return -1.0;

            // Where the tail falls 60 dB below the level just after the burst.
            for (int i = 2000; i + 2000 < static_cast<int>(tail.size()); i += 500)
            {
                if (rmsOver(tail, i, 2000) < reference * 0.001)
                {
                    return static_cast<double>(i) / kSampleRate;
                }
            }
            return static_cast<double>(tail.size()) / kSampleRate;
        };

        const auto shortDecay = measureDecay(220.0f, 0.25f);
        const auto longDecay = measureDecay(220.0f, 2.0f);
        const auto lowPitch = measureDecay(110.0f, 1.0f);
        const auto highPitch = measureDecay(880.0f, 1.0f);

        const auto ordered = longDecay > shortDecay * 2.0;
        // Within a factor of two across three octaves. Not exact: the damping
        // compensation and the loop-gain ceiling both bite differently at the
        // extremes.
        const auto pitchIndependent = lowPitch > 0.0 && highPitch > 0.0
                                      && juce::jmax(lowPitch, highPitch)
                                             < juce::jmin(lowPitch, highPitch) * 2.0;

        check("Comb_DecayIsATimeNotAFeedbackNumber",
              ordered && pitchIndependent,
              "0.25s -> " + fmt((float) shortDecay, 2) + "s, 2.0s -> " + fmt((float) longDecay, 2)
                  + "s; at 1.0s: 110Hz " + fmt((float) lowPitch, 2)
                  + "s vs 880Hz " + fmt((float) highPitch, 2) + "s");
    }

    // ---- damping -----------------------------------------------------------
    {
        // Damping belongs in the feedback loop, so highs die faster than the
        // fundamental. Placed after the output tap it would only equalise, and
        // the ratio measured here would not move.
        px3::CombSettings bright;
        bright.tuneHz = 220.0f;
        bright.decaySeconds = 1.5f;
        bright.damping = 0.0f;

        auto damped = bright;
        damped.damping = 0.85f;

        const auto brightTail = ringComb(bright, 40000);
        const auto dampedTail = ringComb(damped, 40000);

        // High-frequency energy as a share of total, late in the tail. A share
        // rather than an absolute, so this measures spectral tilt rather than
        // the fact that a damped tail is also quieter.
        const auto brightShare = highFrequencyRms(brightTail, 20000, 12000)
                                 / juce::jmax(1.0e-9, rmsOver(brightTail, 20000, 12000));
        const auto dampedShare = highFrequencyRms(dampedTail, 20000, 12000)
                                 / juce::jmax(1.0e-9, rmsOver(dampedTail, 20000, 12000));

        check("Comb_DampingDarkensTheTailNotJustQuietensIt",
              dampedShare < brightShare * 0.8,
              "HF share: bright " + fmt((float) brightShare, 3)
                  + " vs damped " + fmt((float) dampedShare, 3));
    }

    // ---- polarity ----------------------------------------------------------
    {
        // Negative feedback puts the resonance on odd harmonics of half the
        // tuning, so the same delay length rings an octave down. That is a
        // different timbre, not a sign detail, which is why it is exposed.
        px3::CombSettings positive;
        positive.tuneHz = 300.0f;
        positive.decaySeconds = 2.0f;
        positive.damping = 0.02f;

        auto negative = positive;
        negative.invertPolarity = true;

        const auto positiveHz = estimateFrequency(ringComb(positive, 48000), 4000, 32000, 60.0, 900.0);
        const auto negativeHz = estimateFrequency(ringComb(negative, 48000), 4000, 32000, 60.0, 900.0);

        check("Comb_InvertedPolarityResonatesAnOctaveLower",
              positiveHz > 0.0 && negativeHz > 0.0
                  && std::abs(negativeHz - positiveHz * 0.5) < positiveHz * 0.12,
              "positive " + fmt((float) positiveHz, 1) + "Hz, inverted "
                  + fmt((float) negativeHz, 1) + "Hz");
    }

    // ---- mix ---------------------------------------------------------------
    {
        px3::CombSettings settings;
        settings.tuneHz = 200.0f;
        settings.decaySeconds = 1.0f;
        settings.mix = 0.0f;

        px3::CombResonator comb;
        comb.prepare(kSampleRate);
        comb.setCurrentSettingsImmediate(settings);

        auto matchesDry = true;
        juce::Random random { 7 };
        for (int i = 0; i < 4000; ++i)
        {
            const auto in = random.nextFloat() * 2.0f - 1.0f;
            if (std::abs(comb.processSample(in) - in) > 1.0e-4f)
            {
                matchesDry = false;
                break;
            }
        }

        check("Comb_ZeroMixIsExactlyDry",
              matchesDry,
              "mix 0 returns the input unchanged");
    }

    // ---- stability ---------------------------------------------------------
    {
        // Every extreme of every control, including the combinations that would
        // run away in a naive design: maximum decay with maximum drive is a
        // loop gain at its ceiling being pushed into the saturator.
        juce::StringArray problems;

        const std::array<float, 4> tunes { { px3::CombResonator::kMinTuneHz, 100.0f, 1000.0f,
                                             px3::CombResonator::kMaxTuneHz } };
        const std::array<float, 2> decays { { px3::CombResonator::kMinDecaySeconds,
                                              px3::CombResonator::kMaxDecaySeconds } };
        const std::array<float, 2> dampings { { 0.0f, 1.0f } };
        const std::array<float, 2> dispersions { { 0.0f, 1.0f } };
        const std::array<float, 2> drives { { 0.0f, 1.0f } };
        const std::array<bool, 2> polarities { { false, true } };

        for (const auto tune : tunes)
        for (const auto decay : decays)
        for (const auto damping : dampings)
        for (const auto dispersion : dispersions)
        for (const auto drive : drives)
        for (const auto invert : polarities)
        {
            px3::CombSettings settings;
            settings.tuneHz = tune;
            settings.decaySeconds = decay;
            settings.damping = damping;
            settings.dispersion = dispersion;
            settings.drive = drive;
            settings.invertPolarity = invert;

            px3::CombResonator comb;
            comb.prepare(kSampleRate);
            comb.setCurrentSettingsImmediate(settings);

            float peak = 0.0f;
            juce::Random random { 99 };
            for (int i = 0; i < 24000; ++i)
            {
                // Hot input: full scale, which is what a self-oscillating loop
                // would be pushed by in the worst case.
                const auto in = i < 2000 ? (random.nextFloat() * 2.0f - 1.0f) : 0.0f;
                const auto out = comb.processSample(in);
                if (! std::isfinite(out))
                {
                    problems.add("non-finite at tune " + juce::String(tune, 0));
                    break;
                }
                peak = juce::jmax(peak, std::abs(out));
            }

            if (peak > 12.0f)
            {
                problems.add("runaway to " + fmt(peak, 1) + " at tune " + juce::String(tune, 0)
                             + " decay " + fmt(decay, 2) + " drive " + fmt(drive, 1));
            }
        }

        check("Comb_StableAcrossEveryParameterExtreme",
              problems.isEmpty(),
              problems.isEmpty() ? juce::String(tunes.size() * decays.size() * dampings.size()
                                                * dispersions.size() * drives.size() * polarities.size())
                                       + " combinations, all finite and bounded"
                                 : problems.joinIntoString("; "));
    }

    // ---- modulation --------------------------------------------------------
    {
        // Tune is a modulation destination, so sweeping it must not step. The
        // delay length is smoothed per sample precisely so that the read
        // pointer cannot jump to an unrelated part of the line, which is a
        // click rather than a pitch change.
        px3::CombResonator comb;
        comb.prepare(kSampleRate);

        // The sweep's own value at i = 0, so the resonator starts where the
        // modulation starts. Initialising it elsewhere would make the first
        // instants a jump to the sweep rather than the sweep itself, and that
        // transient is a property of the test, not of modulating Tune.
        const auto sweptTuneAt = [](int i)
        {
            return 200.0f + 1800.0f * (0.5f + 0.5f * std::sin(static_cast<float>(i) * 0.00026f));
        };

        px3::CombSettings settings;
        settings.tuneHz = sweptTuneAt(0);
        settings.decaySeconds = 1.0f;
        settings.mix = 1.0f;
        comb.setCurrentSettingsImmediate(settings);

        std::vector<float> out;
        juce::Random random { 31 };
        float previous = 0.0f;
        float largestStep = 0.0f;
        int largestStepIndex = -1;

        for (int i = 0; i < 40000; ++i)
        {
            // A full-range sweep every half second, far faster than an LFO
            // would move it.
            settings.tuneHz = sweptTuneAt(i);
            comb.setTargetSettings(settings);

            const auto in = i < 1000 ? (random.nextFloat() * 2.0f - 1.0f) * 0.5f : 0.0f;
            const auto value = comb.processSample(in);
            out.push_back(value);

            if (i > 1200)
            {
                const auto step = std::abs(value - previous);
                if (step > largestStep)
                {
                    largestStep = step;
                    largestStepIndex = i;
                }
            }
            previous = value;
        }

        // Measured against a static run at the top of the sweep, in units of
        // local level. Modulating the delay resamples the line - shortening it
        // speeds playback up, exactly as a tape does - so the swept signal is
        // legitimately steeper. What it must not be is DISCONTINUOUS, and a
        // step far out of line with its own neighbourhood is what that means.
        px3::CombResonator staticComb;
        staticComb.prepare(kSampleRate);
        px3::CombSettings top = settings;
        top.tuneHz = 2000.0f;
        staticComb.setCurrentSettingsImmediate(top);

        std::vector<float> staticOut;
        staticOut.reserve(out.size());
        juce::Random staticRandom { 31 };
        for (int i = 0; i < 40000; ++i)
        {
            const auto in = i < 1000 ? (staticRandom.nextFloat() * 2.0f - 1.0f) * 0.5f : 0.0f;
            staticOut.push_back(staticComb.processSample(in));
        }

        const auto sweptRatio = worstLocalStepRatio(out, 1200);
        const auto staticRatio = worstLocalStepRatio(staticOut, 1200);

        check("Comb_TuneSweepsWithoutDiscontinuities",
              allFinite(out) && sweptRatio < staticRatio * 2.0,
              "worst step / local level: swept " + fmt((float) sweptRatio, 2)
                  + " vs static " + fmt((float) staticRatio, 2)
                  + " (largest raw step " + fmt(largestStep, 4) + " at sample "
                  + juce::String(largestStepIndex) + ")");
    }

    // ---- sample rate -------------------------------------------------------
    {
        // Tuning is derived from the sample rate, so the same setting has to
        // produce the same pitch at any rate.
        px3::CombSettings settings;
        settings.tuneHz = 330.0f;
        settings.decaySeconds = 1.5f;
        settings.damping = 0.05f;

        // estimateFrequency works in kSampleRate, so a tail rendered at another
        // rate reads scaled by the ratio between them. Both measurements are
        // corrected - missing this on the 44.1k one made a correctly tuned
        // resonator look 8% sharp.
        const auto measureAt = [&settings](double rate, int tailSamples, int from, int window,
                                           double minHz, double maxHz)
        {
            const auto tail = ringComb(settings, tailSamples, rate);
            const auto raw = estimateFrequency(tail, from, window, minHz, maxHz);
            return raw * (rate / kSampleRate);
        };

        const auto at44 = measureAt(44100.0, 40000, 4000, 24000, 140.0, 640.0);
        const auto at96 = measureAt(96000.0, 80000, 8000, 48000, 60.0, 400.0);

        check("Comb_TuningSurvivesSampleRateChanges",
              std::abs(at44 - 330.0) < 12.0 && std::abs(at96 - 330.0) < 15.0,
              "44.1k -> " + fmt((float) at44, 1) + "Hz, 96k -> " + fmt((float) at96, 1) + "Hz");
    }

    // ---- reaching the instrument -------------------------------------------
    {
        // Everything above tests the resonator in isolation. This checks the
        // whole path: selecting Comb on a filter, playing a note, and hearing
        // the comb's tuning in the output rather than the note's own pitch.
        // A resonator that works perfectly but is never reached is not a
        // feature.
        const auto renderWithMode = [](int modeIndex, float combTune)
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
            setParam(processor, "ampSustain", 1.0f);
            // Noise in, so the comb's resonance is what shapes the output
            // rather than the oscillator's own harmonics.
            setChoice(processor, "osc1Mode", 4);

            setParam(processor, "filter1Enabled", 1.0f);
            setParam(processor, "filter2Enabled", 0.0f);
            setChoice(processor, "filter1Type", modeIndex);
            setParam(processor, "filter1CombTune", combTune);
            setParam(processor, "filter1CombDecay", 1.2f);
            setParam(processor, "filter1CombDamping", 0.15f);
            setParam(processor, "filter1CombDispersion", 0.0f);
            setParam(processor, "filter1CombDrive", 0.0f);
            setParam(processor, "filter1CombMix", 1.0f);

            return render(processor, 64000, { { 1000, true, 45, 0.9f } });
        };

        const auto combbed = renderWithMode(static_cast<int>(px3::FilterMode::comb), 330.0f);
        const auto measured = estimateFrequency(combbed.left, 20000, 40000, 150.0, 700.0);

        check("Comb_ReachesTheInstrumentThroughFilterMode",
              std::abs(measured - 330.0) < 33.0,
              "noise through a comb tuned to 330Hz resonates at " + fmt((float) measured, 1) + "Hz");

        // And selecting a different mode must not leave the comb in circuit.
        const auto lowpassed = renderWithMode(0, 330.0f);
        const auto lowpassHz = estimateFrequency(lowpassed.left, 20000, 40000, 150.0, 700.0);

        check("Comb_IsOnlyInCircuitInCombMode",
              std::abs(lowpassHz - 330.0) > 20.0 || lowpassed.rmsOver(20000, 40000) < 1.0e-4f,
              "the same patch in LP12 resonates at " + fmt((float) lowpassHz, 1)
                  + "Hz rather than the comb's 330Hz");
    }

    // ---- voice independence ------------------------------------------------
    {
        // The synth holds one resonator per source per filter slot per voice.
        // They must not share state: a resonator still ringing from one note
        // must not colour another.
        px3::CombSettings settings;
        settings.tuneHz = 220.0f;
        settings.decaySeconds = 2.0f;

        px3::CombResonator a;
        px3::CombResonator b;
        a.prepare(kSampleRate);
        b.prepare(kSampleRate);
        a.setCurrentSettingsImmediate(settings);
        b.setCurrentSettingsImmediate(settings);

        // Ring A only.
        juce::Random random { 5 };
        for (int i = 0; i < 8000; ++i)
        {
            a.processSample(i < 500 ? (random.nextFloat() * 2.0f - 1.0f) : 0.0f);
        }

        // B has seen nothing, so it must still be silent.
        double bEnergy = 0.0;
        for (int i = 0; i < 4000; ++i)
        {
            const auto out = b.processSample(0.0f);
            bEnergy += std::abs(static_cast<double>(out));
        }

        // And a reset must clear A's tail completely.
        a.reset();
        double aAfterReset = 0.0;
        for (int i = 0; i < 4000; ++i)
        {
            aAfterReset += std::abs(static_cast<double>(a.processSample(0.0f)));
        }

        check("Comb_ResonatorsAreIndependentAndResetClears",
              bEnergy < 1.0e-6 && aAfterReset < 1.0e-6,
              "untouched resonator energy " + fmt((float) bEnergy, 6)
                  + ", after reset " + fmt((float) aAfterReset, 6));
    }
}

} // namespace px3tests
