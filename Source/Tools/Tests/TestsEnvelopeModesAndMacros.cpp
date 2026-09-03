#include "TestSupport.h"

// testEnvelopeModes, testMacroSystem, testMidiMapping

namespace px3tests
{


// ============================================================================
// FACTORY PRESETS
// ============================================================================

// Envelope modes. See docs/envelope-editor-design.md.
void testEnvelopeModes()
{
    suite("ENVELOPE MODES");

    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256;

    const auto adsrSettings = []
    {
        EnvelopeSettings settings;
        settings.attackSeconds = 0.200f;
        settings.decaySeconds = 0.300f;
        settings.sustainLevel = 0.60f;
        settings.releaseSeconds = 0.400f;
        return settings;
    }();

    // ---- ADSR mode is a topology constraint ---------------------------------
    {
        auto envelope = px3::BreakpointEnvelope::fromAdsr(adsrSettings);

        check("EnvMode_ADSRIsTheDefault",
              envelope.getMode() == px3::BreakpointEnvelope::Mode::adsr
                  && ! envelope.isBreakpointMode() && envelope.getPointCount() == 4,
              "a new envelope has " + juce::String(envelope.getPointCount())
                  + " points and is in "
                  + juce::String(envelope.isBreakpointMode() ? "Breakpoint" : "ADSR") + " mode");

        const auto refused = envelope.addPoint(0.25, 0.5);
        check("EnvMode_ADSRRefusesExtraPoints",
              refused < 0 && envelope.getPointCount() == 4,
              "adding a point returned " + juce::String(refused) + " and left "
                  + juce::String(envelope.getPointCount()) + " points");

        juce::StringArray removable;
        for (int i = 0; i < envelope.getPointCount(); ++i)
        {
            if (envelope.canRemovePoint(i)) { removable.add(juce::String(i)); }
        }
        check("EnvMode_ADSRRefusesToRemoveAnyPoint",
              removable.isEmpty(),
              removable.isEmpty() ? "none of the four stages can be removed"
                                  : "these could be removed: " + removable.joinIntoString(", "));

        // Curves are fully available - the topology is fixed, the shape is not.
        envelope.setCurve(0, 0.7);
        envelope.setCurve(1, -0.4);
        envelope.setCurve(2, 0.55);
        check("EnvMode_ADSRKeepsItsCurves",
              std::abs(envelope.getPoint(0).curveToNext - 0.7) < 1.0e-9
                  && std::abs(envelope.getPoint(1).curveToNext + 0.4) < 1.0e-9
                  && std::abs(envelope.getPoint(2).curveToNext - 0.55) < 1.0e-9
                  && envelope.getPointCount() == 4,
              "all three segments bend and the shape is still four points");

        // The peak is pinned in ADSR mode: ATTACK is a duration.
        envelope.setPoint(1, envelope.getPoint(1).timeSeconds, 0.3);
        check("EnvMode_ADSRPinsThePeakToTheTop",
              envelope.getPoint(1).value >= 1.0 - 1.0e-9,
              "the peak reads " + fmt(envelope.getPoint(1).value, 3));
    }

    // ---- Breakpoint mode lifts it ------------------------------------------
    {
        auto envelope = px3::BreakpointEnvelope::fromAdsr(adsrSettings);
        envelope.setMode(px3::BreakpointEnvelope::Mode::breakpoint);

        check("EnvMode_BreakpointStartsFromTheAdsrShape",
              envelope.getPointCount() == 4 && envelope.isBreakpointMode(),
              "it opens on the same " + juce::String(envelope.getPointCount())
                  + " points");

        const auto added = envelope.addPoint(0.25, 0.85);
        check("EnvMode_BreakpointAcceptsPoints",
              added > 0 && envelope.getPointCount() == 5,
              "adding a point returned index " + juce::String(added) + " for "
                  + juce::String(envelope.getPointCount()) + " points");

        // Up to sixteen, and no further.
        while (envelope.getPointCount() < px3::BreakpointEnvelope::kMaxPoints)
        {
            const auto at = 0.02 * envelope.getPointCount();
            if (envelope.addPoint(at, 0.5) < 0) { break; }
        }

        const auto atLimit = envelope.getPointCount();
        const auto seventeenth = envelope.addPoint(0.9, 0.4);

        check("EnvMode_BreakpointStopsAtSixteenPoints",
              atLimit == 16 && seventeenth < 0 && envelope.getPointCount() == 16,
              "filled to " + juce::String(atLimit)
                  + " points and the next attempt returned " + juce::String(seventeenth));

        // The peak is NOT pinned here - a drawn envelope rises where it likes.
        envelope.setPoint(1, envelope.getPoint(1).timeSeconds, 0.42);
        check("EnvMode_BreakpointDoesNotPinThePeak",
              std::abs(envelope.getPoint(1).value - 0.42) < 1.0e-6,
              "point 1 reads " + fmt(envelope.getPoint(1).value, 3));

        // Two structural points, for Breakpoint reasons: a function of time
        // needs a start and an end, and an envelope begins and ends at rest.
        // The sustain index is NOT one of them - that would be an ADSR
        // constraint borrowed by a mode that never holds.
        juce::StringArray protectedPoints;
        if (envelope.canRemovePoint(0)) { protectedPoints.add("the first"); }
        if (envelope.canRemovePoint(envelope.getPointCount() - 1)) { protectedPoints.add("the last"); }

        check("EnvMode_BreakpointProtectsOnlyItsOwnStructuralPoints",
              protectedPoints.isEmpty()
                  && envelope.getPoint(0).value <= 1.0e-9
                  && envelope.getPoint(envelope.getPointCount() - 1).value <= 1.0e-9
                  && envelope.canRemovePoint(envelope.getSustainPoint()),
              protectedPoints.isEmpty()
                  ? "the first and last points are protected and the sustain index is not"
                  : "these could be removed: " + protectedPoints.joinIntoString(", "));
    }

    // ---- the DSP traverses what was drawn -----------------------------------
    //
    // Not "audio came out". The audio thread's Snapshot is compared against the
    // model's own valueAt across a dense sweep, for every kind of segment the
    // editor can produce.
    {
        struct Case { const char* name; px3::BreakpointEnvelope envelope; };
        std::vector<Case> cases;

        {
            auto straight = px3::BreakpointEnvelope::fromAdsr(adsrSettings);
            cases.push_back({ "a straight ADSR", straight });
        }
        {
            auto bent = px3::BreakpointEnvelope::fromAdsr(adsrSettings);
            bent.setCurve(0, 0.85);
            bent.setCurve(1, -0.7);
            bent.setCurve(2, 0.4);
            cases.push_back({ "every segment bent", bent });
        }
        {
            EnvelopeSettings tiny;
            tiny.attackSeconds = 0.0008f;
            tiny.decaySeconds = 0.0011f;
            tiny.sustainLevel = 0.5f;
            tiny.releaseSeconds = 0.0015f;
            cases.push_back({ "very short segments", px3::BreakpointEnvelope::fromAdsr(tiny) });
        }
        {
            auto zero = px3::BreakpointEnvelope::fromAdsr(adsrSettings);
            // Decay dragged onto the attack: a stage of no length.
            zero.setPoint(2, zero.getPoint(1).timeSeconds, 0.45);
            cases.push_back({ "a zero-length decay", zero });
        }
        {
            auto stacked = px3::BreakpointEnvelope::fromAdsr(adsrSettings);
            stacked.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
            stacked.addPoint(0.25, 0.9);
            stacked.addPoint(0.26, 0.2);
            stacked.addPoint(0.26, 0.8);     // two points sharing a time
            cases.push_back({ "coincident points", stacked });
        }
        {
            auto many = px3::BreakpointEnvelope::fromAdsr(adsrSettings);
            many.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
            for (int i = 0; i < 8; ++i)
            {
                many.addPoint(0.05 + 0.06 * i, (i % 2 == 0) ? 0.85 : 0.25);
            }
            for (int i = 0; i + 1 < many.getPointCount(); ++i)
            {
                many.setCurve(i, (i % 2 == 0) ? 0.6 : -0.5);
            }
            cases.push_back({ "a twelve-point shape with alternating curves", many });
        }

        juce::StringArray divergent;
        auto worstOverall = 0.0;

        for (const auto& testCase : cases)
        {
            px3::BreakpointEnvelope::Snapshot snapshot;
            snapshot.rebuild(testCase.envelope, kRate);

            // The held phase, through the accessor the audio thread actually
            // calls. Everything before the sustain point is where a drawn
            // multi-point shape lives, and it is directly comparable with the
            // model's own reading of the same curve.
            const auto sustainTime = snapshot.sustainTimeSeconds();
            auto worst = 0.0;

            for (int step = 0; step < 2000; ++step)
            {
                const auto seconds = sustainTime * static_cast<double>(step) / 2000.0;
                const auto fromModel = testCase.envelope.valueAt(seconds);
                const auto fromDsp = static_cast<double>(snapshot.valueAtHeld(seconds));
                worst = juce::jmax(worst, std::abs(fromDsp - fromModel));
            }

            worstOverall = juce::jmax(worstOverall, worst);
            if (worst > 1.0e-6)
            {
                divergent.add(juce::String(testCase.name) + " by " + fmt(worst, 9));
            }
        }

        check("EnvMode_TheDspTraversesExactlyWhatWasDrawn",
              divergent.isEmpty(),
              divergent.isEmpty()
                  ? juce::String(static_cast<int>(cases.size()))
                        + " shapes agree with the drawn curve to "
                        + fmt(worstOverall, 9) + " over 2000 points each"
                  : divergent.joinIntoString("; "));
    }

    // ---- Breakpoint is a trajectory, not a gated ADSR -----------------------
    //
    // The behaviour this whole refactor is about. Before it, a breakpoint
    // envelope played its points up to the sustain index, FROZE there for as
    // long as the key was held, then played the rest on a second clock. The
    // graph showed a shape the DSP never traversed.
    {
        // Two peaks - a shape no ADSR can express, and one whose second peak
        // only exists if the envelope actually keeps travelling.
        auto twoPeaks = px3::BreakpointEnvelope::fromAdsr(adsrSettings);
        twoPeaks.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        {
            px3::BreakpointEnvelope::Point points[7] = {
                { 0.00, 0.0, 0.0 },
                { 0.10, 1.0, 0.0 },
                { 0.25, 0.0, 0.0 },   // down to silence between the strikes
                { 0.30, 0.0, 0.0 },
                { 0.45, 0.9, 0.0 },   // the second peak
                { 0.70, 0.3, 0.0 },
                { 1.00, 0.0, 0.0 }
            };
            twoPeaks.setPoints(points, 7, 2);
            twoPeaks.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        }

        AmpEnvelope amp;
        amp.prepare(kRate);
        amp.setEnvelope(twoPeaks);
        amp.noteOn();

        // Held throughout: the envelope must travel the whole trajectory
        // rather than stopping at any point along it.
        const auto sampleAt = [&](double seconds, std::vector<float>& trace)
        {
            const auto want = static_cast<int>(std::lround(seconds * kRate));
            while (static_cast<int>(trace.size()) <= want) { trace.push_back(amp.getNextSample()); }
            return trace[static_cast<std::size_t>(want)];
        };

        std::vector<float> held;
        const auto atFirstPeak = sampleAt(0.10, held);
        const auto atDip = sampleAt(0.27, held);
        const auto atSecondPeak = sampleAt(0.45, held);
        const auto atEnd = sampleAt(1.02, held);

        check("EnvBp_AHeldNoteTravelsTheWholeTrajectory",
              atFirstPeak > 0.8f && atDip < 1.0e-5f && atSecondPeak > 0.7f && atEnd < 0.05f,
              "held throughout, the envelope reads " + fmt(atFirstPeak, 3) + " at the first peak, "
                  + fmt(atDip, 5) + " in the silent gap, " + fmt(atSecondPeak, 3)
                  + " at the second peak and " + fmt(atEnd, 3) + " at the end");

        check("EnvBp_ItFinishesEvenWithTheKeyStillDown",
              ! amp.isActive(),
              amp.isActive() ? "the voice is still sounding after the envelope ended"
                             : "the envelope ended and the voice retired, key still down");

        // Releasing does not truncate it: the key triggers, it does not gate.
        // Released INSIDE the silent gap, which is the case that bites - a
        // note-off taken at a zero level reads as "nothing left to release"
        // and would retire the voice before the second strike ever sounds.
        AmpEnvelope released;
        released.prepare(kRate);
        released.setEnvelope(twoPeaks);
        released.noteOn();

        auto secondPeakAfterRelease = 0.0f;
        {
            const auto releaseAt = static_cast<int>(0.27 * kRate);
            const auto peakAt = static_cast<int>(0.45 * kRate);

            for (int i = 0; i <= peakAt; ++i)
            {
                if (i == releaseAt) { released.noteOff(); }
                const auto value = released.getNextSample();
                if (i == peakAt) { secondPeakAfterRelease = value; }
            }
        }

        check("EnvBp_ReleasingTheKeyDoesNotTruncateTheTrajectory",
              secondPeakAfterRelease > 0.7f,
              "released into the silent gap at 0.27 s, the envelope still reaches "
                  + fmt(secondPeakAfterRelease, 3) + " at its second peak");
    }

    // ---- the ADSR lifecycle is untouched ------------------------------------
    {
        auto adsr = px3::BreakpointEnvelope::fromAdsr(adsrSettings);

        AmpEnvelope amp;
        amp.prepare(kRate);
        amp.setEnvelope(adsr);
        amp.noteOn();

        // Held well past the point an ADSR reaches its sustain.
        for (int i = 0; i < static_cast<int>(2.0 * kRate); ++i) { amp.getNextSample(); }
        const auto whileHeld = amp.getNextSample();

        check("EnvBp_AnAdsrStillHoldsAtItsSustain",
              std::abs(whileHeld - adsrSettings.sustainLevel) < 0.02f && amp.isActive(),
              "held for two seconds, an ADSR sits at " + fmt(whileHeld, 3)
                  + " against a sustain of " + fmt(adsrSettings.sustainLevel, 3));

        amp.noteOff();
        for (int i = 0; i < static_cast<int>(1.0 * kRate); ++i) { amp.getNextSample(); }

        check("EnvBp_AnAdsrStillReleasesOnNoteOff",
              ! amp.isActive(),
              amp.isActive() ? "the ADSR did not finish its release"
                             : "note-off released it to silence as before");
    }

    // ---- the fill follows elapsed time, not a stage --------------------------
    {
        auto trajectory = px3::BreakpointEnvelope::fromAdsr(adsrSettings);
        {
            px3::BreakpointEnvelope::Point points[5] = {
                { 0.00, 0.0, 0.0 }, { 0.20, 1.0, 0.0 }, { 0.50, 0.3, 0.0 },
                { 0.80, 0.8, 0.0 }, { 1.20, 0.0, 0.0 }
            };
            trajectory.setPoints(points, 5, 2);
            trajectory.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        }

        BreakpointEnvelopeEditor graph;
        graph.setSize(600, 240);
        graph.setEnvelope(trajectory);

        const auto fillAt = [&graph](double elapsed)
        {
            EnvelopePosition position;
            position.active = true;
            position.heldSeconds = elapsed;
            position.sustainSeconds = 0.5;   // an ADSR would stop here
            graph.setProgress(position);
            return graph.progressDisplayTime();
        };

        juce::StringArray readings;
        auto advancesThroughout = true;
        auto previous = -1.0;
        for (const auto elapsed : { 0.0, 0.25, 0.5, 0.75, 1.0, 1.15 })
        {
            const auto shown = fillAt(elapsed);
            readings.add(fmt(shown, 2));
            advancesThroughout = advancesThroughout && shown > previous
                                 && std::abs(shown - elapsed) < 1.0e-6;
            previous = shown;
        }

        check("EnvBp_TheFillFollowsElapsedTimePastEveryPoint",
              advancesThroughout,
              "the fill reads " + readings.joinIntoString(", ")
                  + " s - it does not stop at the 0.50 s point an ADSR would hold at");
    }

    // ---- mode switching -----------------------------------------------------
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kRate, kBlock);
        processor.prepareToPlay(kRate, kBlock);

        // Start from an ADSR with curves, switch to Breakpoint, and draw.
        auto shape = px3::BreakpointEnvelope::fromAdsr(adsrSettings);
        shape.setCurve(0, 0.65);
        shape.setCurve(2, -0.35);
        processor.setShapedEnvelope(1, shape);

        processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
        const auto entered = processor.getShapedEnvelope(1);

        check("EnvMode_SwitchingToBreakpointKeepsTheShape",
              entered.isBreakpointMode() && entered.getPointCount() == 4
                  && std::abs(entered.getPoint(0).curveToNext - 0.65) < 1.0e-9
                  && std::abs(entered.getPoint(2).curveToNext + 0.35) < 1.0e-9,
              "it opens in " + juce::String(entered.isBreakpointMode() ? "Breakpoint" : "ADSR")
                  + " mode on " + juce::String(entered.getPointCount())
                  + " points with curves " + fmt(entered.getPoint(0).curveToNext, 3)
                  + " / " + fmt(entered.getPoint(1).curveToNext, 3)
                  + " / " + fmt(entered.getPoint(2).curveToNext, 3));

        auto drawn = entered;
        drawn.addPoint(0.12, 0.92);
        drawn.addPoint(0.40, 0.15);
        drawn.setCurve(1, 0.8);
        processor.setShapedEnvelope(1, drawn);
        const auto drawnPoints = drawn.getPointCount();

        // Back to ADSR: reduced, not destroyed.
        processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::adsr);
        const auto reduced = processor.getShapedEnvelope(1);

        check("EnvMode_SwitchingToAdsrReducesRatherThanRefusing",
              ! reduced.isBreakpointMode() && reduced.getPointCount() == 4
                  && reduced.getPoint(0).value <= 1.0e-9
                  && reduced.getPoint(3).value <= 1.0e-9,
              "a " + juce::String(drawnPoints) + "-point shape reduces to "
                  + juce::String(reduced.getPointCount()) + " points");

        // And back again: the drawing returns exactly.
        processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
        const auto restored = processor.getShapedEnvelope(1);

        auto identical = restored.getPointCount() == drawn.getPointCount();
        for (int i = 0; identical && i < drawn.getPointCount(); ++i)
        {
            identical = std::abs(restored.getPoint(i).timeSeconds - drawn.getPoint(i).timeSeconds) < 1.0e-9
                        && std::abs(restored.getPoint(i).value - drawn.getPoint(i).value) < 1.0e-9
                        && std::abs(restored.getPoint(i).curveToNext - drawn.getPoint(i).curveToNext) < 1.0e-9;
        }

        check("EnvMode_SwitchingBackRestoresTheDrawingExactly",
              identical,
              identical ? "all " + juce::String(restored.getPointCount())
                              + " points return unchanged"
                        : "the restored shape has " + juce::String(restored.getPointCount())
                              + " points against the " + juce::String(drawn.getPointCount())
                              + " that were drawn");
    }

    // ---- persistence --------------------------------------------------------
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kRate, kBlock);
        processor.prepareToPlay(kRate, kBlock);

        // AMP ENV drawn in Breakpoint mode; ENV 2 left in ADSR with a curve.
        auto drawn = px3::BreakpointEnvelope::fromAdsr(adsrSettings);
        drawn.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        drawn.addPoint(0.15, 0.88);
        drawn.addPoint(0.35, 0.22);
        drawn.setCurve(1, 0.75);
        processor.setShapedEnvelope(1, drawn);

        auto curvedAdsr = px3::BreakpointEnvelope::fromAdsr(adsrSettings);
        curvedAdsr.setCurve(0, -0.6);
        processor.setShapedEnvelope(2, curvedAdsr);

        juce::MemoryBlock saved;
        processor.getStateInformation(saved);

        PX3SynthAudioProcessor reopened;
        reopened.setPlayConfigDetails(0, 2, kRate, kBlock);
        reopened.prepareToPlay(kRate, kBlock);
        reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

        const auto amp = reopened.getShapedEnvelope(1);
        const auto env2 = reopened.getShapedEnvelope(2);

        auto pointsMatch = amp.getPointCount() == drawn.getPointCount();
        for (int i = 0; pointsMatch && i < drawn.getPointCount(); ++i)
        {
            pointsMatch = std::abs(amp.getPoint(i).timeSeconds - drawn.getPoint(i).timeSeconds) < 1.0e-6
                          && std::abs(amp.getPoint(i).value - drawn.getPoint(i).value) < 1.0e-6
                          && std::abs(amp.getPoint(i).curveToNext - drawn.getPoint(i).curveToNext) < 1.0e-6;
        }

        check("EnvMode_ABreakpointEnvelopeSurvivesASessionExactly",
              amp.isBreakpointMode() && pointsMatch,
              "it comes back in " + juce::String(amp.isBreakpointMode() ? "Breakpoint" : "ADSR")
                  + " mode with " + juce::String(amp.getPointCount()) + " of "
                  + juce::String(drawn.getPointCount()) + " points"
                  + (pointsMatch ? ", all matching" : ", not all matching"));

        check("EnvMode_AnAdsrEnvelopeComesBackAsAdsrWithItsCurves",
              ! env2.isBreakpointMode() && env2.getPointCount() == 4
                  && std::abs(env2.getPoint(0).curveToNext + 0.6) < 1.0e-6,
              "ENV 2 comes back as " + juce::String(env2.isBreakpointMode() ? "Breakpoint" : "ADSR")
                  + " with its first curve at " + fmt(env2.getPoint(0).curveToNext, 2));

        // The retained shape round-trips too, so switching back after a reload
        // still restores the drawing.
        processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::adsr);
        juce::MemoryBlock savedInAdsr;
        processor.getStateInformation(savedInAdsr);

        PX3SynthAudioProcessor afterReload;
        afterReload.setPlayConfigDetails(0, 2, kRate, kBlock);
        afterReload.prepareToPlay(kRate, kBlock);
        afterReload.setStateInformation(savedInAdsr.getData(),
                                        static_cast<int>(savedInAdsr.getSize()));
        afterReload.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
        const auto recovered = afterReload.getShapedEnvelope(1);

        check("EnvMode_TheRetainedDrawingSurvivesASaveAndReload",
              recovered.getPointCount() == drawn.getPointCount(),
              "after saving in ADSR mode and reloading, switching back gives "
                  + juce::String(recovered.getPointCount()) + " of "
                  + juce::String(drawn.getPointCount()) + " points");
    }

    // ---- migration ----------------------------------------------------------
    //
    // Every preset that exists records no mode. It must take the mode its shape
    // implies, which is exactly what the old implicit rule did.
    {
        auto skeleton = px3::BreakpointEnvelope::fromAdsr(adsrSettings);
        skeleton.setCurve(0, 0.5);

        auto freeForm = px3::BreakpointEnvelope::fromAdsr(adsrSettings);
        freeForm.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        freeForm.addPoint(0.25, 0.7);

        check("EnvMode_AnUnmarkedShapeTakesTheModeItImplies",
              px3::BreakpointEnvelope::impliedModeFor(skeleton)
                      == px3::BreakpointEnvelope::Mode::adsr
                  && px3::BreakpointEnvelope::impliedModeFor(freeForm)
                         == px3::BreakpointEnvelope::Mode::breakpoint,
              "a four-point shape implies ADSR and a five-point shape implies Breakpoint");

        // And through the actual load path, from a tree written the way this
        // build's predecessor wrote them: version 3, no mode recorded anywhere.
        {
            PX3SynthAudioProcessor writer;
            writer.setPlayConfigDetails(0, 2, kRate, kBlock);
            writer.prepareToPlay(kRate, kBlock);
            // Slot 1, not slot 0: AMP ENV is ADSR-only, so a five-point
            // shape stored there is forced back to ADSR on the way in and the
            // migration this is testing never gets a chance to run.
            writer.setShapedEnvelope(1, freeForm);      // five points
            writer.setShapedEnvelope(2, skeleton);      // four points, curved

            auto legacy = writer.createParameterStateTree();
            auto shapes = legacy.getChildWithName(px3::processor_internal::kEnvelopeShapesId);
            shapes.setProperty(px3::processor_internal::kEnvelopeShapeVersionId, 3, nullptr);
            for (auto node : shapes)
            {
                node.removeProperty(px3::processor_internal::kEnvelopeShapeModeId, nullptr);
            }

            PX3SynthAudioProcessor reader;
            reader.setPlayConfigDetails(0, 2, kRate, kBlock);
            reader.prepareToPlay(kRate, kBlock);
            juce::String error;
            const auto applied = reader.applyParameterStateTree(legacy, &error, true);

            check("EnvMode_AVersionThreeStateMigratesToTheModeItsShapeImplies",
                  applied
                      && reader.getEnvelopeMode(1) == px3::BreakpointEnvelope::Mode::breakpoint
                      && reader.getEnvelopeMode(2) == px3::BreakpointEnvelope::Mode::adsr
                      && reader.getShapedEnvelope(1).getPointCount() == 5,
                  "a version 3 state loads its five-point envelope as "
                      + juce::String(reader.getEnvelopeMode(1)
                                         == px3::BreakpointEnvelope::Mode::breakpoint
                                     ? "Breakpoint" : "ADSR")
                      + " and its four-point one as "
                      + juce::String(reader.getEnvelopeMode(2)
                                         == px3::BreakpointEnvelope::Mode::breakpoint
                                     ? "Breakpoint" : "ADSR"));
        }
    }

    // ---- the selector, on both cards ----------------------------------------
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kRate, kBlock);
        processor.prepareToPlay(kRate, kBlock);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            editor->setSize(1400, 900);

            std::vector<EnvelopeComponent*> cards;
            std::function<void(juce::Component&)> walk = [&](juce::Component& parent)
            {
                for (auto* child : parent.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* env = dynamic_cast<EnvelopeComponent*>(child)) { cards.push_back(env); }
                    walk(*child);
                }
            };
            walk(*editor);

            // Two things, told apart: which cards SHOW the selector, and what it
            // offers. Counting items alone passes on AMP ENV even now that the
            // selector is not drawn there - the ComboBox still holds two items,
            // it is simply never shown.
            juce::StringArray offered;
            auto shownSelectors = 0;
            auto hiddenSelectors = 0;
            auto everyShownOneOffersBoth = true;
            for (auto* card : cards)
            {
                auto& box = card->debugModeBox();

                if (card->isAdsrOnly())
                {
                    if (! box.isVisible()) { ++hiddenSelectors; }
                    continue;
                }

                if (box.isVisible()) { ++shownSelectors; }
                everyShownOneOffersBoth = everyShownOneOffersBoth && box.getNumItems() == 2;
                if (offered.isEmpty())
                {
                    for (int i = 0; i < box.getNumItems(); ++i) { offered.add(box.getItemText(i)); }
                }
            }

            // The ADSR group must not sit hard against the TYPE selector. The
            // gap is read from the config rather than restated here, so this
            // measures the layout instead of reproducing its arithmetic.
            {
                // From the shipped file, so the number this asserts is the
                // number the plugin lays out with.
                auto configured = 16;
                {
                    const auto configFile = juce::File::getCurrentWorkingDirectory()
                                                .getChildFile("Source/UI/UIConfig.json");
                    juce::String configError;
                    if (configFile.existsAsFile())
                    {
                        if (auto config = UIConfig::fromJsonText(configFile.loadFileAsString(),
                                                                 configError))
                        {
                            configured = config->getInt("envelope.modeSelector.gapAfter", 16);
                        }
                    }
                }
                auto worst = 100000;
                auto measuredOn = 0;
                for (auto* card : cards)
                {
                    card->setEnvelopeMode(px3::BreakpointEnvelope::Mode::adsr);
                    const auto group = card->debugAdsrGroupBounds();
                    const auto box = card->debugModeBox().getBounds();
                    if (group.isEmpty() || box.isEmpty()) { continue; }
                    worst = juce::jmin(worst, group.getX() - box.getRight());
                    ++measuredOn;
                }

                check("EnvKnobs_TheAdsrGroupClearsTheTypeSelector",
                      measuredOn > 0 && worst >= configured,
                      measuredOn > 0
                          ? "the tightest of " + juce::String(measuredOn) + " cards leaves "
                                + juce::String(worst) + " px after TYPE, against the "
                                + juce::String(configured) + " px configured"
                          : "no card reported both a TYPE box and an ADSR group");
            }

            // AMP ENV's knobs take the whole row, because there is no selector
            // sharing it. Measured against the row the knobs are laid out in
            // rather than against a number: the point is that the group spans
            // what it is given, whatever that happens to be.
            {
                EnvelopeComponent* ampCard = nullptr;
                EnvelopeComponent* modCard = nullptr;
                for (auto* card : cards)
                {
                    if (card->isAdsrOnly()) { if (ampCard == nullptr) { ampCard = card; } }
                    else if (modCard == nullptr) { modCard = card; }
                }

                const auto spanOf = [](EnvelopeComponent* card)
                {
                    if (card == nullptr) { return 0.0; }
                    card->setEnvelopeMode(px3::BreakpointEnvelope::Mode::adsr);
                    const auto group = card->debugAdsrGroupBounds();
                    const auto row = card->debugAdsrRowBounds();
                    if (row.getWidth() <= 0) { return 0.0; }
                    return static_cast<double>(group.getWidth())
                           / static_cast<double>(row.getWidth());
                };

                const auto ampSpan = spanOf(ampCard);
                const auto modSpan = spanOf(modCard);

                check("EnvKnobs_AmpEnvKnobsTakeTheWholeRow",
                      ampCard != nullptr && ampSpan > 0.97,
                      "AMP ENV's four knobs span " + fmt(ampSpan * 100.0, 1)
                          + "% of their row");

                // And the mod cards are NOT changed by this: theirs still stop
                // short, because the TYPE selector is still there.
                check("EnvKnobs_ModEnvKnobsStillShareTheirRow",
                      modCard != nullptr && modSpan > 0.2 && modSpan < 0.9,
                      "ENV 1's four knobs span " + fmt(modSpan * 100.0, 1)
                          + "% of their row, the rest being the TYPE selector");
            }

            check("EnvMode_TheModeSelectorAppearsOnlyWhereBothModesExist",
                  shownSelectors == 3 && hiddenSelectors == 1,
                  juce::String(shownSelectors) + " cards show a TYPE selector and "
                      + juce::String(hiddenSelectors) + " ADSR-only card hides it");

            check("EnvMode_TheSelectorOffersTheTwoModes",
                  shownSelectors > 0 && everyShownOneOffersBoth && offered.size() == 2,
                  juce::String(shownSelectors) + " cards offering "
                      + offered.joinIntoString(" / "));

            if (! cards.empty())
            {
                auto* card = cards.front();

                // ADSR: the four knobs are there and live.
                card->setEnvelopeMode(px3::BreakpointEnvelope::Mode::adsr);
                const auto shownInAdsr = card->debugAdsrKnobsVisible();
                const auto liveInAdsr = card->debugAdsrKnobsLive();

                // Breakpoint: still on screen, but dead. They are kept rather
                // than hidden because a control that vanishes says nothing
                // about why; greyed in place says the mode does not use them.
                card->setEnvelopeMode(px3::BreakpointEnvelope::Mode::breakpoint);
                const auto shownInBreakpoint = card->debugAdsrKnobsVisible();
                const auto liveInBreakpoint = card->debugAdsrKnobsLive();

                card->setEnvelopeMode(px3::BreakpointEnvelope::Mode::adsr);
                const auto liveAgain = card->debugAdsrKnobsLive();

                check("EnvMode_TheAdsrKnobsAreShownInBothModes",
                      shownInAdsr && shownInBreakpoint,
                      juce::String("ADSR: ") + (shownInAdsr ? "shown" : "hidden")
                          + ", Breakpoint: " + (shownInBreakpoint ? "shown" : "hidden"));

                check("EnvMode_TheAdsrKnobsAreLiveOnlyInAdsrMode",
                      liveInAdsr && ! liveInBreakpoint && liveAgain,
                      juce::String("ADSR: ") + (liveInAdsr ? "live" : "disabled")
                          + ", Breakpoint: " + (liveInBreakpoint ? "live" : "disabled")
                          + ", back to ADSR: " + (liveAgain ? "live" : "disabled"));
            }

            // Choosing a mode on the card reaches the processor slot it edits,
            // and only that slot.
            if (cards.size() >= 2)
            {
                cards[0]->debugModeBox().setSelectedId(2, juce::sendNotificationSync);

                juce::StringArray slots;
                for (int slot = 0; slot < 4; ++slot)
                {
                    slots.add(processor.getEnvelopeMode(slot)
                                  == px3::BreakpointEnvelope::Mode::breakpoint ? "B" : "A");
                }

                check("EnvMode_ChoosingAModeReachesOnlyThatEnvelope",
                      slots.joinIntoString("").contains("B")
                          && slots.joinIntoString("").indexOf("B")
                                 == slots.joinIntoString("").lastIndexOf("B"),
                      "after one card switched, the four slots read "
                          + slots.joinIntoString(", "));
            }
        }
    }

    // ---- the four slots stay independent ------------------------------------
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kRate, kBlock);
        processor.prepareToPlay(kRate, kBlock);

        processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
        processor.setEnvelopeMode(3, px3::BreakpointEnvelope::Mode::breakpoint);

        juce::StringArray modes;
        for (int slot = 0; slot < 4; ++slot)
        {
            modes.add(processor.getEnvelopeMode(slot) == px3::BreakpointEnvelope::Mode::breakpoint
                          ? "B" : "A");
        }

        check("EnvMode_EachSlotKeepsItsOwnMode",
              modes == juce::StringArray({ "A", "B", "A", "B" }),
              "AMP/ENV1/ENV2/ENV3 read " + modes.joinIntoString(", "));
    }
}

// Macro control system. See docs/macro-system-design.md.
void testMacroSystem()
{
    suite("MACRO SYSTEM");

    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256;

    const auto prepared = [](PX3SynthAudioProcessor& processor)
    {
        processor.setPlayConfigDetails(0, 2, kRate, kBlock);
        processor.prepareToPlay(kRate, kBlock);
    };

    // ---- the strip: one instance, on every panel, inside its budget ---------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            editor->setSize(1400, 900);
            auto* strip = editor->debugMacroStrip();

            // The macro knobs are drawn by a look of their own, and it is the
            // pale one. Rendered from the knobs the strip actually shows, not
            // through a look-and-feel handed in here: handing one in tests that
            // the pale look IS pale, which it is either way, and says nothing
            // about whether the strip uses it.
            if (strip != nullptr)
            {
                const auto meanBrightness = [](juce::Component& knob)
                {
                    const auto image = knob.createComponentSnapshot(knob.getLocalBounds());
                    const auto w = image.getWidth();
                    const auto h = image.getHeight();
                    if (w < 8 || h < 8) { return 0.0; }

                    auto total = 0.0;
                    auto counted = 0;
                    for (int py = h / 4; py < h - h / 4; ++py)
                    {
                        for (int px = w / 4; px < w - w / 4; ++px)
                        {
                            total += image.getPixelAt(px, py).getBrightness();
                            ++counted;
                        }
                    }
                    return counted > 0 ? total / counted : 0.0;
                };

                juce::Slider* panelKnob = nullptr;
                std::function<void(juce::Component&)> findPanelKnob = [&](juce::Component& parent)
                {
                    for (auto* child : parent.getChildren())
                    {
                        if (child == nullptr || panelKnob != nullptr) { continue; }
                        if (auto* candidate = dynamic_cast<juce::Slider*>(child))
                        {
                            const auto id = px3::ui::parameterIdOf(*candidate);
                            auto isMacroKnob = id.isEmpty();
                            for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
                            {
                                isMacroKnob = isMacroKnob
                                              || id == PX3SynthAudioProcessor::macroParameterId(macro);
                            }
                            if (! isMacroKnob
                                && candidate->getWidth() > 20 && candidate->getHeight() > 20
                                && candidate->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag)
                            {
                                panelKnob = candidate;
                                return;
                            }
                        }
                        findPanelKnob(*child);
                    }
                };
                findPanelKnob(*editor);

                const auto macroBrightness = meanBrightness(strip->knob(0));
                const auto panelBrightness = panelKnob != nullptr ? meanBrightness(*panelKnob) : 1.0;

                check("MacroUi_TheMacroKnobsUseTheirOwnPaleLook",
                      panelKnob != nullptr && macroBrightness > panelBrightness + 0.2,
                      "the strip's knob renders at brightness " + fmt(macroBrightness, 3)
                          + " against a panel knob's " + fmt(panelBrightness, 3));

                // The tick in the cap: its own dark colour, and one the config
                // actually sets.
                {
                    juce::String configError;
                    const auto fromJson = [&](const char* json)
                    {
                        const auto config = UIConfig::fromJsonText(json, configError);
                        return px3::ui::macroPointerColour(config.get());
                    };

                    const auto configured = fromJson(R"({"macro":{"colors":{"pointer":"#FF0000"}}})");

                    auto shipped = juce::Colours::transparentBlack;
                    {
                        const auto configFile = juce::File::getCurrentWorkingDirectory()
                                                    .getChildFile("Source/UI/UIConfig.json");
                        if (configFile.existsAsFile())
                        {
                            const auto config = UIConfig::fromJsonText(
                                configFile.loadFileAsString(), configError);
                            shipped = px3::ui::macroPointerColour(config.get());
                        }
                    }

                    check("MacroUi_ThePointerColourComesFromConfig",
                          configured == juce::Colour::fromRGB(255, 0, 0)
                              && shipped == juce::Colour::fromRGB(51, 51, 51),
                          "a configured pointer reads " + configured.toDisplayString(false)
                              + " and the shipped one " + shipped.toDisplayString(false));

                    // And the tick is DRAWN in it. Checking the helper alone
                    // would pass with the look-and-feel ignoring the value.
                    auto* look = editor->debugMacroKnobLookAndFeel();
                    const auto restore = look->pointerColour;
                    look->pointerColour = juce::Colour::fromRGB(255, 0, 255);

                    juce::Slider probe;
                    probe.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                    probe.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                    probe.setLookAndFeel(look);
                    probe.setSize(140, 140);
                    probe.setRange(0.0, 1.0);
                    probe.setValue(0.5, juce::dontSendNotification);
                    const auto image = probe.createComponentSnapshot(probe.getLocalBounds());
                    probe.setLookAndFeel(nullptr);
                    look->pointerColour = restore;

                    auto magenta = 0;
                    for (int py = 0; py < 140; ++py)
                    {
                        for (int px = 0; px < 140; ++px)
                        {
                            const auto c = image.getPixelAt(px, py);
                            if (c.getRed() > 150 && c.getBlue() > 150 && c.getGreen() < 110)
                            {
                                ++magenta;
                            }
                        }
                    }

                    check("MacroUi_TheTickIsDrawnInThePointerColour",
                          magenta > 8,
                          juce::String(magenta)
                              + " pixels of the tick take a pointer colour set on the look");
                }

                // The dots: recessed and grey when empty, lit through as the
                // knob turns, and DOTTED rather than an arc.
                {
                    const auto renderMacroKnob = [&](double value)
                    {
                        auto image = std::make_unique<juce::Image>();
                        juce::Slider probe;
                        probe.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                        probe.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                        probe.setLookAndFeel(editor->debugMacroKnobLookAndFeel());
                        probe.setSize(140, 140);
                        probe.setRange(0.0, 1.0);
                        probe.setValue(value, juce::dontSendNotification);
                        *image = probe.createComponentSnapshot(probe.getLocalBounds());
                        probe.setLookAndFeel(nullptr);
                        return image;
                    };

                    // Geometry the look uses: diameter is the 140 px box less
                    // the 10 px inset, and the dots sit at 0.86 of the radius.
                    const auto radius = (140.0f - 10.0f) * 0.5f;
                    const juce::Point<float> knobCentre(70.0f, 70.0f);
                    const auto dotRing = radius * 0.86f;

                    const auto isAccent = [](juce::Colour c)
                    {
                        return c.getGreen() > c.getRed() + 55 && c.getBlue() > c.getRed() + 35;
                    };

                    const auto sampleRing = [&](const juce::Image& image, float atRadius)
                    {
                        struct { int accentSamples, runs; float darkest, bezel; } out { 0, 0, 1.0f, 0.0f };
                        auto wasAccent = false;
                        auto bezelTotal = 0.0f;
                        auto bezelCount = 0;

                        constexpr int steps = 720;
                        for (int i = 0; i < steps; ++i)
                        {
                            const auto theta = juce::MathConstants<float>::twoPi
                                               * static_cast<float>(i) / static_cast<float>(steps);
                            const auto px = juce::roundToInt(knobCentre.x + std::sin(theta) * atRadius);
                            const auto py = juce::roundToInt(knobCentre.y - std::cos(theta) * atRadius);
                            if (px < 0 || py < 0 || px >= image.getWidth() || py >= image.getHeight())
                            {
                                continue;
                            }

                            const auto c = image.getPixelAt(px, py);
                            const auto accented = isAccent(c);
                            if (accented) { ++out.accentSamples; }
                            if (accented && ! wasAccent) { ++out.runs; }
                            wasAccent = accented;

                            out.darkest = juce::jmin(out.darkest, c.getBrightness());
                        }

                        // The bezel, sampled between the dots and the knob's
                        // outer edge, as the surface the holes are cut into.
                        for (int i = 0; i < steps; ++i)
                        {
                            const auto theta = juce::MathConstants<float>::twoPi
                                               * static_cast<float>(i) / static_cast<float>(steps);
                            const auto px = juce::roundToInt(knobCentre.x + std::sin(theta) * (radius * 0.96f));
                            const auto py = juce::roundToInt(knobCentre.y - std::cos(theta) * (radius * 0.96f));
                            if (px < 0 || py < 0 || px >= image.getWidth() || py >= image.getHeight())
                            {
                                continue;
                            }
                            bezelTotal += image.getPixelAt(px, py).getBrightness();
                            ++bezelCount;
                        }
                        out.bezel = bezelCount > 0 ? bezelTotal / static_cast<float>(bezelCount) : 0.0f;
                        return out;
                    };

                    const auto atZero = sampleRing(*renderMacroKnob(0.0), dotRing);
                    const auto atFull = sampleRing(*renderMacroKnob(1.0), dotRing);

                    // Recess, measured INSIDE the holes rather than around the
                    // ring. The darkest point on the ring is the drilled edge,
                    // which stays dark however the interior is filled - a
                    // version of this test that used it passed with the hole
                    // painted lighter than the bezel.
                    {
                        juce::Slider probe;
                        probe.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                        probe.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                        probe.setLookAndFeel(editor->debugMacroKnobLookAndFeel());
                        probe.setSize(300, 300);
                        probe.setRange(0.0, 1.0);
                        probe.setValue(0.0, juce::dontSendNotification);

                        const auto rotary = probe.getRotaryParameters();
                        const auto image = probe.createComponentSnapshot(probe.getLocalBounds());
                        probe.setLookAndFeel(nullptr);

                        const auto bigRadius = (300.0f - 10.0f) * 0.5f;
                        const juce::Point<float> bigCentre(150.0f, 150.0f);
                        const auto bigDotRing = bigRadius * 0.86f;
                        const auto bigDotRadius = juce::jmax(0.9f, bigRadius * 0.042f);

                        auto topTotal = 0.0f;
                        auto bottomTotal = 0.0f;
                        auto samples = 0;
                        for (int i = 0; i < 21; ++i)
                        {
                            const auto t = static_cast<float>(i) / 20.0f;
                            const auto a = rotary.startAngleRadians
                                           + t * (rotary.endAngleRadians - rotary.startAngleRadians);
                            const auto cx = bigCentre.x + std::sin(a) * bigDotRing;
                            const auto cy = bigCentre.y - std::cos(a) * bigDotRing;

                            const auto readAt = [&](float dy)
                            {
                                return image.getPixelAt(juce::roundToInt(cx),
                                                        juce::roundToInt(cy + dy)).getBrightness();
                            };

                            topTotal += readAt(-bigDotRadius * 0.40f);
                            bottomTotal += readAt(bigDotRadius * 0.35f);
                            ++samples;
                        }

                        const auto holeTop = samples > 0 ? topTotal / static_cast<float>(samples) : 0.0f;
                        const auto holeBottom = samples > 0 ? bottomTotal / static_cast<float>(samples) : 0.0f;

                        check("MacroUi_TheEmptyDotsAreRecessedAndGrey",
                              atZero.accentSamples == 0
                                  && holeTop < atZero.bezel - 0.15f
                                  && holeTop < holeBottom - 0.05f,
                              "at zero the holes are grey and darker at the top than the bottom - "
                                  + fmt(holeTop, 3) + " against " + fmt(holeBottom, 3)
                                  + ", in a bezel of " + fmt(atZero.bezel, 3));
                    }

                    check("MacroUi_TheDotsLightUpAsTheKnobTurns",
                          atFull.accentSamples > 200,
                          juce::String(atFull.accentSamples)
                              + " of 720 samples around the dot ring are lit at full, against "
                              + juce::String(atZero.accentSamples) + " at zero");

                    // Dotted, not a curved line: walking the ring at full value
                    // crosses into the highlight colour once per DOT. An arc
                    // would be one unbroken run.
                    check("MacroUi_TheHighlightIsDottedRatherThanAnArc",
                          atFull.runs >= 10,
                          "the lit highlight breaks into " + juce::String(atFull.runs)
                              + " separate runs around the ring, not one continuous arc");
                }

                // And it keeps every indicator the shared look draws: a macro
                // knob mapped to a CC still says so.
                const auto amberPixels = [&](bool mapped, juce::LookAndFeel* laf)
                {
                    juce::Slider probe;
                    probe.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                    probe.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                    probe.setLookAndFeel(laf);
                    // Larger than the strip draws them: "CC21" is a handful of
                    // pixels at 64 px, and a handful is too thin a margin.
                    probe.setSize(140, 140);
                    probe.setRange(0.0, 1.0);
                    probe.setValue(0.5, juce::dontSendNotification);
                    if (mapped) { probe.getProperties().set(px3::knob_properties::midiCc, 21); }
                    const auto image = probe.createComponentSnapshot(probe.getLocalBounds());
                    probe.setLookAndFeel(nullptr);

                    auto amber = 0;
                    auto darkest = 1.0f;
                    for (int py = 0; py < 140; ++py)
                    {
                        for (int px = 0; px < 140; ++px)
                        {
                            // Amber of any lightness: warm, meaning red well
                            // clear of blue. Pinning one RGB triple would miss
                            // the darker amber a pale knob has to use.
                            const auto c = image.getPixelAt(px, py);
                            if (c.getRed() > c.getBlue() + 55
                                && c.getGreen() > c.getBlue() + 20
                                && c.getGreen() < c.getRed())
                            {
                                ++amber;
                                darkest = juce::jmin(darkest, c.getBrightness());
                            }
                        }
                    }
                    return std::pair<int, float> { amber, darkest };
                };

                const auto withCc = amberPixels(true, editor->debugMacroKnobLookAndFeel());
                const auto withoutCc = amberPixels(false, editor->debugMacroKnobLookAndFeel());

                check("MacroUi_AMappedMacroKnobStillShowsItsCcLabel",
                      withCc.first > 40 && withoutCc.first == 0,
                      juce::String(withCc.first) + " amber pixels with CC 21 against "
                          + juce::String(withoutCc.first) + " without");

                // And dark enough to read on a white cap. Counting pixels cannot
                // tell the two ambers apart: the bright one a dark knob uses
                // answers the same warm test.
                //
                // The measure is the label's DARKEST pixel, because on a pale
                // knob bright amber cannot produce one - everything it blends
                // with is bright - while the darker amber's own core is dark by
                // construction. The check above, that the label is there at
                // all, is what stops this passing on an unrendered label.
                check("MacroUi_TheCcLabelIsDarkEnoughForAPaleKnob",
                      withCc.first > 40 && withCc.second < 0.70f,
                      "the darkest pixel of the label reads " + fmt(withCc.second, 3)
                          + " brightness on the pale knob");
            }

            check("MacroUi_TheStripExists",
                  strip != nullptr,
                  strip != nullptr ? "the editor owns a macro strip" : "no macro strip");

            if (strip != nullptr)
            {
                const auto stripArea = editor->debugMacroStripArea();

                check("MacroUi_TheStripStaysInsideItsWidthBudget",
                      stripArea.getWidth() > 0 && stripArea.getWidth() <= 70,
                      "the strip is " + juce::String(stripArea.getWidth())
                          + " px wide against a budget of 70");

                // The padding keys do something, ONE AT A TIME.
                //
                // Moving all three at once proved nothing: changing the caption
                // height alone moves the knob, so a padding key that had
                // stopped being read still looked like it worked. Each key is
                // now checked against the number it was given.
                {
                    juce::String configError;
                    const auto configured = [&](const char* json)
                    { return UIConfig::fromJsonText(json, configError); };

                    strip->setUIConfig(configured(R"({"macro":{"strip":{"padX":18}}})"));
                    const auto wideX = strip->knob(0).getBounds().getX();

                    strip->setUIConfig(configured(R"({"macro":{"strip":{"padY":44}}})"));
                    const auto lastCaptionBottom = strip->debugCaption(3).getBounds().getBottom();

                    strip->setUIConfig(configured(R"({"macro":{"strip":{"captionHeight":22}}})"));
                    const auto tallCaption = strip->debugCaption(0).getHeight();

                    check("MacroUi_TheStripsPaddingComesFromConfig",
                          wideX >= 18
                              && lastCaptionBottom <= strip->getHeight() - 44
                              && tallCaption == 22,
                          "padX 18 puts the knob at x " + juce::String(wideX)
                              + ", padY 44 ends the last caption "
                              + juce::String(strip->getHeight() - lastCaptionBottom)
                              + " px above the bottom, and captionHeight 22 gives "
                              + juce::String(tallCaption) + " px");

                    strip->setUIConfig(nullptr);
                }

                // The assignment highlight has to cover the caption as well as
                // the knob: they are one control, and a box round half of it
                // looks like the label belongs to something else.
                //
                // Measured BELOW the knob's own outline, not across the whole
                // caption band - the ring around the knob is drawn a few pixels
                // proud and bleeds into the top of that band, so measuring the
                // band as a whole passed whether the caption was covered or not.
                {
                    editor->debugEnterMacroAssignMode(1);
                    const auto lit = strip->createComponentSnapshot(strip->getLocalBounds());
                    editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
                    const auto dark = strip->createComponentSnapshot(strip->getLocalBounds());

                    const auto caption = strip->debugCaption(1).getBounds();
                    const auto clearOfTheKnob = strip->knob(1).getBounds().getBottom() + 6;

                    auto changed = 0;
                    for (int y = juce::jmax(caption.getY(), clearOfTheKnob);
                         y < caption.getBottom(); ++y)
                    {
                        for (int x = caption.getX(); x < caption.getRight(); ++x)
                        {
                            if (lit.getPixelAt(x, y) != dark.getPixelAt(x, y)) { ++changed; }
                        }
                    }

                    check("MacroUi_TheAssignmentHighlightCoversTheCaptionToo",
                          changed > 20,
                          juce::String(changed)
                              + " pixels change well below the knob, behind the caption, "
                              + "when its macro is armed");
                }

                // Hovering anywhere over the control says which macro it is.
                juce::StringArray tips;
                auto tipsCorrect = true;
                for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
                {
                    const auto wanted = "Macro " + juce::String(macro + 1);
                    tips.add(strip->debugCaption(macro).getTooltip());
                    tipsCorrect = tipsCorrect
                                  && strip->debugCaption(macro).getTooltip() == wanted
                                  && strip->knob(macro).getTooltip() == wanted;
                }

                check("MacroUi_TheKnobsAndCaptionsSayWhichMacroTheyAre",
                      tipsCorrect,
                      "hovering reads " + tips.joinIntoString(", "));

                // The caption sits directly under its knob, with nothing
                // between them - they are one control, not two things.
                juce::StringArray gaps;
                for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
                {
                    const auto knobBottom = strip->knob(macro).getBounds().getBottom();
                    const auto captionTop = strip->debugCaption(macro).getBounds().getY();
                    if (captionTop - knobBottom != 0)
                    {
                        gaps.add("M" + juce::String(macro + 1) + " leaves "
                                 + juce::String(captionTop - knobBottom) + " px");
                    }
                }

                check("MacroUi_TheCaptionSitsOnItsKnob",
                      gaps.isEmpty(),
                      gaps.isEmpty() ? "every caption starts where its knob ends"
                                     : gaps.joinIntoString(", "));

                // It sits to the LEFT of the rectangle every panel is laid out
                // in, which is what puts it on all six without any panel
                // knowing it exists.
                const auto panelArea = editor->debugPanelArea();
                check("MacroUi_TheStripIsLeftOfEveryPanelsRectangle",
                      stripArea.getRight() <= panelArea.getX()
                          && stripArea.getY() <= panelArea.getY() + 2
                          && panelArea.getWidth() > 200,
                      "the strip ends at x " + juce::String(stripArea.getRight())
                          + " and the panel rectangle starts at x "
                          + juce::String(panelArea.getX()) + " with "
                          + juce::String(panelArea.getWidth()) + " px left for content");

                // Every knob inside the strip, and every one bound to its own
                // macro parameter.
                juce::StringArray bound;
                juce::StringArray expectedStripIds;
                auto insideStrip = true;
                for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
                {
                    const auto& knob = strip->knob(macro);
                    bound.add(px3::ui::parameterIdOf(knob));
                    expectedStripIds.add("macro" + juce::String(macro + 1));
                    insideStrip = insideStrip
                                  && strip->getLocalBounds().contains(knob.getBounds())
                                  && ! knob.getBounds().isEmpty();
                }

                // The knob is scaled down from the width its cell allows.
                // Measured against the CAPTION, which spans that full width, so
                // this is a relationship between two laid-out things rather
                // than a second copy of the layout's own arithmetic.
                {
                    auto configuredScale = 0.9f;
                    {
                        const auto configFile = juce::File::getCurrentWorkingDirectory()
                                                    .getChildFile("Source/UI/UIConfig.json");
                        juce::String configError;
                        if (configFile.existsAsFile())
                        {
                            if (auto config = UIConfig::fromJsonText(configFile.loadFileAsString(),
                                                                     configError))
                            {
                                configuredScale = config->getFloat("macro.strip.knobScale", 0.9f);
                            }
                        }
                    }

                    auto worstError = 0.0f;
                    for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
                    {
                        const auto captionWidth = static_cast<float>(
                            strip->debugCaption(macro).getWidth());
                        const auto knobSide = static_cast<float>(strip->knob(macro).getWidth());
                        if (captionWidth <= 0.0f) { continue; }
                        worstError = juce::jmax(worstError,
                                                std::abs(knobSide / captionWidth - configuredScale));
                    }

                    check("MacroUi_TheKnobsAreScaledDownWithinTheirCells",
                          worstError < 0.03f,
                          "every knob is " + fmt(configuredScale, 2)
                              + " of the width its cell allows, to within "
                              + fmt(worstError, 3));

                    // And the KEY is what does it. The check above passes just
                    // as well against a hard-coded constant that happens to
                    // match the shipped value - verified: mutating the config
                    // read away did not fail it - so this drives a distinctive
                    // scale through the config and watches the knob follow.
                    {
                        juce::String configError;
                        strip->setUIConfig(UIConfig::fromJsonText(
                            R"({"macro":{"strip":{"knobScale":0.5}}})", configError));

                        const auto halfKnob = static_cast<float>(strip->knob(0).getWidth());
                        const auto halfCaption = static_cast<float>(
                            strip->debugCaption(0).getWidth());

                        const auto configFile = juce::File::getCurrentWorkingDirectory()
                                                    .getChildFile("Source/UI/UIConfig.json");
                        if (configFile.existsAsFile())
                        {
                            juce::String restoreError;
                            strip->setUIConfig(UIConfig::fromJsonText(
                                configFile.loadFileAsString(), restoreError));
                        }

                        check("MacroUi_TheKnobScaleKeyIsActuallyRead",
                              halfCaption > 0.0f
                                  && std::abs(halfKnob / halfCaption - 0.5f) < 0.03f,
                              "a configured scale of 0.50 gives a knob "
                                  + fmt(halfCaption > 0.0f ? halfKnob / halfCaption : 0.0f, 3)
                                  + " of its cell's width");
                    }
                }

                // Evenly distributed down the strip: equal centre-to-centre
                // spacing, and the same margin above the first as below the
                // last. Integer division used to pool its remainder at the
                // bottom, which left the last macro sitting high.
                {
                    std::vector<juce::Rectangle<int>> groups;
                    for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
                    {
                        groups.push_back(strip->knob(macro).getBounds()
                                             .getUnion(strip->debugCaption(macro).getBounds()));
                    }

                    auto worstSpacingSkew = 0;
                    for (std::size_t i = 2; i < groups.size(); ++i)
                    {
                        const auto a = groups[i - 1].getCentreY() - groups[i - 2].getCentreY();
                        const auto b = groups[i].getCentreY() - groups[i - 1].getCentreY();
                        worstSpacingSkew = juce::jmax(worstSpacingSkew, std::abs(a - b));
                    }

                    const auto above = groups.front().getY() - strip->getLocalBounds().getY();
                    const auto below = strip->getLocalBounds().getBottom() - groups.back().getBottom();

                    check("MacroUi_TheKnobsAreEvenlyDistributedDownTheStrip",
                          groups.size() == static_cast<std::size_t>(
                              PX3SynthAudioProcessor::kMacroCount)
                              && worstSpacingSkew <= 1 && std::abs(above - below) <= 1,
                          juce::String(static_cast<int>(groups.size())) + " macros, spacing varies by "
                              + juce::String(worstSpacingSkew) + " px, with " + juce::String(above)
                              + " px above the first and " + juce::String(below)
                              + " px below the last");
                }

                check("MacroUi_EachKnobIsBoundToItsOwnMacroAndFitsTheStrip",
                      insideStrip
                          && bound == expectedStripIds,
                      "the strip's knobs are bound to " + bound.joinIntoString(", "));

                // Switching panels cannot reset or duplicate them: the strip is
                // not inside a panel, so panel changes do not reach it.
                strip->knob(0).setValue(0.62, juce::sendNotificationSync);
                juce::StringArray afterSwitching;
                for (int section = 0; section < 6; ++section)
                {
                    editor->debugSelectSection(section);
                    afterSwitching.add(fmt(strip->knob(0).getValue(), 2));
                }

                check("MacroUi_PanelSwitchingLeavesTheMacrosAlone",
                      afterSwitching == juce::StringArray({ "0.62", "0.62", "0.62",
                                                            "0.62", "0.62", "0.62" })
                          && std::abs(processor.getMacroParam(0).get() - 0.62f) < 0.01f,
                      "macro 1 across OSC/MOD/FLT/FX/AMP/MIX reads "
                          + afterSwitching.joinIntoString(", "));
            }
        }
    }

    // ---- assignment mode ----------------------------------------------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            editor->setSize(1400, 900);
            editor->debugRefreshMidiMappingUI();
            auto* strip = editor->debugMacroStrip();

            std::vector<juce::Slider*> knobs;
            std::function<void(juce::Component&)> walk = [&](juce::Component& parent)
            {
                for (auto* child : parent.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* slider = dynamic_cast<juce::Slider*>(child))
                    {
                        const auto id = px3::ui::parameterIdOf(*slider);
                        // Only knobs a click could actually reach right now:
                        // the ones on the visible panel. A knob on a hidden
                        // panel is not clickable, so using one would test the
                        // assignment call rather than the interaction.
                        const auto centre = editor->getLocalPoint(
                            slider, slider->getLocalBounds().getCentre());
                        if (id.isNotEmpty() && ! id.startsWith("macro")
                            && ! slider->getBounds().isEmpty()
                            && editor->debugKnobAt(centre) == slider)
                        {
                            knobs.push_back(slider);
                        }
                    }
                    walk(*child);
                }
            };
            walk(*editor);

            const auto maskOf = [](const juce::Slider& slider)
            {
                return static_cast<int>(
                    slider.getProperties().getWithDefault(px3::knob_properties::macroMask, 0));
            };
            const auto assignableOf = [](const juce::Slider& slider)
            {
                return static_cast<bool>(
                    slider.getProperties().getWithDefault(px3::knob_properties::macroAssignable, false));
            };

            if (strip != nullptr && knobs.size() >= 3)
            {
                // Each enters its own mode, and reports its own index.
                juce::StringArray entered;
                juce::StringArray expectedEntered;
                for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
                {
                    editor->debugEnterMacroAssignMode(macro);
                    entered.add(juce::String(editor->debugAssigningMacro()));
                    expectedEntered.add(juce::String(macro));
                }

                // Double-click does what Cmd-click does, on every macro, and
                // does nothing at all on a knob that is not a macro.
                {
                    juce::StringArray doubleClicked;
                    juce::StringArray expectedDoubleClicked;
                    for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
                    {
                        editor->debugExitMacroAssignMode();
                        editor->debugSimulateKnobDoubleClick(strip->knob(macro));
                        doubleClicked.add(juce::String(editor->debugAssigningMacro()));
                        expectedDoubleClicked.add(juce::String(macro));
                    }

                    check("MacroUi_DoubleClickArmsTheMacroLikeCmdClick",
                          doubleClicked == expectedDoubleClicked,
                          "double-clicking each macro knob arms "
                              + doubleClicked.joinIntoString(", "));

                    editor->debugExitMacroAssignMode();
                    juce::Slider* ordinary = nullptr;
                    for (auto* knob : knobs)
                    {
                        const auto id = px3::ui::parameterIdOf(*knob);
                        auto isMacro = false;
                        for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
                        {
                            isMacro = isMacro || id == PX3SynthAudioProcessor::macroParameterId(macro);
                        }
                        if (! isMacro && ordinary == nullptr) { ordinary = knob; }
                    }

                    if (ordinary != nullptr)
                    {
                        editor->debugSimulateKnobDoubleClick(*ordinary);
                    }

                    check("MacroUi_DoubleClickingAnOrdinaryKnobArmsNothing",
                          ordinary != nullptr && editor->debugAssigningMacro() < 0,
                          ordinary == nullptr
                              ? "no non-macro knob found to try"
                              : "double-clicking " + px3::ui::parameterIdOf(*ordinary)
                                    + " leaves the assigning macro at "
                                    + juce::String(editor->debugAssigningMacro()));
                    editor->debugExitMacroAssignMode();
                }

                check("MacroUi_EachMacroEntersItsOwnAssignmentMode",
                      entered == expectedEntered,
                      "entering each macro's mode reports " + entered.joinIntoString(", "));

                editor->debugEnterMacroAssignMode(0);
                editor->debugRefreshMidiMappingUI();

                check("MacroUi_EligibleKnobsSayTheyCanBeAssigned",
                      assignableOf(*knobs[0]) && assignableOf(*knobs[1])
                          && ! assignableOf(strip->knob(1)),
                      juce::String("eligible knobs are marked assignable and a macro knob is ")
                          + (assignableOf(strip->knob(1)) ? "wrongly assignable" : "correctly not"));

                // Every message the keyboard can show has to FIT. The banner
                // used to size itself to the one fixed warning it was built
                // for, so the longer assignment messages were squeezed and
                // then cut off - and a message you cannot read is worse than
                // no message, because it looks like something is broken.
                {
                    juce::StringArray clipped;
                    juce::StringArray widths;

                    juce::StringArray messages;
                    messages.add("Select knobs, then move a MIDI control to assign");
                    for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
                    {
                        messages.add("Click knobs to assign them to "
                                     + PX3SynthAudioProcessor::macroDisplayName(macro));
                    }
                    messages.add("Please engage an oscillator!");

                    for (const auto& message : messages)
                    {
                        const auto fit = editor->debugKeyboardBannerFit(message);
                        widths.add(fmt(fit.boxWidth, 0));
                        if (! fit.fits())
                        {
                            clipped.add("\"" + message + "\" needs "
                                        + fmt(fit.textWidth + fit.paddingWidth, 0)
                                        + " px and gets " + fmt(fit.boxWidth, 0));
                        }
                    }

                    check("MacroUi_EveryKeyboardMessageFitsItsBanner",
                          clipped.isEmpty(),
                          clipped.isEmpty()
                              ? juce::String(messages.size())
                                    + " messages fit, at widths " + widths.joinIntoString(", ")
                                    + " px"
                              : clipped.joinIntoString("; "));
                }

                check("MacroUi_TheKeyboardNamesTheMacroBeingAssigned",
                      editor->debugKeyboardNotice().contains("MACRO 1"),
                      "the keyboard reads \"" + editor->debugKeyboardNotice() + "\"");

                // Clicking a knob assigns it, and does not move it.
                const auto valueBefore = knobs[0]->getValue();
                editor->debugMacroAssignClickOn(*knobs[0]);
                editor->debugRefreshMidiMappingUI();

                check("MacroUi_ClickingAKnobAssignsItWithoutMovingIt",
                      processor.isMacroDestination(0, px3::ui::parameterIdOf(*knobs[0]))
                          && maskOf(*knobs[0]) == 0b0001
                          && std::abs(knobs[0]->getValue() - valueBefore) < 1.0e-9,
                      "the knob is assigned, shows mask " + juce::String(maskOf(*knobs[0]))
                          + ", and its value is still " + fmt(knobs[0]->getValue(), 4));

                // Clicking again removes it.
                editor->debugMacroAssignClickOn(*knobs[0]);
                editor->debugRefreshMidiMappingUI();
                check("MacroUi_ClickingItAgainRemovesTheAssignment",
                      ! processor.isMacroDestination(0, px3::ui::parameterIdOf(*knobs[0]))
                          && maskOf(*knobs[0]) == 0,
                      "after a second click the mask reads " + juce::String(maskOf(*knobs[0])));

                // Cross-panel: assign on one panel, switch, assign a knob that
                // belongs to the NEW panel, switch again. One macro, three
                // panels, one uninterrupted assignment session.
                //
                // The knob has to be chosen after each switch: a knob on a
                // panel that is no longer showing is not something a user
                // could click, so using one would prove nothing.
                const auto clickAKnobOnThisPanel = [&]
                {
                    juce::Slider* target = nullptr;
                    std::function<void(juce::Component&)> find = [&](juce::Component& parent)
                    {
                        for (auto* child : parent.getChildren())
                        {
                            if (child == nullptr || target != nullptr) { continue; }
                            if (auto* slider = dynamic_cast<juce::Slider*>(child))
                            {
                                const auto id = px3::ui::parameterIdOf(*slider);
                                const auto centre = editor->getLocalPoint(
                                    slider, slider->getLocalBounds().getCentre());
                                if (id.isNotEmpty() && ! id.startsWith("macro")
                                    && ! slider->getBounds().isEmpty()
                                    && ! processor.isMacroDestination(0, id)
                                    && editor->debugKnobAt(centre) == slider)
                                {
                                    target = slider;
                                }
                            }
                            find(*child);
                        }
                    };
                    find(*editor);

                    if (target != nullptr) { editor->debugMacroAssignClickOn(*target); }
                    return target != nullptr;
                };

                juce::StringArray panelsAssigned;
                auto stayedActive = true;
                for (const auto section : { 0, 3, 5 })
                {
                    editor->debugSelectSection(section);
                    stayedActive = stayedActive && editor->debugAssigningMacro() == 0;
                    if (clickAKnobOnThisPanel()) { panelsAssigned.add(juce::String(section)); }
                }
                editor->debugRefreshMidiMappingUI();

                // The overlay must leave the top menu and the keyboard alone,
                // or the user cannot change panel or hear what they are
                // building - and a macro that cannot reach across panels is
                // most of the feature gone.
                editor->debugEnterMacroAssignMode(0);
                const auto overlay = editor->debugMacroOverlayBounds();
                const auto menu = editor->debugTopMenuBounds();
                const auto keys = editor->debugKeyboardBounds();

                check("MacroUi_TheOverlayLeavesTheTopMenuAndKeyboardClickable",
                      ! overlay.isEmpty() && ! menu.isEmpty() && ! keys.isEmpty()
                          && ! overlay.intersects(menu) && ! overlay.intersects(keys)
                          && overlay.contains(editor->debugMacroStripArea())
                          && overlay.contains(editor->debugPanelArea()),
                      "the overlay covers " + overlay.toString()
                          + ", the top menu is at " + menu.toString()
                          + " and the keyboard at " + keys.toString());

                check("MacroUi_AssignmentModeSurvivesPanelChanges",
                      stayedActive && editor->debugAssigningMacro() == 0
                          && panelsAssigned.size() == 3
                          && processor.getMacroDestinations(0).size() == 3,
                      "one macro collected "
                          + juce::String(static_cast<int>(processor.getMacroDestinations(0).size()))
                          + " destinations from panels " + panelsAssigned.joinIntoString(", ")
                          + " without leaving assignment mode");

                // Clicking the active macro knob leaves the mode, without
                // changing its value - which is why the overlay eats the click.
                const auto macroValueBefore = strip->knob(0).getValue();
                editor->debugMacroAssignClickOn(strip->knob(0));
                check("MacroUi_ClickingTheActiveMacroExitsWithoutMovingIt",
                      editor->debugAssigningMacro() == -1
                          && std::abs(strip->knob(0).getValue() - macroValueBefore) < 1.0e-9,
                      "the mode is "
                          + juce::String(editor->debugAssigningMacro() < 0 ? "off" : "still on")
                          + " and the macro still reads " + fmt(strip->knob(0).getValue(), 4));

                // Escape leaves the mode and keeps what was already clicked.
                const auto keptBefore = processor.getMacroDestinations(0).size();
                editor->debugEnterMacroAssignMode(1);
                editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
                check("MacroUi_EscapeExitsAndKeepsWhatWasAlreadyAssigned",
                      editor->debugAssigningMacro() == -1
                          && processor.getMacroDestinations(0).size() == keptBefore,
                      "after Escape the mode is off and macro 1 still holds "
                          + juce::String(static_cast<int>(processor.getMacroDestinations(0).size())));

                // Only one learning mode at a time.
                editor->debugEnterMacroAssignMode(2);
                editor->debugSimulateKnobClick(*knobs[0], true);   // shift = MIDI Learn
                check("MacroUi_StartingMidiLearnLeavesMacroAssignment",
                      editor->debugAssigningMacro() == -1
                          && editor->debugMidiSelection().size() == 1,
                      "a shift-click left macro mode "
                          + juce::String(editor->debugAssigningMacro() < 0 ? "off" : "on")
                          + " with " + juce::String(editor->debugMidiSelection().size())
                          + " knobs selected for MIDI");

                editor->debugEnterMacroAssignMode(2);
                check("MacroUi_EnteringMacroAssignmentLeavesMidiLearn",
                      editor->debugMidiSelection().isEmpty()
                          && editor->debugAssigningMacro() == 2,
                      "entering macro mode left "
                          + juce::String(editor->debugMidiSelection().size())
                          + " knobs selected for MIDI");
                editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
            }
        }
    }

    // ---- the three states look different ------------------------------------
    {
        PX3SynthAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            const auto render = [&](int cc, int macroMask, bool macroAssignable, bool midiSelected)
            {
                juce::Slider knob;
                knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                knob.setLookAndFeel(editor->debugKnobLookAndFeel());
                knob.setSize(64, 64);
                knob.setValue(0.5, juce::dontSendNotification);
                knob.getProperties().set(px3::knob_properties::midiCc, cc);
                knob.getProperties().set(px3::knob_properties::midiSelected, midiSelected);
                knob.getProperties().set(px3::knob_properties::macroMask, macroMask);
                knob.getProperties().set(px3::knob_properties::macroAssignable, macroAssignable);
                const auto image = knob.createComponentSnapshot(knob.getLocalBounds());
                knob.setLookAndFeel(nullptr);
                return image;
            };

            const auto differs = [](const juce::Image& a, const juce::Image& b)
            {
                auto changed = 0;
                for (int y = 0; y < a.getHeight(); ++y)
                {
                    for (int x = 0; x < a.getWidth(); ++x)
                    {
                        if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) { ++changed; }
                    }
                }
                return changed;
            };

            const auto plain = render(-1, 0, false, false);
            const auto macroDriven = render(-1, 0b0001, false, false);
            const auto midiMapped = render(21, 0, false, false);
            const auto both = render(21, 0b0001, false, false);
            const auto assignHighlight = render(-1, 0, true, false);
            const auto midiHighlight = render(-1, 0, false, true);

            check("MacroUi_AMacroDrivenKnobLooksDifferentFromAMidiMappedOne",
                  differs(plain, macroDriven) > 20 && differs(plain, midiMapped) > 20
                      && differs(macroDriven, midiMapped) > 20,
                  "macro " + juce::String(differs(plain, macroDriven)) + " px, MIDI "
                      + juce::String(differs(plain, midiMapped)) + " px, and "
                      + juce::String(differs(macroDriven, midiMapped))
                      + " px between the two");

            check("MacroUi_AKnobThatIsBothShowsBoth",
                  differs(both, macroDriven) > 20 && differs(both, midiMapped) > 20,
                  "a knob on both differs from macro-only by "
                      + juce::String(differs(both, macroDriven))
                      + " px and from MIDI-only by " + juce::String(differs(both, midiMapped)));

            check("MacroUi_TheAssignmentHighlightDiffersFromTheMidiOne",
                  differs(plain, assignHighlight) > 20
                      && differs(assignHighlight, midiHighlight) > 20,
                  "the macro highlight changes " + juce::String(differs(plain, assignHighlight))
                      + " px and differs from the MIDI highlight by "
                      + juce::String(differs(assignHighlight, midiHighlight)));
        }
    }

    // ---- one macro per count, with stable identities ------------------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);

        juce::StringArray ids;
        juce::StringArray names;
        auto allParameters = true;
        for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
        {
            ids.add(PX3SynthAudioProcessor::macroParameterId(macro));
            names.add(PX3SynthAudioProcessor::macroDisplayName(macro));
            allParameters = allParameters
                            && processor.getMacroParam(macro).getParameterID()
                                   == PX3SynthAudioProcessor::macroParameterId(macro);
        }

        // The IDs are the serialization keys, so their FORM is the contract -
        // macroN, one-based, in order - rather than a list that has to be
        // rewritten every time a macro is added. Pinning the list instead is
        // what makes adding one look like a test failure.
        juce::StringArray expectedIds;
        juce::StringArray expectedNames;
        for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
        {
            expectedIds.add("macro" + juce::String(macro + 1));
            expectedNames.add("MACRO " + juce::String(macro + 1));
        }

        check("Macro_EachHasAStableIdAndName",
              PX3SynthAudioProcessor::kMacroCount >= 4 && allParameters
                  && ids == expectedIds && names == expectedNames,
              juce::String(PX3SynthAudioProcessor::kMacroCount) + " macros: ids "
                  + ids.joinIntoString(", ") + " named " + names.joinIntoString(", "));

        // Being real parameters is what makes them automatable, serialized and
        // MIDI-mappable for free. Check they are actually registered as such.
        auto found = 0;
        for (auto* parameter : processor.getParameters())
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            {
                if (ids.contains(ranged->getParameterID())) { ++found; }
            }
        }

        check("Macro_TheyAreRealParametersNotUiState",
              found == PX3SynthAudioProcessor::kMacroCount,
              juce::String(found) + " of " + juce::String(PX3SynthAudioProcessor::kMacroCount)
                  + " macros are registered parameters");
    }

    // ---- a macro moves what it is assigned to, and nothing else -------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);

        auto& cutoff = processor.getFilterCutoffParam(0);
        auto& resonance = processor.getFilterResonanceParam(0);
        auto& reverb = processor.getReverbAmountParam();

        const auto cutoffId = cutoff.getParameterID();
        const auto resonanceId = resonance.getParameterID();
        const auto reverbId = reverb.getParameterID();

        const auto assigned = processor.toggleMacroDestination(0, cutoffId)
                              && processor.toggleMacroDestination(0, resonanceId);

        const auto baseCutoff = processor.getModulatedNormalisedValue(cutoff);
        const auto baseReverb = processor.getModulatedNormalisedValue(reverb);

        processor.getMacroParam(0).setValueNotifyingHost(1.0f);
        const auto drivenCutoff = processor.getModulatedNormalisedValue(cutoff);
        const auto drivenResonance = processor.getModulatedNormalisedValue(resonance);
        const auto drivenReverb = processor.getModulatedNormalisedValue(reverb);

        check("Macro_MovingItMovesEveryDestination",
              assigned && drivenCutoff > baseCutoff + 0.01f
                  && drivenResonance > 0.5f,
              "the cutoff went from " + fmt(baseCutoff, 3) + " to " + fmt(drivenCutoff, 3)
                  + " and the resonance reached " + fmt(drivenResonance, 3));

        check("Macro_AnUnassignedParameterIsUntouched",
              std::abs(drivenReverb - baseReverb) < 1.0e-6f,
              "the unassigned reverb stayed at " + fmt(drivenReverb, 4));

        // The BASE is untouched: a macro is a control source, not an alias.
        // This is what lets the knob, automation and a CC keep owning the
        // parameter while the macro moves the sound.
        const auto rawBase = static_cast<juce::RangedAudioParameter&>(cutoff).getValue();
        check("Macro_TheBaseParameterIsNotOverwritten",
              std::abs(rawBase - baseCutoff) < 1.0e-6f,
              "the cutoff parameter itself still reads " + fmt(rawBase, 4)
                  + " while its effective value is " + fmt(drivenCutoff, 4));

        // Toggling removes it again.
        const auto stillAssigned = processor.toggleMacroDestination(0, cutoffId);
        check("Macro_AssignmentIsAToggle",
              ! stillAssigned && ! processor.isMacroDestination(0, cutoffId)
                  && processor.isMacroDestination(0, resonanceId),
              "clicking the cutoff twice leaves it "
                  + juce::String(processor.isMacroDestination(0, cutoffId) ? "assigned" : "unassigned")
                  + " with the resonance still "
                  + juce::String(processor.isMacroDestination(0, resonanceId) ? "assigned" : "unassigned"));

        check("Macro_AMacroCannotDriveAMacro",
              ! processor.toggleMacroDestination(0, PX3SynthAudioProcessor::macroParameterId(1))
                  && ! processor.isMacroDestination(0, PX3SynthAudioProcessor::macroParameterId(1)),
              "assigning macro 2 to macro 1 was refused");
    }

    // ---- a macro on the AMP ENV knobs -------------------------------------
    //
    // Reported as "the AMP ENV ADSR knobs do not respond to the macro they are
    // assigned to", and they did not: currentAmpEnvelopeSettings read the four
    // parameters RAW, and a macro is applied when a parameter is READ. It was
    // the one place in the synth that skipped the read.
    {
        const auto renderWithMacro = [&](float macroValue, float* attackSeconds)
        {
            PX3SynthAudioProcessor processor;
            prepared(processor);
            setParam(processor, "osc1Enabled", 1.0f);
            setChoice(processor, "osc1Mode", 0);
            setParam(processor, "ampAttack", 0.005f);
            setParam(processor, "ampDecay", 0.100f);
            setParam(processor, "ampSustain", 1.00f);
            setParam(processor, "ampRelease", 0.300f);

            processor.toggleMacroDestination(0, processor.getAttackParam().getParameterID());
            processor.getMacroParam(0).setValueNotifyingHost(macroValue);

            if (attackSeconds != nullptr)
            {
                *attackSeconds = processor.currentAmpEnvelopeSettings().attackSeconds;
            }

            juce::AudioBuffer<float> buffer(2, kBlock);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);

            // How loud the note is a short way in. A long attack is quiet here;
            // a short one has already arrived.
            buffer.clear();
            processor.processBlock(buffer, midi);

            juce::MidiBuffer empty;
            auto level = 0.0f;
            for (int block = 0; block < 6; ++block)
            {
                buffer.clear();
                processor.processBlock(buffer, empty);
                level = buffer.getMagnitude(0, buffer.getNumSamples());
            }
            return level;
        };

        float shortAttack = 0.0f;
        float longAttack = 0.0f;
        const auto loudAtZero = renderWithMacro(0.0f, &shortAttack);
        const auto quietAtFull = renderWithMacro(1.0f, &longAttack);

        check("Macro_ReachesTheAmpEnvelopeSettings",
              longAttack > shortAttack * 10.0f,
              "the macro takes the amp attack from " + fmt(shortAttack, 4)
                  + " s to " + fmt(longAttack, 4) + " s");

        check("Macro_OnTheAmpEnvelopeChangesTheSound",
              loudAtZero > quietAtFull * 4.0f,
              "a note 32 ms in reads " + fmt(loudAtZero, 5)
                  + " with the macro at 0 and " + fmt(quietAtFull, 5) + " at 1");

        // A BENT envelope follows its parameters too, keeping its curves.
        //
        // The graph already did this - it applies the parameters over the
        // stored curves - while the DSP tested isPlainAdsr and used the stored
        // shape as-is. So on a bent envelope, turning an ADSR knob moved the
        // picture and not the sound. Both sides read it the same way now.
        {
            PX3SynthAudioProcessor processor;
            prepared(processor);

            EnvelopeSettings stale;
            stale.attackSeconds = 0.500f;
            stale.decaySeconds = 0.100f;
            stale.sustainLevel = 0.80f;
            stale.releaseSeconds = 0.300f;

            auto bent = px3::BreakpointEnvelope::fromAdsr(stale);
            bent.setCurve(0, 0.6);
            processor.setShapedEnvelope(0, bent);

            setParam(processor, "ampAttack", 2.500f);
            processor.toggleMacroDestination(0, processor.getAttackParam().getParameterID());
            processor.getMacroParam(0).setValueNotifyingHost(0.0f);

            const auto played = processor.currentAmpEnvelope();
            const auto adsr = played.toAdsr();

            check("Macro_ABentAmpEnvelopeStillFollowsItsParameters",
                  played.isAdsrSkeleton()
                      && std::abs(adsr.attackSeconds - 2.500f) < 0.01f
                      && std::abs(played.getPoint(0).curveToNext - 0.6) < 1.0e-6,
                  "the played envelope has a " + fmt(adsr.attackSeconds, 3)
                      + " s attack against the parameter's 2.500, with its curve at "
                      + fmt(played.getPoint(0).curveToNext, 2));

            // And the macro moves it from there, curve intact.
            processor.getMacroParam(0).setValueNotifyingHost(1.0f);
            const auto driven = processor.currentAmpEnvelope();

            check("Macro_MovesABentAmpEnvelopeWithoutStraighteningIt",
                  driven.toAdsr().attackSeconds > adsr.attackSeconds + 0.05f
                      && std::abs(driven.getPoint(0).curveToNext - 0.6) < 1.0e-6,
                  "the macro takes the bent envelope's attack to "
                      + fmt(driven.toAdsr().attackSeconds, 3) + " s with its curve still "
                      + fmt(driven.getPoint(0).curveToNext, 2));
        }

        // And the knob shows it. The knob itself does not move - that is the
        // rule for every control source - but its ring must, or a macro
        // assigned to it looks like it did nothing at all.
        {
            PX3SynthAudioProcessor processor;
            prepared(processor);
            std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
            auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

            if (editor != nullptr)
            {
                editor->setSize(1400, 900);
                processor.toggleMacroDestination(0, processor.getAttackParam().getParameterID());
                processor.getMacroParam(0).setValueNotifyingHost(0.8f);
                editor->debugRefreshMidiMappingUI();

                juce::Slider* attackKnob = nullptr;
                std::function<void(juce::Component&)> walk = [&](juce::Component& parent)
                {
                    for (auto* child : parent.getChildren())
                    {
                        if (child == nullptr) { continue; }
                        if (auto* slider = dynamic_cast<juce::Slider*>(child))
                        {
                            if (px3::ui::parameterIdOf(*slider)
                                == processor.getAttackParam().getParameterID())
                            {
                                attackKnob = slider;
                            }
                        }
                        walk(*child);
                    }
                };
                walk(*editor);

                const auto ring = attackKnob != nullptr
                                    ? static_cast<double>(attackKnob->getProperties()
                                          .getWithDefault("modulatedPos", -1.0))
                                    : -1.0;

                check("Macro_TheAmpEnvelopeKnobShowsItsRing",
                      attackKnob != nullptr && ring > 0.0,
                      attackKnob == nullptr
                          ? "no amp attack knob found in the editor"
                          : "the amp attack knob's ring reads " + fmt(ring, 3));
            }
        }
    }

    // ---- every macro, each minding only its own business --------------------
    //
    // Everything above leans on macro 1. This gives each macro a destination of
    // its own, sweeps them one at a time, and checks that the others do not
    // move - which is the difference between N macros and one macro drawn N
    // times.
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);

        // One parameter per macro, each from a different corner of the synth, so
        // a routing mistake cannot hide behind two of them being neighbours.
        // Sized from the macro count rather than written out, because a fixed
        // array here is indexed by macro and overruns the moment one is added.
        std::vector<juce::RangedAudioParameter*> destinations {
            &processor.getFilterCutoffParam(0),
            &processor.getFilterResonanceParam(0),
            &processor.getReverbAmountParam(),
            &processor.getFilterCutoffParam(1),
            &processor.getDelayAmountParam()
        };

        check("Macro_TheIsolationTestCoversEveryMacro",
              destinations.size() >= static_cast<std::size_t>(PX3SynthAudioProcessor::kMacroCount),
              juce::String(static_cast<int>(destinations.size())) + " destinations for "
                  + juce::String(PX3SynthAudioProcessor::kMacroCount) + " macros");

        destinations.resize(static_cast<std::size_t>(PX3SynthAudioProcessor::kMacroCount));

        juce::StringArray ids;
        for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
        {
            const auto id = destinations[macro]->getParameterID();
            ids.add(id);

            // Start each base low, so there is room above it to measure into.
            destinations[macro]->setValueNotifyingHost(0.1f);
            processor.toggleMacroDestination(macro, id);
        }

        const auto effective = [&](int index)
        {
            return processor.getModulatedNormalisedValue(*destinations[index]);
        };

        // Each destination belongs to exactly one macro.
        juce::StringArray masks;
        auto masksCorrect = true;
        for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
        {
            const auto mask = processor.getMacroMaskForParameter(ids[macro]);
            masks.add(juce::String(mask));
            masksCorrect = masksCorrect && mask == (1 << macro);
        }

        check("Macro_EachDestinationBelongsToExactlyOneMacro",
              masksCorrect,
              "the destinations report masks " + masks.joinIntoString(", ")
                  + ", one bit each");

        // Sweep one macro at a time and watch every destination.
        const auto destinationCount = static_cast<int>(destinations.size());
        juce::StringArray crosstalk;
        for (int moving = 0; moving < PX3SynthAudioProcessor::kMacroCount; ++moving)
        {
            std::vector<float> before(destinations.size(), 0.0f);
            for (int i = 0; i < destinationCount; ++i)
            {
                before[static_cast<std::size_t>(i)] = effective(i);
            }

            processor.getMacroParam(moving).setValueNotifyingHost(1.0f);

            for (int i = 0; i < destinationCount; ++i)
            {
                const auto after = effective(i);
                const auto moved
                    = std::abs(after - before[static_cast<std::size_t>(i)]) > 0.01f;

                if (i == moving && ! moved)
                {
                    crosstalk.add("macro " + juce::String(moving + 1)
                                  + " did not move its own destination");
                }
                if (i != moving && moved)
                {
                    crosstalk.add("macro " + juce::String(moving + 1) + " moved macro "
                                  + juce::String(i + 1) + "'s destination from "
                                  + fmt(before[static_cast<std::size_t>(i)], 3) + " to "
                                  + fmt(after, 3));
                }
            }

            processor.getMacroParam(moving).setValueNotifyingHost(0.0f);
        }

        check("Macro_EachMacroMovesOnlyItsOwnDestinations",
              crosstalk.isEmpty(),
              crosstalk.isEmpty()
                  ? "all four macros swept independently with no crosstalk"
                  : crosstalk.joinIntoString("; "));

        // Turning one macro does not disturb another macro's VALUE either.
        processor.getMacroParam(2).setValueNotifyingHost(0.8f);
        juce::StringArray values;
        auto othersUntouched = true;
        for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
        {
            const auto value = processor.getMacroParam(macro).get();
            values.add(fmt(value, 2));
            if (macro != 2) { othersUntouched = othersUntouched && value < 0.01f; }
        }

        check("Macro_MovingOneMacroDoesNotMoveTheOthers",
              othersUntouched && processor.getMacroParam(2).get() > 0.7f,
              "with macro 3 at 0.8 they read " + values.joinIntoString(", "));
        processor.getMacroParam(2).setValueNotifyingHost(0.0f);

        // And their destination LISTS are separate: clearing one leaves every
        // other exactly as it was.
        processor.clearMacroDestinations(1);

        juce::StringArray sizes;
        for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
        {
            sizes.add(juce::String(static_cast<int>(processor.getMacroDestinations(macro).size())));
        }

        juce::StringArray expectedSizes;
        auto othersKeptTheirs = true;
        for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
        {
            expectedSizes.add(macro == 1 ? "0" : "1");
            if (macro != 1)
            {
                othersKeptTheirs = othersKeptTheirs
                                   && processor.isMacroDestination(macro, ids[macro]);
            }
        }

        check("Macro_ClearingOneMacroLeavesTheOthersIntact",
              sizes == expectedSizes && othersKeptTheirs,
              "after clearing macro 2 they hold " + sizes.joinIntoString(", ")
                  + " destinations");

        // The one that was cleared really is inert now. Checked against the
        // parameter's own value rather than the modulated read, because the
        // modulated read returns its "nothing is driving this" sentinel once
        // the macro is gone - which would make this pass for the wrong reason.
        const auto clearedBase = destinations[1]->getValue();
        processor.getMacroParam(1).setValueNotifyingHost(1.0f);
        check("Macro_AClearedMacroDrivesNothing",
              std::abs(destinations[1]->getValue() - clearedBase) < 1.0e-6f
                  && processor.getMacroMaskForParameter(ids[1]) == 0
                  && processor.getModulatedNormalisedValue(*destinations[1]) < 0.0f,
              "the cleared macro reports mask "
                  + juce::String(processor.getMacroMaskForParameter(ids[1]))
                  + " and its old destination is no longer driven at all");

        // ---- and the same set, driven by the actual knobs -------------------
        //
        // Everything above sets the parameter directly. This turns the knob a
        // player would turn, through its attachment, which is the path the
        // question is really about.
        {
            PX3SynthAudioProcessor knobProcessor;
            prepared(knobProcessor);

            std::unique_ptr<juce::AudioProcessorEditor> base(knobProcessor.createEditor());
            auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());
            auto* strip = editor != nullptr ? editor->debugMacroStrip() : nullptr;

            if (strip != nullptr)
            {
                // Sized from the macro count: a fixed array indexed by macro
                // overruns the moment one is added.
                std::vector<juce::RangedAudioParameter*> targets {
                    &knobProcessor.getFilterCutoffParam(0),
                    &knobProcessor.getFilterResonanceParam(0),
                    &knobProcessor.getReverbAmountParam(),
                    &knobProcessor.getFilterCutoffParam(1),
                    &knobProcessor.getDelayAmountParam()
                };
                targets.resize(static_cast<std::size_t>(PX3SynthAudioProcessor::kMacroCount));
                const auto targetCount = static_cast<int>(targets.size());

                for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
                {
                    targets[macro]->setValueNotifyingHost(0.1f);
                    knobProcessor.toggleMacroDestination(macro, targets[macro]->getParameterID());
                }

                juce::StringArray knobFaults;
                for (int moving = 0; moving < PX3SynthAudioProcessor::kMacroCount; ++moving)
                {
                    std::vector<float> before(targets.size(), 0.0f);
                    for (int i = 0; i < targetCount; ++i)
                    {
                        before[static_cast<std::size_t>(i)]
                            = knobProcessor.getModulatedNormalisedValue(*targets[static_cast<std::size_t>(i)]);
                    }

                    strip->knob(moving).setValue(1.0, juce::sendNotificationSync);

                    for (int i = 0; i < targetCount; ++i)
                    {
                        const auto after
                            = knobProcessor.getModulatedNormalisedValue(*targets[static_cast<std::size_t>(i)]);
                        const auto moved
                            = std::abs(after - before[static_cast<std::size_t>(i)]) > 0.01f;

                        if (i == moving && ! moved)
                        {
                            knobFaults.add("turning the M" + juce::String(moving + 1)
                                           + " knob moved nothing");
                        }
                        if (i != moving && moved)
                        {
                            knobFaults.add("the M" + juce::String(moving + 1)
                                           + " knob moved macro " + juce::String(i + 1)
                                           + "'s destination");
                        }
                    }

                    strip->knob(moving).setValue(0.0, juce::sendNotificationSync);
                }

                check("Macro_TurningEachKnobDrivesOnlyThatMacrosDestinations",
                      knobFaults.isEmpty(),
                      knobFaults.isEmpty()
                          ? "all four knobs drive their own destinations and nothing else"
                          : knobFaults.joinIntoString("; "));
            }
        }

        // ---- and each comes back on its OWN index after a round trip --------
        //
        // A serialization bug that wrote the index wrongly would put macro 4's
        // destinations on macro 1, which every single-macro test would miss.
        {
            PX3SynthAudioProcessor saver;
            prepared(saver);

            std::vector<juce::RangedAudioParameter*> targets {
                &saver.getFilterCutoffParam(0),
                &saver.getFilterResonanceParam(0),
                &saver.getReverbAmountParam(),
                &saver.getFilterCutoffParam(1),
                &saver.getDelayAmountParam()
            };
            targets.resize(static_cast<std::size_t>(PX3SynthAudioProcessor::kMacroCount));

            for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
            {
                saver.toggleMacroDestination(macro, targets[macro]->getParameterID());
                saver.getMacroParam(macro).setValueNotifyingHost(0.2f * static_cast<float>(macro + 1));
            }

            juce::MemoryBlock saved;
            saver.getStateInformation(saved);

            PX3SynthAudioProcessor loader;
            prepared(loader);
            loader.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

            juce::StringArray restored;
            auto correct = true;
            for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
            {
                const auto id = targets[macro]->getParameterID();
                const auto mask = loader.getMacroMaskForParameter(id);
                const auto value = loader.getMacroParam(macro).get();

                restored.add("M" + juce::String(macro + 1) + " mask " + juce::String(mask)
                             + " at " + fmt(value, 2));
                correct = correct && mask == (1 << macro)
                          && std::abs(value - 0.2f * static_cast<float>(macro + 1)) < 0.02f;
            }

            check("Macro_EachComesBackOnItsOwnIndex",
                  correct,
                  "after a round trip: " + restored.joinIntoString(", "));
        }
    }

    // ---- two macros on one parameter sum ------------------------------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);

        auto& cutoff = processor.getFilterCutoffParam(0);
        const auto cutoffId = cutoff.getParameterID();

        // Start the base low, so there is room above it to measure into.
        cutoff.setValueNotifyingHost(0.1f);
        const auto base = processor.getModulatedNormalisedValue(cutoff);

        processor.toggleMacroDestination(0, cutoffId);
        processor.getMacroParam(0).setValueNotifyingHost(0.5f);
        const auto oneMacro = processor.getModulatedNormalisedValue(cutoff);

        processor.toggleMacroDestination(1, cutoffId);
        processor.getMacroParam(1).setValueNotifyingHost(0.5f);
        const auto twoMacros = processor.getModulatedNormalisedValue(cutoff);

        check("Macro_TwoMacrosOnOneParameterSumRatherThanFight",
              oneMacro > base + 0.01f && twoMacros > oneMacro + 0.01f,
              "base " + fmt(base, 3) + ", one macro " + fmt(oneMacro, 3)
                  + ", two macros " + fmt(twoMacros, 3));

        check("Macro_TheMaskNamesEveryMacroDrivingAParameter",
              processor.getMacroMaskForParameter(cutoffId) == 0b0011,
              "the cutoff reports mask "
                  + juce::String(processor.getMacroMaskForParameter(cutoffId)));
    }

    // ---- a macro coexists with an LFO, an envelope and a direct MIDI CC ------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        auto& cutoff = processor.getFilterCutoffParam(0);
        const auto cutoffId = cutoff.getParameterID();
        cutoff.setValueNotifyingHost(0.2f);

        // Direct MIDI onto the same parameter the macro drives. The CC writes
        // the BASE; the macro adds on top. Neither clears the other.
        processor.setMidiLearnTargets({ cutoffId });
        juce::MidiBuffer learn;
        learn.addEvent(juce::MidiMessage::controllerEvent(1, 22, 40), 0);
        buffer.clear();
        processor.processBlock(buffer, learn);
        processor.applyPendingMidiMappings();

        processor.toggleMacroDestination(0, cutoffId);
        processor.getMacroParam(0).setValueNotifyingHost(1.0f);

        juce::MidiBuffer sweep;
        sweep.addEvent(juce::MidiMessage::controllerEvent(1, 22, 100), 0);
        buffer.clear();
        processor.processBlock(buffer, sweep);
        processor.applyPendingMidiMappings();

        const auto baseAfterCc = static_cast<juce::RangedAudioParameter&>(cutoff).getValue();
        const auto effective = processor.getModulatedNormalisedValue(cutoff);

        check("Macro_ADirectMidiMappingAndAMacroBothKeepWorking",
              processor.getMidiCcForParameter(cutoffId) == 22
                  && processor.isMacroDestination(0, cutoffId)
                  && baseAfterCc > 0.5f && effective > baseAfterCc,
              "CC 22 moved the base to " + fmt(baseAfterCc, 3)
                  + " and the macro raised the effective value to " + fmt(effective, 3));

        // And the LFO/envelope path is untouched: assigning a macro to a
        // parameter does not disturb what modulation does to it.
        setChoice(processor, "lfo1Assign", 1);
        const auto withLfo = processor.getModulatedNormalisedValue(cutoff);
        check("Macro_ModulationStillReachesAMacroDrivenParameter",
              std::isfinite(withLfo) && withLfo >= 0.0f && withLfo <= 1.0f,
              "with an LFO assigned as well the effective value is " + fmt(withLfo, 3));
    }

    // ---- MIDI onto the macros themselves ------------------------------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        const auto sendAndApply = [&](const juce::MidiMessage& message)
        {
            juce::MidiBuffer midi;
            midi.addEvent(message, 0);
            buffer.clear();
            processor.processBlock(buffer, midi);
            processor.applyPendingMidiMappings();
        };

        juce::StringArray mapped;
        juce::StringArray expectedCcs;
        for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
        {
            processor.setMidiLearnTargets({ PX3SynthAudioProcessor::macroParameterId(macro) });
            sendAndApply(juce::MidiMessage::controllerEvent(1, 21 + macro, 64));
            mapped.add(juce::String(processor.getMidiCcForParameter(
                PX3SynthAudioProcessor::macroParameterId(macro))));
            expectedCcs.add(juce::String(21 + macro));
        }

        check("Macro_EveryMacroCanBeMidiMapped",
              mapped == expectedCcs,
              "each of the " + juce::String(PX3SynthAudioProcessor::kMacroCount)
                  + " macros maps to its own CC: " + mapped.joinIntoString(", "));

        // The whole chain: CC -> macro -> parameter -> effective value.
        auto& cutoff = processor.getFilterCutoffParam(0);
        cutoff.setValueNotifyingHost(0.1f);
        processor.toggleMacroDestination(0, cutoff.getParameterID());

        const auto before = processor.getModulatedNormalisedValue(cutoff);
        sendAndApply(juce::MidiMessage::controllerEvent(1, 21, 127));
        const auto macroValue = processor.getMacroParam(0).get();
        const auto after = processor.getModulatedNormalisedValue(cutoff);

        check("Macro_TheChainFromCcToMacroToParameterWorks",
              macroValue > 0.9f && after > before + 0.05f,
              "CC 21 took macro 1 to " + fmt(macroValue, 3)
                  + " and the cutoff's effective value from " + fmt(before, 3)
                  + " to " + fmt(after, 3));
    }

    // ---- it reaches the audio ----------------------------------------------
    {
        const auto renderWithMacro = [&](float macroValue)
        {
            PX3SynthAudioProcessor processor;
            prepared(processor);
            setParam(processor, "filter1Enabled", 1.0f);
            setChoice(processor, "filter1Type", 0);
            setParam(processor, "filter1Resonance", 0.2f);
            processor.getFilterCutoffParam(0).setValueNotifyingHost(0.05f);

            processor.toggleMacroDestination(0, processor.getFilterCutoffParam(0).getParameterID());
            processor.getMacroParam(0).setValueNotifyingHost(macroValue);

            juce::AudioBuffer<float> buffer(2, kBlock);
            juce::MidiBuffer notes;
            notes.addEvent(juce::MidiMessage::noteOn(1, 45, 1.0f), 0);
            buffer.clear();
            processor.processBlock(buffer, notes);

            juce::MidiBuffer empty;
            for (int b = 0; b < 20; ++b) { buffer.clear(); processor.processBlock(buffer, empty); }

            auto brightness = 0.0;
            auto previous = 0.0f;
            for (int b = 0; b < 20; ++b)
            {
                buffer.clear();
                processor.processBlock(buffer, empty);
                const auto* data = buffer.getReadPointer(0);
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    brightness += std::abs(data[i] - previous);
                    previous = data[i];
                }
            }
            return brightness;
        };

        const auto closed = renderWithMacro(0.0f);
        const auto open = renderWithMacro(1.0f);

        check("Macro_TheChangeReachesTheAudio",
              open > closed * 2.0,
              "a note rendered with the macro at 0 carries " + fmt(closed, 1)
                  + " of high-frequency energy against " + fmt(open, 1) + " at 1");
    }

    // ---- persistence: values and destinations, session and preset -----------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);

        auto& cutoff = processor.getFilterCutoffParam(0);
        auto& reverb = processor.getReverbAmountParam();
        const auto cutoffId = cutoff.getParameterID();
        const auto reverbId = reverb.getParameterID();

        processor.toggleMacroDestination(0, cutoffId);
        processor.toggleMacroDestination(0, reverbId);
        processor.toggleMacroDestination(2, reverbId);
        processor.getMacroParam(0).setValueNotifyingHost(0.72f);
        processor.getMacroParam(2).setValueNotifyingHost(0.35f);

        juce::MemoryBlock saved;
        processor.getStateInformation(saved);

        PX3SynthAudioProcessor reopened;
        prepared(reopened);
        reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

        check("Macro_AssignmentsAndValuesSurviveASession",
              reopened.isMacroDestination(0, cutoffId)
                  && reopened.isMacroDestination(0, reverbId)
                  && reopened.isMacroDestination(2, reverbId)
                  && std::abs(reopened.getMacroParam(0).get() - 0.72f) < 0.01f
                  && std::abs(reopened.getMacroParam(2).get() - 0.35f) < 0.01f,
              "macro 1 holds " + juce::String(static_cast<int>(reopened.getMacroDestinations(0).size()))
                  + " destinations at " + fmt(reopened.getMacroParam(0).get(), 2)
                  + " and macro 3 holds "
                  + juce::String(static_cast<int>(reopened.getMacroDestinations(2).size()))
                  + " at " + fmt(reopened.getMacroParam(2).get(), 2));

        // A preset ships its performance controls.
        const auto preset = processor.createPresetStateTree();
        PX3SynthAudioProcessor loaded;
        prepared(loaded);
        juce::String error;
        loaded.applyParameterStateTree(preset, &error, false);

        check("Macro_APresetCarriesItsAssignmentsAndValues",
              preset.getChildWithName(px3::processor_internal::kMacroRoutesId).isValid()
                  && loaded.isMacroDestination(0, cutoffId)
                  && loaded.isMacroDestination(2, reverbId)
                  && std::abs(loaded.getMacroParam(0).get() - 0.72f) < 0.01f,
              "the preset restores " + juce::String(static_cast<int>(loaded.getMacroDestinations(0).size()))
                  + " destinations on macro 1 at " + fmt(loaded.getMacroParam(0).get(), 2));

        // Every macro through a PRESET FILE, not only macro 1 and not only the
        // session tree. A preset is what users trade, and an off-by-one in the
        // index would put the last macro's destinations on the first - which a
        // single-macro check cannot see.
        {
            PX3SynthAudioProcessor saver;
            prepared(saver);

            std::vector<juce::RangedAudioParameter*> targets {
                &saver.getFilterCutoffParam(0),
                &saver.getFilterResonanceParam(0),
                &saver.getReverbAmountParam(),
                &saver.getFilterCutoffParam(1),
                &saver.getDelayAmountParam()
            };
            targets.resize(static_cast<std::size_t>(PX3SynthAudioProcessor::kMacroCount));

            juce::StringArray targetIds;
            for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
            {
                const auto id = targets[static_cast<std::size_t>(macro)]->getParameterID();
                targetIds.add(id);
                saver.toggleMacroDestination(macro, id);
                saver.getMacroParam(macro).setValueNotifyingHost(
                    0.10f + 0.15f * static_cast<float>(macro));
            }

            auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                     .getChildFile("px3-component-tests");
            tempDirectory.createDirectory();
            const auto presetFile = tempDirectory.getChildFile("macros.px3preset");
            presetFile.deleteFile();

            PresetManager manager(saver);
            juce::String error;
            PresetManager::PresetMetadata metadata;
            metadata.name = "Macros";
            metadata.category = "Test";
            metadata.author = "component tests";
            const auto wrote = manager.dumpCurrentStateToPresetFile(presetFile, metadata, true,
                                                                    true, error, nullptr);

            PX3SynthAudioProcessor loader;
            prepared(loader);
            PresetManager loaderManager(loader);
            juce::String loadError;
            const auto read = wrote && loaderManager.loadPresetFile(presetFile, loadError);

            juce::StringArray report;
            auto allCorrect = read;
            for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
            {
                const auto mask = loader.getMacroMaskForParameter(targetIds[macro]);
                const auto value = loader.getMacroParam(macro).get();
                const auto wantValue = 0.10f + 0.15f * static_cast<float>(macro);

                allCorrect = allCorrect && mask == (1 << macro)
                             && std::abs(value - wantValue) < 0.01f;
                report.add("M" + juce::String(macro + 1) + " mask " + juce::String(mask)
                           + " at " + fmt(value, 2));
            }

            check("Macro_EveryMacroSurvivesAPresetFile",
                  allCorrect,
                  read ? "after a preset file round trip: " + report.joinIntoString(", ")
                       : "the preset did not round-trip: " + error + loadError);

            presetFile.deleteFile();
        }

        // Loading a preset must not take the MIDI mapping of a macro away:
        // the preset says what macro 1 DOES, the instance says what moves it.
        juce::AudioBuffer<float> buffer(2, kBlock);
        loaded.setMidiLearnTargets({ PX3SynthAudioProcessor::macroParameterId(0) });
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 31, 64), 0);
        buffer.clear();
        loaded.processBlock(buffer, midi);
        loaded.applyPendingMidiMappings();

        auto bare = processor.createPresetStateTree();
        bare.removeChild(bare.getChildWithName(px3::processor_internal::kMidiMappingsId), nullptr);
        loaded.applyParameterStateTree(bare, &error, false);

        check("Macro_APresetLoadKeepsTheMidiMappingOfAMacro",
              loaded.getMidiCcForParameter(PX3SynthAudioProcessor::macroParameterId(0)) == 31,
              "after a preset load macro 1 is still on CC "
                  + juce::String(loaded.getMidiCcForParameter(
                        PX3SynthAudioProcessor::macroParameterId(0))));
    }

    // ---- state that is missing, unknown or malformed ------------------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::String error;

        // Nothing at all: what a project written before macros existed says.
        PX3SynthAudioProcessor older;
        prepared(older);
        older.toggleMacroDestination(0, older.getFilterCutoffParam(0).getParameterID());

        auto legacy = processor.createParameterStateTree();
        legacy.removeChild(legacy.getChildWithName(px3::processor_internal::kMacroRoutesId), nullptr);
        const auto legacyApplied = older.applyParameterStateTree(legacy, &error, true);

        auto empty = true;
        for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
        {
            empty = empty && older.getMacroDestinations(macro).empty();
        }

        check("Macro_StateWithoutMacrosLoadsToFourEmptyMacros",
              legacyApplied && empty,
              legacyApplied ? "an older project leaves every macro empty"
                            : "an older project failed to load: " + error);

        // A destination naming something that does not exist, and a macro
        // index out of range.
        processor.toggleMacroDestination(1, processor.getReverbAmountParam().getParameterID());
        auto tree = processor.createParameterStateTree();
        auto routes = tree.getChildWithName(px3::processor_internal::kMacroRoutesId);

        juce::ValueTree ghost(px3::processor_internal::kMacroDestId);
        ghost.setProperty(px3::processor_internal::kMacroDestParamId, "noSuchParameter", nullptr);
        routes.getChild(0).appendChild(ghost, nullptr);

        juce::ValueTree impossible(px3::processor_internal::kMacroEntryId);
        impossible.setProperty(px3::processor_internal::kMacroIndexId, 97, nullptr);
        routes.appendChild(impossible, nullptr);

        PX3SynthAudioProcessor restored;
        prepared(restored);
        const auto applied = restored.applyParameterStateTree(tree, &error, true);

        check("Macro_MalformedStateDegradesRatherThanFails",
              applied && restored.getMacroDestinations(1).size() == 1
                  && restored.isMacroDestination(1, processor.getReverbAmountParam().getParameterID()),
              "state applied " + juce::String(applied ? "cleanly" : "with an error")
                  + " keeping " + juce::String(static_cast<int>(restored.getMacroDestinations(1).size()))
                  + " real destination of 2 offered");
    }

    // ---- two instances, no leakage ------------------------------------------
    {
        PX3SynthAudioProcessor a;
        PX3SynthAudioProcessor b;
        prepared(a);
        prepared(b);

        const auto cutoffId = a.getFilterCutoffParam(0).getParameterID();
        const auto reverbId = a.getReverbAmountParam().getParameterID();

        a.toggleMacroDestination(0, cutoffId);
        a.getMacroParam(0).setValueNotifyingHost(0.9f);
        b.toggleMacroDestination(0, reverbId);
        b.getMacroParam(0).setValueNotifyingHost(0.1f);

        check("Macro_TwoInstancesKeepTheirOwnMacros",
              a.isMacroDestination(0, cutoffId) && ! a.isMacroDestination(0, reverbId)
                  && b.isMacroDestination(0, reverbId) && ! b.isMacroDestination(0, cutoffId)
                  && std::abs(a.getMacroParam(0).get() - 0.9f) < 0.01f
                  && std::abs(b.getMacroParam(0).get() - 0.1f) < 0.01f,
              "A's macro 1 is at " + fmt(a.getMacroParam(0).get(), 2) + " driving "
                  + juce::String(static_cast<int>(a.getMacroDestinations(0).size()))
                  + ", B's at " + fmt(b.getMacroParam(0).get(), 2) + " driving "
                  + juce::String(static_cast<int>(b.getMacroDestinations(0).size())));
    }

    // ========================================================================
    // Per-route depth
    // ========================================================================
    //
    // A depth belongs to a macro-and-parameter PAIR, not to either half. The
    // routing is many-to-many - one macro drives several parameters, one
    // parameter is driven by several macros - so a depth stored on the macro
    // or on the parameter would be shared by routes that have nothing to do
    // with each other.
    //
    // The field, the serialised property and the accumulator's use of it all
    // predate the editor for it. That makes the interesting question not "does
    // the model hold a number" but "does the SOUND follow it", which is what
    // the DSP tests below are for.

    const auto preparedForDepth = [](PX3SynthAudioProcessor& processor)
    {
        processor.setPlayConfigDetails(0, 2, 48000.0, 256);
        processor.prepareToPlay(48000.0, 256);
    };

    // ---- a new assignment is full depth, as it always was -------------------
    {
        PX3SynthAudioProcessor processor;
        preparedForDepth(processor);

        auto& cutoff = processor.getFilterCutoffParam(0);
        const auto cutoffId = cutoff.getParameterID();
        processor.toggleMacroDestination(0, cutoffId);

        check("MacroDepth_ANewAssignmentIsFullDepth",
              std::abs(processor.getMacroDestinationDepth(0, cutoffId) - 1.0f) < 1.0e-6f,
              "a freshly assigned route reads "
                  + fmt(processor.getMacroDestinationDepth(0, cutoffId), 3)
                  + ", so a patch made before the depth editor existed sounds the same");
    }

    // ---- one macro's routes hold separate depths ----------------------------
    {
        PX3SynthAudioProcessor processor;
        preparedForDepth(processor);

        const auto cutoffId = processor.getFilterCutoffParam(0).getParameterID();
        const auto resonanceId = processor.getFilterResonanceParam(0).getParameterID();

        processor.toggleMacroDestination(0, cutoffId);
        processor.toggleMacroDestination(0, resonanceId);
        processor.setMacroDestinationDepth(0, cutoffId, 0.75f);
        processor.setMacroDestinationDepth(0, resonanceId, 0.30f);

        check("MacroDepth_EachRouteFromOneMacroHasItsOwn",
              std::abs(processor.getMacroDestinationDepth(0, cutoffId) - 0.75f) < 1.0e-6f
                  && std::abs(processor.getMacroDestinationDepth(0, resonanceId) - 0.30f) < 1.0e-6f,
              "macro 1 drives cutoff at "
                  + fmt(processor.getMacroDestinationDepth(0, cutoffId), 2) + " and resonance at "
                  + fmt(processor.getMacroDestinationDepth(0, resonanceId), 2));
    }

    // ---- and two macros on ONE parameter do not share one ------------------
    {
        PX3SynthAudioProcessor processor;
        preparedForDepth(processor);

        const auto cutoffId = processor.getFilterCutoffParam(0).getParameterID();
        processor.toggleMacroDestination(0, cutoffId);
        processor.toggleMacroDestination(1, cutoffId);

        processor.setMacroDestinationDepth(0, cutoffId, 0.75f);
        processor.setMacroDestinationDepth(1, cutoffId, 0.40f);

        // Moving one must leave the other exactly where it was.
        processor.setMacroDestinationDepth(0, cutoffId, 0.10f);

        check("MacroDepth_TwoMacrosOnOneParameterKeepTheirOwn",
              std::abs(processor.getMacroDestinationDepth(0, cutoffId) - 0.10f) < 1.0e-6f
                  && std::abs(processor.getMacroDestinationDepth(1, cutoffId) - 0.40f) < 1.0e-6f,
              "after moving macro 1's route to "
                  + fmt(processor.getMacroDestinationDepth(0, cutoffId), 2)
                  + ", macro 2's route to the same parameter is still "
                  + fmt(processor.getMacroDestinationDepth(1, cutoffId), 2));
    }

    // ---- the DSP scales its contribution by the route's depth ---------------
    //
    // The accumulator has read this field since macros existed, but nothing
    // could ever set it to anything but 1, so "the DSP honours depth" was
    // untested by construction. It is measured here through the same accessor
    // the knob's modulation ring reads.
    //
    // The arithmetic is exact rather than approximate: a positive depth gets
    // the whole of the room above the base, so at base b, signal s and depth d
    // the effective value is b + d*(1-b)*s. Half the depth is therefore half
    // the distance travelled, and that is checked as a number rather than as
    // "it moved less".
    {
        PX3SynthAudioProcessor processor;
        preparedForDepth(processor);

        auto& cutoff = processor.getFilterCutoffParam(0);
        const auto cutoffId = cutoff.getParameterID();
        auto& parameter = static_cast<juce::RangedAudioParameter&>(cutoff);

        parameter.setValueNotifyingHost(0.20f);
        processor.toggleMacroDestination(0, cutoffId);
        processor.getMacroParam(0).setValueNotifyingHost(1.0f);

        const auto base = parameter.getValue();

        processor.setMacroDestinationDepth(0, cutoffId, 1.0f);
        const auto atFull = processor.getModulatedNormalisedValue(cutoff);

        processor.setMacroDestinationDepth(0, cutoffId, 0.5f);
        const auto atHalf = processor.getModulatedNormalisedValue(cutoff);

        processor.setMacroDestinationDepth(0, cutoffId, 0.0f);
        const auto atZero = processor.getModulatedNormalisedValue(cutoff);

        const auto fullTravel = atFull - base;
        const auto halfTravel = atHalf - base;

        check("MacroDepth_TheDspScalesItsContributionByTheRouteDepth",
              fullTravel > 0.5f
                  && std::abs(halfTravel - fullTravel * 0.5f) < 1.0e-4f
                  && std::abs(atZero - base) < 1.0e-6f,
              "from base " + fmt(base, 3) + ": full depth travels " + fmt(fullTravel, 4)
                  + ", half depth " + fmt(halfTravel, 4) + " (half of full is "
                  + fmt(fullTravel * 0.5f, 4) + "), zero depth " + fmt(atZero - base, 6));
    }

    // ---- several routes from one macro, each scaled its own way -------------
    //
    // The case the brief describes: one macro moving three parameters by
    // different amounts. A single global depth applied to every destination
    // would pass every test above and fail this one.
    {
        PX3SynthAudioProcessor processor;
        preparedForDepth(processor);

        auto& cutoff = processor.getFilterCutoffParam(0);
        auto& resonance = processor.getFilterResonanceParam(0);
        auto& reverbMix = processor.getReverbAmountParam();

        struct Route { juce::RangedAudioParameter& parameter; float depth; float travel; };
        std::array<Route, 3> routes { { { cutoff, 1.00f, 0.0f },
                                        { resonance, 0.25f, 0.0f },
                                        { reverbMix, 0.10f, 0.0f } } };

        for (auto& route : routes)
        {
            route.parameter.setValueNotifyingHost(0.0f);
            processor.toggleMacroDestination(0, route.parameter.getParameterID());
            processor.setMacroDestinationDepth(0, route.parameter.getParameterID(), route.depth);
        }
        processor.getMacroParam(0).setValueNotifyingHost(1.0f);

        juce::StringArray detail;
        auto allCorrect = true;
        for (auto& route : routes)
        {
            const auto base = route.parameter.getValue();
            route.travel = processor.getModulatedNormalisedValue(route.parameter) - base;
            // base 0, positive depth: travel is exactly depth * (1 - 0) * 1.
            if (std::abs(route.travel - route.depth) > 1.0e-4f) { allCorrect = false; }
            detail.add(route.parameter.getParameterID() + " depth " + fmt(route.depth, 2)
                       + " -> travel " + fmt(route.travel, 4));
        }

        check("MacroDepth_OneMacroMovesEachDestinationByItsOwnDepth",
              allCorrect,
              detail.joinIntoString(", "));
    }

    // ---- a negative depth inverts rather than doing nothing -----------------
    {
        PX3SynthAudioProcessor processor;
        preparedForDepth(processor);

        auto& cutoff = processor.getFilterCutoffParam(0);
        auto& parameter = static_cast<juce::RangedAudioParameter&>(cutoff);
        parameter.setValueNotifyingHost(0.60f);

        processor.toggleMacroDestination(0, cutoff.getParameterID());
        processor.getMacroParam(0).setValueNotifyingHost(1.0f);
        processor.setMacroDestinationDepth(0, cutoff.getParameterID(), -0.5f);

        const auto base = parameter.getValue();
        const auto effective = processor.getModulatedNormalisedValue(cutoff);

        // Negative depth takes the room BELOW the base: base - 0.5 * base.
        check("MacroDepth_ANegativeDepthInverts",
              effective < base && std::abs(effective - (base - 0.5f * base)) < 1.0e-4f,
              "from base " + fmt(base, 3) + " a depth of -0.50 reached " + fmt(effective, 4)
                  + ", expected " + fmt(base - 0.5f * base, 4));
    }

    // ---- depths survive a save and a reload ---------------------------------
    {
        PX3SynthAudioProcessor source;
        preparedForDepth(source);

        const auto cutoffId = source.getFilterCutoffParam(0).getParameterID();
        const auto resonanceId = source.getFilterResonanceParam(0).getParameterID();

        source.toggleMacroDestination(0, cutoffId);
        source.toggleMacroDestination(0, resonanceId);
        source.toggleMacroDestination(1, cutoffId);
        source.setMacroDestinationDepth(0, cutoffId, 0.75f);
        source.setMacroDestinationDepth(0, resonanceId, 0.30f);
        source.setMacroDestinationDepth(1, cutoffId, -0.40f);

        juce::MemoryBlock state;
        source.getStateInformation(state);

        PX3SynthAudioProcessor reopened;
        preparedForDepth(reopened);
        reopened.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        check("MacroDepth_SurvivesASaveAndReload",
              std::abs(reopened.getMacroDestinationDepth(0, cutoffId) - 0.75f) < 1.0e-4f
                  && std::abs(reopened.getMacroDestinationDepth(0, resonanceId) - 0.30f) < 1.0e-4f
                  && std::abs(reopened.getMacroDestinationDepth(1, cutoffId) + 0.40f) < 1.0e-4f,
              "reloaded macro 1 -> cutoff "
                  + fmt(reopened.getMacroDestinationDepth(0, cutoffId), 3) + ", macro 1 -> resonance "
                  + fmt(reopened.getMacroDestinationDepth(0, resonanceId), 3) + ", macro 2 -> cutoff "
                  + fmt(reopened.getMacroDestinationDepth(1, cutoffId), 3));
    }

    // ---- a state written before depths were editable loads at full ----------
    //
    // Every macro route in an existing preset was written at 1.0, so this is
    // really asking whether a route with NO depth property still arrives at
    // full - which is what a state from an older build would look like if the
    // property had not been written at all.
    {
        PX3SynthAudioProcessor source;
        preparedForDepth(source);
        const auto cutoffId = source.getFilterCutoffParam(0).getParameterID();
        source.toggleMacroDestination(0, cutoffId);

        juce::MemoryBlock block;
        source.getStateInformation(block);

        // State travels as XML inside JUCE's binary wrapper, so the property is
        // removed from the XML rather than from a ValueTree read of the bytes.
        auto stripped = 0;
        std::unique_ptr<juce::XmlElement> xml(
            juce::AudioProcessor::getXmlFromBinary(block.getData(),
                                                   static_cast<int>(block.getSize())));

        if (xml != nullptr)
        {
            if (auto* routes = xml->getChildByName(px3::processor_internal::kMacroRoutesId.toString()))
            {
                for (auto* node : routes->getChildIterator())
                {
                    for (auto* dest : node->getChildIterator())
                    {
                        const auto attribute
                            = px3::processor_internal::kMacroDestDepthId.toString();
                        if (dest->hasAttribute(attribute))
                        {
                            dest->removeAttribute(attribute);
                            ++stripped;
                        }
                    }
                }
            }
        }

        juce::MemoryBlock stripped_block;
        if (xml != nullptr) { juce::AudioProcessor::copyXmlToBinary(*xml, stripped_block); }

        PX3SynthAudioProcessor reopened;
        preparedForDepth(reopened);
        reopened.setStateInformation(stripped_block.getData(),
                                     static_cast<int>(stripped_block.getSize()));

        check("MacroDepth_ARouteWithNoStoredDepthLoadsAtFull",
              stripped == 1 && reopened.isMacroDestination(0, cutoffId)
                  && std::abs(reopened.getMacroDestinationDepth(0, cutoffId) - 1.0f) < 1.0e-6f,
              juce::String(stripped) + " depth property removed; the route reloaded at "
                  + fmt(reopened.getMacroDestinationDepth(0, cutoffId), 3));
    }
}

// MIDI parameter mapping. See docs/midi-mapping-design.md.
void testMidiMapping()
{
    suite("MIDI MAPPING");

    constexpr double kRate = 48000.0;
    constexpr int kBlock = 256;

    // ---- what counts as a mappable knob ------------------------------------
    //
    // Eligibility is a property of the control, not a list: every slider bound
    // through attachParameterKnob carries its parameter's ID, and that is what
    // makes it mappable. This counts them, so a future knob bound the old way
    // shows up as a number that went down rather than as a knob that silently
    // will not map.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kRate, kBlock);
        processor.prepareToPlay(kRate, kBlock);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        if (editor != nullptr)
        {
            auto sliders = 0;
            auto mappable = 0;
            juce::StringArray unknownIds;

            std::function<void(juce::Component&)> walk = [&](juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* slider = dynamic_cast<juce::Slider*>(child))
                    {
                        ++sliders;
                        const auto id = px3::ui::parameterIdOf(*slider);
                        if (id.isNotEmpty())
                        {
                            ++mappable;
                            if (processor.getMidiCcForParameter(id) == -2) { unknownIds.add(id); }
                        }
                    }
                    walk(*child);
                }
            };
            walk(*editor);

            check("MidiMap_KnobsCarryTheirParameterIdSoNoListIsNeeded",
                  mappable >= 40 && unknownIds.isEmpty(),
                  juce::String(mappable) + " of " + juce::String(sliders)
                      + " sliders in the editor carry a parameter ID");
        }
    }

    // A controller message, as a hardware knob would send it.
    const auto ccMessage = [](int cc, int value, int channel = 1)
    {
        return juce::MidiMessage::controllerEvent(channel, cc, value);
    };

    // One block carrying MIDI, then the message-thread pump the processor's
    // own timer would call. Split out because a test has no message loop.
    const auto sendAndApply = [&](PX3SynthAudioProcessor& processor,
                                  juce::AudioBuffer<float>& buffer,
                                  const juce::MidiMessage& message)
    {
        juce::MidiBuffer midi;
        midi.addEvent(message, 0);
        buffer.clear();
        processor.processBlock(buffer, midi);
        processor.applyPendingMidiMappings();
    };

    const auto prepared = [](PX3SynthAudioProcessor& processor)
    {
        processor.setPlayConfigDetails(0, 2, kRate, kBlock);
        processor.prepareToPlay(kRate, kBlock);
    };

    // ---- what a mapped knob looks like --------------------------------------
    //
    // The label and the selection ring come from the shared rotary
    // look-and-feel, so this renders one knob through it and looks at the
    // pixels rather than trusting that the code was reached.
    {
        PX3SynthAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            const auto render = [&](int cc, bool selected)
            {
                juce::Slider knob;
                knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                knob.setLookAndFeel(editor->debugKnobLookAndFeel());
                knob.setSize(64, 64);
                knob.setValue(0.5, juce::dontSendNotification);
                knob.getProperties().set(px3::knob_properties::midiCc, cc);
                knob.getProperties().set(px3::knob_properties::midiSelected, selected);

                const auto image = knob.createComponentSnapshot(knob.getLocalBounds());
                knob.setLookAndFeel(nullptr);
                return image;
            };

            const auto plain = render(-1, false);
            const auto mapped = render(21, false);
            const auto chosen = render(-1, true);

            const auto differs = [](const juce::Image& a, const juce::Image& b)
            {
                auto changed = 0;
                for (int y = 0; y < a.getHeight(); ++y)
                {
                    for (int x = 0; x < a.getWidth(); ++x)
                    {
                        if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) { ++changed; }
                    }
                }
                return changed;
            };

            const auto labelPixels = differs(plain, mapped);
            const auto ringPixels = differs(plain, chosen);

            check("MidiMapUi_AMappedKnobDrawsItsCcAndASelectedOneItsRing",
                  plain.isValid() && labelPixels > 20 && ringPixels > 20,
                  "the CC label changes " + juce::String(labelPixels)
                      + " pixels and the selection ring " + juce::String(ringPixels)
                      + " against an unmapped knob");
        }
    }

    // ---- the interaction, through the real editor ---------------------------
    //
    // Shift-click knobs, move a control, and the assignment lands. Driven
    // through the editor's own components rather than through the processor's
    // API, because the gesture is the feature.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kRate, kBlock);
        processor.prepareToPlay(kRate, kBlock);
        juce::AudioBuffer<float> buffer(2, kBlock);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            editor->debugRefreshMidiMappingUI();

            std::vector<juce::Slider*> knobs;
            std::function<void(juce::Component&)> walk = [&](juce::Component& parent)
            {
                for (auto* child : parent.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* slider = dynamic_cast<juce::Slider*>(child))
                    {
                        if (px3::ui::isParameterKnob(*slider)) { knobs.push_back(slider); }
                    }
                    walk(*child);
                }
            };
            walk(*editor);

            const auto clickOn = [&](juce::Slider& slider, bool shift)
            {
                editor->debugSimulateKnobClick(slider, shift);
            };

            const auto ccOf = [&](const juce::Slider& slider)
            {
                return static_cast<int>(
                    slider.getProperties().getWithDefault(px3::knob_properties::midiCc, -1));
            };
            const auto isSelected = [&](const juce::Slider& slider)
            {
                return static_cast<bool>(
                    slider.getProperties().getWithDefault(px3::knob_properties::midiSelected, false));
            };

            // Every mappable knob has the editor listening on it. This is the
            // wiring a headless test cannot drive through JUCE's own dispatch,
            // so it is asserted by count: a knob the editor never registered
            // is a knob shift-click could never reach.
            check("MidiMapUi_TheEditorListensOnEveryMappableKnob",
                  knobs.size() >= 3
                      && editor->debugRegisteredKnobCount() == static_cast<int>(knobs.size()),
                  juce::String(editor->debugRegisteredKnobCount()) + " listeners registered across "
                      + juce::String(static_cast<int>(knobs.size())) + " mappable knobs");

            if (knobs.size() >= 3)
            {
                auto& first = *knobs[0];
                auto& second = *knobs[1];
                auto& third = *knobs[2];

                // A PLAIN click must change nothing about mapping: ordinary
                // knob use has to be exactly what it was.
                clickOn(first, false);
                editor->debugRefreshMidiMappingUI();
                check("MidiMapUi_APlainClickDoesNotSelectAnything",
                      editor->debugMidiSelection().isEmpty()
                          && editor->debugKeyboardNotice().isEmpty(),
                      "a plain click left "
                          + juce::String(editor->debugMidiSelection().size())
                          + " knobs selected");

                // Shift-click enters Select Mode and says so on the keyboard.
                clickOn(first, true);
                editor->debugRefreshMidiMappingUI();
                check("MidiMapUi_ShiftClickEntersSelectModeAndSaysSo",
                      editor->debugMidiSelection().size() == 1 && isSelected(first)
                          && editor->debugKeyboardNotice().containsIgnoreCase("MIDI"),
                      "selection of " + juce::String(editor->debugMidiSelection().size())
                          + " with the keyboard reading \"" + editor->debugKeyboardNotice() + "\"");

                // More knobs join, from wherever they are in the UI.
                clickOn(second, true);
                clickOn(third, true);
                editor->debugRefreshMidiMappingUI();
                check("MidiMapUi_MoreKnobsJoinTheSelection",
                      editor->debugMidiSelection().size() == 3
                          && isSelected(second) && isSelected(third),
                      juce::String(editor->debugMidiSelection().size()) + " knobs selected");

                // Shift-clicking a selected knob takes it back out.
                clickOn(third, true);
                editor->debugRefreshMidiMappingUI();
                check("MidiMapUi_ShiftClickingASelectedKnobRemovesIt",
                      editor->debugMidiSelection().size() == 2 && ! isSelected(third),
                      juce::String(editor->debugMidiSelection().size())
                          + " left after clicking one twice");

                // Move a control: everything selected lands on that CC, Select
                // Mode ends, and the knobs say what they are on.
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 41, 70), 0);
                buffer.clear();
                processor.processBlock(buffer, midi);
                processor.applyPendingMidiMappings();
                editor->debugRefreshMidiMappingUI();

                check("MidiMapUi_MovingAControlAssignsEverythingSelected",
                      ccOf(first) == 41 && ccOf(second) == 41
                          && editor->debugMidiSelection().isEmpty()
                          && editor->debugKeyboardNotice().isEmpty(),
                      "the two selected knobs read CC " + juce::String(ccOf(first)) + " and CC "
                          + juce::String(ccOf(second)) + ", with "
                          + juce::String(editor->debugMidiSelection().size())
                          + " still selected");

                check("MidiMapUi_AnUnselectedKnobIsLeftUnmapped",
                      ccOf(third) == -1,
                      "the knob taken back out of the selection reads CC "
                          + juce::String(ccOf(third)));

                // The physical control moves the UI, through the parameter and
                // its own attachment - there is no second value being drawn.
                const auto before = first.getValue();
                juce::MidiBuffer sweep;
                sweep.addEvent(juce::MidiMessage::controllerEvent(1, 41, 127), 0);
                buffer.clear();
                processor.processBlock(buffer, sweep);
                processor.applyPendingMidiMappings();

                check("MidiMapUi_TheControlMovesTheKnobThroughTheParameter",
                      std::abs(first.getValue() - before) > 1.0e-6,
                      "the knob moved from " + fmt(before, 4) + " to " + fmt(first.getValue(), 4)
                          + " when the controller swept");

                // Shift-clicking a mapped knob drops its assignment and selects
                // it, ready for a new one.
                clickOn(first, true);
                editor->debugRefreshMidiMappingUI();
                check("MidiMapUi_ShiftClickingAMappedKnobClearsAndSelectsIt",
                      ccOf(first) == -1 && isSelected(first)
                          && editor->debugMidiSelection().size() == 1
                          && ccOf(second) == 41,
                      "the clicked knob reads CC " + juce::String(ccOf(first))
                          + " and is " + (isSelected(first) ? "selected" : "not selected")
                          + ", while its neighbour keeps CC " + juce::String(ccOf(second)));

                // Escape leaves Select Mode with nothing assigned.
                editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
                editor->debugRefreshMidiMappingUI();
                check("MidiMapUi_EscapeLeavesSelectModeWithoutAssigning",
                      editor->debugMidiSelection().isEmpty() && ccOf(first) == -1
                          && editor->debugKeyboardNotice().isEmpty()
                          && ! processor.isMidiLearnArmed(),
                      "after Escape the selection holds "
                          + juce::String(editor->debugMidiSelection().size())
                          + " and learn is "
                          + (processor.isMidiLearnArmed() ? "still armed" : "disarmed"));
            }
        }
    }

    // ---- learning one parameter, and then several --------------------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        const auto cutoffId = processor.getFilterCutoffParam(0).getParameterID();
        const auto resonanceId = processor.getFilterResonanceParam(0).getParameterID();
        const auto reverbId = processor.getReverbAmountParam().getParameterID();

        processor.setMidiLearnTargets({ cutoffId });
        sendAndApply(processor, buffer, ccMessage(21, 64));

        check("MidiMap_ASingleParameterLearnsTheCcThatMoved",
              processor.getMidiCcForParameter(cutoffId) == 21
                  && ! processor.isMidiLearnArmed(),
              "cutoff is on CC " + juce::String(processor.getMidiCcForParameter(cutoffId))
                  + " and learn is "
                  + (processor.isMidiLearnArmed() ? "still armed" : "disarmed"));

        // Three at once, which is the interaction the whole feature is for.
        processor.setMidiLearnTargets({ cutoffId, resonanceId, reverbId });
        sendAndApply(processor, buffer, ccMessage(22, 40));

        check("MidiMap_EverySelectedParameterLandsOnOneCc",
              processor.getMidiCcForParameter(cutoffId) == 22
                  && processor.getMidiCcForParameter(resonanceId) == 22
                  && processor.getMidiCcForParameter(reverbId) == 22,
              "cutoff " + juce::String(processor.getMidiCcForParameter(cutoffId))
                  + ", resonance " + juce::String(processor.getMidiCcForParameter(resonanceId))
                  + ", reverb " + juce::String(processor.getMidiCcForParameter(reverbId)));

        // And the cutoff left CC 21 when it joined CC 22: one parameter, one
        // CC, so "what drives this knob" always has a single answer.
        auto onTwentyOne = 0;
        for (const auto& mapping : processor.getMidiMappings())
        {
            if (mapping.ccNumber == 21) { onTwentyOne = mapping.parameterIds.size(); }
        }
        check("MidiMap_ReassigningLeavesTheOldCcBehind",
              onTwentyOne == 0,
              juce::String(onTwentyOne) + " destinations left on CC 21");
    }

    // ---- the controller moves the parameter, in each destination's units ----
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        auto& cutoff = processor.getFilterCutoffParam(0);
        auto& resonance = processor.getFilterResonanceParam(0);

        processor.setMidiLearnTargets({ cutoff.getParameterID(), resonance.getParameterID() });
        sendAndApply(processor, buffer, ccMessage(21, 0));

        sendAndApply(processor, buffer, ccMessage(21, 127));
        const auto cutoffHigh = cutoff.get();
        const auto resonanceHigh = resonance.get();

        sendAndApply(processor, buffer, ccMessage(21, 0));
        const auto cutoffLow = cutoff.get();
        const auto resonanceLow = resonance.get();

        // Full travel in each destination's OWN range, not one shared number.
        const auto& cutoffRange = cutoff.getNormalisableRange();
        const auto& resonanceRange = resonance.getNormalisableRange();

        check("MidiMap_OneCcSweepsEachDestinationThroughItsOwnRange",
              std::abs(cutoffHigh - cutoffRange.end) < cutoffRange.end * 0.01f
                  && std::abs(cutoffLow - cutoffRange.start) < 1.0f
                  && std::abs(resonanceHigh - resonanceRange.end) < 0.01f
                  && std::abs(resonanceLow - resonanceRange.start) < 0.01f,
              "cutoff swept " + fmt(cutoffLow, 1) + " -> " + fmt(cutoffHigh, 1)
                  + " Hz and resonance " + fmt(resonanceLow, 3) + " -> "
                  + fmt(resonanceHigh, 3));
    }

    // ---- two CCs at once, and note input untouched --------------------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        auto& cutoff = processor.getFilterCutoffParam(0);
        auto& reverb = processor.getReverbAmountParam();

        processor.setMidiLearnTargets({ cutoff.getParameterID() });
        sendAndApply(processor, buffer, ccMessage(21, 10));
        processor.setMidiLearnTargets({ reverb.getParameterID() });
        sendAndApply(processor, buffer, ccMessage(22, 10));

        sendAndApply(processor, buffer, ccMessage(21, 100));
        const auto cutoffAfter = cutoff.get();
        const auto reverbBefore = reverb.get();
        sendAndApply(processor, buffer, ccMessage(22, 120));

        check("MidiMap_TwoCcsDriveTheirOwnDestinations",
              processor.getMidiCcForParameter(cutoff.getParameterID()) == 21
                  && processor.getMidiCcForParameter(reverb.getParameterID()) == 22
                  && cutoffAfter > 1000.0f && reverb.get() > reverbBefore,
              "CC 21 took the cutoff to " + fmt(cutoffAfter, 0) + " Hz and CC 22 took the reverb to "
                  + fmt(reverb.get(), 3));

        // A note still plays while mappings exist: the CC scan reads the
        // buffer and consumes nothing.
        juce::MidiBuffer notes;
        notes.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        buffer.clear();
        processor.processBlock(buffer, notes);

        auto sounded = false;
        juce::MidiBuffer empty;
        for (int b = 0; b < 20 && ! sounded; ++b)
        {
            buffer.clear();
            processor.processBlock(buffer, empty);
            sounded = buffer.getMagnitude(0, buffer.getNumSamples()) > 1.0e-4f;
        }

        check("MidiMap_NoteInputStillWorksAlongsideMappings",
              sounded,
              sounded ? "a note still sounds with two CCs mapped"
                      : "the synth went silent once mappings existed");
    }

    // ---- the change reaches the AUDIO, not just the parameter ---------------
    //
    // A mapping that moved a number and nothing else would pass every test
    // above. This one listens.
    {
        const auto renderWithCc = [&](int ccValue)
        {
            PX3SynthAudioProcessor processor;
            prepared(processor);
            setParam(processor, "filter1Enabled", 1.0f);
            setChoice(processor, "filter1Type", 0);           // low pass
            setParam(processor, "filter1Resonance", 0.2f);

            juce::AudioBuffer<float> buffer(2, kBlock);

            processor.setMidiLearnTargets({ processor.getFilterCutoffParam(0).getParameterID() });
            sendAndApply(processor, buffer, ccMessage(30, 64));
            sendAndApply(processor, buffer, ccMessage(30, ccValue));

            juce::MidiBuffer notes;
            notes.addEvent(juce::MidiMessage::noteOn(1, 45, 1.0f), 0);
            buffer.clear();
            processor.processBlock(buffer, notes);

            // Settle, then measure how much high-frequency energy survives the
            // filter - a first difference is a crude but honest high-pass.
            juce::MidiBuffer empty;
            for (int b = 0; b < 20; ++b)
            {
                buffer.clear();
                processor.processBlock(buffer, empty);
            }

            auto brightness = 0.0;
            auto previous = 0.0f;
            for (int b = 0; b < 20; ++b)
            {
                buffer.clear();
                processor.processBlock(buffer, empty);
                const auto* data = buffer.getReadPointer(0);
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    brightness += std::abs(data[i] - previous);
                    previous = data[i];
                }
            }

            return brightness;
        };

        const auto closed = renderWithCc(0);
        const auto open = renderWithCc(127);

        check("MidiMap_TheChangeReachesTheAudioNotJustTheParameter",
              open > closed * 2.0,
              "a note rendered with the mapped cutoff CC at 0 carries " + fmt(closed, 1)
                  + " of high-frequency energy against " + fmt(open, 1) + " at 127");
    }

    // ---- clearing ----------------------------------------------------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        const auto cutoffId = processor.getFilterCutoffParam(0).getParameterID();
        const auto resonanceId = processor.getFilterResonanceParam(0).getParameterID();

        processor.setMidiLearnTargets({ cutoffId, resonanceId });
        sendAndApply(processor, buffer, ccMessage(21, 64));

        // What a shift-click on a mapped knob does.
        processor.clearMidiMappingForParameter(cutoffId);

        check("MidiMap_ClearingOneParameterLeavesTheOthersMapped",
              processor.getMidiCcForParameter(cutoffId) == -1
                  && processor.getMidiCcForParameter(resonanceId) == 21,
              "cutoff reads " + juce::String(processor.getMidiCcForParameter(cutoffId))
                  + " and resonance reads "
                  + juce::String(processor.getMidiCcForParameter(resonanceId)));

        processor.clearMidiMappingForParameter(resonanceId);
        check("MidiMap_TheLastDestinationLeavingRemovesTheMapping",
              processor.getMidiMappings().empty(),
              juce::String(static_cast<int>(processor.getMidiMappings().size()))
                  + " mappings left once nothing points at CC 21");
    }

    // ---- two instances, one CC, no leakage ----------------------------------
    {
        PX3SynthAudioProcessor instanceA;
        PX3SynthAudioProcessor instanceB;
        prepared(instanceA);
        prepared(instanceB);
        juce::AudioBuffer<float> bufferA(2, kBlock);
        juce::AudioBuffer<float> bufferB(2, kBlock);

        const auto cutoffId = instanceA.getFilterCutoffParam(0).getParameterID();
        const auto reverbId = instanceB.getReverbAmountParam().getParameterID();

        instanceA.setMidiLearnTargets({ cutoffId });
        sendAndApply(instanceA, bufferA, ccMessage(21, 64));
        instanceB.setMidiLearnTargets({ reverbId });
        sendAndApply(instanceB, bufferB, ccMessage(21, 64));

        check("MidiMap_TwoInstancesKeepSeparateMappingsForOneCc",
              instanceA.getMidiCcForParameter(cutoffId) == 21
                  && instanceA.getMidiCcForParameter(reverbId) == -1
                  && instanceB.getMidiCcForParameter(reverbId) == 21
                  && instanceB.getMidiCcForParameter(cutoffId) == -1,
              "A maps cutoff=" + juce::String(instanceA.getMidiCcForParameter(cutoffId))
                  + " reverb=" + juce::String(instanceA.getMidiCcForParameter(reverbId))
                  + "; B maps cutoff=" + juce::String(instanceB.getMidiCcForParameter(cutoffId))
                  + " reverb=" + juce::String(instanceB.getMidiCcForParameter(reverbId)));

        // And driving one does not move the other.
        const auto reverbInABefore = instanceA.getReverbAmountParam().get();
        sendAndApply(instanceB, bufferB, ccMessage(21, 127));
        check("MidiMap_DrivingOneInstanceDoesNotMoveTheOther",
              std::abs(instanceA.getReverbAmountParam().get() - reverbInABefore) < 1.0e-6f,
              "A's reverb stayed at " + fmt(instanceA.getReverbAmountParam().get(), 4)
                  + " while B's CC 21 swept");
    }

    // ---- persistence, and the preset boundary -------------------------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        const auto cutoffId = processor.getFilterCutoffParam(0).getParameterID();
        const auto resonanceId = processor.getFilterResonanceParam(0).getParameterID();

        processor.setMidiLearnTargets({ cutoffId, resonanceId });
        sendAndApply(processor, buffer, ccMessage(21, 64));

        // A DAW project round trip.
        juce::MemoryBlock saved;
        processor.getStateInformation(saved);

        PX3SynthAudioProcessor reopened;
        prepared(reopened);
        reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

        check("MidiMap_MappingsSurviveASessionRoundTrip",
              reopened.getMidiCcForParameter(cutoffId) == 21
                  && reopened.getMidiCcForParameter(resonanceId) == 21,
              "after reload cutoff reads " + juce::String(reopened.getMidiCcForParameter(cutoffId))
                  + " and resonance reads "
                  + juce::String(reopened.getMidiCcForParameter(resonanceId)));

        // A preset carries the hardware layout the sound was designed around.
        const auto presetTree = processor.createPresetStateTree();
        check("MidiMap_APresetCarriesItsMappings",
              presetTree.getChildWithName(px3::processor_internal::kMidiMappingsId).isValid(),
              presetTree.getChildWithName(px3::processor_internal::kMidiMappingsId).isValid()
                  ? "the preset tree carries a midiMappings child"
                  : "no midiMappings child in the preset tree");

        // Loading it into a synth that has never seen a controller brings the
        // assignments with it.
        PX3SynthAudioProcessor loaded;
        prepared(loaded);
        juce::String error;
        loaded.applyParameterStateTree(presetTree, &error, false);

        check("MidiMap_LoadingAPresetBringsItsMappingsWithIt",
              loaded.getMidiCcForParameter(cutoffId) == 21
                  && loaded.getMidiCcForParameter(resonanceId) == 21,
              "after loading the preset cutoff reads "
                  + juce::String(loaded.getMidiCcForParameter(cutoffId))
                  + " and resonance reads "
                  + juce::String(loaded.getMidiCcForParameter(resonanceId)));

        // A preset that carries NONE leaves the controller alone rather than
        // wiping it - otherwise auditioning a factory sound would cost the
        // user every assignment they had made.
        PX3SynthAudioProcessor mapped;
        prepared(mapped);
        juce::AudioBuffer<float> mappedBuffer(2, kBlock);
        mapped.setMidiLearnTargets({ cutoffId });
        sendAndApply(mapped, mappedBuffer, ccMessage(30, 64));

        auto bare = processor.createPresetStateTree();
        bare.removeChild(bare.getChildWithName(px3::processor_internal::kMidiMappingsId), nullptr);
        mapped.applyParameterStateTree(bare, &error, false);

        check("MidiMap_APresetWithNoMappingsLeavesYoursAlone",
              mapped.getMidiCcForParameter(cutoffId) == 30,
              "after a preset carrying no mappings, the cutoff still reads CC "
                  + juce::String(mapped.getMidiCcForParameter(cutoffId)));

        // A DAW session, by contrast, IS the whole truth: one saved with no
        // mappings restores none.
        PX3SynthAudioProcessor session;
        prepared(session);
        juce::AudioBuffer<float> sessionBuffer(2, kBlock);
        session.setMidiLearnTargets({ cutoffId });
        sendAndApply(session, sessionBuffer, ccMessage(30, 64));
        session.applyParameterStateTree(bare, &error, true);

        check("MidiMap_ASessionWithNoMappingsRestoresNone",
              session.getMidiMappings().empty(),
              juce::String(static_cast<int>(session.getMidiMappings().size()))
                  + " mappings survived restoring a session that had none");
    }

    // ---- through the preset MANAGER, to a real file and back ----------------
    //
    // The tests above exercise the state tree. This one writes an actual
    // .px3preset to disk and loads it into a synth that has never seen a
    // controller, because "saved to presets" is a claim about the file.
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        PresetManager manager(processor);
        juce::String error;
        manager.initialise(error);

        const auto cutoffId = processor.getFilterCutoffParam(0).getParameterID();
        const auto reverbId = processor.getReverbAmountParam().getParameterID();

        processor.setMidiLearnTargets({ cutoffId, reverbId });
        sendAndApply(processor, buffer, ccMessage(53, 64, 7));

        PresetManager::PresetMetadata metadata;
        metadata.name = "MidiMappingRoundTrip";
        metadata.category = "TEST";
        metadata.author = "PX3Tests";
        metadata.description = "Written by the MIDI mapping suite.";

        juce::File written;
        const auto saved = manager.saveUserPreset(metadata, true, error, &written);

        // A fresh synth, no mappings of its own, loading that file.
        PX3SynthAudioProcessor reopened;
        prepared(reopened);
        PresetManager reopenedManager(reopened);
        reopenedManager.initialise(error);
        const auto loaded = saved && reopenedManager.loadPresetFile(written, error);

        const auto restored = reopened.getMidiMappings();
        const auto channel = restored.empty() ? -1 : restored.front().learnedChannel;

        check("MidiMap_APresetFileCarriesTheAssignmentsToAnotherSynth",
              saved && loaded
                  && reopened.getMidiCcForParameter(cutoffId) == 53
                  && reopened.getMidiCcForParameter(reverbId) == 53,
              saved ? (loaded ? "after loading " + written.getFileName() + " the cutoff reads CC "
                                    + juce::String(reopened.getMidiCcForParameter(cutoffId))
                                    + " and the reverb reads CC "
                                    + juce::String(reopened.getMidiCcForParameter(reverbId))
                              : "the preset failed to load: " + error)
                    : "the preset failed to save: " + error);

        // The channel it was taught on rides along, for the strict matching
        // the design leaves the door open to.
        check("MidiMap_ThePresetRemembersTheChannelItLearnedOn",
              channel == 7,
              "the restored mapping was learned on channel " + juce::String(channel));

        if (written.existsAsFile()) { written.deleteFile(); }
    }

    // ---- state naming a parameter that does not exist -----------------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        const auto cutoffId = processor.getFilterCutoffParam(0).getParameterID();
        processor.setMidiLearnTargets({ cutoffId });
        sendAndApply(processor, buffer, ccMessage(21, 64));

        auto tree = processor.createParameterStateTree();
        auto mappings = tree.getChildWithName(px3::processor_internal::kMidiMappingsId);
        auto mapping = mappings.getChild(0);

        juce::ValueTree ghost(px3::processor_internal::kMidiDestinationId);
        ghost.setProperty(px3::processor_internal::kMidiParameterId,
                          "aParameterThatDoesNotExist", nullptr);
        mapping.appendChild(ghost, nullptr);

        PX3SynthAudioProcessor reopened;
        prepared(reopened);
        juce::String error;
        const auto applied = reopened.applyParameterStateTree(tree, &error, true);

        const auto restored = reopened.getMidiMappings();
        const auto destinations = restored.empty() ? 0 : restored.front().parameterIds.size();

        check("MidiMap_AnUnknownParameterIdIsDroppedNotFatal",
              applied && destinations == 1
                  && reopened.getMidiCcForParameter(cutoffId) == 21,
              "state applied " + juce::String(applied ? "cleanly" : "with an error")
                  + " and the mapping kept " + juce::String(destinations)
                  + " of 2 destinations");
    }

    // ---- doing nothing when nothing is mapped -------------------------------
    {
        PX3SynthAudioProcessor mapped;
        PX3SynthAudioProcessor untouched;
        prepared(mapped);
        prepared(untouched);

        juce::AudioBuffer<float> a(2, kBlock);
        juce::AudioBuffer<float> b(2, kBlock);

        // The same CC into both: one has learned nothing and must not move.
        const auto cutoffBefore = untouched.getFilterCutoffParam(0).get();
        sendAndApply(untouched, b, ccMessage(21, 127));

        mapped.setMidiLearnTargets({ mapped.getFilterCutoffParam(0).getParameterID() });
        sendAndApply(mapped, a, ccMessage(21, 127));
        sendAndApply(mapped, a, ccMessage(21, 20));

        check("MidiMap_AnUnmappedSynthIgnoresControllers",
              std::abs(untouched.getFilterCutoffParam(0).get() - cutoffBefore) < 1.0e-6f
                  && untouched.getMidiMappings().empty(),
              "an unmapped synth's cutoff stayed at "
                  + fmt(untouched.getFilterCutoffParam(0).get(), 1) + " Hz through a full CC sweep");

        // A CC arriving with nothing selected never learns.
        check("MidiMap_ACcArrivingWithNoSelectionNeverLearns",
              untouched.getMidiMappings().empty() && ! untouched.isMidiLearnArmed(),
              juce::String(static_cast<int>(untouched.getMidiMappings().size()))
                  + " mappings created by a controller nobody asked to learn");
    }

    // ---- the CC that taught a mapping does not also jump it ------------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        auto& cutoff = processor.getFilterCutoffParam(0);
        const auto before = cutoff.get();

        processor.setMidiLearnTargets({ cutoff.getParameterID() });
        sendAndApply(processor, buffer, ccMessage(21, 127));

        check("MidiMap_TheTeachingMoveDoesNotAlsoJumpTheKnob",
              std::abs(cutoff.get() - before) < 1.0e-6f,
              "the cutoff stayed at " + fmt(cutoff.get(), 1)
                  + " Hz when the controller taught the mapping, rather than jumping to "
                  + fmt(cutoff.getNormalisableRange().end, 1));

        // The NEXT movement drives it.
        sendAndApply(processor, buffer, ccMessage(21, 20));
        check("MidiMap_TheNextMoveDrivesIt",
              std::abs(cutoff.get() - before) > 1.0f,
              "the cutoff moved to " + fmt(cutoff.get(), 1) + " Hz on the following movement");
    }

    // ========================================================================
    // A CC drives the DSP from the block it arrives in
    // ========================================================================
    //
    // The message-thread pump is what tells the HOST a parameter moved, and a
    // DAW records automation from it, so it stays. What it must no longer be
    // is the only thing that moves the DSP: at 30 Hz that put up to 33 ms
    // between a controller and the sound, which is audible as a step on a
    // sweep.
    //
    // Every test below renders WITHOUT calling applyPendingMidiMappings, so
    // anything it observes came from the audio thread.

    // One block carrying MIDI, and no pump.
    const auto sendOnly = [](PX3SynthAudioProcessor& processor,
                             juce::AudioBuffer<float>& buffer,
                             const juce::MidiMessage& message)
    {
        juce::MidiBuffer midi;
        midi.addEvent(message, 0);
        buffer.clear();
        processor.processBlock(buffer, midi);
    };

    // Arm, teach, and settle the mapping - the teaching move never drives, so
    // this leaves the parameter where it was.
    const auto learnCc = [&](PX3SynthAudioProcessor& processor,
                             juce::AudioBuffer<float>& buffer,
                             const juce::String& parameterId,
                             int cc)
    {
        processor.setMidiLearnTargets({ parameterId });
        sendAndApply(processor, buffer, ccMessage(cc, 127));
    };

    // ---- the parameter itself moves inside processBlock ---------------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        auto& cutoff = processor.getFilterCutoffParam(0);
        learnCc(processor, buffer, cutoff.getParameterID(), 21);

        const auto before = cutoff.get();
        sendOnly(processor, buffer, ccMessage(21, 20));

        check("MidiMap_ACcReachesItsParameterWithoutWaitingForThePump",
              std::abs(cutoff.get() - before) > 1.0f,
              "the cutoff moved from " + fmt(before, 1) + " Hz to " + fmt(cutoff.get(), 1)
                  + " Hz inside processBlock, with no message-thread tick in between");
    }

    // ---- and the audio of that same block is rendered with it ---------------
    //
    // The parameter check above proves the write happened; it does not prove
    // the block that carried the CC was rendered with the new value rather
    // than the next one. Two synths in identical states, one block apart:
    // only one gets the CC, and the difference has to be in the audio of THAT
    // block.
    //
    // Master gain, because it is unconditionally in the path and needs no
    // panel enabled. Its 15 ms smoother is why the block is long enough to
    // hear a change across.
    {
        constexpr int kLongBlock = 1024;

        const auto renderRms = [&](bool sendTheCc)
        {
            PX3SynthAudioProcessor processor;
            processor.setPlayConfigDetails(0, 2, kRate, kLongBlock);
            processor.prepareToPlay(kRate, kLongBlock);

            juce::AudioBuffer<float> buffer(2, kLongBlock);
            auto& gain = processor.getMasterGainParam();
            gain.setValueNotifyingHost(1.0f);
            learnCc(processor, buffer, gain.getParameterID(), 21);

            // A note, held past its attack so the measured block is steady.
            juce::MidiBuffer noteOn;
            noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            buffer.clear();
            processor.processBlock(buffer, noteOn);

            for (int i = 0; i < 12; ++i)
            {
                juce::MidiBuffer empty;
                buffer.clear();
                processor.processBlock(buffer, empty);
            }

            juce::MidiBuffer midi;
            if (sendTheCc) { midi.addEvent(juce::MidiMessage::controllerEvent(1, 21, 0), 0); }
            buffer.clear();
            processor.processBlock(buffer, midi);

            return buffer.getRMSLevel(0, 0, kLongBlock);
        };

        const auto quiet = renderRms(true);
        const auto loud = renderRms(false);

        check("MidiMap_TheDspRendersTheNewValueInTheBlockTheCcArrivesIn",
              loud > 1.0e-4f && quiet < loud * 0.75f,
              "the block carrying CC 21 -> 0 rendered at RMS " + fmt(quiet, 5)
                  + " against " + fmt(loud, 5) + " for the identical block without it");
    }

    // ---- authoritative, not additive ---------------------------------------
    //
    // A CC OWNS its parameter's base value. Sending the same value three times
    // has to land in the same place three times; a CC accumulated as a delta
    // would walk the parameter up the range instead.
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        auto& cutoff = processor.getFilterCutoffParam(0);
        learnCc(processor, buffer, cutoff.getParameterID(), 21);

        sendOnly(processor, buffer, ccMessage(21, 90));
        const auto first = cutoff.get();
        sendOnly(processor, buffer, ccMessage(21, 90));
        sendOnly(processor, buffer, ccMessage(21, 90));
        const auto third = cutoff.get();

        // And it is the value the controller asked for, not one relative to
        // wherever the knob happened to be.
        const auto expected
            = static_cast<juce::RangedAudioParameter&>(cutoff).convertTo0to1(cutoff.get());

        check("MidiMap_ACcIsAuthoritativeNotAdditive",
              std::abs(third - first) < 1.0e-3f
                  && std::abs(expected - 90.0f / 127.0f) < 1.0e-3f,
              "three identical CC 90 messages all landed on " + fmt(third, 1)
                  + " Hz, at normalised " + fmt(expected, 4) + " for 90/127");
    }

    // ---- and modulation still sums on top of it -----------------------------
    //
    // Base, modulation and the CC override stay three separate things. The CC
    // replaces the base; an LFO, envelope or macro still adds to whatever the
    // controller last asked for.
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        auto& cutoff = processor.getFilterCutoffParam(0);
        const auto cutoffId = cutoff.getParameterID();
        learnCc(processor, buffer, cutoffId, 21);

        processor.toggleMacroDestination(0, cutoffId);
        processor.getMacroParam(0).setValueNotifyingHost(1.0f);

        sendOnly(processor, buffer, ccMessage(21, 40));

        const auto base = static_cast<juce::RangedAudioParameter&>(cutoff).getValue();
        const auto effective = processor.getModulatedNormalisedValue(cutoff);

        check("MidiMap_ModulationStillSumsOnTopOfACcSetBase",
              std::abs(base - 40.0f / 127.0f) < 1.0e-3f && effective > base + 0.05f,
              "CC 40/127 set the base to " + fmt(base, 4)
                  + " and the macro carried the effective value to " + fmt(effective, 4));
    }

    // ---- clearing ONE knob's mapping stops that knob and nothing else -------
    //
    // Shift-clicking a mapped knob clears it, and that path does not go
    // through MIDI learn - it is the one place a route can be left behind
    // while the mapping it came from is gone. The knob that kept its mapping
    // has to keep moving, so this cannot pass by tearing the whole table down.
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        auto& cutoff = processor.getFilterCutoffParam(0);
        auto& resonance = processor.getFilterResonanceParam(0);

        processor.setMidiLearnTargets({ cutoff.getParameterID(), resonance.getParameterID() });
        sendAndApply(processor, buffer, ccMessage(21, 127));
        sendOnly(processor, buffer, ccMessage(21, 30));

        processor.clearMidiMappingForParameter(cutoff.getParameterID());

        const auto cutoffAfterClear = cutoff.get();
        const auto resonanceAfterClear = resonance.get();
        sendOnly(processor, buffer, ccMessage(21, 110));

        check("MidiMap_ClearingOneKnobsMappingStopsOnlyThatKnob",
              std::abs(cutoff.get() - cutoffAfterClear) < 1.0e-6f
                  && std::abs(resonance.get() - resonanceAfterClear) > 1.0e-3f,
              "the cleared cutoff held at " + fmt(cutoff.get(), 1)
                  + " Hz while the resonance that kept its mapping moved to "
                  + fmt(resonance.get(), 3));
    }

    // ---- clearing a mapping takes the audio thread's route with it ----------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        auto& cutoff = processor.getFilterCutoffParam(0);
        learnCc(processor, buffer, cutoff.getParameterID(), 21);
        sendOnly(processor, buffer, ccMessage(21, 30));

        const auto driven = cutoff.get();
        processor.clearAllMidiMappings();
        sendOnly(processor, buffer, ccMessage(21, 110));

        check("MidiMap_ClearingAMappingStopsTheAudioThreadDrive",
              std::abs(cutoff.get() - driven) < 1.0e-6f,
              "the cutoff held at " + fmt(cutoff.get(), 1)
                  + " Hz when a CC arrived for a mapping that had been cleared");
    }

    // ---- a restored mapping drives without a pump tick ----------------------
    //
    // The route table is a view of the mapping list, so every path that
    // changes the list has to rebuild it - including the one that does not go
    // through MIDI learn at all.
    {
        PX3SynthAudioProcessor source;
        prepared(source);
        juce::AudioBuffer<float> sourceBuffer(2, kBlock);
        learnCc(source, sourceBuffer, source.getFilterCutoffParam(0).getParameterID(), 21);

        juce::MemoryBlock state;
        source.getStateInformation(state);

        PX3SynthAudioProcessor reopened;
        prepared(reopened);
        reopened.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        juce::AudioBuffer<float> buffer(2, kBlock);
        auto& cutoff = reopened.getFilterCutoffParam(0);
        const auto before = cutoff.get();
        sendOnly(reopened, buffer, ccMessage(21, 25));

        check("MidiMap_ARestoredMappingDrivesWithoutAPumpTick",
              reopened.getMidiCcForParameter(cutoff.getParameterID()) == 21
                  && std::abs(cutoff.get() - before) > 1.0f,
              "a mapping restored from session state moved the cutoff from "
                  + fmt(before, 1) + " Hz to " + fmt(cutoff.get(), 1)
                  + " Hz with no message-thread tick");
    }

    // ---- and the host is still told, which is what records automation -------
    //
    // Logic's Touch, Latch and Write read a parameter change through the
    // host's own notification, which only setValueNotifyingHost sends and
    // which must never be called from the audio thread. So the pump keeps
    // doing exactly what it did: the audio-thread write is in addition to it,
    // not instead of it. Without this the CC would move the sound and record
    // nothing.
    {
        struct Watcher final : public juce::AudioProcessorParameter::Listener
        {
            void parameterValueChanged(int, float newValue) override
            {
                ++values;
                last = newValue;
            }
            void parameterGestureChanged(int, bool starting) override
            {
                starting ? ++begins : ++ends;
            }

            int values { 0 };
            int begins { 0 };
            int ends { 0 };
            float last { -1.0f };
        };

        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        auto& cutoff = processor.getFilterCutoffParam(0);
        learnCc(processor, buffer, cutoff.getParameterID(), 21);

        Watcher watcher;
        cutoff.addListener(&watcher);
        sendAndApply(processor, buffer, ccMessage(21, 64));
        cutoff.removeListener(&watcher);

        check("MidiMap_ThePumpStillNotifiesTheHostSoAutomationRecords",
              watcher.values >= 1 && watcher.begins == 1 && watcher.ends == 1
                  && std::abs(watcher.last - 64.0f / 127.0f) < 1.0e-3f,
              juce::String(watcher.values) + " value notifications inside "
                  + juce::String(watcher.begins) + " begin / " + juce::String(watcher.ends)
                  + " end gesture, carrying " + fmt(watcher.last, 4));
    }

    // ---- one CC, several destinations, all from the audio thread ------------
    {
        PX3SynthAudioProcessor processor;
        prepared(processor);
        juce::AudioBuffer<float> buffer(2, kBlock);

        auto& cutoff = processor.getFilterCutoffParam(0);
        auto& resonance = processor.getFilterResonanceParam(0);

        processor.setMidiLearnTargets({ cutoff.getParameterID(), resonance.getParameterID() });
        sendAndApply(processor, buffer, ccMessage(21, 127));

        const auto cutoffBefore = cutoff.get();
        const auto resonanceBefore = resonance.get();
        sendOnly(processor, buffer, ccMessage(21, 15));

        check("MidiMap_OneCcDrivesEveryDestinationFromTheAudioThread",
              std::abs(cutoff.get() - cutoffBefore) > 1.0f
                  && std::abs(resonance.get() - resonanceBefore) > 1.0e-3f,
              "cutoff " + fmt(cutoffBefore, 1) + " -> " + fmt(cutoff.get(), 1)
                  + " Hz and resonance " + fmt(resonanceBefore, 3) + " -> "
                  + fmt(resonance.get(), 3) + " in one block");
    }
}

} // namespace px3tests
