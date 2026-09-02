#include "TestSupport.h"

// testBreakpointEnvelope

namespace px3tests
{

void testBreakpointEnvelope()
{
    suite("BREAKPOINT ENVELOPE");

    // ---- the curve ---------------------------------------------------------
    // The shape function is the whole envelope in miniature: everything else is
    // bookkeeping around it, so its properties are asserted directly.
    {
        check("Envelope_ZeroCurveIsExactlyLinear",
              std::abs(px3::BreakpointEnvelope::shape(0.25, 0.0) - 0.25) < 1.0e-12
                  && std::abs(px3::BreakpointEnvelope::shape(0.75, 0.0) - 0.75) < 1.0e-12,
              "curve 0 returns x unchanged, with no exp() taken");

        // Endpoints have to be exact, not nearly exact: they are what makes the
        // envelope continuous at every breakpoint without any clamping.
        auto worstEndpoint = 0.0;
        for (const auto curve : { -1.0, -0.5, 0.0, 0.5, 1.0 })
        {
            worstEndpoint = juce::jmax(worstEndpoint,
                                       std::abs(px3::BreakpointEnvelope::shape(0.0, curve)));
            worstEndpoint = juce::jmax(worstEndpoint,
                                       std::abs(px3::BreakpointEnvelope::shape(1.0, curve) - 1.0));
        }
        check("Envelope_CurveHitsBothEndpointsExactly", worstEndpoint < 1.0e-12,
              "worst endpoint error across the curve range: " + fmt(worstEndpoint, 15));

        // Monotone and bounded at every setting including the stops, because an
        // editor will be dragged to its stops.
        juce::StringArray broken;
        for (int c = -20; c <= 20; ++c)
        {
            const auto curve = c / 20.0;
            auto previous = -1.0;
            for (int i = 0; i <= 1000; ++i)
            {
                const auto y = px3::BreakpointEnvelope::shape(i / 1000.0, curve);
                if (! std::isfinite(y)) { broken.addIfNotAlreadyThere("non-finite"); }
                else if (y < -1.0e-9 || y > 1.0 + 1.0e-9) { broken.addIfNotAlreadyThere("out of range"); }
                else if (y < previous - 1.0e-9) { broken.addIfNotAlreadyThere("not monotone"); }
                previous = y;
            }
        }
        check("Envelope_CurveIsMonotoneAndBoundedAtEverySetting", broken.isEmpty(),
              broken.isEmpty() ? "41 curve settings x 1001 points, all finite, ordered and in range"
                               : broken.joinIntoString(", "));

        // Symmetry is what makes one normalised control feel like one control:
        // +50% and -50% have to be the same amount of bend.
        auto worstSymmetry = 0.0;
        for (const auto curve : { -0.9, -0.5, -0.2, 0.2, 0.5, 0.9 })
        {
            for (int i = 0; i <= 100; ++i)
            {
                const auto x = i / 100.0;
                const auto roundTrip = px3::BreakpointEnvelope::shape(
                    px3::BreakpointEnvelope::shape(x, curve), -curve);
                worstSymmetry = juce::jmax(worstSymmetry, std::abs(roundTrip - x));
            }
        }
        check("Envelope_BendingUpAndDownAreMirrorImages", worstSymmetry < 1.0e-9,
              "worst round-trip error bending by +c then -c: " + fmt(worstSymmetry, 12));

        // A curve that can become a step is a click waiting to happen - which is
        // what a power curve does at wide exponents.
        auto worstStep = 0.0;
        for (const auto curve : { -1.0, 1.0 })
        {
            for (int i = 1; i <= 4000; ++i)
            {
                worstStep = juce::jmax(worstStep,
                                       std::abs(px3::BreakpointEnvelope::shape(i / 4000.0, curve)
                                                - px3::BreakpointEnvelope::shape((i - 1) / 4000.0, curve)));
            }
        }
        check("Envelope_CurveNeverDegeneratesIntoAStep", worstStep < 0.05,
              "largest jump between adjacent samples at full curve: " + fmt(worstStep, 6));
    }

    // ---- ADSR is a special case, not a separate concept ---------------------
    {
        EnvelopeSettings settings;
        settings.attackSeconds = 0.15f;
        settings.decaySeconds = 0.25f;
        settings.sustainLevel = 0.6f;
        settings.releaseSeconds = 0.4f;

        const auto envelope = px3::BreakpointEnvelope::fromAdsr(settings);
        const auto back = envelope.toAdsr();

        check("Envelope_AdsrRoundTripsThroughTheBreakpointModel",
              envelope.getPointCount() == 4
                  && std::abs(back.attackSeconds - 0.15f) < 1.0e-6f
                  && std::abs(back.decaySeconds - 0.25f) < 1.0e-6f
                  && std::abs(back.sustainLevel - 0.6f) < 1.0e-6f
                  && std::abs(back.releaseSeconds - 0.4f) < 1.0e-6f,
              "four points in - attack, decay, sustain, release - and the same numbers "
              "back out");

        check("Envelope_AdsrIsRecognisedAsAdsr", envelope.isPlainAdsr(),
              "a freshly built ADSR reports itself as still parameter-describable");

        check("Envelope_SustainPointIsTheOneAfterTheDecay", envelope.getSustainPoint() == 2,
              "the envelope holds at point 2 - after the attack and the decay");


        // The shape the four parameters describe, sampled where it matters.
        const auto atPeak = envelope.valueAt(0.15);
        const auto atSustain = envelope.valueAt(0.4);
        // Float tolerance, not double: the sustain arrives as a float parameter,
        // so 0.6f is 0.60000002 by the time it is a double and a 1e-9 bar fails
        // on precision rather than on behaviour.
        check("Envelope_AdsrPassesThroughItsOwnCorners",
              std::abs(atPeak - 1.0) < 1.0e-6 && std::abs(atSustain - 0.6) < 1.0e-6,
              "peak " + fmt(atPeak, 6) + " at the end of the attack, "
                  + fmt(atSustain, 6) + " at the end of the decay");
    }

    // ---- adding a point splits a segment ------------------------------------
    {
        auto envelope = px3::BreakpointEnvelope::fromAdsr(EnvelopeSettings {});
        envelope.setCurve(0, 0.5);   // bend the attack

        const auto before = envelope.getPointCount();
        // Adding points is a Breakpoint-mode capability.
        envelope.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        const auto added = envelope.addPoint(envelope.getPoint(1).timeSeconds * 0.5, 0.4);

        check("Envelope_AddedPointSplitsTheSegmentItLandsIn",
              added == 1 && envelope.getPointCount() == before + 1
                  && envelope.getPoint(1).value > 0.39 && envelope.getPoint(1).value < 0.41,
              "inserted at index " + juce::String(added) + ", not appended to the end");

        check("Envelope_SplitInheritsTheCurveItSplit",
              std::abs(envelope.getPoint(0).curveToNext - 0.5) < 1.0e-9
                  && std::abs(envelope.getPoint(1).curveToNext - 0.5) < 1.0e-9,
              "both halves of a bent segment stay bent");

        check("Envelope_SustainFollowsTheInsertion", envelope.getSustainPoint() == 3,
              "the sustain moved from point 2 to point 3 with the point it marks");
    }

    // ---- removal protects what the envelope needs ---------------------------
    {
        auto envelope = px3::BreakpointEnvelope::fromAdsr(EnvelopeSettings {});
        envelope.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        envelope.addPoint(0.001, 0.5);

        check("Envelope_TheAnchorCannotBeRemoved", ! envelope.removePoint(0),
              "point 0 is the note-on instant, not a stage");
        check("Envelope_TheEndCannotBeRemoved",
              ! envelope.removePoint(envelope.getPointCount() - 1),
              "the final point is where the release lands");
        // The sustain index is ADSR bookkeeping. In Breakpoint mode the
        // envelope holds nowhere, so that point is an ordinary one and
        // protecting it was an ADSR constraint applied to a mode that has no
        // sustain.
        {
            const auto sustain = envelope.getSustainPoint();
            const auto removable = sustain > 0 && sustain < envelope.getPointCount() - 1;
            check("Envelope_TheSustainPointIsOrdinaryInBreakpointMode",
                  removable && envelope.canRemovePoint(sustain),
                  removable ? "the point at the sustain index can be removed like any other"
                            : "the sustain index landed on a structural point, so this says "
                              "nothing");
        }

        const auto before = envelope.getPointCount();
        check("Envelope_AnOrdinaryPointCanBeRemoved",
              envelope.removePoint(1) && envelope.getPointCount() == before - 1,
              juce::String(before) + " points, then " + juce::String(envelope.getPointCount()));
    }

    // ---- points cannot cross their neighbours -------------------------------
    {
        auto envelope = px3::BreakpointEnvelope::fromAdsr(EnvelopeSettings {});
        // Drag point 1 far past point 2.
        envelope.setPoint(1, 99.0, 0.5);

        auto ordered = true;
        for (int i = 1; i < envelope.getPointCount(); ++i)
        {
            if (envelope.getPoint(i).timeSeconds < envelope.getPoint(i - 1).timeSeconds)
            {
                ordered = false;
            }
        }
        check("Envelope_DraggingAPointPastItsNeighbourClampsInstead", ordered,
              "point 1 stopped at point 2's time rather than reordering the envelope");

        envelope.setPoint(0, 5.0, 0.5);
        check("Envelope_TheAnchorStaysAtTimeZero",
              std::abs(envelope.getPoint(0).timeSeconds) < 1.0e-12,
              "point 0 ignores a time drag");
    }

    // ---- evaluation ---------------------------------------------------------
    {
        EnvelopeSettings settings;
        settings.attackSeconds = 0.1f;
        settings.decaySeconds = 0.1f;
        settings.sustainLevel = 0.5f;
        settings.releaseSeconds = 0.2f;

        const auto envelope = px3::BreakpointEnvelope::fromAdsr(settings);
        px3::BreakpointEnvelope::Snapshot snapshot;
        snapshot.rebuild(envelope, kSampleRate);

        check("Envelope_HeldPhaseReachesTheSustainAndStaysThere",
              std::abs(snapshot.valueAtHeld(0.2) - 0.5f) < 1.0e-6f
                  && std::abs(snapshot.valueAtHeld(10.0) - 0.5f) < 1.0e-6f,
              "0.5 at the end of the decay and still 0.5 ten seconds later");

        check("Envelope_HeldPhaseStartsAtZeroAndPeaksAtOne",
              std::abs(snapshot.valueAtHeld(0.0)) < 1.0e-6f
                  && std::abs(snapshot.valueAtHeld(0.1) - 1.0f) < 1.0e-6f,
              "0 at note-on, 1 at the end of the attack");

        check("Envelope_ReleaseReachesZero",
              std::abs(snapshot.valueAtReleased(0.2, 0.5f)) < 1.0e-6f,
              "the release lands exactly on zero at its full length");

        // The one that matters: releasing early must start from where the
        // envelope is, not jump to the sustain first.
        const auto releasedFromPeak = snapshot.valueAtReleased(0.0, 1.0f);
        check("Envelope_ReleaseBeginsFromTheCurrentValue",
              std::abs(releasedFromPeak - 1.0f) < 1.0e-6f,
              "releasing at full level starts the release from 1.0, measured "
                  + fmt(releasedFromPeak, 6) + " - not from the 0.5 sustain");

        // And it still arrives at zero from wherever it started.
        auto worstEnd = 0.0f;
        for (const auto from : { 0.0f, 0.15f, 0.5f, 0.83f, 1.0f })
        {
            worstEnd = juce::jmax(worstEnd, std::abs(snapshot.valueAtReleased(0.2, from)));
        }
        check("Envelope_ReleaseAlwaysArrivesAtZero", worstEnd < 1.0e-6f,
              "worst end-of-release value across five starting levels: " + fmt(worstEnd, 9));

        check("Envelope_ReleaseProgressIsIndependentOfTheCurve",
              std::abs(snapshot.releaseProgress(0.0)) < 1.0e-9f
                  && std::abs(snapshot.releaseProgress(0.1) - 0.5f) < 1.0e-6f
                  && std::abs(snapshot.releaseProgress(0.2) - 1.0f) < 1.0e-9f,
              "0, 0.5 and 1 at the start, middle and end of a 0.2 s release");
    }

    // ---- the release must not step ------------------------------------------
    // A release that starts from the current value is where a discontinuity
    // would come from, so it is scanned for one at every starting level.
    {
        EnvelopeSettings settings;
        settings.attackSeconds = 0.05f;
        settings.decaySeconds = 0.05f;
        settings.sustainLevel = 0.4f;
        settings.releaseSeconds = 0.3f;

        auto envelope = px3::BreakpointEnvelope::fromAdsr(settings);
        envelope.setCurve(2, 0.8);   // a strongly bent release

        px3::BreakpointEnvelope::Snapshot snapshot;
        snapshot.rebuild(envelope, kSampleRate);

        auto worstStep = 0.0f;
        juce::String offender;
        for (const auto from : { 0.05f, 0.4f, 0.7f, 1.0f })
        {
            auto previous = snapshot.valueAtReleased(0.0, from);
            for (int i = 1; i <= 6000; ++i)
            {
                const auto value = snapshot.valueAtReleased(i * 0.3 / 6000.0, from);
                const auto step = std::abs(value - previous);
                if (step > worstStep)
                {
                    worstStep = step;
                    offender = "starting from " + fmt(from, 2);
                }
                previous = value;
            }
        }

        // 6000 steps across a 0.3 s release: a smooth curve moves at most a few
        // thousandths per step.
        check("Envelope_ReleaseFromAnyLevelIsContinuous", worstStep < 0.01f,
              "largest single step across the release: " + fmt(worstStep, 6)
                  + " (" + offender + ")");
    }

    // ---- edge cases ---------------------------------------------------------
    {
        juce::StringArray failures;

        // Zero-length segments, which are a legitimate instant jump.
        {
            EnvelopeSettings instant;
            instant.attackSeconds = 0.0f;
            instant.decaySeconds = 0.0f;
            instant.sustainLevel = 1.0f;
            instant.releaseSeconds = 0.0f;
            const auto envelope = px3::BreakpointEnvelope::fromAdsr(instant);
            px3::BreakpointEnvelope::Snapshot snapshot;
            snapshot.rebuild(envelope, kSampleRate);
            for (const auto t : { 0.0, 0.001, 1.0 })
            {
                if (! std::isfinite(snapshot.valueAtHeld(t))) { failures.add("zero-length held"); }
                if (! std::isfinite(snapshot.valueAtReleased(t, 1.0f))) { failures.add("zero-length release"); }
            }
        }

        // Non-finite input, which is what a broken preset or a divide upstream
        // would deliver.
        {
            EnvelopeSettings nonsense;
            nonsense.attackSeconds = std::numeric_limits<float>::quiet_NaN();
            nonsense.decaySeconds = std::numeric_limits<float>::infinity();
            nonsense.sustainLevel = -5.0f;
            nonsense.releaseSeconds = -1.0f;
            const auto envelope = px3::BreakpointEnvelope::fromAdsr(nonsense);
            for (int i = 0; i < envelope.getPointCount(); ++i)
            {
                const auto& point = envelope.getPoint(i);
                if (! std::isfinite(point.timeSeconds) || ! std::isfinite(point.value)
                    || point.value < 0.0 || point.value > 1.0)
                {
                    failures.add("NaN/inf leaked into the model");
                }
            }
        }

        // Filling to the maximum and past it.
        {
            auto envelope = px3::BreakpointEnvelope::fromAdsr(EnvelopeSettings {});
            for (int i = 0; i < 40; ++i)
            {
                envelope.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
                envelope.addPoint(0.001 + i * 0.0001, 0.5);
            }
            if (envelope.getPointCount() > px3::BreakpointEnvelope::kMaxPoints)
            {
                failures.add("exceeded the point limit");
            }
            if (envelope.addPoint(0.002, 0.5) != -1) { failures.add("added past the limit"); }

            px3::BreakpointEnvelope::Snapshot snapshot;
            snapshot.rebuild(envelope, kSampleRate);
            for (int i = 0; i <= 200; ++i)
            {
                if (! std::isfinite(snapshot.valueAtHeld(i * 0.01)))
                {
                    failures.addIfNotAlreadyThere("full envelope evaluated to non-finite");
                }
            }
        }

        // Points stacked at the same instant.
        {
            auto envelope = px3::BreakpointEnvelope::fromAdsr(EnvelopeSettings {});
            envelope.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
            envelope.addPoint(0.005, 0.3);
            envelope.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
            envelope.addPoint(0.005, 0.9);
            px3::BreakpointEnvelope::Snapshot snapshot;
            snapshot.rebuild(envelope, kSampleRate);
            for (int i = 0; i <= 400; ++i)
            {
                if (! std::isfinite(snapshot.valueAtHeld(i * 0.001)))
                {
                    failures.addIfNotAlreadyThere("coincident points evaluated to non-finite");
                }
            }
        }

        check("Envelope_SurvivesItsEdgeCases", failures.isEmpty(),
              failures.isEmpty()
                  ? "zero-length segments, NaN and infinite input, a full point list and "
                    "coincident points all evaluate finite and in range"
                  : failures.joinIntoString(", "));
    }

    // ---- the swap must not change how anything sounds -----------------------
    // Captured from the juce::ADSR implementation BEFORE the breakpoint model
    // replaced it, and asserted against ever since. Rewriting the engine under
    // every preset in the library is the risk this whole exercise carries, and
    // "it still sounds fine to me" is not a way to manage it.
    //
    // The two engines differ deliberately and the reference shows it: AMP ENV
    // releases exponentially, because it drives a VCA and perceived loudness is
    // logarithmic, while ENV 1/2/3 release linearly.
    {
        EnvelopeSettings settings;
        settings.attackSeconds = 0.05f;
        settings.decaySeconds = 0.10f;
        settings.sustainLevel = 0.6f;
        settings.releaseSeconds = 0.20f;

        static const std::array<float, 20> ampReference {
            { 0.000011f, 0.984644f, 0.803088f, 0.603104f, 0.600001f, 0.600001f, 0.600001f,
              0.600001f, 0.600001f, 0.600001f, 0.599990f, 0.109073f, 0.018905f, 0.002868f,
              0.000016f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f }
        };
        static const std::array<float, 20> generatorReference {
            { 0.000011f, 0.985019f, 0.803013f, 0.603029f, 0.600000f, 0.600000f, 0.600000f,
              0.600000f, 0.600000f, 0.600000f, 0.599998f, 0.452213f, 0.302224f, 0.152235f,
              0.002235f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f }
        };

        const auto compare = [&settings](bool ampPath, const std::array<float, 20>& reference)
        {
            AmpEnvelope amp;
            EnvelopeGenerator generator;
            amp.prepare(kSampleRate);
            generator.prepare(kSampleRate);
            amp.setSettings(settings);
            generator.setSettings(settings);
            amp.noteOn();
            generator.noteOn();

            auto worst = 0.0;
            int at = -1;
            std::size_t slot = 0;
            for (int i = 0; i < 48000; ++i)
            {
                if (i == 24000)
                {
                    if (ampPath) { amp.noteOff(); } else { generator.noteOff(); }
                }
                const auto v = ampPath ? amp.getNextSample() : generator.getNextSample();
                if (i % 2400 == 0 && slot < reference.size())
                {
                    const auto error = std::abs(static_cast<double>(v) - reference[slot]);
                    if (error > worst) { worst = error; at = i; }
                    ++slot;
                }
            }
            return std::make_pair(worst, at);
        };

        const auto ampResult = compare(true, ampReference);
        check("Envelope_AmpEnvelopeStillSoundsAsItDid", ampResult.first < 0.002,
              "worst deviation from the pre-change reference: " + fmt(ampResult.first, 6)
                  + (ampResult.second >= 0
                         ? " at sample " + juce::String(ampResult.second)
                         : juce::String()));

        const auto generatorResult = compare(false, generatorReference);
        check("Envelope_ModEnvelopesStillSoundAsTheyDid", generatorResult.first < 0.002,
              "worst deviation from the pre-change reference: " + fmt(generatorResult.first, 6)
                  + (generatorResult.second >= 0
                         ? " at sample " + juce::String(generatorResult.second)
                         : juce::String()));

        // The distinction itself, asserted rather than left to the two reference
        // arrays: an amp release that stopped being exponential would still
        // match its own reference if the reference were regenerated by mistake.
        check("Envelope_AmpReleaseIsExponentialAndTheModReleaseIsNot",
              ampReference[12] < generatorReference[12] * 0.25f,
              "a quarter of the way through the release the amp envelope is at "
                  + fmt(ampReference[12], 4) + " and a mod envelope at "
                  + fmt(generatorReference[12], 4));
    }

    // ---- both modes' state travels in a PRESET FILE too ---------------------
    //
    // Presets take everything the session tree carries bar the preset's own
    // identity, so this holds by construction - which is not the same as
    // holding, and a preset file is the artefact users actually trade.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        setParam(processor, "env1Attack", 0.180f);
        {
            auto bent = processor.getShapedEnvelope(1);
            bent.setCurve(0, 0.62);
            processor.setShapedEnvelope(1, bent);
        }

        processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
        {
            auto drawn = processor.getShapedEnvelope(1);
            drawn.addPoint(0.35, 0.88);
            drawn.addPoint(0.58, 0.12);
            drawn.setCurve(3, -0.4);
            processor.setShapedEnvelope(1, drawn);
        }

        auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("px3-component-tests");
        tempDirectory.createDirectory();
        const auto presetFile = tempDirectory.getChildFile("envelope-modes.px3preset");
        presetFile.deleteFile();

        PresetManager manager(processor);
        juce::String error;
        PresetManager::PresetMetadata metadata;
        metadata.name = "Envelope Modes";
        metadata.category = "Test";
        metadata.author = "component tests";

        const auto saved
            = manager.dumpCurrentStateToPresetFile(presetFile, metadata, true, true, error, nullptr);

        PX3SynthAudioProcessor target;
        target.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        target.prepareToPlay(kSampleRate, kBlockSize);
        PresetManager targetManager(target);
        juce::String loadError;
        const auto loaded = saved && targetManager.loadPresetFile(presetFile, loadError);

        const auto live = target.getShapedEnvelope(1);
        check("EnvBp_APresetFileCarriesTheActiveBreakpointShape",
              loaded && live.isBreakpointMode() && live.getPointCount() == 6
                  && std::abs(live.getPoint(3).curveToNext + 0.4) < 1.0e-9,
              loaded ? "it loads in Breakpoint mode on " + juce::String(live.getPointCount())
                           + " points with the drawn bend at "
                           + fmt(static_cast<float>(live.getPoint(3).curveToNext), 2)
                     : "the preset did not round-trip: " + error + loadError);

        target.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::adsr);
        const auto adsrAfter = target.currentModEnvelope(0);
        check("EnvBp_APresetFileCarriesTheAdsrPutAsideBehindIt",
              loaded && adsrAfter.getPointCount() == 4
                  && std::abs(adsrAfter.getPoint(0).curveToNext - 0.62) < 1.0e-9
                  && std::abs(target.envelopeParameterSettings(0).attackSeconds - 0.180f) < 1.0e-4f,
              "switching back gives " + juce::String(adsrAfter.getPointCount()) + " points bending "
                  + fmt(static_cast<float>(adsrAfter.getPoint(0).curveToNext), 2) + " at A "
                  + fmt(target.envelopeParameterSettings(0).attackSeconds, 3));

        presetFile.deleteFile();
    }

    // ---- no sustain region in Breakpoint mode -------------------------------
    //
    // The shaded band from the sustain point to the right edge means "the
    // envelope holds here for as long as the key is down". A breakpoint
    // envelope never holds, so drawing it there states something untrue about
    // what the DSP will do.
    {
        EnvelopeSettings settings;
        settings.attackSeconds = 0.20f;
        settings.decaySeconds = 0.30f;
        settings.sustainLevel = 0.5f;
        settings.releaseSeconds = 0.40f;

        const auto sample = [](px3::BreakpointEnvelope::Mode mode)
        {
            EnvelopeSettings local;
            local.attackSeconds = 0.20f;
            local.decaySeconds = 0.30f;
            local.sustainLevel = 0.5f;
            local.releaseSeconds = 0.40f;

            auto shape = px3::BreakpointEnvelope::fromAdsr(local);
            shape.setMode(mode);

            BreakpointEnvelopeEditor graph;
            graph.setSize(400, 200);
            graph.setEnvelope(shape);

            const auto image = graph.createComponentSnapshot(graph.getLocalBounds());

            // Top right: inside the shaded region in ADSR mode, and well clear
            // of the curve, which has fallen to silence by then.
            const auto insideRegion = image.getPixelAt(380, 14).getBrightness();
            // Top left: outside the region in both modes, so it says whether
            // anything ELSE changed between the two renders.
            const auto outsideRegion = image.getPixelAt(12, 14).getBrightness();
            return std::pair<float, float> { insideRegion, outsideRegion };
        };

        const auto adsr = sample(px3::BreakpointEnvelope::Mode::adsr);
        const auto bp = sample(px3::BreakpointEnvelope::Mode::breakpoint);

        check("EnvBp_TheSustainRegionIsDrawnInAdsrMode",
              adsr.first > adsr.second + 0.004f,
              "in ADSR mode the band reads " + fmt(adsr.first, 4) + " against "
                  + fmt(adsr.second, 4) + " outside it");

        check("EnvBp_NoSustainRegionIsDrawnInBreakpointMode",
              std::abs(bp.first - bp.second) < 1.0e-4f,
              "in Breakpoint mode the same band reads " + fmt(bp.first, 4) + " against "
                  + fmt(bp.second, 4) + " outside it");

        check("EnvBp_TheRestOfTheGraphIsUnchangedBetweenModes",
              std::abs(adsr.second - bp.second) < 1.0e-4f,
              "outside the region both modes read " + fmt(adsr.second, 4));
    }

    // ---- the ADSR knobs answer to bypass AND mode, together -----------------
    //
    // Two independent things gate these knobs, and refreshFromParameters runs
    // on a timer. If the mode state is applied anywhere but alongside bypass,
    // the next refresh re-enables the knobs in Breakpoint mode within a frame -
    // and a test that only sets the mode and looks immediately would never see
    // it. So every case here ends with a refresh through ModPanel.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        if (editor != nullptr)
        {
            editor->setSize(1400, 900);

            ModPanel* panel = nullptr;
            std::function<void(juce::Component&)> findPanel = [&](juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* p = dynamic_cast<ModPanel*>(child)) { panel = p; }
                    findPanel(*child);
                }
            };
            findPanel(*editor);

            EnvelopeComponent* env1 = nullptr;
            std::function<void(juce::Component&)> findEnv = [&](juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* e = dynamic_cast<EnvelopeComponent*>(child))
                    {
                        if (env1 == nullptr) { env1 = e; }
                    }
                    findEnv(*child);
                }
            };
            if (panel != nullptr) { findEnv(*panel); }

            if (panel != nullptr && env1 != nullptr)
            {
                auto& enabledParam = processor.getEnvelopeEnabledParam(0);

                const auto stateFor = [&](bool cardEnabled, px3::BreakpointEnvelope::Mode mode)
                {
                    enabledParam.setValueNotifyingHost(cardEnabled ? 1.0f : 0.0f);
                    processor.setEnvelopeMode(1, mode);
                    env1->setEnvelopeMode(mode);

                    // The refresh is the point: it is what would undo a mode
                    // state applied on its own.
                    panel->refreshFromParameters();
                    return env1->debugAdsrKnobsLive();
                };

                const auto onAdsr = stateFor(true, px3::BreakpointEnvelope::Mode::adsr);
                const auto onBreakpoint = stateFor(true, px3::BreakpointEnvelope::Mode::breakpoint);
                const auto offBreakpoint = stateFor(false, px3::BreakpointEnvelope::Mode::breakpoint);
                const auto offAdsr = stateFor(false, px3::BreakpointEnvelope::Mode::adsr);
                const auto onAdsrAgain = stateFor(true, px3::BreakpointEnvelope::Mode::adsr);

                check("EnvMode_TheKnobsAreLiveOnlyWhenEnabledAndInAdsrMode",
                      onAdsr && ! onBreakpoint && ! offBreakpoint && ! offAdsr && onAdsrAgain,
                      juce::String("enabled+ADSR ") + (onAdsr ? "live" : "dead")
                          + ", enabled+Breakpoint " + (onBreakpoint ? "live" : "dead")
                          + ", bypassed+Breakpoint " + (offBreakpoint ? "live" : "dead")
                          + ", bypassed+ADSR " + (offAdsr ? "live" : "dead")
                          + ", back to enabled+ADSR " + (onAdsrAgain ? "live" : "dead"));

                // Un-bypassing while in Breakpoint mode must not wake them.
                // This is the exact ordering that breaks if bypass is applied
                // without consulting the mode.
                processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
                env1->setEnvelopeMode(px3::BreakpointEnvelope::Mode::breakpoint);
                enabledParam.setValueNotifyingHost(0.0f);
                panel->refreshFromParameters();
                enabledParam.setValueNotifyingHost(1.0f);
                panel->refreshFromParameters();

                check("EnvMode_UnBypassingInBreakpointModeLeavesTheKnobsDead",
                      ! env1->debugAdsrKnobsLive(),
                      env1->debugAdsrKnobsLive()
                          ? "the knobs woke up when the card was re-enabled"
                          : "the knobs stay disabled when the card is re-enabled");

                // And they are still SHOWN through all of it.
                check("EnvMode_TheKnobsStayOnScreenThroughBypassAndMode",
                      env1->debugAdsrKnobsVisible(),
                      env1->debugAdsrKnobsVisible() ? "still on screen" : "they disappeared");
            }
        }
    }

    // ---- the animation preference is one value, shared by every instance ----
    //
    // The requirement is that turning it off in one open window turns it off in
    // all of them. Instances know about the settings service; they never know
    // about each other.
    {
        // Onto a scratch file first: without this the suite reads and writes
        // the developer's own preference.
        auto scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("px3-component-tests")
                           .getChildFile("global-settings.xml");
        scratch.getParentDirectory().createDirectory();
        scratch.deleteFile();
        px3::GlobalSettings::debugUseSettingsFile(scratch);
        px3::GlobalSettings::getInstance().debugReloadFromDisk();

        const ScopedAnimationPreference restoreAfterwards(true);

        PX3SynthAudioProcessor processorA, processorB, processorC;
        for (auto* p : { &processorA, &processorB, &processorC })
        {
            p->setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            p->prepareToPlay(kSampleRate, kBlockSize);
        }

        std::unique_ptr<juce::AudioProcessorEditor> baseA(processorA.createEditor());
        std::unique_ptr<juce::AudioProcessorEditor> baseB(processorB.createEditor());
        std::unique_ptr<juce::AudioProcessorEditor> baseC(processorC.createEditor());

        auto* editorA = dynamic_cast<PX3SynthAudioProcessorEditor*>(baseA.get());
        auto* editorB = dynamic_cast<PX3SynthAudioProcessorEditor*>(baseB.get());
        auto* editorC = dynamic_cast<PX3SynthAudioProcessorEditor*>(baseC.get());

        if (editorA != nullptr && editorB != nullptr && editorC != nullptr)
        {
            for (auto* e : { editorA, editorB, editorC }) { e->setSize(1400, 900); }

            // Whether an instance's own keyboard will spark is the observable
            // end of the chain, so that is what every case below reads.
            const auto animatesIn = [](PX3SynthAudioProcessorEditor* editor)
            {
                // Ask the keyboard to spark and see whether it did. When the
                // preference is off the propagation has already cleared any
                // live sparks and the spawn is refused, so this reads false;
                // when it is on the spawn succeeds and it reads true.
                auto& keyboard = editor->debugPianoKeyboard();
                keyboard.debugSpawnSparks(60);
                return keyboard.hasSparks();
            };

            // A: they start agreeing.
            check("GlobalSettings_EveryInstanceStartsWithTheSameValue",
                  animatesIn(editorA) && animatesIn(editorB) && animatesIn(editorC),
                  "all three instances animate at startup");

            // B: turning it off in the first reaches the other two.
            editorA->debugSettingsPanel()->debugAnimationsToggle()
                .setToggleState(false, juce::sendNotificationSync);

            const auto offEverywhere = ! animatesIn(editorA) && ! animatesIn(editorB)
                                       && ! animatesIn(editorC);
            check("GlobalSettings_TurningItOffInOneInstanceReachesTheOthers",
                  offEverywhere,
                  offEverywhere ? "all three stopped"
                                : juce::String("A ") + (animatesIn(editorA) ? "on" : "off")
                                      + ", B " + (animatesIn(editorB) ? "on" : "off")
                                      + ", C " + (animatesIn(editorC) ? "on" : "off"));

            // C: and back on from a DIFFERENT instance.
            editorC->debugSettingsPanel()->debugAnimationsToggle()
                .setToggleState(true, juce::sendNotificationSync);

            const auto onEverywhere = animatesIn(editorA) && animatesIn(editorB)
                                      && animatesIn(editorC);
            check("GlobalSettings_TurningItOnFromAnotherInstanceReachesThemAll",
                  onEverywhere,
                  onEverywhere ? "all three resumed" : "they did not all resume");

            // The checkbox on every open page shows the current value, not the
            // one it had when the page was drawn.
            const auto boxesAgree
                = editorA->debugSettingsPanel()->debugAnimationsToggle().getToggleState()
                  && editorB->debugSettingsPanel()->debugAnimationsToggle().getToggleState()
                  && editorC->debugSettingsPanel()->debugAnimationsToggle().getToggleState();

            check("GlobalSettings_EveryOpenCheckboxFollowsTheValue",
                  boxesAgree,
                  boxesAgree ? "all three checkboxes show it on"
                             : "a checkbox is showing a stale value");

            // D: closing one must not leave a listener behind. If it did, the
            // next change would call into freed memory - so the change AFTER
            // the destruction is the test.
            baseB.reset();

            editorA->debugSettingsPanel()->debugAnimationsToggle()
                .setToggleState(false, juce::sendNotificationSync);

            check("GlobalSettings_ClosingAnEditorLeavesNoListenerBehind",
                  ! animatesIn(editorA) && ! animatesIn(editorC),
                  "with one editor destroyed, the remaining two still follow the setting");

            // E: persistence. The value is on disk, and a fresh session picks
            // it up - which is what debugReloadFromDisk stands in for.
            const auto written = scratch.existsAsFile()
                                 && scratch.loadFileAsString().contains("animationsEnabled");
            px3::GlobalSettings::getInstance().debugReloadFromDisk();

            check("GlobalSettings_ThePreferencePersistsAcrossSessions",
                  written && ! px3::GlobalSettings::getInstance().areAnimationsEnabled(),
                  written ? juce::String("reloaded from disk as ")
                                + (px3::GlobalSettings::getInstance().areAnimationsEnabled()
                                       ? "on" : "off")
                          : "nothing was written to the settings file");
        }

        // Back to the real file for anything that follows.
        px3::GlobalSettings::debugUseSettingsFile({});
        scratch.deleteFile();
    }

    // ---- a held key still lights up with animations off ---------------------
    //
    // Two halves to this, and they must not be confused: the WIGGLE is an
    // animation, the colour is feedback. Turning animations off should stop the
    // key moving without taking away the thing that tells you which note is
    // sounding.
    {
        const auto renderHeldKey = [](bool animations)
        {
            PianoKeyboard keyboard;
            keyboard.setAnimationsEnabled(animations);
            keyboard.setSize(900, 120);

            std::array<bool, PianoKeyboard::totalKeys> held {};
            std::array<float, PianoKeyboard::totalKeys> velocities {};
            held[static_cast<std::size_t>(60 - PianoKeyboard::firstMidiNote)] = true;
            velocities[static_cast<std::size_t>(60 - PianoKeyboard::firstMidiNote)] = 1.0f;
            keyboard.setActiveNotes(held, velocities);

            // Two frames apart: with the wiggle running the key has moved
            // between them, so the images differ. Static, they are identical.
            const auto first = keyboard.createComponentSnapshot(keyboard.getLocalBounds());
            for (int i = 0; i < 12; ++i) { keyboard.debugAdvanceAnimationFrame(); }
            const auto second = keyboard.createComponentSnapshot(keyboard.getLocalBounds());

            auto movedPixels = 0;
            for (int y = 0; y < first.getHeight(); y += 2)
            {
                for (int x = 0; x < first.getWidth(); x += 2)
                {
                    if (first.getPixelAt(x, y) != second.getPixelAt(x, y)) { ++movedPixels; }
                }
            }
            return std::pair<juce::Image, int> { first, movedPixels };
        };

        const auto animated = renderHeldKey(true);
        const auto still = renderHeldKey(false);

        check("Keyboard_TheHeldKeyStopsWigglingWithAnimationsOff",
              animated.second > 0 && still.second == 0,
              juce::String(animated.second) + " pixels move between frames with animations on, "
                  + juce::String(still.second) + " with them off");

        // And it is still lit: the held key differs from the same key unheld,
        // by the same amount either way. Without this the test above would
        // pass on a keyboard that had stopped drawing the key at all.
        const auto renderIdleKeyboard = [](bool animations)
        {
            PianoKeyboard keyboard;
            keyboard.setAnimationsEnabled(animations);
            keyboard.setSize(900, 120);
            return keyboard.createComponentSnapshot(keyboard.getLocalBounds());
        };

        const auto litPixels = [](const juce::Image& held, const juce::Image& idle)
        {
            auto differing = 0;
            for (int y = 0; y < held.getHeight(); y += 2)
            {
                for (int x = 0; x < held.getWidth(); x += 2)
                {
                    if (held.getPixelAt(x, y) != idle.getPixelAt(x, y)) { ++differing; }
                }
            }
            return differing;
        };

        const auto litWithAnimations = litPixels(animated.first, renderIdleKeyboard(true));
        const auto litWithout = litPixels(still.first, renderIdleKeyboard(false));

        // And it comes to rest in the right PLACE.
        //
        // Freezing the wiggle's clock is not the same as not applying it: with
        // the clock stopped but the offset still applied, the key sits
        // permanently displaced by whatever phase it happened to stop at. The
        // check above cannot see that - both frames are identical either way -
        // so this compares a keyboard that animated for a while before being
        // turned off against one that never animated at all. They must be the
        // same picture.
        {
            const auto restingImage = [](int framesBeforeTurningOff)
            {
                PianoKeyboard keyboard;
                keyboard.setSize(900, 120);

                std::array<bool, PianoKeyboard::totalKeys> held {};
                std::array<float, PianoKeyboard::totalKeys> velocities {};
                held[static_cast<std::size_t>(60 - PianoKeyboard::firstMidiNote)] = true;
                velocities[static_cast<std::size_t>(60 - PianoKeyboard::firstMidiNote)] = 1.0f;
                keyboard.setActiveNotes(held, velocities);

                for (int i = 0; i < framesBeforeTurningOff; ++i)
                {
                    keyboard.debugAdvanceAnimationFrame();
                }

                keyboard.setAnimationsEnabled(false);
                return keyboard.createComponentSnapshot(keyboard.getLocalBounds());
            };

            const auto neverAnimated = restingImage(0);
            const auto animatedFirst = restingImage(37);

            auto differing = 0;
            for (int y = 0; y < neverAnimated.getHeight(); y += 2)
            {
                for (int x = 0; x < neverAnimated.getWidth(); x += 2)
                {
                    if (neverAnimated.getPixelAt(x, y) != animatedFirst.getPixelAt(x, y))
                    {
                        ++differing;
                    }
                }
            }

            check("Keyboard_TheHeldKeyRestsInThePlaceItBelongs",
                  differing == 0,
                  differing == 0
                      ? "the key rests in the same place however long it animated first"
                      : juce::String(differing) + " pixels differ, so it froze mid-wiggle");
        }

        check("Keyboard_TheHeldKeyStillLightsUpWithAnimationsOff",
              litWithout > 0 && std::abs(litWithout - litWithAnimations) < litWithAnimations / 2 + 8,
              juce::String(litWithout) + " pixels mark the held key with animations off, against "
                  + juce::String(litWithAnimations) + " with them on");
    }

    // ---- the animation preference reaches the things that animate -----------
    //
    // Checked THROUGH THE EDITOR, which is the part that was broken while the
    // component-level tests passed: PianoKeyboard and PerformanceControls have
    // to be told, and the push lived in the constructor instead of the tick, so
    // toggling the setting never reached them. The logo reads the flag itself
    // and so appeared to work, which is exactly what made the fault look like
    // "only the logo is gated".
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            editor->setSize(1400, 900);

            auto& keyboard = editor->debugPianoKeyboard();
            auto& wheels = editor->debugPerformanceControls();

            // Long enough for the editor's timer to tick at least once.
            const auto settle = []
            { juce::MessageManager::getInstance()->runDispatchLoopUntil(120); };

            settle();
            keyboard.debugSpawnSparks(60);
            wheels.debugSpawnSparkles();
            const auto sparkedWhileOn = keyboard.hasSparks() && wheels.hasSparkles();

            px3::GlobalSettings::getInstance().setAnimationsEnabled(false);
            settle();

            const auto clearedByTheSetting = ! keyboard.hasSparks() && ! wheels.hasSparkles();

            keyboard.debugSpawnSparks(60);
            wheels.debugSpawnSparkles();
            const auto sparkedWhileOff = keyboard.hasSparks() || wheels.hasSparkles();

            px3::GlobalSettings::getInstance().setAnimationsEnabled(true);
            settle();
            keyboard.debugSpawnSparks(60);
            wheels.debugSpawnSparkles();
            const auto sparkedAgain = keyboard.hasSparks() && wheels.hasSparkles();

            check("Settings_TurningAnimationsOffReachesTheKeyboardAndTheWheels",
                  sparkedWhileOn && clearedByTheSetting && ! sparkedWhileOff && sparkedAgain,
                  juce::String(sparkedWhileOn ? "sparks while on" : "no sparks even while on")
                      + ", " + (clearedByTheSetting ? "cleared when turned off"
                                                    : "left running when turned off")
                      + ", " + (sparkedWhileOff ? "still sparks while off" : "silent while off")
                      + ", " + (sparkedAgain ? "sparks again when re-enabled"
                                             : "stayed silent when re-enabled"));
        }
    }

    // ---- SETTINGS: what the two controls actually do ------------------------
    {
        // ---- animations ----------------------------------------------------
        {
            PianoKeyboard keyboard;
            keyboard.setSize(600, 80);

            keyboard.debugSpawnSparks(60);
            const auto sparkedWhenOn = keyboard.hasSparks();

            keyboard.setAnimationsEnabled(false);
            const auto clearedWhenTurnedOff = ! keyboard.hasSparks();
            keyboard.debugSpawnSparks(60);
            const auto sparkedWhenOff = keyboard.hasSparks();

            check("Settings_TheKeyboardStopsSparkingWhenAnimationsAreOff",
                  sparkedWhenOn && clearedWhenTurnedOff && ! sparkedWhenOff,
                  juce::String(sparkedWhenOn ? "sparks when on" : "no sparks even when on")
                      + ", " + (clearedWhenTurnedOff ? "cleared on turning off" : "left running")
                      + ", " + (sparkedWhenOff ? "still sparks when off" : "silent when off"));

            PerformanceControls wheels;
            wheels.setSize(120, 120);

            wheels.debugSpawnSparkles();
            const auto sparkledWhenOn = wheels.hasSparkles();

            wheels.setAnimationsEnabled(false);
            const auto sparklesCleared = ! wheels.hasSparkles();
            wheels.debugSpawnSparkles();
            const auto sparkledWhenOff = wheels.hasSparkles();

            check("Settings_ThePerformanceWheelsStopSparklingWhenAnimationsAreOff",
                  sparkledWhenOn && sparklesCleared && ! sparkledWhenOff,
                  juce::String(sparkledWhenOn ? "sparkles when on" : "none even when on")
                      + ", " + (sparklesCleared ? "cleared on turning off" : "left running")
                      + ", " + (sparkledWhenOff ? "still sparkles when off" : "silent when off"));
        }

        // ---- the setting itself, and where it is kept ----------------------
        {
            PX3SynthAudioProcessor processor;
            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            check("Settings_AnimationsAreOnByDefault",
                  px3::GlobalSettings::getInstance().areAnimationsEnabled(),
                  px3::GlobalSettings::getInstance().areAnimationsEnabled() ? "on" : "off");

            check("Settings_TheAnalogConsoleIsOnByDefault",
                  processor.getAnalogEnabledParam().get(),
                  processor.getAnalogEnabledParam().get() ? "the console runs by default"
                                                          : "the console is off by default");

            check("Settings_TheAnalogProfileStartsClean",
                  processor.getAnalogProfileParam().getIndex() == 0
                      && processor.getAnalogProfileParam().getCurrentChoiceName() == "CLEAN",
                  "the default profile is "
                      + processor.getAnalogProfileParam().getCurrentChoiceName());

            const ScopedAnimationPreference animationsOff(false);
            setChoice(processor, "analogProfile", 3);

            juce::MemoryBlock session;
            processor.getStateInformation(session);

            PX3SynthAudioProcessor reloaded;
            reloaded.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            reloaded.prepareToPlay(kSampleRate, kBlockSize);
            reloaded.setStateInformation(session.getData(), static_cast<int>(session.getSize()));

            // The preference is not IN the session any more - it is global - so
            // what matters here is that restoring a session does not disturb
            // it. Its own persistence is tested against the settings file
            // below.
            check("Settings_RestoringASessionLeavesThePreferenceAlone",
                  ! px3::GlobalSettings::getInstance().areAnimationsEnabled(),
                  px3::GlobalSettings::getInstance().areAnimationsEnabled()
                      ? "a session restore turned it back on"
                      : "the session restore left it off");

            check("Settings_TheAnalogProfileSurvivesASession",
                  reloaded.getAnalogProfileParam().getIndex() == 3,
                  "it comes back as "
                      + reloaded.getAnalogProfileParam().getCurrentChoiceName());

            // A preset carries the profile - it is part of the sound - but NOT
            // the animation preference, which belongs to this editor on this
            // machine. Loading someone else's patch must not turn their
            // preference into yours.
            auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                     .getChildFile("px3-component-tests");
            tempDirectory.createDirectory();
            const auto presetFile = tempDirectory.getChildFile("settings.px3preset");
            presetFile.deleteFile();

            PresetManager manager(processor);
            juce::String error;
            PresetManager::PresetMetadata metadata;
            metadata.name = "Settings";
            metadata.category = "Test";
            metadata.author = "component tests";
            const auto wrote = manager.dumpCurrentStateToPresetFile(presetFile, metadata, true,
                                                                    true, error, nullptr);

            PX3SynthAudioProcessor loader;
            loader.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            loader.prepareToPlay(kSampleRate, kBlockSize);
            PresetManager loaderManager(loader);
            juce::String loadError;
            const auto read = wrote && loaderManager.loadPresetFile(presetFile, loadError);

            check("Settings_APresetCarriesTheAnalogProfile",
                  read && loader.getAnalogProfileParam().getIndex() == 3,
                  read ? "the preset restores " + loader.getAnalogProfileParam().getCurrentChoiceName()
                       : "the preset did not round-trip: " + error + loadError);

            // There is no per-loader preference to keep any more, so the claim
            // is about the GLOBAL one: loading a patch must not move it. It is
            // off for this block, and it has to still be off afterwards.
            check("Settings_LoadingAPresetDoesNotMoveTheGlobalPreference",
                  read && ! px3::GlobalSettings::getInstance().areAnimationsEnabled(),
                  px3::GlobalSettings::getInstance().areAnimationsEnabled()
                      ? "the preset load turned animations back on"
                      : "the preset load left the preference where it was");

            // Not in EITHER tree now, which is stronger than "a preset does not
            // carry it": the property left the plugin's state entirely when the
            // setting became global, so there is nothing to strip and nothing
            // that could come back.
            const auto presetTree = processor.createPresetStateTree();
            const auto sessionTree = processor.createParameterStateTree();
            const juce::Identifier animationsProperty("animationsEnabled");

            check("Settings_NoPluginStateCarriesThePreferenceAtAll",
                  ! presetTree.hasProperty(animationsProperty)
                      && ! sessionTree.hasProperty(animationsProperty),
                  juce::String("the preset tree ")
                      + (presetTree.hasProperty(animationsProperty) ? "carries it" : "omits it")
                      + " and the session tree "
                      + (sessionTree.hasProperty(animationsProperty) ? "carries it" : "omits it"));

            presetFile.deleteFile();
        }
    }

    // ---- the debug panel's preset dump needs a name and an author -----------
    //
    // The dump itself goes through a modal file chooser, which nothing headless
    // can drive. What CAN be tested is the part that gets got wrong: whether
    // the button is available, and what metadata the fields describe.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            editor->setSize(1400, 900);

            // This build has PX3_DEBUG_PANEL off, so the panel's controls have
            // never been configured. Run the setup here rather than skipping
            // the tests in the build the suite is actually run in.
            editor->debugForceSetupPanel();

            auto& name = editor->debugPresetNameField();
            auto& author = editor->debugPresetAuthorField();
            auto& category = editor->debugPresetCategoryField();
            auto& dump = editor->debugPresetDumpButton();

            const auto setFields = [&](const juce::String& n, const juce::String& a)
            {
                name.setText(n, true);
                author.setText(a, true);

                // TextEditor::textChanged POSTS its notification rather than
                // calling it, so onTextChange lands on the next message-loop
                // pump. In the plugin that is immediate; here it has to be
                // asked for, or the button never sees the typing and this
                // reads as a wiring bug.
                juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
                return dump.isEnabled();
            };

            const auto empty = setFields("", "");
            const auto nameOnly = setFields("Glass Filament", "");
            const auto authorOnly = setFields("", "Nate");
            const auto whitespaceOnly = setFields("   ", "   ");
            const auto both = setFields("Glass Filament", "Nate");

            check("DebugPreset_DumpNeedsBothANameAndAnAuthor",
                  ! empty && ! nameOnly && ! authorOnly && ! whitespaceOnly && both,
                  juce::String("empty ") + (empty ? "enabled" : "disabled")
                      + ", name only " + (nameOnly ? "enabled" : "disabled")
                      + ", author only " + (authorOnly ? "enabled" : "disabled")
                      + ", whitespace " + (whitespaceOnly ? "enabled" : "disabled")
                      + ", both " + (both ? "enabled" : "disabled"));

            // The categories offered are the ones the library actually has, so
            // a dump cannot be filed under a category that exists nowhere else.
            PresetManager manager(processor);
            juce::StringArray libraryCategories;
            for (const auto& entry : manager.getAllCategories())
            {
                libraryCategories.add(entry);
            }

            juce::StringArray offered;
            for (int i = 0; i < category.getNumItems(); ++i)
            {
                offered.add(category.getItemText(i));
            }

            check("DebugPreset_TheCategoryDropdownOffersTheLibrarysCategories",
                  offered.size() > 0 && offered == libraryCategories,
                  juce::String(offered.size()) + " offered: " + offered.joinIntoString(", "));

            // What the fields describe is what a dump would carry.
            // Deliberately NOT the second entry, which is EXPERIMENTAL - the
            // same value the empty-category fallback produces. Selecting it
            // would let this pass with the dropdown never being read.
            category.setSelectedId(3, juce::sendNotificationSync);
            const auto metadata = editor->debugPresetDumpMetadata();

            check("DebugPreset_TheFieldsBecomeThePresetsMetadata",
                  metadata.name == "Glass Filament"
                      && metadata.author == "Nate"
                      && metadata.category == category.getText()
                      && metadata.category != "EXPERIMENTAL",
                  "name \"" + metadata.name + "\", author \"" + metadata.author
                      + "\", category \"" + metadata.category + "\"");

            // And a file written with that metadata reads back with it, which
            // is the half the chooser would have done.
            auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                     .getChildFile("px3-component-tests");
            tempDirectory.createDirectory();
            const auto dumpFile = tempDirectory.getChildFile("dumped.px3preset");
            dumpFile.deleteFile();

            juce::String error;
            const auto wrote = manager.dumpCurrentStateToPresetFile(dumpFile, metadata, true, true,
                                                                    error, nullptr);

            // Read straight out of the file, which is what a browser or
            // another install would see.
            juce::String writtenName, writtenAuthor, writtenCategory;
            if (wrote)
            {
                if (auto xml = juce::XmlDocument::parse(dumpFile))
                {
                    const auto tree = juce::ValueTree::fromXml(*xml);
                    writtenName = tree.getProperty("name").toString();
                    writtenAuthor = tree.getProperty("author").toString();
                    writtenCategory = tree.getProperty("category").toString();
                }
            }

            check("DebugPreset_ADumpedFileCarriesTheAuthorAndCategory",
                  wrote && writtenAuthor == "Nate"
                      && writtenCategory == metadata.category
                      && writtenName == "Glass Filament",
                  wrote ? "the file reads \"" + writtenName + "\" by \"" + writtenAuthor
                              + "\" in " + writtenCategory
                        : "the dump was not written: " + error);

            dumpFile.deleteFile();
        }
    }

    // ---- every graph key in the shipped config does something ---------------
    //
    // The project's rule is that a property in UIConfig.json corresponds to
    // real behaviour. Four stroke keys under every envelope's visual.graph
    // block did not: the card read them into locals and discarded all of them,
    // and the editor that took over the drawing reads a different set. Editing
    // them changed nothing.
    //
    // This walks the shipped file and asserts that each graph key it finds is
    // one some component actually reads.
    {
        const auto configFile = juce::File::getCurrentWorkingDirectory()
                                    .getChildFile("Source/UI/UIConfig.json");

        juce::StringArray orphaned;
        auto checked = 0;

        if (configFile.existsAsFile())
        {
            const auto text = configFile.loadFileAsString();
            const auto sources = juce::File::getCurrentWorkingDirectory()
                                     .getChildFile("Source/UI/EnvelopeComponent.cpp")
                                     .loadFileAsString()
                                 + juce::File::getCurrentWorkingDirectory()
                                       .getChildFile("Source/UI/BreakpointEnvelopeEditor.cpp")
                                       .loadFileAsString();

            // The keys the envelope graphs ship under visual.graph.
            const juce::StringArray graphKeys { "cornerRadius", "fillColour", "fillAlpha",
                                                "strokeThickness", "strokeAlphaEnabled",
                                                "strokeAlphaDisabled", "strokeColourEnabled",
                                                "strokeColourDisabled", "bottomGap" };

            for (const auto& key : graphKeys)
            {
                const auto inConfig = text.contains("\"" + key + "\"");
                if (! inConfig) { continue; }

                ++checked;

                // Read by name somewhere in the two files that draw an
                // envelope graph.
                if (! sources.contains("\"" + key + "\"")
                    && ! sources.contains("." + key + "\""))
                {
                    orphaned.add(key);
                }
            }
        }

        check("Config_EveryEnvelopeGraphKeyIsRead",
              checked > 0 && orphaned.isEmpty(),
              orphaned.isEmpty()
                  ? juce::String(checked) + " graph keys in the shipped config are all read"
                  : "shipped but read by nobody: " + orphaned.joinIntoString(", "));
    }

    // ---- SETTINGS: a view of its own ----------------------------------------
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            editor->setSize(1400, 900);

            auto* bar = editor->debugTopMenuBar();
            auto* strip = editor->debugMacroStrip();
            auto* panel = editor->debugSettingsPanel();

            check("Settings_TheEditorHasASettingsButtonAndPanel",
                  bar != nullptr && panel != nullptr,
                  juce::String(bar != nullptr ? "bar" : "no bar") + ", "
                      + (panel != nullptr ? "panel" : "no panel"));

            if (bar != nullptr && panel != nullptr)
            {
                auto& gear = bar->getSettingsButton();

                check("Settings_TheButtonSaysWhatItIs",
                      gear.getTooltip() == "Settings",
                      "hovering reads \"" + gear.getTooltip() + "\"");

                // About as wide as a section tab, which is the requirement -
                // measured against the tab rather than against a number, so it
                // stays true when the bar is resized.
                const auto oscWidth = bar->getSectionButtonBounds(0).getWidth();
                const auto gearWidth = gear.getWidth();
                check("Settings_TheButtonIsAboutAsWideAsASectionTab",
                      oscWidth > 0 && std::abs(gearWidth - oscWidth) <= 2,
                      juce::String(gearWidth) + " px against OSC's " + juce::String(oscWidth));

                // It sits after MENU, which is what "between the menu and the
                // master gain" means inside the bar.
                check("Settings_TheButtonSitsAfterTheMenuButton",
                      gear.getX() >= bar->getPresetMenuButtonBounds().getRight(),
                      "the gear starts at x " + juce::String(gear.getX())
                          + " and MENU ends at "
                          + juce::String(bar->getPresetMenuButtonBounds().getRight()));

                // Selecting SETTINGS lights the gear and unlights every panel
                // tab: the strip must not claim a panel is open when it is not.
                editor->debugSelectSection(6);
                editor->resized();

                auto anySectionLit = false;
                for (int i = 0; i < 6; ++i)
                {
                    anySectionLit = anySectionLit || bar->getSectionButton(i).getToggleState();
                }

                check("Settings_SelectingItUnlightsEveryPanelTab",
                      gear.getToggleState() && ! anySectionLit,
                      juce::String(gear.getToggleState() ? "the gear is lit" : "the gear is dark")
                          + ", and a panel tab is "
                          + (anySectionLit ? "still lit" : "not lit"));

                check("Settings_ThePanelIsTheVisibleOne",
                      panel->isVisible(),
                      panel->isVisible() ? "the settings panel is showing" : "it is hidden");

                // Full width: no macro strip beside it.
                const auto settingsWidth = panel->getWidth();
                const auto stripHiddenHere = strip == nullptr || ! strip->isVisible();

                editor->debugSelectSection(0);
                editor->resized();
                const auto stripBackOnPanels = strip != nullptr && strip->isVisible();

                editor->debugSelectSection(6);
                editor->resized();

                check("Settings_TheMacroStripIsNotShownHere",
                      stripHiddenHere && stripBackOnPanels,
                      juce::String(stripHiddenHere ? "hidden on SETTINGS" : "still on SETTINGS")
                          + ", " + (stripBackOnPanels ? "back on OSC" : "gone from OSC too"));

                // The controls drive the processor, which is the whole
                // point of the panel: a form that displays state without
                // writing it is a picture of a settings page.
                {
                    auto& toggle = panel->debugAnimationsToggle();
                    auto& profiles = panel->debugAnalogProfileBox();

                    check("Settings_TheCheckboxStartsCheckedLikeTheSetting",
                          toggle.getToggleState() && px3::GlobalSettings::getInstance().areAnimationsEnabled(),
                          juce::String("the box is ")
                              + (toggle.getToggleState() ? "checked" : "clear")
                              + " and the setting is "
                              + (px3::GlobalSettings::getInstance().areAnimationsEnabled() ? "on" : "off"));

                    toggle.setToggleState(false, juce::sendNotificationSync);
                    const auto turnedOff = ! px3::GlobalSettings::getInstance().areAnimationsEnabled();
                    toggle.setToggleState(true, juce::sendNotificationSync);
                    const auto turnedBackOn = px3::GlobalSettings::getInstance().areAnimationsEnabled();

                    check("Settings_TheCheckboxDrivesTheSetting",
                          turnedOff && turnedBackOn,
                          juce::String("unchecking ")
                              + (turnedOff ? "turned it off" : "did nothing")
                              + ", rechecking "
                              + (turnedBackOn ? "turned it back on" : "did nothing"));

                    const auto names = px3::AnalogEngine::profileNames();
                    juce::StringArray listed;
                    for (int i = 0; i < profiles.getNumItems(); ++i)
                    {
                        listed.add(profiles.getItemText(i));
                    }

                    check("Settings_TheDropdownListsEveryAnalogProfile",
                          listed == names,
                          juce::String(profiles.getNumItems()) + " profiles: "
                              + listed.joinIntoString(", "));

                    profiles.setSelectedId(2, juce::sendNotificationSync);
                    const auto movedToBritish = processor.getAnalogProfileParam().getIndex() == 1;

                    check("Settings_TheDropdownDrivesTheParameter",
                          movedToBritish,
                          "choosing the second profile leaves the parameter at "
                              + processor.getAnalogProfileParam().getCurrentChoiceName());

                    // And it follows the parameter back, so host automation or
                    // a preset load is reflected here rather than leaving the
                    // menu showing something the synth is not doing.
                    setChoice(processor, "analogProfile", 4);
                    panel->refreshFromParameters();

                    check("Settings_TheDropdownFollowsTheParameter",
                          profiles.getSelectedId() == 5,
                          "with the parameter on "
                              + processor.getAnalogProfileParam().getCurrentChoiceName()
                              + " the menu shows " + profiles.getText());
                }

                // The gear reads as a gear: teeth around the outside and a
                // hole through the middle. Checked by rendering, because the
                // requirement is about what it looks like.
                {
                    editor->debugSelectSection(6);
                    editor->resized();

                    const auto image = gear.createComponentSnapshot(gear.getLocalBounds());
                    const auto w = image.getWidth();
                    const auto h = image.getHeight();

                    const auto brightnessAt = [&](int x, int y)
                    {
                        return image.getPixelAt(juce::jlimit(0, w - 1, x),
                                                juce::jlimit(0, h - 1, y)).getBrightness();
                    };

                    // The face behind the icon, sampled in a corner well clear
                    // of it.
                    const auto face = brightnessAt(2, 2);
                    const auto centre = brightnessAt(w / 2, h / 2);

                    // A ring of samples between the bore and the teeth: the
                    // body of the gear, which must be lit.
                    const auto side = juce::jmin(w, h);
                    auto bodySamples = 0;
                    auto litBody = 0;
                    for (int i = 0; i < 24; ++i)
                    {
                        const auto theta = juce::MathConstants<float>::twoPi
                                           * static_cast<float>(i) / 24.0f;
                        // The icon is scaled to a square of side*0.46, so its
                        // outer RADIUS is half of that. Sampling at 0.46 of
                        // the side puts the ring outside the gear entirely,
                        // which is how the first version of this read 2 lit
                        // samples out of 24 and looked like a drawing bug.
                        const auto outerRadius = static_cast<float>(side) * 0.46f * 0.5f;
                        const auto r = outerRadius * 0.55f;
                        const auto x = w / 2 + juce::roundToInt(std::cos(theta) * r);
                        const auto y = h / 2 + juce::roundToInt(std::sin(theta) * r);
                        ++bodySamples;
                        if (brightnessAt(x, y) > face + 0.25f) { ++litBody; }
                    }

                    check("Settings_TheGearHasALitBodyAndAHoleThroughIt",
                          litBody > bodySamples * 3 / 4
                              && std::abs(centre - face) < 0.12f,
                          juce::String(litBody) + " of " + juce::String(bodySamples)
                              + " body samples are lit, and the centre reads "
                              + fmt(centre, 3) + " against a face of " + fmt(face, 3));
                }

                check("Settings_ThePanelIsFullWidth",
                      strip != nullptr
                          && settingsWidth > editor->debugPanelViewportArea().getWidth()
                                                 - 4,
                      juce::String(settingsWidth) + " px wide against the viewport's "
                          + juce::String(editor->debugPanelViewportArea().getWidth()));
            }
        }
    }

    // ---- mode is the authority, not the point count -------------------------
    //
    // A breakpoint envelope with four points is EXACTLY what seeding from an
    // ADSR produces, and it is what deleting points lands back on. If anything
    // reads "four points, sustain at index 2" as "this is an ADSR", the mode
    // stops meaning anything at precisely the shapes users arrive at.
    {
        auto seeded = px3::BreakpointEnvelope::fromAdsr([]
        {
            EnvelopeSettings s;
            s.attackSeconds = 0.10f;
            s.decaySeconds = 0.20f;
            s.sustainLevel = 0.5f;
            s.releaseSeconds = 0.30f;
            return s;
        }());
        seeded.setMode(px3::BreakpointEnvelope::Mode::breakpoint);

        check("EnvIso_AFourPointBreakpointIsNotAnAdsrSkeleton",
              ! seeded.isAdsrSkeleton(),
              seeded.isAdsrSkeleton()
                  ? "a 4-point Breakpoint envelope reports itself as an ADSR skeleton"
                  : "it does not claim to be an ADSR skeleton");

        check("EnvIso_AStraightBreakpointIsNotAPlainAdsr",
              ! seeded.isPlainAdsr(),
              seeded.isPlainAdsr()
                  ? "a straight 4-point Breakpoint envelope reports itself as a plain ADSR, so "
                    "the voice is never given the shape at all"
                  : "it does not claim to be a plain ADSR");

        // Deleting down to two points. A breakpoint envelope needs a start and
        // an end and nothing else; the sustain index is ADSR bookkeeping and
        // must not protect a point here.
        auto shrinking = seeded;
        auto removals = 0;
        for (int guard = 0; guard < 8 && shrinking.getPointCount() > 2; ++guard)
        {
            auto removedAny = false;
            for (int i = shrinking.getPointCount() - 2; i >= 1; --i)
            {
                if (shrinking.removePoint(i)) { removedAny = true; ++removals; break; }
            }
            if (! removedAny) { break; }
        }

        check("EnvIso_BreakpointPointsDeleteDownToThree",
              shrinking.getPointCount() == 3,
              "after " + juce::String(removals) + " removals it has "
                  + juce::String(shrinking.getPointCount()) + " points");

        // And the last one stays. At two, both points are the anchored ends and
        // there is nothing left to drag, so the third is what keeps the mode
        // usable rather than a point like any other.
        check("EnvIso_TheLastMovablePointCannotBeRemoved",
              shrinking.getPointCount() == 3 && ! shrinking.canRemovePoint(1),
              "with 3 points the middle one reports "
                  + juce::String(shrinking.canRemovePoint(1) ? "removable" : "not removable"));

        check("EnvIso_AShrunkEnvelopeIsStillBreakpoint",
              shrinking.isBreakpointMode() && ! shrinking.isAdsrSkeleton(),
              shrinking.isBreakpointMode() ? "still in Breakpoint mode"
                                           : "it left Breakpoint mode on the way down");
    }

    // ---- the editor/parameter loop is broken in Breakpoint mode -------------
    //
    // The reported symptom. Dragging a point wrote the four ADSR parameters,
    // and the next refresh rebuilt the shape from them - so a Breakpoint
    // envelope that happened to have four points behaved as a partly working
    // ADSR editor. Both halves are tested through the real components, because
    // the loop only exists once they are wired to each other.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        setParam(processor, "env1Attack", 0.100f);
        setParam(processor, "env1Decay", 0.200f);
        setParam(processor, "env1Sustain", 0.50f);
        setParam(processor, "env1Release", 0.300f);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
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

            // ENV 1, which is slot 1 - the loop this test is about only exists
            // where Breakpoint mode does, and AMP ENV is ADSR-only. The first
            // EnvelopeComponent the walk reaches IS ENV 1; naming that here
            // rather than relying on it is what keeps the test honest if the
            // order ever changes.
            EnvelopeComponent* env1 = nullptr;
            ModPanel* modPanel = nullptr;
            std::function<void(juce::Component&)> findEnv1 = [&](juce::Component& parent)
            {
                for (auto* child : parent.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* panel = dynamic_cast<ModPanel*>(child)) { modPanel = panel; }
                    if (auto* env = dynamic_cast<EnvelopeComponent*>(child))
                    {
                        if (env1 == nullptr && ! env->isAdsrOnly()) { env1 = env; }
                    }
                    findEnv1(*child);
                }
            };
            findEnv1(*editor);
            if (env1 != nullptr && modPanel != nullptr && env1->onEnvelopeEdited != nullptr)
            {
                processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);

                // Four points, the shape seeding produces - the exact geometry
                // that used to read as an ADSR - dragged somewhere an ADSR
                // cannot describe.
                const auto seeded = processor.getShapedEnvelope(1);
                check("EnvIso_SeedingUsesTheAdsrTheUserCanSee",
                      std::abs(seeded.getPoint(1).timeSeconds - 0.100) < 1.0e-4
                          && std::abs(seeded.getPoint(2).value - 0.50) < 1.0e-4,
                      "the seed reads a " + fmt(seeded.getPoint(1).timeSeconds, 3)
                          + " s attack to a sustain of " + fmt(seeded.getPoint(2).value, 2)
                          + ", against the parameters' 0.100 / 0.50");

                // Dragged inside the shape's own span, so nothing is clamped by
                // its neighbours - the point is what the edit DOES downstream.
                auto drawn = seeded;
                drawn.setPoint(1, 0.040, 0.62);
                drawn.setPoint(2, 0.220, 0.90);
                env1->onEnvelopeEdited(drawn);

                const auto after = processor.envelopeParameterSettings(0);
                check("EnvIso_DraggingInBreakpointModeDoesNotWriteTheAdsrParameters",
                      std::abs(after.attackSeconds - 0.100f) < 1.0e-4f
                          && std::abs(after.decaySeconds - 0.200f) < 1.0e-4f
                          && std::abs(after.sustainLevel - 0.50f) < 1.0e-4f
                          && std::abs(after.releaseSeconds - 0.300f) < 1.0e-4f,
                      "after a 4-point Breakpoint drag the parameters read A "
                          + fmt(after.attackSeconds, 3) + " D " + fmt(after.decaySeconds, 3)
                          + " S " + fmt(after.sustainLevel, 2) + " R "
                          + fmt(after.releaseSeconds, 3));

                // And the refresh must not rebuild the drawing from them.
                for (auto* child : editor->getChildren())
                modPanel->refreshFromParameters();

                // What the GRAPH was handed, not what the processor stored. A
                // refresh writes the component, so reading the processor here
                // measured a value the refresh never touches - it passed with
                // the rebuild fully in place.
                const auto kept = env1->debugEditor().getEnvelope();
                check("EnvIso_ARefreshDoesNotRebuildABreakpointShapeFromTheAdsr",
                      kept.isBreakpointMode()
                          && std::abs(kept.getPoint(1).timeSeconds - 0.040) < 1.0e-6
                          && std::abs(kept.getPoint(1).value - 0.62) < 1.0e-6
                          && std::abs(kept.getPoint(2).value - 0.90) < 1.0e-6,
                      "the drawing still reads P1 (" + fmt(kept.getPoint(1).timeSeconds, 2) + ", "
                          + fmt(kept.getPoint(1).value, 2) + ") P2 ("
                          + fmt(kept.getPoint(2).timeSeconds, 2) + ", "
                          + fmt(kept.getPoint(2).value, 2) + ")");
            }

            // Every card that OFFERS the mode: they are one component, so the
            // rule has to hold on all of them or the abstraction is not the
            // boundary. AMP ENV is excluded because it has no Breakpoint mode
            // to hold - and that exclusion is asserted rather than assumed,
            // below, so this cannot quietly become a test of nothing.
            auto capable = 0;
            auto cardsHolding = 0;
            for (std::size_t i = 0; i < cards.size(); ++i)
            {
                const auto slot = static_cast<int>(i);
                if (! PX3SynthAudioProcessor::envelopeSupportsBreakpointMode(slot)) { continue; }

                ++capable;
                processor.setEnvelopeMode(slot, px3::BreakpointEnvelope::Mode::breakpoint);
                auto shape = processor.getShapedEnvelope(slot);
                if (! shape.isAdsrSkeleton() && shape.isBreakpointMode()) { ++cardsHolding; }
            }
            check("EnvIso_NoCapableCardReadsAFourPointDrawingAsAnAdsr",
                  capable > 0 && cardsHolding == capable,
                  juce::String(cardsHolding) + " of " + juce::String(capable)
                      + " breakpoint-capable cards keep Breakpoint semantics at four points");

            // AMP ENV refuses the mode outright, whatever it is asked.
            processor.setEnvelopeMode(0, px3::BreakpointEnvelope::Mode::breakpoint);
            check("EnvIso_AmpEnvRefusesBreakpointMode",
                  processor.getEnvelopeMode(0) == px3::BreakpointEnvelope::Mode::adsr
                      && ! processor.getShapedEnvelope(0).isBreakpointMode(),
                  processor.getEnvelopeMode(0) == px3::BreakpointEnvelope::Mode::adsr
                      ? "asked for Breakpoint, AMP ENV stays ADSR"
                      : "AMP ENV entered Breakpoint mode");
        }
    }

    // ---- the card itself edits no ADSR parameter ----------------------------
    //
    // EnvelopeComponent used to carry a second, older ADSR editor: it painted
    // an A / D-S / R handle path and dragged those handles straight into the
    // four parameters. That is deleted; this is the guard that keeps card-level
    // dragging from acquiring parameter-writing powers again. The band just
    // outside the child editor is where such a thing would live, because the
    // child does not cover the whole of the card's graph rectangle.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        setParam(processor, "env1Attack", 0.120f);
        setParam(processor, "env1Decay", 0.240f);
        setParam(processor, "env1Sustain", 0.55f);
        setParam(processor, "env1Release", 0.360f);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        if (editor != nullptr)
        {
            editor->setSize(1400, 900);

            EnvelopeComponent* card = nullptr;
            std::function<void(juce::Component&)> walk = [&](juce::Component& parent)
            {
                for (auto* child : parent.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* env = dynamic_cast<EnvelopeComponent*>(child))
                    {
                        if (card == nullptr) { card = env; }
                    }
                    walk(*child);
                }
            };
            walk(*editor);

            if (card != nullptr)
            {
                processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
                card->setEnvelopeMode(px3::BreakpointEnvelope::Mode::breakpoint);

                const auto before = processor.envelopeParameterSettings(0);

                const auto event = [card](juce::Point<float> at)
                {
                    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                                            juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                            card, card, juce::Time::getCurrentTime(), at,
                                            juce::Time::getCurrentTime(), 1, false);
                };

                // Sweep the band just outside the child editor, where the
                // parent still gets the click.
                const auto ed = card->debugEditorBounds().toFloat();
                auto moved = 0;
                for (auto dy : { -4.0f, -2.0f, 2.0f, 4.0f, 6.0f, 8.0f })
                {
                    for (auto fx : { 0.15f, 0.35f, 0.55f, 0.75f, 0.95f })
                    {
                        const auto y = dy < 0.0f ? ed.getY() + dy : ed.getBottom() + dy;
                        const juce::Point<float> at(ed.getX() + ed.getWidth() * fx, y);
                        card->mouseDown(event(at));
                        card->mouseDrag(event(at.translated(-25.0f, -18.0f)));
                        card->mouseUp(event(at.translated(-25.0f, -18.0f)));
                    }
                }

                const auto after = processor.envelopeParameterSettings(0);
                const auto unchanged
                    = std::abs(after.attackSeconds - before.attackSeconds) < 1.0e-5f
                      && std::abs(after.decaySeconds - before.decaySeconds) < 1.0e-5f
                      && std::abs(after.sustainLevel - before.sustainLevel) < 1.0e-5f
                      && std::abs(after.releaseSeconds - before.releaseSeconds) < 1.0e-5f;

                check("EnvIso_DraggingTheCardItselfMovesNoAdsrParameter",
                      unchanged,
                      unchanged
                          ? "30 drags around the graph's edge moved no ADSR parameter"
                          : "a drag on the card moved the ADSR to A "
                                + fmt(after.attackSeconds, 3) + " D " + fmt(after.decaySeconds, 3)
                                + " S " + fmt(after.sustainLevel, 2) + " R "
                                + fmt(after.releaseSeconds, 3));
            }
        }
    }

    // ---- ADSR -> BP -> ADSR -> BP, editing at every stop --------------------
    //
    // Each mode has to survive edits made in the other, in both directions,
    // repeatedly. Run on every slot, because the four cards are one component
    // and a per-slot mistake would otherwise hide behind AMP ENV passing.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        juce::StringArray failures;

        // Slot 0 is AMP ENV, which has no Breakpoint mode to round-trip
        // through. It gets its own check afterwards rather than being skipped
        // silently.
        for (int slot = 1; slot < 4; ++slot)
        {
            const juce::String prefix = "env" + juce::String(slot);
            const auto attackId = slot == 0 ? juce::String("ampAttack") : prefix + "Attack";
            const auto sustainId = slot == 0 ? juce::String("ampSustain") : prefix + "Sustain";

            const auto attackWanted = 0.080f + 0.020f * static_cast<float>(slot);
            const auto sustainWanted = 0.30f + 0.10f * static_cast<float>(slot);
            setParam(processor, attackId, attackWanted);
            setParam(processor, sustainId, sustainWanted);

            const auto settingsNow = [&]
            { return slot == 0 ? processor.currentAmpEnvelopeSettings()
                               : processor.envelopeParameterSettings(slot - 1); };

            // ADSR: bend a segment.
            {
                auto shape = processor.getShapedEnvelope(slot);
                shape.setCurve(1, 0.45);
                processor.setShapedEnvelope(slot, shape);
            }

            // -> Breakpoint, and draw something no ADSR can say.
            processor.setEnvelopeMode(slot, px3::BreakpointEnvelope::Mode::breakpoint);
            {
                auto drawn = processor.getShapedEnvelope(slot);
                drawn.addPoint(0.18, 0.93);
                drawn.addPoint(0.30, 0.11);
                processor.setShapedEnvelope(slot, drawn);
            }
            const auto drawnCount = processor.getShapedEnvelope(slot).getPointCount();

            // -> ADSR. The parameters and the bend must be exactly as left.
            processor.setEnvelopeMode(slot, px3::BreakpointEnvelope::Mode::adsr);
            const auto adsrBack = settingsNow();
            const auto runningAdsr = slot == 0 ? processor.currentAmpEnvelope()
                                               : processor.currentModEnvelope(slot - 1);
            if (std::abs(adsrBack.attackSeconds - attackWanted) > 1.0e-4f
                || std::abs(adsrBack.sustainLevel - sustainWanted) > 1.0e-4f
                || runningAdsr.getPointCount() != 4
                || std::abs(runningAdsr.getPoint(1).curveToNext - 0.45) > 1.0e-9)
            {
                failures.add(prefix + " lost its ADSR on the way back");
            }

            // Edit the ADSR while it is live.
            setParam(processor, attackId, attackWanted + 0.030f);

            // -> Breakpoint. The drawing returns untouched by that edit.
            processor.setEnvelopeMode(slot, px3::BreakpointEnvelope::Mode::breakpoint);
            const auto drawingBack = processor.getShapedEnvelope(slot);
            // By value, not by index: where an added point lands depends on the
            // seeded times, which differ per slot, so an index here tests the
            // seed rather than the round trip.
            auto foundTheDrawnPeak = false;
            for (int i = 0; i < drawingBack.getPointCount(); ++i)
            {
                foundTheDrawnPeak = foundTheDrawnPeak
                                    || std::abs(drawingBack.getPoint(i).value - 0.93) < 1.0e-9;
            }

            if (drawingBack.getPointCount() != drawnCount
                || ! drawingBack.isBreakpointMode()
                || ! foundTheDrawnPeak)
            {
                failures.add(prefix + " lost its drawing on the way back ("
                             + juce::String(drawingBack.getPointCount()) + " of "
                             + juce::String(drawnCount) + " points, peak "
                             + (foundTheDrawnPeak ? "kept" : "gone") + ")");
            }

            // Edit the drawing, then -> ADSR: the ADSR must show the edit made
            // to IT, not anything derived from the drawing.
            {
                auto more = drawingBack;
                more.addPoint(0.44, 0.66);
                processor.setShapedEnvelope(slot, more);
            }
            processor.setEnvelopeMode(slot, px3::BreakpointEnvelope::Mode::adsr);
            if (std::abs(settingsNow().attackSeconds - (attackWanted + 0.030f)) > 1.0e-4f)
            {
                failures.add(prefix + " did not keep the ADSR edit");
            }
        }

        check("EnvIso_EveryCapableCardSurvivesRepeatedSwitchingWithEditsBetween",
              failures.isEmpty(),
              failures.isEmpty()
                  ? "all three mod envelopes round-tripped twice with edits at every stop"
                  : failures.joinIntoString("; "));

        // AMP ENV has nothing to round-trip: asked repeatedly for Breakpoint,
        // it stays ADSR and keeps the shape it had.
        setParam(processor, "ampAttack", 0.135f);
        {
            auto bent = processor.getShapedEnvelope(0);
            bent.setCurve(1, -0.55);
            processor.setShapedEnvelope(0, bent);
        }
        for (int i = 0; i < 4; ++i)
        {
            processor.setEnvelopeMode(0, px3::BreakpointEnvelope::Mode::breakpoint);
            processor.setEnvelopeMode(0, px3::BreakpointEnvelope::Mode::adsr);
        }

        const auto ampAfter = processor.currentAmpEnvelope();
        check("EnvIso_AmpEnvStaysAdsrThroughRepeatedRequests",
              processor.getEnvelopeMode(0) == px3::BreakpointEnvelope::Mode::adsr
                  && ampAfter.getPointCount() == 4
                  && std::abs(ampAfter.getPoint(1).curveToNext + 0.55) < 1.0e-9
                  && std::abs(processor.currentAmpEnvelopeSettings().attackSeconds - 0.135f)
                         < 1.0e-4f,
              "after four requests for Breakpoint it is still ADSR over "
                  + juce::String(ampAfter.getPointCount()) + " points, bend "
                  + fmt(static_cast<float>(ampAfter.getPoint(1).curveToNext), 2) + ", A "
                  + fmt(processor.currentAmpEnvelopeSettings().attackSeconds, 3));
    }

    // ---- a two-point breakpoint envelope is a first-class envelope ----------
    //
    // P0 ---------------- P1. The brief's critical case: two points must not
    // read as "not enough points to be an envelope" and quietly recover ADSR
    // behaviour anywhere along the chain.
    {
        constexpr double kRate = 1000.0;

        auto ramp = px3::BreakpointEnvelope::fromAdsr(EnvelopeSettings {});
        ramp.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        {
            px3::BreakpointEnvelope::Point points[2] = {
                { 0.00, 0.0, 0.0 },
                { 0.50, 0.0, 0.0 }
            };
            ramp.setPoints(points, 2, 1);
            ramp.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        }

        check("EnvIso_TwoPointsSurviveBeingSet",
              ramp.getPointCount() == 2 && ramp.isBreakpointMode(),
              juce::String(ramp.getPointCount()) + " points, mode "
                  + (ramp.isBreakpointMode() ? "Breakpoint" : "ADSR"));

        // It plays, on one clock, and it ends. Flat, because BOTH of its points
        // are the anchored ends - an envelope begins and ends at rest, so two
        // points is the count at which a shape has no room to say anything.
        // That is a Breakpoint rule about anchoring, not ADSR meaning returning:
        // the tests above show it is still a Breakpoint envelope throughout.
        AmpEnvelope amp;
        amp.prepare(kRate);
        amp.setEnvelope(ramp);
        amp.noteOn();

        auto peak = 0.0f;
        for (int i = 0; i <= static_cast<int>(0.60 * kRate); ++i)
        {
            peak = juce::jmax(peak, std::abs(amp.getNextSample()));
        }

        check("EnvIso_ATwoPointEnvelopeIsFlatBecauseBothEndsAreAnchored",
              peak < 1.0e-4f,
              "it peaks at " + fmt(peak, 5) + ", both of its points being the anchored ends");

        check("EnvIso_ATwoPointEnvelopeFinishesRatherThanHolding",
              ! amp.isActive(),
              amp.isActive() ? "it is still sounding past its last point, so something is holding"
                             : "it ended at its last point, key still down");

        // Three points is the smallest envelope that can carry a shape, and it
        // must play exactly what it draws - no hold, no ADSR stage anywhere.
        auto three = ramp;
        {
            px3::BreakpointEnvelope::Point points[3] = {
                { 0.00, 0.0, 0.0 },
                { 0.25, 1.0, 0.0 },
                { 0.50, 0.0, 0.0 }
            };
            three.setPoints(points, 3, 1);
            three.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        }

        AmpEnvelope shaped;
        shaped.prepare(kRate);
        shaped.setEnvelope(three);
        shaped.noteOn();
        std::vector<float> trace;
        for (int i = 0; i <= static_cast<int>(0.60 * kRate); ++i) { trace.push_back(shaped.getNextSample()); }
        const auto at = [&trace](double seconds)
        { return trace[static_cast<std::size_t>(std::lround(seconds * kRate))]; };

        check("EnvIso_AThreePointEnvelopePlaysExactlyItsShape",
              at(0.00) < 0.05f && at(0.125) > 0.4f && at(0.125) < 0.6f && at(0.24) > 0.9f
                  && at(0.49) < 0.1f,
              "it reads " + fmt(at(0.00), 3) + " at the start, " + fmt(at(0.125), 3)
                  + " mid-rise, " + fmt(at(0.24), 3) + " at the peak and " + fmt(at(0.49), 3)
                  + " at the end");

        // The curve is editable, and the DSP follows it.
        auto bent = three;
        bent.setCurve(0, 0.8);
        AmpEnvelope curved;
        curved.prepare(kRate);
        curved.setEnvelope(bent);
        curved.noteOn();
        float halfway = 0.0f;
        for (int i = 0; i <= static_cast<int>(0.125 * kRate); ++i) { halfway = curved.getNextSample(); }

        check("EnvIso_TheSegmentCurveOfAThreePointEnvelopeIsHonoured",
              halfway > at(0.125) + 0.05f,
              "bent, the midpoint reads " + fmt(halfway, 3) + " against the straight "
                  + fmt(at(0.125), 3));

        // The fill follows elapsed time across the single segment.
        {
            BreakpointEnvelopeEditor graph;
            graph.setSize(400, 200);
            graph.setEnvelope(ramp);

            EnvelopePosition position;
            position.active = true;
            position.inRelease = false;
            position.heldSeconds = 0.35;
            graph.setProgress(position);

            check("EnvIso_TheFillOfATwoPointEnvelopeIsTimeDriven",
                  std::abs(graph.progressDisplayTime() - 0.35) < 1.0e-6,
                  "the fill reads " + fmt(graph.progressDisplayTime(), 3)
                      + " s at 0.35 s elapsed");
        }
    }

    // ---- state carrying only two points is put right on the way in ----------
    //
    // Editing cannot reach two points any more, so the repair in the restore
    // path has no test unless one is written deliberately: this builds the
    // two-point state directly and loads it. Without that, the repair is code
    // nothing exercises, which is the same as code that does not work.
    {
        PX3SynthAudioProcessor saver;
        saver.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        saver.prepareToPlay(kSampleRate, kBlockSize);
        saver.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);

        {
            // setPoints is the model's structural floor, which is still two -
            // that is what makes such a state representable and therefore
            // loadable.
            auto bare = saver.getShapedEnvelope(1);
            px3::BreakpointEnvelope::Point points[2] = {
                { 0.00, 0.0, 0.0 },
                { 0.80, 0.0, 0.0 }
            };
            bare.setPoints(points, 2, 0);
            bare.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
            saver.setShapedEnvelope(1, bare);

            check("EnvFloor_TheModelStillRepresentsTwoPoints",
                  saver.getShapedEnvelope(1).getPointCount() == 2,
                  "the stored shape holds "
                      + juce::String(saver.getShapedEnvelope(1).getPointCount()) + " points");
        }

        juce::MemoryBlock saved;
        saver.getStateInformation(saved);

        PX3SynthAudioProcessor loader;
        loader.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        loader.prepareToPlay(kSampleRate, kBlockSize);
        loader.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

        const auto loaded = loader.getShapedEnvelope(1);
        const auto midpointOnTheLine
            = loaded.getPointCount() == 3
              && std::abs(loaded.getPoint(1).timeSeconds - 0.40) < 1.0e-9
              && std::abs(loaded.getPoint(1).value) < 1.0e-9;

        check("EnvFloor_LoadingTwoPointsGivesBackAMovableMiddle",
              loaded.isBreakpointMode() && midpointOnTheLine,
              "a two-point state loads as " + juce::String(loaded.getPointCount())
                  + " points with the middle at " + fmt(loaded.getPoint(1).timeSeconds, 2)
                  + " s, value " + fmt(loaded.getPoint(1).value, 2));

        // And it lands ON the line, so nothing about the sound changed.
        check("EnvFloor_TheRepairDoesNotChangeTheSound",
              loaded.getTotalSeconds() > 0.79 && loaded.getTotalSeconds() < 0.81,
              "the envelope still runs " + fmt(loaded.getTotalSeconds(), 2) + " s");
    }

    // ---- a session claiming AMP ENV is a breakpoint envelope ----------------
    //
    // The processor will not create such a state, so it is built by editing the
    // saved tree - which is exactly how one would arrive: a project saved
    // before AMP ENV became ADSR-only. The restore path does not go through
    // setEnvelopeMode; it builds a shape, sets the mode on it and stores it. So
    // this is the case that proves the rule lives at the STORE, not at the UI's
    // door.
    {
        PX3SynthAudioProcessor writer;
        writer.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        writer.prepareToPlay(kSampleRate, kBlockSize);

        writer.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
        {
            auto drawn = writer.getShapedEnvelope(1);
            drawn.addPoint(0.22, 0.90);
            drawn.addPoint(0.48, 0.20);
            writer.setShapedEnvelope(1, drawn);
        }

        auto tree = writer.createParameterStateTree();
        auto shapes = tree.getChildWithName(px3::processor_internal::kEnvelopeShapesId);

        auto rewrote = false;
        if (shapes.isValid())
        {
            for (auto node : shapes)
            {
                const auto index = static_cast<int>(
                    node.getProperty(px3::processor_internal::kEnvelopeShapeIndexId, -1));
                if (index != 1) { continue; }

                // The same six-point breakpoint envelope, relabelled as slot 0.
                auto forged = node.createCopy();
                forged.setProperty(px3::processor_internal::kEnvelopeShapeIndexId, 0, nullptr);
                shapes.addChild(forged, -1, nullptr);
                rewrote = true;
                break;
            }
        }

        PX3SynthAudioProcessor reader;
        reader.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        reader.prepareToPlay(kSampleRate, kBlockSize);
        juce::String error;
        const auto applied = rewrote && reader.applyParameterStateTree(tree, &error, true);

        const auto amp = reader.getShapedEnvelope(0);
        check("EnvIso_AStateClaimingABreakpointAmpEnvLoadsAsAdsr",
              applied && ! amp.isBreakpointMode()
                  && reader.getEnvelopeMode(0) == px3::BreakpointEnvelope::Mode::adsr
                  // Reduced to a shape the four knobs can actually drive, not
                  // a six-point drawing wearing an ADSR label.
                  && amp.getPointCount() == 4 && amp.isAdsrSkeleton(),
              applied ? juce::String("it loads as ")
                            + (amp.isBreakpointMode() ? "Breakpoint" : "ADSR")
                            + " over " + juce::String(amp.getPointCount()) + " points"
                      : "the forged state did not apply: " + error);

        // The same forgery with a FOUR-point shape, which is what seeding a
        // Breakpoint envelope from an ADSR produces. It needs no reduction -
        // the geometry is already an ADSR skeleton - so nothing but the mode
        // has to be corrected, and it is the only case that proves the mode is
        // corrected at all. The six-point version above cannot: reducedToAdsr
        // sets the mode itself on its way out.
        {
            PX3SynthAudioProcessor bentWriter;
            bentWriter.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            bentWriter.prepareToPlay(kSampleRate, kBlockSize);

            // Curved, so the writer stores a node for it at all - a plain ADSR
            // says nothing the parameters do not and is skipped.
            auto bent = bentWriter.getShapedEnvelope(0);
            bent.setCurve(1, 0.48);
            bentWriter.setShapedEnvelope(0, bent);

            auto bentTree = bentWriter.createParameterStateTree();
            auto bentShapes
                = bentTree.getChildWithName(px3::processor_internal::kEnvelopeShapesId);

            auto relabelled = false;
            if (bentShapes.isValid())
            {
                for (auto node : bentShapes)
                {
                    const auto index = static_cast<int>(
                        node.getProperty(px3::processor_internal::kEnvelopeShapeIndexId, -1));
                    if (index != 0) { continue; }

                    node.setProperty(px3::processor_internal::kEnvelopeShapeModeId,
                                     "breakpoint", nullptr);
                    relabelled = true;
                    break;
                }
            }

            PX3SynthAudioProcessor bentReader;
            bentReader.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            bentReader.prepareToPlay(kSampleRate, kBlockSize);
            juce::String bentError;
            const auto bentApplied
                = relabelled && bentReader.applyParameterStateTree(bentTree, &bentError, true);

            const auto bentAmp = bentReader.getShapedEnvelope(0);
            check("EnvIso_AFourPointShapeMarkedBreakpointStillLoadsAsAdsr",
                  bentApplied && ! bentAmp.isBreakpointMode()
                      && bentAmp.getPointCount() == 4
                      && std::abs(bentAmp.getPoint(1).curveToNext - 0.48) < 1.0e-9,
                  bentApplied
                      ? juce::String("it loads as ")
                            + (bentAmp.isBreakpointMode() ? "Breakpoint" : "ADSR")
                            + " over " + juce::String(bentAmp.getPointCount())
                            + " points, keeping its 0.48 bend at "
                            + fmt(static_cast<float>(bentAmp.getPoint(1).curveToNext), 2)
                      : "the relabelled state did not apply: " + bentError);
        }

        // ENV 1 is untouched by that rule and keeps its drawing.
        check("EnvIso_TheSameStateStillGivesEnv1ItsBreakpointShape",
              applied && reader.getShapedEnvelope(1).isBreakpointMode()
                  && reader.getShapedEnvelope(1).getPointCount() == 6,
              "ENV 1 comes back in "
                  + juce::String(reader.getShapedEnvelope(1).isBreakpointMode() ? "Breakpoint"
                                                                                : "ADSR")
                  + " mode on " + juce::String(reader.getShapedEnvelope(1).getPointCount())
                  + " points");
    }

    // ---- the duration floor is Breakpoint's, and reaches saved state --------
    {
        // ADSR is untouched by it. Its times come from parameters with ranges
        // of their own, a zero-length stage there is deliberate and tested, and
        // an ADSR holds at its sustain rather than retiring - so it has none of
        // the trap the floor exists to close.
        EnvelopeSettings zeroed;
        zeroed.attackSeconds = 0.0f;
        zeroed.decaySeconds = 0.0f;
        zeroed.sustainLevel = 0.5f;
        zeroed.releaseSeconds = 0.0f;

        // Through setPoints, which is what runs sortAndClamp - the floor lives
        // there. fromAdsr builds its points directly and never reaches it, so
        // asserting on a shape straight out of fromAdsr tests nothing about the
        // gate: it passes whether the floor is gated on the mode or not.
        auto adsr = px3::BreakpointEnvelope::fromAdsr(zeroed);
        {
            px3::BreakpointEnvelope::Point flat[4] = {
                { 0.0, 0.0, 0.0 },
                { 0.0, 1.0, 0.0 },
                { 0.0, 0.5, 0.0 },
                { 0.0, 0.0, 0.0 }
            };
            adsr.setPoints(flat, 4, 2);
        }

        check("EnvFloor_TheFloorDoesNotReachAdsrMode",
              ! adsr.isBreakpointMode()
                  && adsr.getTotalSeconds() < px3::BreakpointEnvelope::kMinBreakpointSeconds,
              "an all-zero ADSR still runs " + fmt(adsr.getTotalSeconds() * 1000.0, 1)
                  + " ms, under the " + fmt(px3::BreakpointEnvelope::kMinBreakpointSeconds * 1000.0, 0)
                  + " ms Breakpoint floor");

        // And a collapsed envelope in SAVED state is put right on the way in.
        // The restore path sets the points and only then the mode, so the floor
        // inside sortAndClamp has not seen a Breakpoint envelope at that point:
        // without the repair on load, such a state would come back collapsed.
        PX3SynthAudioProcessor saver;
        saver.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        saver.prepareToPlay(kSampleRate, kBlockSize);
        saver.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);

        {
            // Built points-first so it escapes the floor, which is exactly the
            // shape older state can carry.
            auto collapsed = px3::BreakpointEnvelope::fromAdsr(EnvelopeSettings {});
            px3::BreakpointEnvelope::Point points[3] = {
                { 0.0, 0.0, 0.0 },
                { 0.0, 0.8, 0.0 },
                { 0.0, 0.0, 0.0 }
            };
            collapsed.setPoints(points, 3, 1);
            collapsed.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
            saver.setShapedEnvelope(1, collapsed);

            check("EnvFloor_SuchAStateIsRepresentable",
                  saver.getShapedEnvelope(1).getTotalSeconds() < 1.0e-9,
                  "the stored shape runs "
                      + fmt(saver.getShapedEnvelope(1).getTotalSeconds() * 1000.0, 1) + " ms");
        }

        juce::MemoryBlock saved;
        saver.getStateInformation(saved);

        PX3SynthAudioProcessor loader;
        loader.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        loader.prepareToPlay(kSampleRate, kBlockSize);
        loader.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

        const auto reloaded = loader.getShapedEnvelope(1);
        check("EnvFloor_ACollapsedEnvelopeIsRepairedOnLoad",
              reloaded.getTotalSeconds()
                  >= px3::BreakpointEnvelope::kMinBreakpointSeconds - 1.0e-9,
              "it reloads running " + fmt(reloaded.getTotalSeconds() * 1000.0, 1) + " ms");
    }

    // ---- what the smallest envelope does to the DSP -------------------------
    //
    // The three-point floor lets the user leave the middle point ON the line,
    // which is a flat, silent envelope. It also lets them collapse the whole
    // thing in time. Both are reachable, so both are measured rather than
    // assumed harmless.
    {
        constexpr double kRate = 48000.0;

        // Mode FIRST, then the points - which is the order the app works in,
        // and the order that matters now the duration floor lives in
        // sortAndClamp: points set while the shape still calls itself an ADSR
        // are not floored, and that is a state editing cannot produce.
        const auto build = [](double t1, double v1, double t2)
        {
            auto env = px3::BreakpointEnvelope::fromAdsr(EnvelopeSettings {});
            env.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
            px3::BreakpointEnvelope::Point points[3] = {
                { 0.00, 0.0, 0.0 },
                { t1, v1, 0.0 },
                { t2, 0.0, 0.0 }
            };
            env.setPoints(points, 3, 1);
            return env;
        };

        const auto play = [](const px3::BreakpointEnvelope& shape, double seconds)
        {
            AmpEnvelope amp;
            amp.prepare(kRate);
            amp.setEnvelope(shape);
            amp.noteOn();

            struct { float peak, worstStep; bool finite, stillActive; } out { 0.0f, 0.0f, true, true };
            // From silence, INCLUDING the first sample. Skipping i == 0 skips
            // exactly the sample a note-on click lives on - the envelope starts
            // from silence, so the step into the first sample is a real step
            // and the most important one to measure.
            auto previous = 0.0f;
            const auto samples = static_cast<int>(seconds * kRate);
            for (int i = 0; i < samples; ++i)
            {
                const auto value = amp.getNextSample();
                out.finite = out.finite && std::isfinite(value);
                out.peak = juce::jmax(out.peak, std::abs(value));
                out.worstStep = juce::jmax(out.worstStep, std::abs(value - previous));
                previous = value;
            }
            out.stillActive = amp.isActive();
            return out;
        };

        // 1. The middle point left on the line: a flat, silent envelope.
        const auto flat = play(build(0.25, 0.0, 0.50), 0.60);
        check("EnvFloor_AFlatEnvelopeIsSilentAndRetires",
              flat.finite && flat.peak < 1.0e-6f && ! flat.stillActive,
              "it peaks at " + fmt(flat.peak, 6) + " and the voice "
                  + (flat.stillActive ? "is still running" : "retired"));

        // 2. Asked to collapse in time - every point dragged to zero - the
        //    envelope keeps its 10 ms floor instead. A one-shot with no
        //    duration is a note that never sounds, and on screen it looks like
        //    a very short envelope rather than a broken one.
        const auto shortest = build(0.0, 0.9, 0.0);
        check("EnvFloor_ABreakpointEnvelopeCannotCollapseToNothing",
              std::abs(shortest.getTotalSeconds()
                       - px3::BreakpointEnvelope::kMinBreakpointSeconds) < 1.0e-9,
              "collapsed to zero it still runs " + fmt(shortest.getTotalSeconds() * 1000.0, 1)
                  + " ms");

        const auto collapsed = play(shortest, 0.10);
        check("EnvFloor_TheShortestEnvelopeStillSounds",
              collapsed.finite && collapsed.peak > 0.1f && ! collapsed.stillActive,
              "it peaks at " + fmt(collapsed.peak, 4) + " with a worst step of "
                  + fmt(collapsed.worstStep, 5) + "; the voice "
                  + (collapsed.stillActive ? "is still running" : "retired"));

        // 3. A zero-length FIRST segment - an instant jump to full at note-on,
        //    which is the sharpest thing the shape can ask for. The envelope's
        //    own smoothing is what has to keep that from being a click.
        //
        //    The bound is a rate: 0.05 per sample at 48 kHz is 20 samples to
        //    cross the whole range, or 0.4 ms. Anything at or under that is a
        //    fast attack; a genuine step would be ~1.0 in one sample. Measured
        //    at 0.025, which is the smoother's own limit - about 0.8 ms - and
        //    is the same limit every envelope in the synth is held to.
        const auto instant = play(build(0.0, 1.0, 0.50), 0.60);
        check("EnvFloor_AnInstantAttackIsSmoothedNotStepped",
              instant.finite && instant.peak > 0.5f && instant.worstStep < 0.05f,
              "it reaches " + fmt(instant.peak, 3) + " with a worst per-sample step of "
                  + fmt(instant.worstStep, 5) + ", against 1.0 for a true step");
    }

    // ---- two points survive persistence and a round trip through ADSR -------
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        setParam(processor, "env1Attack", 0.150f);
        setParam(processor, "env1Sustain", 0.45f);

        processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
        {
            auto two = processor.getShapedEnvelope(1);
            while (two.getPointCount() > 2)
            {
                auto removed = false;
                for (int i = two.getPointCount() - 2; i >= 1 && ! removed; --i)
                {
                    removed = two.removePoint(i);
                }
                if (! removed) { break; }
            }
            // The LAST point, which is the end of the envelope. Index 1 is the
            // movable middle now that the smallest breakpoint envelope is
            // three points.
            two.setPoint(two.getPointCount() - 1, 0.42, 0.0);
            two.setCurve(0, -0.35);
            processor.setShapedEnvelope(1, two);
        }

        const auto beforeSave = processor.getShapedEnvelope(1);
        check("EnvIso_DeletingDownToThreeInTheProcessorWorks",
              beforeSave.getPointCount() == 3 && beforeSave.isBreakpointMode(),
              juce::String(beforeSave.getPointCount()) + " points in Breakpoint mode");

        juce::MemoryBlock saved;
        processor.getStateInformation(saved);

        PX3SynthAudioProcessor reloaded;
        reloaded.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        reloaded.prepareToPlay(kSampleRate, kBlockSize);
        reloaded.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

        const auto back = reloaded.getShapedEnvelope(1);
        const auto backLast = back.getPoint(back.getPointCount() - 1);
        check("EnvIso_TheSmallestEnvelopeSurvivesASession",
              back.getPointCount() == 3 && back.isBreakpointMode()
                  && std::abs(backLast.timeSeconds - 0.42) < 1.0e-9
                  && std::abs(back.getPoint(0).curveToNext + 0.35) < 1.0e-9,
              "it reloads as " + juce::String(back.getPointCount()) + " points ending at "
                  + fmt(backLast.timeSeconds, 2) + " s bending "
                  + fmt(static_cast<float>(back.getPoint(0).curveToNext), 2));

        // Out to ADSR and back: the two points must still be two points, and
        // the ADSR must be the stored one rather than anything derived from a
        // single ramp.
        reloaded.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::adsr);
        const auto adsrSettings = reloaded.envelopeParameterSettings(0);
        check("EnvIso_LeavingATwoPointDrawingRestoresTheStoredAdsr",
              std::abs(adsrSettings.attackSeconds - 0.150f) < 1.0e-4f
                  && std::abs(adsrSettings.sustainLevel - 0.45f) < 1.0e-4f
                  && reloaded.currentModEnvelope(0).getPointCount() == 4,
              "ADSR reads A " + fmt(adsrSettings.attackSeconds, 3) + " S "
                  + fmt(adsrSettings.sustainLevel, 2) + " over "
                  + juce::String(reloaded.currentModEnvelope(0).getPointCount()) + " points");

        reloaded.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
        const auto smallAgain = reloaded.getShapedEnvelope(1);
        check("EnvIso_ReturningToBreakpointGivesBackTheSameSmallEnvelope",
              smallAgain.getPointCount() == 3
                  && std::abs(smallAgain.getPoint(smallAgain.getPointCount() - 1).timeSeconds - 0.42)
                         < 1.0e-9,
              "it comes back as " + juce::String(smallAgain.getPointCount()) + " points");

        // And a save made while ADSR is live still carries the two-point
        // drawing behind it.
        reloaded.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::adsr);
        juce::MemoryBlock savedInAdsr;
        reloaded.getStateInformation(savedInAdsr);

        PX3SynthAudioProcessor third;
        third.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        third.prepareToPlay(kSampleRate, kBlockSize);
        third.setStateInformation(savedInAdsr.getData(), static_cast<int>(savedInAdsr.getSize()));
        third.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);

        const auto retained = third.getShapedEnvelope(1);
        check("EnvIso_TheSmallestDrawingSurvivesASaveMadeInAdsrMode",
              retained.getPointCount() == 3
                  && std::abs(retained.getPoint(retained.getPointCount() - 1).timeSeconds - 0.42)
                         < 1.0e-9,
              "after saving in ADSR mode and reloading, switching back gives "
                  + juce::String(retained.getPointCount()) + " points");
    }

    // ---- ENV 1-3 travel the same trajectory ---------------------------------
    //
    // The mod envelopes are a different class from AMP ENV, so nothing about
    // the amp envelope's behaviour carries over on its own.
    {
        constexpr double kRate = 1000.0;

        EnvelopeSettings base;
        base.attackSeconds = 0.10f;
        base.decaySeconds = 0.20f;
        base.sustainLevel = 0.5f;
        base.releaseSeconds = 0.30f;

        auto twoPeaks = px3::BreakpointEnvelope::fromAdsr(base);
        {
            px3::BreakpointEnvelope::Point points[7] = {
                { 0.00, 0.0, 0.0 },
                { 0.10, 1.0, 0.0 },
                { 0.25, 0.0, 0.0 },
                { 0.30, 0.0, 0.0 },
                { 0.45, 0.9, 0.0 },
                { 0.70, 0.3, 0.0 },
                { 1.00, 0.0, 0.0 }
            };
            twoPeaks.setPoints(points, 7, 2);
            twoPeaks.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        }

        const auto trace = [&](bool releaseInTheGap)
        {
            EnvelopeGenerator env;
            env.prepare(kRate);
            env.setEnvelope(twoPeaks);
            env.noteOn();

            struct { float firstPeak, gap, secondPeak, end; bool stillActive; } out {};
            const auto at = [](double seconds) { return static_cast<int>(std::lround(seconds * kRate)); };

            for (int i = 0; i <= at(1.02); ++i)
            {
                if (releaseInTheGap && i == at(0.27)) { env.noteOff(); }
                const auto value = env.getNextSample();
                if (i == at(0.10)) { out.firstPeak = value; }
                if (i == at(0.27)) { out.gap = value; }
                if (i == at(0.45)) { out.secondPeak = value; }
                if (i == at(1.02)) { out.end = value; }
            }
            out.stillActive = env.isActive();
            return out;
        };

        const auto held = trace(false);
        check("EnvBp_AModEnvelopeTravelsTheWholeTrajectory",
              held.firstPeak > 0.8f && held.gap < 1.0e-5f && held.secondPeak > 0.7f
                  && held.end < 0.05f,
              "ENV reads " + fmt(held.firstPeak, 3) + " at the first peak, " + fmt(held.gap, 5)
                  + " in the silent gap, " + fmt(held.secondPeak, 3) + " at the second peak and "
                  + fmt(held.end, 3) + " at the end");

        check("EnvBp_AModEnvelopeFinishesWithTheKeyStillDown",
              ! held.stillActive,
              held.stillActive ? "ENV is still running after the envelope ended"
                               : "ENV ended and stopped running, key still down");

        const auto released = trace(true);
        check("EnvBp_ReleasingAModEnvelopeDoesNotTruncateIt",
              released.secondPeak > 0.7f,
              "released into the silent gap, ENV still reaches " + fmt(released.secondPeak, 3)
                  + " at its second peak");
    }

    // ---- both modes' state survives a session, whichever is active ----------
    //
    // Saving in ADSR mode and keeping the drawing is covered elsewhere. This is
    // the mirror: saved while the BREAKPOINT shape is the live one, the ADSR
    // put aside - curves and all - has to come back too.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        setParam(processor, "env1Attack", 0.250f);
        setParam(processor, "env1Sustain", 0.35f);
        {
            auto bent = processor.getShapedEnvelope(1);
            bent.setCurve(0, 0.55);
            bent.setCurve(2, -0.45);
            processor.setShapedEnvelope(1, bent);
        }

        processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
        {
            auto drawn = processor.getShapedEnvelope(1);
            drawn.addPoint(0.40, 0.85);
            drawn.addPoint(0.62, 0.10);
            processor.setShapedEnvelope(1, drawn);
        }

        juce::MemoryBlock saved;
        processor.getStateInformation(saved);

        PX3SynthAudioProcessor reloaded;
        reloaded.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        reloaded.prepareToPlay(kSampleRate, kBlockSize);
        reloaded.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

        const auto liveAfter = reloaded.getShapedEnvelope(1);
        check("EnvBp_TheActiveBreakpointShapeSurvivesTheSession",
              liveAfter.isBreakpointMode() && liveAfter.getPointCount() == 6,
              "it reloads in Breakpoint mode on " + juce::String(liveAfter.getPointCount())
                  + " points");

        reloaded.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::adsr);
        const auto adsrAfter = reloaded.currentModEnvelope(0);

        check("EnvBp_TheStoredAdsrSurvivesASaveMadeInBreakpointMode",
              adsrAfter.getPointCount() == 4
                  && std::abs(adsrAfter.getPoint(0).curveToNext - 0.55) < 1.0e-9
                  && std::abs(adsrAfter.getPoint(2).curveToNext + 0.45) < 1.0e-9,
              "the ADSR comes back over " + juce::String(adsrAfter.getPointCount())
                  + " points bending " + fmt(static_cast<float>(adsrAfter.getPoint(0).curveToNext), 3)
                  + " / " + fmt(static_cast<float>(adsrAfter.getPoint(2).curveToNext), 3));

        const auto settingsAfter = reloaded.envelopeParameterSettings(0);
        check("EnvBp_TheStoredAdsrParametersSurviveThatSaveToo",
              std::abs(settingsAfter.attackSeconds - 0.250f) < 1.0e-4f
                  && std::abs(settingsAfter.sustainLevel - 0.35f) < 1.0e-4f,
              "A " + fmt(settingsAfter.attackSeconds, 3) + " S " + fmt(settingsAfter.sustainLevel, 2));
    }

    // ---- switching is non-destructive in both directions --------------------
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        setParam(processor, "env1Attack", 0.120f);
        setParam(processor, "env1Decay", 0.400f);
        setParam(processor, "env1Sustain", 0.60f);
        setParam(processor, "env1Release", 0.800f);

        const auto adsrBefore = processor.envelopeParameterSettings(0);

        // Bend the decay while still in ADSR mode. Curves live on the shape,
        // not in the four parameters, so this is the part a round trip can
        // actually lose.
        {
            auto bent = processor.getShapedEnvelope(1);
            bent.setCurve(1, -0.7);
            processor.setShapedEnvelope(1, bent);
        }
        constexpr double adsrCurveBefore = -0.7;

        // First visit seeds from the ADSR, so the user starts somewhere
        // familiar.
        processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
        const auto seeded = processor.getShapedEnvelope(1);

        check("EnvBp_TheFirstVisitSeedsFromTheAdsrShape",
              seeded.isBreakpointMode() && seeded.getPointCount() == 4,
              "the breakpoint editor opens on " + juce::String(seeded.getPointCount())
                  + " points taken from the ADSR");

        // Draw something the four stages cannot say.
        auto drawn = seeded;
        drawn.addPoint(0.30, 0.95);
        drawn.addPoint(0.55, 0.15);
        drawn.addPoint(0.80, 0.70);
        drawn.setCurve(2, 0.6);
        processor.setShapedEnvelope(1, drawn);
        const auto drawnCount = drawn.getPointCount();

        // Editing the breakpoint envelope must not touch the ADSR settings.
        const auto adsrAfterDrawing = processor.envelopeParameterSettings(0);
        check("EnvBp_EditingTheBreakpointLeavesTheAdsrAlone",
              std::abs(adsrAfterDrawing.attackSeconds - adsrBefore.attackSeconds) < 1.0e-6f
                  && std::abs(adsrAfterDrawing.decaySeconds - adsrBefore.decaySeconds) < 1.0e-6f
                  && std::abs(adsrAfterDrawing.sustainLevel - adsrBefore.sustainLevel) < 1.0e-6f
                  && std::abs(adsrAfterDrawing.releaseSeconds - adsrBefore.releaseSeconds) < 1.0e-6f,
              "after drawing a " + juce::String(drawnCount) + "-point envelope the ADSR still reads A "
                  + fmt(adsrAfterDrawing.attackSeconds, 3) + " D " + fmt(adsrAfterDrawing.decaySeconds, 3)
                  + " S " + fmt(adsrAfterDrawing.sustainLevel, 2) + " R "
                  + fmt(adsrAfterDrawing.releaseSeconds, 3));

        // Back to ADSR: the stored settings return, not values derived from
        // the drawing.
        processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::adsr);
        const auto adsrParamsAfterReturn = processor.envelopeParameterSettings(0);
        const auto running = processor.currentModEnvelope(0);

        check("EnvBp_ReturningToAdsrDoesNotWriteDerivedValuesToTheParameters",
              std::abs(adsrParamsAfterReturn.attackSeconds - adsrBefore.attackSeconds) < 1.0e-6f
                  && std::abs(adsrParamsAfterReturn.decaySeconds - adsrBefore.decaySeconds) < 1.0e-6f
                  && std::abs(adsrParamsAfterReturn.sustainLevel - adsrBefore.sustainLevel) < 1.0e-6f
                  && std::abs(adsrParamsAfterReturn.releaseSeconds - adsrBefore.releaseSeconds) < 1.0e-6f,
              "the parameters still read A " + fmt(adsrParamsAfterReturn.attackSeconds, 3) + " D "
                  + fmt(adsrParamsAfterReturn.decaySeconds, 3) + " S "
                  + fmt(adsrParamsAfterReturn.sustainLevel, 2) + " R "
                  + fmt(adsrParamsAfterReturn.releaseSeconds, 3));

        const auto backToAdsr = running.toAdsr();

        check("EnvBp_ReturningToAdsrRestoresWhatWasStored",
              std::abs(backToAdsr.attackSeconds - adsrBefore.attackSeconds) < 0.01f
                  && std::abs(backToAdsr.decaySeconds - adsrBefore.decaySeconds) < 0.01f
                  && std::abs(backToAdsr.sustainLevel - adsrBefore.sustainLevel) < 0.01f
                  && std::abs(backToAdsr.releaseSeconds - adsrBefore.releaseSeconds) < 0.01f
                  && running.getPointCount() == 4,
              "the envelope the DSP runs comes back as A " + fmt(backToAdsr.attackSeconds, 3)
                  + " D " + fmt(backToAdsr.decaySeconds, 3) + " S " + fmt(backToAdsr.sustainLevel, 2)
                  + " R " + fmt(backToAdsr.releaseSeconds, 3) + " over "
                  + juce::String(running.getPointCount()) + " points");

        check("EnvBp_ReturningToAdsrKeepsTheCurvesDrawnInAdsrMode",
              std::abs(running.getPoint(1).curveToNext - adsrCurveBefore) < 1.0e-9,
              "the decay bend reads " + fmt(static_cast<float>(running.getPoint(1).curveToNext), 3)
                  + " against the " + fmt(static_cast<float>(adsrCurveBefore), 3) + " it was drawn at");

        // And back to Breakpoint: the drawing returns, not a fresh seed.
        processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
        const auto returned = processor.getShapedEnvelope(1);

        auto identical = returned.getPointCount() == drawn.getPointCount();
        for (int i = 0; identical && i < drawn.getPointCount(); ++i)
        {
            identical = std::abs(returned.getPoint(i).timeSeconds - drawn.getPoint(i).timeSeconds) < 1.0e-9
                        && std::abs(returned.getPoint(i).value - drawn.getPoint(i).value) < 1.0e-9
                        && std::abs(returned.getPoint(i).curveToNext - drawn.getPoint(i).curveToNext) < 1.0e-9;
        }

        check("EnvBp_ReturningToBreakpointRestoresTheDrawingNotAFreshSeed",
              identical,
              identical ? "all " + juce::String(returned.getPointCount()) + " points return unchanged"
                        : "it came back with " + juce::String(returned.getPointCount())
                              + " points against the " + juce::String(drawn.getPointCount())
                              + " that were drawn");

        // Repeated switching causes no drift.
        for (int i = 0; i < 6; ++i)
        {
            processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::adsr);
            processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);
        }
        const auto afterSix = processor.getShapedEnvelope(1);

        check("EnvBp_RepeatedSwitchingDoesNotDrift",
              afterSix.getPointCount() == drawn.getPointCount()
                  && std::abs(afterSix.getPoint(2).value - drawn.getPoint(2).value) < 1.0e-9,
              "after six round trips the drawing still has "
                  + juce::String(afterSix.getPointCount()) + " points");
    }

    // ---- persistence --------------------------------------------------------
    {
        PX3SynthAudioProcessor source;

        // A shape the four ADSR numbers cannot describe.
        auto shaped = px3::BreakpointEnvelope::fromAdsr(EnvelopeSettings {});
        // Adding points is a Breakpoint-mode capability.
        shaped.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        const auto extra = shaped.addPoint(0.02, 0.35);
        shaped.setCurve(0, 0.62);
        if (extra >= 0) { shaped.setCurve(extra, -0.41); }
        // Slots 1 and 2. Not slot 0: AMP ENV is ADSR-only, so a Breakpoint
        // shape stored there is reduced on the way in and this would be
        // measuring that instead of the round trip.
        source.setShapedEnvelope(1, shaped);
        source.setShapedEnvelope(2, shaped);

        juce::MemoryBlock state;
        source.getStateInformation(state);

        PX3SynthAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        const auto back = restored.getShapedEnvelope(1);
        auto worstTime = 0.0, worstValue = 0.0, worstCurve = 0.0;
        const auto sameCount = back.getPointCount() == shaped.getPointCount();
        if (sameCount)
        {
            for (int i = 0; i < shaped.getPointCount(); ++i)
            {
                worstTime = juce::jmax(worstTime,
                                       std::abs(back.getPoint(i).timeSeconds - shaped.getPoint(i).timeSeconds));
                worstValue = juce::jmax(worstValue,
                                        std::abs(back.getPoint(i).value - shaped.getPoint(i).value));
                worstCurve = juce::jmax(worstCurve,
                                        std::abs(back.getPoint(i).curveToNext - shaped.getPoint(i).curveToNext));
            }
        }

        check("Envelope_ShapeSurvivesTheStateRoundTrip",
              sameCount && worstTime < 1.0e-9 && worstValue < 1.0e-9 && worstCurve < 1.0e-9
                  && back.getSustainPoint() == shaped.getSustainPoint(),
              sameCount ? juce::String(back.getPointCount()) + " points restored, worst error "
                              + fmt(juce::jmax(worstTime, juce::jmax(worstValue, worstCurve)), 12)
                        : juce::String(back.getPointCount()) + " points back from "
                              + juce::String(shaped.getPointCount()));

        // Each envelope is its own. Shaping ENV 1 and ENV 2 must not have
        // touched AMP ENV or ENV 3.
        check("Envelope_ShapingOneDoesNotTouchTheOthers",
              restored.getShapedEnvelope(0).isPlainAdsr()
                  && restored.getShapedEnvelope(3).isPlainAdsr()
                  && ! restored.getShapedEnvelope(1).isPlainAdsr()
                  && ! restored.getShapedEnvelope(2).isPlainAdsr(),
              "ENV 1 and ENV 2 came back shaped, AMP ENV and ENV 3 came back plain");

        // A preset from before the editor has no node at all, and must load as
        // plain ADSR rather than as an error.
        PX3SynthAudioProcessor legacy;
        juce::MemoryBlock legacyState;
        legacy.getStateInformation(legacyState);

        PX3SynthAudioProcessor migrated;
        migrated.setShapedEnvelope(0, shaped);   // something to be cleared
        migrated.setStateInformation(legacyState.getData(), static_cast<int>(legacyState.getSize()));

        check("Envelope_APresetWithNoShapeLoadsAsPlainAdsr",
              migrated.getShapedEnvelope(0).isPlainAdsr(),
              "absence of the node means ADSR, and clears whatever was shaped before");
    }

    // ---- AMP ENV, measured from the audio it actually produces --------------
    // The model tests above all pass on the shape. This asks the different
    // question: does moving an ADSR parameter move the SOUND, after the
    // breakpoint engine replaced the ADSR one underneath it.
    {
        struct Heard { double peakSeconds; double settled; double releaseSeconds; };

        const auto play = [](float attack, float decay, float sustain, float release)
        {
            PX3SynthAudioProcessor processor;
            makePlainPatch(processor);
            setParam(processor, "ampAttack", attack);
            setParam(processor, "ampDecay", decay);
            setParam(processor, "ampSustain", sustain);
            setParam(processor, "ampRelease", release);

            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);

            juce::AudioBuffer<float> buffer(2, kBlockSize);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

            const auto secondsPerBlock = static_cast<double>(kBlockSize) / kSampleRate;
            std::vector<double> held;
            for (int block = 0; block < static_cast<int>(3.0 / secondsPerBlock); ++block)
            {
                buffer.clear();
                processor.processBlock(buffer, midi);
                midi.clear();
                held.push_back(buffer.getMagnitude(0, kBlockSize));
            }

            Heard heard {};
            auto peak = 0.0;
            std::size_t peakBlock = 0;
            for (std::size_t i = 0; i < held.size(); ++i)
            {
                if (held[i] > peak) { peak = held[i]; peakBlock = i; }
            }
            heard.peakSeconds = static_cast<double>(peakBlock + 1) * secondsPerBlock;

            auto tail = 0.0;
            auto count = 0;
            for (auto i = held.size() > 45 ? held.size() - 45 : 0; i < held.size(); ++i)
            {
                tail += held[i]; ++count;
            }
            heard.settled = count > 0 ? tail / count : 0.0;

            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            const auto silence = heard.settled * 0.001;
            auto blocks = 0;
            for (int block = 0; block < static_cast<int>(12.0 / secondsPerBlock); ++block)
            {
                buffer.clear();
                processor.processBlock(buffer, off);
                off.clear();
                ++blocks;
                if (buffer.getMagnitude(0, kBlockSize) <= silence) { break; }
            }
            heard.releaseSeconds = static_cast<double>(blocks) * secondsPerBlock;
            return heard;
        };

        const auto full = play(0.010f, 0.100f, 1.00f, 0.100f);
        const auto half = play(0.010f, 0.100f, 0.50f, 0.100f);
        const auto none = play(0.010f, 0.100f, 0.00f, 0.100f);

        // Ordered, not proportional. The ENVELOPE is exactly linear in sustain -
        // pinned below - but the level you hear is not, because the signal path
        // between the envelope and the output is not: at sustain 0.5 the audio
        // settles at 0.64 of full, not 0.50. Asserting 0.50 here would be
        // asserting that the analog stages do nothing.
        check("AmpEnv_SustainSetsTheLevelTheNoteHoldsAt",
              full.settled > 0.0
                  && half.settled < full.settled * 0.95
                  && half.settled > full.settled * 0.05
                  && none.settled < full.settled * 0.02,
              "sustain 1.0 holds at " + fmt(full.settled, 4) + ", 0.5 at "
                  + fmt(half.settled, 4) + " (" + fmt(half.settled / full.settled, 2)
                  + " of full through the signal path), 0.0 at " + fmt(none.settled, 4));

        const auto slow = play(0.500f, 0.100f, 1.00f, 0.100f);
        check("AmpEnv_AttackSetsHowLongItTakesToReachFullLevel",
              full.peakSeconds < 0.06 && std::abs(slow.peakSeconds - 0.5) < 0.15,
              "a 0.010 s attack peaks at " + fmt(full.peakSeconds, 3)
                  + " s, a 0.500 s attack at " + fmt(slow.peakSeconds, 3) + " s");

        const auto longTail = play(0.010f, 0.100f, 1.00f, 2.000f);
        check("AmpEnv_ReleaseSetsHowLongTheTailLasts",
              std::abs(full.releaseSeconds - 0.100) < 0.05
                  && std::abs(longTail.releaseSeconds - 2.000) < 0.35,
              "a 0.100 s release falls silent in " + fmt(full.releaseSeconds, 3)
                  + " s, a 2.000 s release in " + fmt(longTail.releaseSeconds, 3) + " s");

        // The envelope alone, with no oscillator in the way. Sustain is 1.0, so
        // the level after the attack must be flat - the audio measurement shows
        // a peak 1.35x the held level at short attacks, and this is what says
        // that is the waveform's onset rather than the envelope overshooting.
        auto worstOvershoot = 1.0f;
        for (const auto attack : { 0.001f, 0.010f, 0.040f, 0.080f, 0.320f })
        {
            AmpEnvelope envelope;
            envelope.prepare(kSampleRate);
            EnvelopeSettings settings;
            settings.attackSeconds = attack;
            settings.decaySeconds = 0.100f;
            settings.sustainLevel = 1.0f;
            settings.releaseSeconds = 0.100f;
            envelope.setSettings(settings);
            envelope.noteOn();

            auto peak = 0.0f;
            auto settled = 0.0f;
            const auto samples = static_cast<int>(kSampleRate * 2.0);
            for (int i = 0; i < samples; ++i)
            {
                const auto value = envelope.getNextSample();
                peak = juce::jmax(peak, value);
                if (i > samples - 1000) { settled = value; }
            }
            if (settled > 1.0e-9f)
            {
                worstOvershoot = juce::jmax(worstOvershoot, peak / settled);
            }
        }

        // And the envelope itself, which IS exactly proportional. This is what
        // makes the ratio above attributable to the signal path rather than to
        // the envelope, and it is the stronger claim of the two.
        {
            const auto settledAt = [](float sustain)
            {
                AmpEnvelope envelope;
                envelope.prepare(kSampleRate);
                EnvelopeSettings settings;
                settings.attackSeconds = 0.010f;
                settings.decaySeconds = 0.100f;
                settings.sustainLevel = sustain;
                settings.releaseSeconds = 0.100f;
                envelope.setSettings(settings);
                envelope.noteOn();
                auto value = 0.0f;
                for (int i = 0; i < static_cast<int>(kSampleRate * 2.0); ++i)
                {
                    value = envelope.getNextSample();
                }
                return value;
            };
            const auto atFull = settledAt(1.0f);
            const auto atHalf = settledAt(0.5f);
            const auto atQuarter = settledAt(0.25f);

            check("AmpEnv_TheEnvelopeItselfIsExactlyItsSustainLevel",
                  std::abs(atFull - 1.0f) < 1.0e-4f
                      && std::abs(atHalf - 0.5f) < 1.0e-4f
                      && std::abs(atQuarter - 0.25f) < 1.0e-4f,
                  "sustain 1.0 -> " + fmt(atFull, 4) + ", 0.5 -> " + fmt(atHalf, 4)
                      + ", 0.25 -> " + fmt(atQuarter, 4));
        }

        check("AmpEnv_DoesNotOvershootAtAnyAttackTime",
              worstOvershoot < 1.001f,
              "at sustain 1.0 the envelope is flat after the attack - worst peak "
              "over held level " + fmt(worstOvershoot, 4));
    }

    // ---- what the AMP ENV editor actually SHOWS ------------------------------
    // The processor builds the four-point shape; the question this answers is
    // whether the editor on screen is showing that shape or a different one.
    // Every previous test asked the processor, which is why a hold handle
    // survived on the amp envelope through three rounds of fixes.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        AmpEnvelopeComponent amp(processor, juce::Colour::fromRGB(120, 186, 255));
        amp.setSize(360, 220);

        const auto* graph = amp.debugGraph();
        if (graph == nullptr)
        {
            check("AmpEnvUi_TheEditorShowsFourStages", false, "no envelope graph was built");
        }
        else
        {
            // As constructed, before any refresh - which is what is on screen
            // for the first frame after the plugin opens.
            const auto asBuilt = graph->debugEditor().getEnvelope();

            amp.refreshFromParameters();
            const auto afterRefresh = graph->debugEditor().getEnvelope();

            check("AmpEnvUi_TheEditorShowsFourStages",
                  afterRefresh.getPointCount() == 4 && afterRefresh.getSustainPoint() == 2,
                  "the amp editor holds " + juce::String(afterRefresh.getPointCount())
                      + " points with sustain at "
                      + juce::String(afterRefresh.getSustainPoint()));

            juce::StringArray labels;
            for (int i = 1; i < afterRefresh.getPointCount(); ++i)
            {
                const auto role = graph->debugEditor().roleLabelFor(i);
                if (role.isNotEmpty()) { labels.add(role); }
            }
            check("AmpEnvUi_TheEditorLabelsAreAdsr",
                  labels == juce::StringArray({ "ATTACK", "DECAY / SUSTAIN", "RELEASE" }),
                  "the amp editor offers: " + labels.joinIntoString(", "));

            check("AmpEnvUi_TheEditorHasNoHoldHandle",
                  ! labels.contains("HOLD"),
                  labels.contains("HOLD") ? "a HOLD handle is on the amp envelope"
                                          : "no HOLD handle on the amp envelope");

            // Every handle inside the panel and reachable, at the DEFAULT
            // parameters - which is what the plugin opens with. A handle that
            // exists but is drawn past the right-hand edge is a control the
            // user does not have, and RELEASE is the last point, so it is the
            // one that lands there.
            {
                const auto& editor = graph->debugEditor();
                const auto bounds = editor.getLocalBounds().toFloat();

                juce::StringArray offPanel;
                juce::StringArray unreachable;
                juce::String detail;

                const auto examine = [&](const juce::String& role, juce::Point<float> at,
                                         int wantIndex)
                {
                    detail << (detail.isEmpty() ? "" : ", ") << role << " at "
                           << juce::String(juce::roundToInt(at.x)) << ","
                           << juce::String(juce::roundToInt(at.y));

                    if (! bounds.contains(at)) { offPanel.add(role); return; }

                    const auto hit = editor.grabAt(at);
                    if (hit.target != BreakpointEnvelopeEditor::Target::point
                        || hit.index != wantIndex)
                    {
                        unreachable.add(role);
                    }
                };

                for (int i = 1; i < afterRefresh.getPointCount(); ++i)
                {
                    const auto role = editor.roleLabelFor(i);
                    if (role.isEmpty()) { continue; }
                    examine(role, editor.drawnPointPosition(i), i);
                }

                check("AmpEnvUi_EveryHandleIsOnThePanelAtInit", offPanel.isEmpty(),
                      offPanel.isEmpty() ? "all three are inside " + bounds.toString()
                                             + " - " + detail
                                         : "drawn outside the panel: "
                                               + offPanel.joinIntoString(", "));

                check("AmpEnvUi_EveryHandleIsReachableAtInit", unreachable.isEmpty(),
                      unreachable.isEmpty()
                          ? "all three can be grabbed where they are drawn"
                          : "cannot be grabbed: " + unreachable.joinIntoString(", "));
            }

            // The case from the screenshot: a five-point shape whose peak has
            // been dragged off full level, so isPlainAdsr() says no. The
            // earlier fix converted the amp slot only when the shape WAS a
            // plain ADSR, so this walked straight past it and the editor drew
            // a HOLD handle - which is what was on screen.
            {
                EnvelopeSettings shaped;
                shaped.attackSeconds = 0.10f;
                shaped.decaySeconds = 0.17f;
                shaped.sustainLevel = 0.55f;
                shaped.releaseSeconds = 0.10f;

                // Built by hand, because nothing constructs one any more - this
                // is what arrives from state saved while the hold existed, and
                // with its peak dragged off full level it is not a plain ADSR,
                // which is what defeated the earlier conversion.
                px3::BreakpointEnvelope::Point legacy[5];
                legacy[0] = { 0.0, 0.0, 0.0 };
                legacy[1] = { 0.10, 0.82, 0.0 };
                legacy[2] = { 0.25, 1.0, 0.0 };
                legacy[3] = { 0.42, 0.55, 0.0 };
                legacy[4] = { 0.52, 0.0, 0.0 };

                px3::BreakpointEnvelope edited;
                edited.setPoints(legacy, 5, 3);

                check("AmpEnvUi_TheScreenshotShapeIsNotAPlainAdsr",
                      ! edited.isPlainAdsr() && edited.getPointCount() == 5,
                      "a saved five-point shape with the peak at 0.82 reports "
                      "isPlainAdsr() = "
                          + juce::String(edited.isPlainAdsr() ? "true" : "false"));

                processor.setShapedEnvelope(0, edited);
                amp.refreshFromParameters();

                const auto shown = graph->debugEditor().getEnvelope();
                juce::StringArray shownRoles;
                for (int i = 1; i < shown.getPointCount(); ++i)
                {
                    const auto role = graph->debugEditor().roleLabelFor(i);
                    if (role.isNotEmpty()) { shownRoles.add(role); }
                }

                check("AmpEnvUi_LegacyShapesAreCollapsedOnLoad",
                      px3::withoutHoldStage(edited).getPointCount() == 4
                          && px3::withoutHoldStage(edited).getSustainPoint() == 2,
                      "the migration turns that saved shape into "
                          + juce::String(px3::withoutHoldStage(edited).getPointCount())
                          + " points holding at "
                          + juce::String(px3::withoutHoldStage(edited).getSustainPoint()));

                // And whatever the editor is handed, it names no hold - the last
                // line of defence, since a five-point shape can still be built
                // by hand.
                BreakpointEnvelopeEditor forced;
                forced.setSize(400, 200);
                forced.setEnvelope(edited);

                juce::StringArray forcedRoles;
                for (int i = 1; i < forced.getEnvelope().getPointCount(); ++i)
                {
                    const auto role = forced.roleLabelFor(i);
                    if (role.isNotEmpty()) { forcedRoles.add(role); }
                }
                check("AmpEnvUi_AnEditorNeverSaysHold",
                      ! forcedRoles.contains("HOLD"),
                      "handed a five-point shape directly, the editor offers: "
                          + (forcedRoles.isEmpty() ? juce::String("nothing")
                                                   : forcedRoles.joinIntoString(", ")));

                processor.setShapedEnvelope(0, px3::BreakpointEnvelope::fromAdsr(shaped));
                amp.refreshFromParameters();
            }

            // And it is right from the first frame, not only after a refresh.
            check("AmpEnvUi_ItIsRightBeforeTheFirstRefreshToo",
                  asBuilt.getPointCount() == 4 && asBuilt.getSustainPoint() == 2,
                  "as constructed the amp editor holds "
                      + juce::String(asBuilt.getPointCount()) + " points with sustain at "
                      + juce::String(asBuilt.getSustainPoint()));
        }
    }

    // ---- four envelopes, four sets of parameters ----------------------------
    // The coupling this guards against is the whole reason the brief exists:
    // ENV 1's release changing AMP ENV's, or a preset load writing one
    // envelope's values over another's.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        // Deliberately all different, so any bleed shows up as a wrong number
        // rather than a coincidence.
        setParam(processor, "ampAttack", 0.100f);
        setParam(processor, "ampDecay", 0.110f);
        setParam(processor, "ampSustain", 0.120f);
        setParam(processor, "ampRelease", 0.130f);

        for (int env = 1; env <= 3; ++env)
        {
            const auto prefix = "env" + juce::String(env);
            const auto base = 0.200f * static_cast<float>(env);
            setParam(processor, prefix + "Attack", base + 0.001f);
            setParam(processor, prefix + "Decay", base + 0.003f);
            setParam(processor, prefix + "Sustain", 0.10f * static_cast<float>(env));
            setParam(processor, prefix + "Release", base + 0.005f);
        }

        const auto readBack = [](PX3SynthAudioProcessor& p)
        {
            juce::StringArray values;
            values.add(fmt(p.getAttackParam().get(), 3));
            values.add(fmt(p.getDecayParam().get(), 3));
            values.add(fmt(p.getSustainParam().get(), 3));
            values.add(fmt(p.getReleaseParam().get(), 3));
            for (int env = 0; env < 3; ++env)
            {
                values.add(fmt(p.getEnvelopeAttackParam(env).get(), 3));
                values.add(fmt(p.getEnvelopeDecayParam(env).get(), 3));
                values.add(fmt(p.getEnvelopeSustainParam(env).get(), 3));
                values.add(fmt(p.getEnvelopeReleaseParam(env).get(), 3));
            }
            return values;
        };

        const auto written = readBack(processor);
        const juce::StringArray expected {
            "0.100", "0.110", "0.120", "0.130",
            "0.201", "0.203", "0.100", "0.205",
            "0.401", "0.403", "0.200", "0.405",
            "0.601", "0.603", "0.300", "0.605",
        };

        check("Envelopes_EveryParameterIsItsOwn",
              written == expected,
              written == expected
                  ? "16 values written and read back unchanged - four for each of "
                    "AMP ENV and ENV 1-3"
                  : "read back " + written.joinIntoString(", "));

        // And they survive a save/load round trip without crossing over.
        juce::MemoryBlock state;
        processor.getStateInformation(state);

        PX3SynthAudioProcessor restored;
        restored.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        restored.prepareToPlay(kSampleRate, kBlockSize);
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        check("Envelopes_EveryParameterSurvivesASaveAndLoad",
              readBack(restored) == expected,
              readBack(restored) == expected
                  ? "all 16 restored exactly, none overwritten by another envelope"
                  : "restored " + readBack(restored).joinIntoString(", "));

        // The shapes the DSP is handed reflect those parameters, and differ
        // from one another.
        const auto amp = processor.currentAmpEnvelope();
        juce::StringArray attacks;
        attacks.add(fmt(amp.getPoint(1).timeSeconds, 3));
        for (int env = 0; env < 3; ++env)
        {
            attacks.add(fmt(processor.currentModEnvelope(env).getPoint(1).timeSeconds, 3));
        }
        check("Envelopes_TheShapesHandedToTheDspAreAllDifferent",
              attacks == juce::StringArray({ "0.100", "0.201", "0.401", "0.601" }),
              "attack times in the four built shapes: " + attacks.joinIntoString(", "));
    }

    // ---- the graph and the DSP are the same numbers -------------------------
    // Section 28: no separate hard-coded envelope for the UI.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        setParam(processor, "env1Attack", 0.310f);
        setParam(processor, "env1Decay", 0.130f);
        setParam(processor, "env1Sustain", 0.44f);
        setParam(processor, "env1Release", 0.550f);

        const auto fromProcessor = processor.currentModEnvelope(0);

        BreakpointEnvelopeEditor editor;
        editor.setSize(400, 200);
        editor.setEnvelope(fromProcessor);
        const auto shown = editor.getEnvelope().toAdsr();

        check("Envelopes_TheEditorShowsTheParametersTheDspUses",
              std::abs(shown.attackSeconds - 0.310f) < 1.0e-3f
                  && std::abs(shown.decaySeconds - 0.130f) < 1.0e-3f
                  && std::abs(shown.sustainLevel - 0.44f) < 1.0e-3f
                  && std::abs(shown.releaseSeconds - 0.550f) < 1.0e-3f,
              "the editor reads back A " + fmt(shown.attackSeconds, 3)
                  + " D " + fmt(shown.decaySeconds, 3)
                  + " S " + fmt(shown.sustainLevel, 2) + " R "
                  + fmt(shown.releaseSeconds, 3));

        // And the graph moves the way section 27 requires: sustain changes the
        // plateau's height and not its place in time.
        setParam(processor, "env1Sustain", 0.80f);
        const auto higher = processor.currentModEnvelope(0);
        const auto sustainIndex = higher.getSustainPoint();
        check("Envelopes_MoreSustainRaisesThePlateauWithoutMovingIt",
              std::abs(higher.getPoint(sustainIndex).value - 0.80) < 1.0e-3
                  && std::abs(higher.getPoint(sustainIndex).timeSeconds
                              - fromProcessor.getPoint(sustainIndex).timeSeconds) < 1.0e-6,
              "the plateau rises from " + fmt(fromProcessor.getPoint(sustainIndex).value, 2)
                  + " to " + fmt(higher.getPoint(sustainIndex).value, 2) + " at the same time, "
                  + fmt(higher.getPoint(sustainIndex).timeSeconds, 3) + " s");
    }

    // ---- the AMP ENV progress fill -----------------------------------------
    //
    // The graph fills in the area under the part of the envelope the playing
    // note has already been through. The fill is driven by the DSP's own
    // runtime position, so these tests run a real AmpEnvelope and read the
    // display time the editor derives from it - the same number that decides
    // where the fill stops.
    {
        constexpr double kRate = 1000.0;

        px3::BreakpointEnvelope shape;
        {
            EnvelopeSettings settings;
            settings.attackSeconds = 3.0f;
            settings.decaySeconds = 1.0f;
            settings.sustainLevel = 0.5f;
            settings.releaseSeconds = 2.0f;
            shape = px3::BreakpointEnvelope::fromAdsr(settings);
        }

        BreakpointEnvelopeEditor graph;
        graph.setSize(400, 200);
        graph.setEnvelope(shape);

        AmpEnvelope voice;
        voice.prepare(kRate);
        voice.setEnvelope(shape);

        const auto advance = [&voice](double seconds)
        {
            const auto samples = static_cast<int>(std::lround(seconds * kRate));
            for (int i = 0; i < samples; ++i) { voice.getNextSample(); }
        };

        // Push whatever the voice currently reports into the graph, exactly as
        // the editor's timer does, and read back where the fill would stop.
        const auto displayTimeNow = [&graph, &voice]
        {
            const auto position = voice.currentPosition();
            EnvelopePosition progress;
            progress.active = position.active;
            progress.inRelease = position.inRelease;
            progress.heldSeconds = position.heldSeconds;
            progress.releasedSeconds = position.releasedSeconds;
            progress.sustainSeconds = position.sustainSeconds;
            graph.setProgress(progress);
            return graph.progressDisplayTime();
        };

        // Idle: nothing is playing, so there is nothing to fill.
        juce::Path idle;
        graph.buildCurvePath(idle, displayTimeNow(), true);
        check("AmpProgress_NothingIsFilledWhileTheEnvelopeIsIdle",
              idle.isEmpty(),
              "an untriggered envelope draws no progress fill");

        // Through the attack: a third of a 3 s attack is a third of the way.
        voice.noteOn();
        const auto atOnset = displayTimeNow();
        advance(1.0);
        const auto atOneSecond = displayTimeNow();
        advance(1.0);
        const auto atTwoSeconds = displayTimeNow();
        advance(1.0);
        const auto atAttackEnd = displayTimeNow();

        check("AmpProgress_TheFillAdvancesWithTheAttack",
              atOnset < 1.0e-6 && std::abs(atOneSecond - 1.0) < 0.02
                  && std::abs(atTwoSeconds - 2.0) < 0.02
                  && std::abs(atAttackEnd - 3.0) < 0.02,
              "a 3 s attack reads " + fmt(atOnset, 2) + " / " + fmt(atOneSecond, 2)
                  + " / " + fmt(atTwoSeconds, 2) + " / " + fmt(atAttackEnd, 2) + " s");

        // Into the decay: the fill carries on across the stage boundary rather
        // than restarting, which is what a per-stage progress would do.
        advance(0.5);
        const auto midDecay = displayTimeNow();
        check("AmpProgress_TheFillCarriesOnThroughTheDecay",
              midDecay > atAttackEnd && std::abs(midDecay - 3.5) < 0.02,
              "half a second into the decay the fill is at " + fmt(midDecay, 2)
                  + " s, not back at " + fmt(midDecay - atAttackEnd, 2));

        // Holding at the sustain: the envelope is waiting, not advancing, so
        // the fill must stop moving however long the note is held.
        advance(0.5);
        const auto atSustain = displayTimeNow();
        advance(5.0);
        const auto muchLater = displayTimeNow();
        check("AmpProgress_TheFillStopsMovingAtTheSustain",
              std::abs(muchLater - atSustain) < 1.0e-6 && std::abs(atSustain - 4.0) < 0.02,
              "five further seconds of holding leave the fill at " + fmt(muchLater, 3)
                  + " s, where it was at " + fmt(atSustain, 3));

        // Release picks up from the sustain edge and runs on - it does not
        // start over at the left of the graph.
        voice.noteOff();
        const auto releaseStart = displayTimeNow();
        advance(1.0);
        const auto midRelease = displayTimeNow();
        // The release picks up at the sustain point - not back at the left of
        // the graph - and a second of release is a second further along.
        check("AmpProgress_TheReleaseResumesFromTheSustainRatherThanRestarting",
              std::abs(releaseStart - atSustain) < 0.02
                  && std::abs(midRelease - (releaseStart + 1.0)) < 0.02,
              "release begins at " + fmt(releaseStart, 2) + " s and reaches "
                  + fmt(midRelease, 2) + " s after a second");

        // The fill's upper edge is the curve. Not an approximation of it: the
        // point the truncated path stops at has to lie on the full curve.
        {
            juce::Path full, partial;
            graph.buildCurvePath(full);
            graph.buildCurvePath(partial, 1.5, false);

            const auto lastPointOf = [](const juce::Path& path)
            {
                juce::Point<float> last;
                juce::PathFlatteningIterator it(path);
                while (it.next()) { last = { it.x2, it.y2 }; }
                return last;
            };

            // Where the full curve is at the x the fill stopped at.
            const auto stop = lastPointOf(partial);
            auto onCurve = 0.0f;
            auto bestDx = 1.0e9f;
            juce::PathFlatteningIterator walk(full);
            while (walk.next())
            {
                const auto dx = std::abs(walk.x2 - stop.x);
                if (dx < bestDx) { bestDx = dx; onCurve = walk.y2; }
            }

            check("AmpProgress_TheFillsEdgeIsTheEnvelopeCurveItself",
                  bestDx < 2.0f && std::abs(onCurve - stop.y) < 1.0f,
                  "the fill stops at (" + fmt(stop.x, 1) + ", " + fmt(stop.y, 1)
                      + ") and the curve passes through y " + fmt(onCurve, 1) + " there");
        }

        // A note released during the attack has left the held part of the
        // envelope behind, so the fill moves on to the release segment instead
        // of staying where the attack was cut short.
        {
            AmpEnvelope shortened;
            shortened.prepare(kRate);
            shortened.setEnvelope(shape);
            shortened.noteOn();
            for (int i = 0; i < 500; ++i) { shortened.getNextSample(); }
            shortened.noteOff();

            const auto position = shortened.currentPosition();
            EnvelopePosition progress;
            progress.active = position.active;
            progress.inRelease = position.inRelease;
            progress.heldSeconds = position.heldSeconds;
            progress.releasedSeconds = position.releasedSeconds;
            progress.sustainSeconds = position.sustainSeconds;
            graph.setProgress(progress);

            check("AmpProgress_AnEarlyNoteOffMovesTheFillIntoTheRelease",
                  progress.inRelease
                      && std::abs(graph.progressDisplayTime() - releaseStart) < 0.02,
                  "a note released half a second in fills to " + fmt(graph.progressDisplayTime(), 2)
                      + " s, the start of the release");
        }

        // Retriggering starts the fill over rather than continuing where the
        // last note had got to.
        {
            AmpEnvelope retriggered;
            retriggered.prepare(kRate);
            retriggered.setEnvelope(shape);
            retriggered.noteOn();
            for (int i = 0; i < 2000; ++i) { retriggered.getNextSample(); }
            const auto before = retriggered.currentPosition().heldSeconds;
            retriggered.noteOn();
            const auto after = retriggered.currentPosition().heldSeconds;
            check("AmpProgress_ARetriggerStartsTheFillOver",
                  before > 1.9 && after < 1.0e-6,
                  "a retrigger takes the fill from " + fmt(before, 2) + " s back to "
                      + fmt(after, 2) + " s");
        }
    }

    // ---- ENV 1-3 report their progress the same way -------------------------
    //
    // The mod envelopes use a different class from AMP ENV, so the graph's
    // guarantees only hold if that class reports the same shape of answer.
    {
        constexpr double kRate = 1000.0;

        EnvelopeSettings settings;
        settings.attackSeconds = 2.0f;
        settings.decaySeconds = 1.0f;
        settings.sustainLevel = 0.4f;
        settings.releaseSeconds = 1.5f;
        const auto shape = px3::BreakpointEnvelope::fromAdsr(settings);

        BreakpointEnvelopeEditor graph;
        graph.setSize(400, 200);
        graph.setEnvelope(shape);

        EnvelopeGenerator modEnvelope;
        modEnvelope.prepare(kRate);
        modEnvelope.setEnvelope(shape);

        const auto advance = [&modEnvelope](double seconds)
        {
            const auto samples = static_cast<int>(std::lround(seconds * kRate));
            for (int i = 0; i < samples; ++i) { modEnvelope.getNextSample(); }
        };
        const auto displayTimeNow = [&graph, &modEnvelope]
        {
            graph.setProgress(modEnvelope.currentPosition());
            return graph.progressDisplayTime();
        };

        const auto idle = displayTimeNow();
        modEnvelope.noteOn();
        advance(1.0);
        const auto midAttack = displayTimeNow();
        advance(2.0);
        const auto atSustain = displayTimeNow();
        advance(4.0);
        const auto stillHeld = displayTimeNow();
        modEnvelope.noteOff();
        advance(0.5);
        const auto inRelease = displayTimeNow();

        check("ModProgress_TheModEnvelopeReportsProgressLikeTheAmpEnvelope",
              idle < 1.0e-9 && std::abs(midAttack - 1.0) < 0.02
                  && std::abs(atSustain - 3.0) < 0.02
                  && std::abs(stillHeld - atSustain) < 1.0e-6 && inRelease > atSustain,
              "idle " + fmt(idle, 2) + ", attack " + fmt(midAttack, 2) + ", sustain "
                  + fmt(atSustain, 2) + " held to " + fmt(stillHeld, 2) + ", release "
                  + fmt(inRelease, 2));
    }

    // ---- the four progress slots carry four different envelopes -------------
    //
    // AMP ENV is slot 0 and ENV 1-3 are slots 1-3. Give each a sustain time no
    // other envelope has and the routing cannot pass by accident.
    {
        constexpr double kRate = 48000.0;
        constexpr int kBlock = 256;

        PX3SynthAudioProcessor processor;
        setParam(processor, "ampAttack", 1.000f);
        setParam(processor, "ampDecay", 1.000f);   // AMP sustains at 2.0 s

        const float attacks[3] = { 0.200f, 0.400f, 0.800f };
        const float decays[3] = { 0.100f, 0.200f, 0.400f };
        for (int env = 0; env < 3; ++env)
        {
            const auto name = juce::String("env") + juce::String(env + 1);
            setParam(processor, name + "Attack", attacks[static_cast<std::size_t>(env)]);
            setParam(processor, name + "Decay", decays[static_cast<std::size_t>(env)]);
        }

        processor.setPlayConfigDetails(0, 2, kRate, kBlock);
        processor.prepareToPlay(kRate, kBlock);

        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer empty;

        setParam(processor, "ampSustain", 0.60f);
        setParam(processor, "ampRelease", 0.100f);

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        buffer.clear();
        processor.processBlock(buffer, midi);

        for (int b = 0; b < static_cast<int>(0.05 * kRate / kBlock); ++b)
        {
            buffer.clear();
            processor.processBlock(buffer, empty);
        }

        const double expected[4] = { 2.0, 0.300, 0.600, 1.200 };
        auto worst = 0.0;
        auto allActive = true;
        juce::String reported;
        for (int slot = 0; slot < 4; ++slot)
        {
            const auto position = processor.getEnvelopeProgress(slot);
            allActive = allActive && position.active;
            worst = std::max(worst, std::abs(position.sustainSeconds
                                             - expected[static_cast<std::size_t>(slot)]));
            reported += (slot > 0 ? ", " : "") + fmt(position.sustainSeconds, 3);
        }

        check("ModProgress_EachEnvelopeSlotReportsItsOwnEnvelope",
              allActive && worst < 1.0e-3,
              "slots 0-3 sustain at " + reported + " s (want 2.000, 0.300, 0.600, 1.200)");

        // Once the note has finished, every slot goes idle - so the graphs
        // clear rather than keeping the last note's fill on screen forever.
        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        buffer.clear();
        processor.processBlock(buffer, off);

        for (int b = 0; b < static_cast<int>(1.0 * kRate / kBlock); ++b)
        {
            buffer.clear();
            processor.processBlock(buffer, empty);
        }

        auto stillActive = 0;
        for (int slot = 0; slot < 4; ++slot)
        {
            if (processor.getEnvelopeProgress(slot).active) { ++stillActive; }
        }

        check("ModProgress_TheSlotsGoIdleWhenTheNoteHasFinished",
              stillActive == 0,
              juce::String(stillActive) + " of 4 slots still report a playing envelope "
                  + "a second after the note ended");
    }

    // ---- the two envelope MODELS, tested as state machines ------------------
    //
    // At 1000 Hz, so a 100 ms stage is exactly 100 samples and a stage boundary
    // is a sample index rather than a tolerance. The point is to test the
    // DURATION of each stage, not only that the final value is right.
    {
        constexpr double kSlowRate = 1000.0;

        const auto settingsFor = [](float attack, float hold, float decay,
                                    float sustain, float release)
        {
            EnvelopeSettings settings;
            settings.attackSeconds = attack;
            settings.decaySeconds = decay;
            settings.sustainLevel = sustain;
            settings.releaseSeconds = release;
            return settings;
        };

        // Every sample of a held note, then every sample after note-off.
        const auto runAmp = [](const EnvelopeSettings& settings, int heldSamples,
                               int releaseSamples)
        {
            AmpEnvelope envelope;
            envelope.prepare(kSlowRate);
            envelope.setSettings(settings);
            envelope.noteOn();

            std::vector<float> trace;
            for (int i = 0; i < heldSamples; ++i) { trace.push_back(envelope.getNextSample()); }
            envelope.noteOff();
            for (int i = 0; i < releaseSamples; ++i) { trace.push_back(envelope.getNextSample()); }
            return trace;
        };

        const auto runMod = [](const EnvelopeSettings& settings, int heldSamples,
                               int releaseSamples)
        {
            EnvelopeGenerator envelope;
            envelope.prepare(kSlowRate);
            envelope.setSettings(settings);
            envelope.noteOn();

            std::vector<float> trace;
            for (int i = 0; i < heldSamples; ++i) { trace.push_back(envelope.getNextSample()); }
            envelope.noteOff();
            for (int i = 0; i < releaseSamples; ++i) { trace.push_back(envelope.getNextSample()); }
            return trace;
        };

        const auto at = [](const std::vector<float>& trace, int index)
        {
            return trace.empty() ? 0.0f
                                 : trace[static_cast<std::size_t>(
                                       juce::jlimit(0, static_cast<int>(trace.size()) - 1, index))];
        };

        // ---- AMP ENV: A -> D -> S -> R, no hold ----
        {
            const auto settings = settingsFor(0.100f, 0.0f, 0.100f, 0.5f, 0.100f);
            const auto trace = runAmp(settings, 600, 300);

            check("AmpEnvDsp_RisesToFullOverTheAttack",
                  at(trace, 0) < 0.05f && at(trace, 50) > 0.35f && at(trace, 50) < 0.65f
                      && at(trace, 100) > 0.98f,
                  "0 ms " + fmt(at(trace, 0), 3) + ", 50 ms " + fmt(at(trace, 50), 3)
                      + ", 100 ms " + fmt(at(trace, 100), 3));

            check("AmpEnvDsp_FallsToSustainOverTheDecay",
                  at(trace, 150) > 0.5f && at(trace, 150) < 1.0f
                      && std::abs(at(trace, 200) - 0.5f) < 0.02f,
                  "mid-decay " + fmt(at(trace, 150), 3) + ", end of decay "
                      + fmt(at(trace, 200), 3) + " (sustain 0.5)");

            check("AmpEnvDsp_HoldsAtSustainUntilNoteOff",
                  std::abs(at(trace, 300) - 0.5f) < 0.02f
                      && std::abs(at(trace, 599) - 0.5f) < 0.02f,
                  "still " + fmt(at(trace, 599), 3) + " after 600 ms of holding");

            check("AmpEnvDsp_ReleasesToSilence",
                  at(trace, 650) < at(trace, 599) && at(trace, 750) < 0.01f,
                  "100 ms release: " + fmt(at(trace, 599), 3) + " -> "
                      + fmt(at(trace, 650), 3) + " -> " + fmt(at(trace, 750), 4));

            // Monotonic where it should be. A stage that wobbles is a stage
            // that is being computed twice.
            auto attackRises = true;
            for (int i = 1; i <= 100; ++i)
            {
                if (at(trace, i) < at(trace, i - 1) - 1.0e-6f) { attackRises = false; }
            }
            auto decayFalls = true;
            for (int i = 102; i <= 200; ++i)
            {
                if (at(trace, i) > at(trace, i - 1) + 1.0e-6f) { decayFalls = false; }
            }
            check("AmpEnvDsp_EachStageIsMonotone", attackRises && decayFalls,
                  "the attack only rises and the decay only falls");
        }

        // ---- AMP ENV has no hold: setting one changes nothing ----
        {
            const auto without = runAmp(settingsFor(0.100f, 0.0f, 0.100f, 0.5f, 0.100f), 400, 200);
            const auto with = runAmp(settingsFor(0.100f, 0.200f, 0.100f, 0.5f, 0.100f), 400, 200);

            auto identical = with.size() == without.size();
            auto worst = 0.0f;
            for (std::size_t i = 0; identical && i < with.size(); ++i)
            {
                worst = juce::jmax(worst, std::abs(with[i] - without[i]));
            }
            check("AmpEnvDsp_IgnoresHoldEntirely", identical && worst < 1.0e-6f,
                  "a 200 ms hold changes the amp envelope by " + fmt(worst, 6)
                      + " - it has no hold stage to change");
        }

        // ---- ENV 1-3: the same four stages ----
        {
            const auto settings = settingsFor(0.100f, 0.0f, 0.100f, 0.25f, 0.100f);
            const auto trace = runMod(settings, 600, 300);

            check("ModEnvDsp_RisesToFullOverTheAttack",
                  at(trace, 0) < 0.05f && at(trace, 100) > 0.98f,
                  "0 ms " + fmt(at(trace, 0), 3) + " -> 100 ms " + fmt(at(trace, 100), 3));

            check("ModEnvDsp_DecaysToSustain",
                  at(trace, 150) < 0.98f && at(trace, 150) > 0.25f
                      && std::abs(at(trace, 200) - 0.25f) < 0.02f,
                  "mid-decay " + fmt(at(trace, 150), 3) + ", end of decay "
                      + fmt(at(trace, 200), 3) + " (sustain 0.25)");

            check("ModEnvDsp_HoldsAtSustainUntilNoteOff",
                  std::abs(at(trace, 599) - 0.25f) < 0.02f,
                  "still " + fmt(at(trace, 599), 3) + " at 600 ms");

            check("ModEnvDsp_ReleasesToSilence", at(trace, 750) < 0.01f,
                  "silent at " + fmt(at(trace, 750), 4) + " after the release");
        }

        // ---- sustain is a LEVEL ----
        {
            const auto quiet = runMod(settingsFor(0.100f, 0.0f, 0.100f, 0.25f, 0.100f), 600, 100);
            const auto loud = runMod(settingsFor(0.100f, 0.0f, 0.100f, 0.75f, 0.100f), 600, 100);

            check("ModEnvDsp_SustainChangesLevelNotDuration",
                  std::abs(at(quiet, 500) - 0.25f) < 0.02f
                      && std::abs(at(loud, 500) - 0.75f) < 0.02f
                      && std::abs(at(quiet, 100) - at(loud, 100)) < 0.02f,
                  "the plateau moves from " + fmt(at(quiet, 500), 2) + " to "
                      + fmt(at(loud, 500), 2) + " while the attack ends at the same place");
        }

        // ---- release starts from WHEREVER the envelope is ----
        {
            const auto settings = settingsFor(0.200f, 0.200f, 0.200f, 0.30f, 0.200f);

            struct Moment { const char* stage; int atSample; };
            const Moment moments[] = {
                { "attack", 100 },   // half way up
                { "hold",   300 },   // at full level
                { "decay",  500 },   // part way down
                { "sustain",700 },   // at the sustain level
            };

            juce::StringArray jumps;
            juce::String detail;
            for (const auto& moment : moments)
            {
                EnvelopeGenerator envelope;
                envelope.prepare(kSlowRate);
                envelope.setSettings(settings);
                envelope.noteOn();

                auto lastHeld = 0.0f;
                for (int i = 0; i < moment.atSample; ++i) { lastHeld = envelope.getNextSample(); }

                envelope.noteOff();
                const auto firstReleased = envelope.getNextSample();

                detail << (detail.isEmpty() ? "" : ", ") << moment.stage << " "
                       << fmt(lastHeld, 3) << "->" << fmt(firstReleased, 3);

                // No step. Release continues from where the envelope was, so
                // the first released sample is next to the last held one.
                if (std::abs(firstReleased - lastHeld) > 0.05f)
                {
                    jumps.add(juce::String(moment.stage) + " jumped from "
                              + fmt(lastHeld, 3) + " to " + fmt(firstReleased, 3));
                }
            }

            check("ModEnvDsp_ReleaseStartsFromTheCurrentLevel", jumps.isEmpty(),
                  jumps.isEmpty() ? "note-off during " + detail
                                  : jumps.joinIntoString("; "));
        }

        // ---- retrigger ----
        // Section 14 asks what the existing policy IS rather than for a new
        // one. It is: note-on restarts the contour from the beginning, and the
        // output smoother carries the level across so a retrigger from a
        // sounding note ramps rather than steps. This records that, and would
        // catch either half of it changing.
        {
            // At the REAL sample rate, not the 1000 Hz rig above. The output
            // smoother's ramp is a duration, so at 1000 Hz it is a handful of
            // samples and judging it there measures the rig rather than the
            // envelope. Stage TIMING is what 1000 Hz is for.
            const auto settings = settingsFor(0.050f, 0.0f, 0.050f, 0.6f, 0.100f);

            AmpEnvelope envelope;
            envelope.prepare(kSampleRate);
            envelope.setSettings(settings);
            envelope.noteOn();

            auto beforeRetrigger = 0.0f;
            const auto toSustain = static_cast<int>(kSampleRate * 0.3);
            for (int i = 0; i < toSustain; ++i) { beforeRetrigger = envelope.getNextSample(); }

            envelope.noteOn();

            // Straight through the new attack, watching for a step and for the
            // peak at the same time - the attack is 50 ms, so measuring the
            // peak afterwards would measure the sustain it had already reached.
            auto worstStep = 0.0f;
            auto peak = 0.0f;
            auto previous = beforeRetrigger;
            for (int i = 0; i < static_cast<int>(kSampleRate * 0.08); ++i)
            {
                const auto value = envelope.getNextSample();
                worstStep = juce::jmax(worstStep, std::abs(value - previous));
                peak = juce::jmax(peak, value);
                previous = value;
            }

            check("Envelopes_RetriggerRestartsWithoutAStep",
                  worstStep < 0.02f,
                  "retriggered from " + fmt(beforeRetrigger, 3)
                      + ": largest sample-to-sample step through the new attack is "
                      + fmt(worstStep, 5));

            check("Envelopes_RetriggerActuallyRestartsTheContour",
                  peak > 0.95f,
                  "the contour runs again from the top, reaching " + fmt(peak, 3));
        }

        // ---- four independent instances ----
        {
            AmpEnvelope amp;
            std::array<EnvelopeGenerator, 3> mods;

            amp.prepare(kSlowRate);
            amp.setSettings(settingsFor(0.100f, 0.0f, 0.050f, 0.9f, 0.050f));
            for (int i = 0; i < 3; ++i)
            {
                mods[static_cast<std::size_t>(i)].prepare(kSlowRate);
                mods[static_cast<std::size_t>(i)].setSettings(
                    settingsFor(0.100f + 0.200f * static_cast<float>(i + 1), 0.0f,
                                0.050f, 0.9f, 0.050f));
            }

            // Only ENV 1 is triggered.
            mods[0].noteOn();
            for (int i = 0; i < 50; ++i)
            {
                mods[0].getNextSample();
                mods[1].getNextSample();
                mods[2].getNextSample();
                amp.getNextSample();
            }

            check("Envelopes_TriggeringOneDoesNotTriggerAnother",
                  mods[0].isActive() && ! mods[1].isActive() && ! mods[2].isActive()
                      && ! amp.isActive(),
                  "ENV 1 is running; ENV 2, ENV 3 and AMP ENV are not");

            // And their timings do not bleed into each other.
            const auto reach = [&settingsFor](float attack)
            {
                EnvelopeGenerator envelope;
                envelope.prepare(kSlowRate);
                envelope.setSettings(settingsFor(attack, 0.0f, 0.050f, 0.9f, 0.050f));
                envelope.noteOn();
                for (int i = 0; i < 300; ++i)
                {
                    if (envelope.getNextSample() > 0.98f) { return i; }
                }
                return -1;
            };
            const auto fast = reach(0.100f);
            const auto slow = reach(0.250f);
            check("Envelopes_EachInstanceKeepsItsOwnTiming",
                  fast > 90 && fast < 115 && slow > 240 && slow < 265,
                  "a 100 ms attack peaks at sample " + juce::String(fast)
                      + " and a 250 ms attack at sample " + juce::String(slow));
        }
    }

    // ---- the on-screen keyboard reaches the audio without a lock ------------
    // The path a standalone user plays through, which is NOT the path a DAW
    // uses: the keyboard queues notes on the message thread and processBlock
    // drains them. It used to do that behind a CriticalSection taken on the
    // audio thread.
    {
        PX3SynthAudioProcessor processor;
        makePlainPatch(processor);
        setParam(processor, "ampAttack", 0.010f);
        setParam(processor, "ampSustain", 1.00f);
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        juce::AudioBuffer<float> buffer(2, kBlockSize);
        juce::MidiBuffer empty;

        buffer.clear();
        processor.processBlock(buffer, empty);
        const auto silent = buffer.getMagnitude(0, kBlockSize);

        processor.queueVirtualKeyboardNoteOn(60, 1.0f);
        processor.queueVirtualKeyboardNoteOn(64, 1.0f);
        processor.queueVirtualKeyboardNoteOn(67, 1.0f);

        auto heard = 0.0f;
        for (int block = 0; block < 20; ++block)
        {
            buffer.clear();
            empty.clear();
            processor.processBlock(buffer, empty);
            heard = juce::jmax(heard, buffer.getMagnitude(0, kBlockSize));
        }

        check("VirtualKeyboard_QueuedNotesReachTheAudio",
              silent < 1.0e-6f && heard > 0.01f,
              "silent before at " + fmt(silent, 8) + ", sounding after at " + fmt(heard, 4));

        // Note-off travels the same way.
        processor.queueVirtualKeyboardNoteOff(60);
        processor.queueVirtualKeyboardNoteOff(64);
        processor.queueVirtualKeyboardNoteOff(67);
        auto tail = 0.0f;
        for (int block = 0; block < 60; ++block)
        {
            buffer.clear();
            empty.clear();
            processor.processBlock(buffer, empty);
            tail = buffer.getMagnitude(0, kBlockSize);
        }
        check("VirtualKeyboard_NoteOffTravelsTheSameWay", tail < heard * 0.05f,
              "the chord falls to " + fmt(tail, 6) + " after the queued note-offs");

        // More events than the ring holds are dropped, not blocked on. The
        // audio thread must never wait for the message thread.
        for (int i = 0; i < 5000; ++i)
        {
            processor.queueVirtualKeyboardNoteOn(60 + (i % 12), 0.5f);
            processor.queueVirtualKeyboardNoteOff(60 + (i % 12));
        }
        auto stillFinite = true;
        for (int block = 0; block < 10; ++block)
        {
            buffer.clear();
            empty.clear();
            processor.processBlock(buffer, empty);
            for (int i = 0; i < kBlockSize; ++i)
            {
                if (! std::isfinite(buffer.getSample(0, i))) { stillFinite = false; }
            }
        }
        check("VirtualKeyboard_AFloodIsDroppedNotBlockedOn", stillFinite,
              "10000 queued events overflow the ring and the audio stays finite");
    }

    // ---- a SHAPED envelope must survive note-on -----------------------------
    // Captured from the standalone: the voice held attackSeconds 0.0120 while
    // the user's envelope had a four second attack, and only for the first
    // block. That is the ADSR parameters and the drawn shape disagreeing -
    // which happens the moment the curve is edited past what the four
    // parameters can describe, because the write-back only runs while the
    // shape is still a plain ADSR.
    {
        PX3SynthAudioProcessor processor;
        setParam(processor, "osc1Enabled", 1.0f);
        setParam(processor, "osc2Enabled", 0.0f);
        setParam(processor, "osc3Enabled", 0.0f);
        setParam(processor, "subOscEnabled", 0.0f);
        setParam(processor, "delayEnabled", 0.0f);
        setParam(processor, "reverbEnabled", 0.0f);
        setParam(processor, "moodEnabled", 0.0f);
        setParam(processor, "vibeEnabled", 0.0f);
        setChoice(processor, "osc1Mode", 0);

        // The parameters say a short attack, the stored shape says four seconds,
        // and the shape is FREE-FORM - a point has been added, so it is no
        // longer anything the four parameters can describe.
        //
        // That last part is what makes this a real state rather than a
        // contrived one. On a four-point skeleton the two can no longer
        // disagree: the editor writes both, and the graph and the DSP both
        // apply the parameters over the stored curves. Once a point is ADDED
        // the parameters stop being written, the shape is the whole truth, and
        // the disagreement this test needs is exactly what the user has.
        //
        // The defect was startNote rebuilding from the ADSR settings instead of
        // playing that shape, which starts a four second attack at full level.
        setParam(processor, "ampAttack", 0.012f);
        setParam(processor, "ampDecay", 0.100f);
        setParam(processor, "ampSustain", 1.00f);
        setParam(processor, "ampRelease", 0.500f);

        EnvelopeSettings slow;
        slow.attackSeconds = 4.000f;
        slow.decaySeconds = 0.100f;
        slow.sustainLevel = 1.0f;
        slow.releaseSeconds = 0.500f;

        // AMP ENV is ADSR-only now, so the four points and their times come from
        // the parameters: the shape cannot disagree with them about timing any
        // more, and that is the architecture, not a workaround. What the shape
        // still owns alone is its CURVES - so the long attack is asked for
        // through the parameters, and the bend is what a note-on must not
        // throw away.
        setParam(processor, "ampAttack", 4.000f);
        setParam(processor, "ampDecay", 0.100f);
        setParam(processor, "ampSustain", 1.00f);
        setParam(processor, "ampRelease", 0.500f);
        juce::ignoreUnused(slow);

        auto shaped = px3::BreakpointEnvelope::fromAdsr(slow);
        shaped.setCurve(0, 0.62);
        processor.setShapedEnvelope(0, shaped);

        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        juce::AudioBuffer<float> buffer(2, kBlockSize);
        juce::MidiBuffer midi;
        for (const auto note : { 72, 76, 79 })
        {
            midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
        }

        auto firstBlock = 0.0f;
        auto laterPeak = 0.0f;
        for (int block = 0; block < 20; ++block)
        {
            buffer.clear();
            processor.processBlock(buffer, midi);
            midi.clear();

            const auto level = buffer.getMagnitude(0, kBlockSize);
            if (block == 0) { firstBlock = level; }
            else { laterPeak = juce::jmax(laterPeak, level); }
        }

        // With a four second attack the first block is the QUIETEST, by a wide
        // margin. 1.5x was too loose to fail against the defect it describes -
        // the bug measured 1.3x - which is the mistake this investigation has
        // made more than once.
        check("Onset_ANoteOnDoesNotJumpPastItsAttack",
              firstBlock < laterPeak * 0.5f,
              "first block peaks at " + fmt(firstBlock, 6)
                  + " against " + fmt(laterPeak, 6) + " over the next 20 - "
                  + fmt(laterPeak > 1.0e-9f ? firstBlock / laterPeak : 0.0f, 1) + "x");

        // And the bend survived the note-on. This is what is left of the
        // original defect on an ADSR-only card: startNote rebuilding the
        // envelope from the four parameters would straighten a curve the
        // parameters cannot describe.
        const auto playedAfterNoteOn = processor.currentAmpEnvelope();
        check("Onset_ANoteOnDoesNotStraightenADrawnCurve",
              std::abs(playedAfterNoteOn.getPoint(0).curveToNext - 0.62) < 1.0e-9,
              "after the note-on the played envelope's attack still bends "
                  + fmt(static_cast<float>(playedAfterNoteOn.getPoint(0).curveToNext), 2));
    }

    // ---- a note-on may not be louder than its own envelope ------------------
    //
    // The criterion comes from a real recording of the fault. In a standalone
    // capture of this exact patch - one sine, no modulation, FX bypassed, a
    // four second attack, a three note chord at full velocity - the first 100
    // ms peaked at 0.289, a level the attack does not reach again until 2500
    // ms. That burst lasted 492 samples, which is 0.96 of a 512 sample block:
    // one block rendered as though the envelope were open, then the real
    // attack.
    //
    // So the assertion is not "no discontinuity" - the burst was at the chord's
    // own pitch with no step in it - but "no louder than the envelope allows".
    // That is what a superimposed full-level block violates and an attack ramp
    // never does.
    {
        PX3SynthAudioProcessor processor;
        setParam(processor, "osc1Enabled", 1.0f);
        setParam(processor, "osc2Enabled", 0.0f);
        setParam(processor, "osc3Enabled", 0.0f);
        setParam(processor, "subOscEnabled", 0.0f);
        setParam(processor, "delayEnabled", 0.0f);
        setParam(processor, "reverbEnabled", 0.0f);
        setParam(processor, "moodEnabled", 0.0f);
        setParam(processor, "vibeEnabled", 0.0f);
        setChoice(processor, "osc1Mode", 0);
        setParam(processor, "ampAttack", 4.000f);
        setParam(processor, "ampDecay", 0.100f);
        setParam(processor, "ampSustain", 1.00f);
        setParam(processor, "ampRelease", 0.500f);

        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        juce::AudioBuffer<float> buffer(2, kBlockSize);
        juce::MidiBuffer empty;

        // Idle first, the way a running plugin waits for a key.
        for (int b = 0; b < static_cast<int>(0.5 * kSampleRate / kBlockSize); ++b)
        {
            buffer.clear();
            processor.processBlock(buffer, empty);
        }

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 67, 1.0f), 0);

        AmpEnvelope reference;
        reference.prepare(kSampleRate);
        EnvelopeSettings settings;
        settings.attackSeconds = 4.000f;
        settings.decaySeconds = 0.100f;
        settings.sustainLevel = 1.0f;
        settings.releaseSeconds = 0.500f;
        reference.setSettings(settings);
        reference.noteOn();

        auto worstRatio = 0.0f;
        auto worstAtMs = 0.0;
        auto worstLevel = 0.0f;

        for (int block = 0; block < static_cast<int>(0.10 * kSampleRate / kBlockSize); ++block)
        {
            buffer.clear();
            processor.processBlock(buffer, midi);
            midi.clear();

            auto env = 0.0f;
            for (int i = 0; i < kBlockSize; ++i) { env = reference.getNextSample(); }

            const auto level = buffer.getMagnitude(0, kBlockSize);
            const auto ratio = level / juce::jmax(1.0e-6f, env);
            if (ratio > worstRatio)
            {
                worstRatio = ratio;
                worstLevel = level;
                worstAtMs = 1000.0 * (block + 1) * kBlockSize / kSampleRate;
            }
        }

        // Three voices at unity would give 3; the recording's burst was 170.
        check("Onset_ANoteIsNeverLouderThanItsEnvelopeAllows",
              worstRatio < 5.0f,
              "worst level against the envelope in the first 100 ms: " + fmt(worstRatio, 2)
                  + "x (" + fmt(worstLevel, 6) + " at " + fmt(worstAtMs, 1) + " ms)");
    }

    // ---- retriggering must not dive to silence first ------------------------
    // Reported as a click at note-on under a long attack. Restarting the
    // contour at zero means the level falls from wherever the release tail was
    // down to nothing before the new attack begins - measured at 0.4934 to
    // 0.0052 in 5 ms with a one second attack, which is what is heard.
    {
        const auto retrigger = [](auto& envelope, float attack)
        {
            EnvelopeSettings settings;
            settings.attackSeconds = attack;
            settings.decaySeconds = 0.100f;
            settings.sustainLevel = 1.0f;
            settings.releaseSeconds = 0.500f;
            envelope.prepare(kSampleRate);
            envelope.setSettings(settings);

            envelope.noteOn();
            for (int i = 0; i < static_cast<int>(kSampleRate * 1.2); ++i)
            {
                envelope.getNextSample();
            }
            envelope.noteOff();

            auto atRetrigger = 0.0f;
            for (int i = 0; i < static_cast<int>(kSampleRate * 0.05); ++i)
            {
                atRetrigger = envelope.getNextSample();
            }

            envelope.noteOn();

            auto lowest = atRetrigger;
            auto worstStep = 0.0f;
            auto previous = atRetrigger;
            for (int i = 0; i < static_cast<int>(kSampleRate * 0.05); ++i)
            {
                const auto value = envelope.getNextSample();
                lowest = juce::jmin(lowest, value);
                worstStep = juce::jmax(worstStep, std::abs(value - previous));
                previous = value;
            }

            struct Result { float atRetrigger, lowest, worstStep; };
            return Result { atRetrigger, lowest, worstStep };
        };

        {
            AmpEnvelope envelope;
            const auto result = retrigger(envelope, 1.000f);
            check("AmpEnv_RetriggerDoesNotDiveToSilenceFirst",
                  result.lowest > result.atRetrigger * 0.95f,
                  "retriggered at " + fmt(result.atRetrigger, 4)
                      + ", the lowest the envelope reaches over the next 50 ms is "
                      + fmt(result.lowest, 4));

            check("AmpEnv_RetriggerHasNoStep",
                  result.worstStep < 0.005f,
                  "largest sample-to-sample step through the retriggered attack: "
                      + fmt(result.worstStep, 6));
        }

        {
            EnvelopeGenerator envelope;
            const auto result = retrigger(envelope, 1.000f);
            check("ModEnv_RetriggerDoesNotDiveToSilenceFirst",
                  result.lowest > result.atRetrigger * 0.95f,
                  "retriggered at " + fmt(result.atRetrigger, 4)
                      + ", lowest over the next 50 ms " + fmt(result.lowest, 4));
        }

        // And a FRESH voice still rises from silence - the anchor is zero
        // there, so a first note is unchanged.
        {
            AmpEnvelope fresh;
            EnvelopeSettings settings;
            settings.attackSeconds = 1.000f;
            settings.decaySeconds = 0.100f;
            settings.sustainLevel = 1.0f;
            settings.releaseSeconds = 0.100f;
            fresh.prepare(kSampleRate);
            fresh.setSettings(settings);
            fresh.noteOn();

            const auto first = fresh.getNextSample();
            auto after10ms = 0.0f;
            for (int i = 0; i < static_cast<int>(kSampleRate * 0.01); ++i)
            {
                after10ms = fresh.getNextSample();
            }

            check("AmpEnv_AFirstNoteStillRisesFromSilence",
                  first < 1.0e-4f && after10ms < 0.02f,
                  "a fresh voice starts at " + fmt(first, 6) + " and is only "
                      + fmt(after10ms, 4) + " after 10 ms of a 1 s attack");
        }
    }

    // ---- the mod panel scrolls past its last card ---------------------------
    //
    // The ENV cards grew and the panel did not: the LFO row took half the
    // panel, the ENV row then took its own minimum, and the pair added up to
    // more than the content - so the envelope cards ran off the bottom with
    // nothing to scroll to.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        if (editor != nullptr)
        {
            ModPanel* panel = nullptr;
            std::function<void(juce::Component&)> findPanel = [&](juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* p = dynamic_cast<ModPanel*>(child)) { panel = p; }
                    findPanel(*child);
                }
            };
            findPanel(*editor);

            // The REAL UIConfig, because every number this checks lives in
            // that file: the row percentages, the card minimums and the tail.
            // Without it the panel falls back to the defaults compiled in and
            // the test guards numbers nobody ships.
            const auto configFile = juce::File::getCurrentWorkingDirectory()
                                        .getChildFile("Source/UI/UIConfig.json");
            if (panel != nullptr && configFile.existsAsFile())
            {
                juce::String configError;
                if (auto config = UIConfig::fromJsonText(configFile.loadFileAsString(), configError))
                {
                    panel->setUIConfig(config);
                }
            }

            if (panel != nullptr)
            {
                // Sized the way the viewport sizes it: the panel's own
                // preferred height plus the tail it scrolls to.
                constexpr int kTail = 30;
                const auto width = panel->getPreferredContentWidth();
                const auto height = panel->getPreferredContentHeight() + kTail;
                panel->setBounds(0, 0, width, height);
                panel->resized();

                std::vector<EnvelopeComponent*> cards;
                std::function<void(juce::Component&)> find = [&](juce::Component& c)
                {
                    for (auto* child : c.getChildren())
                    {
                        if (child == nullptr) { continue; }
                        if (auto* env = dynamic_cast<EnvelopeComponent*>(child))
                        {
                            cards.push_back(env);
                        }
                        find(*child);
                    }
                };
                find(*panel);

                auto lowest = 0;
                for (auto* card : cards) { lowest = juce::jmax(lowest, card->getBounds().getBottom()); }

                // The knobs sit clear of the graph rather than hard against
                // it, and the whole row is inside the card.
                juce::StringArray cramped;
                for (std::size_t i = 0; i < cards.size(); ++i)
                {
                    auto* card = cards[i];
                    if (card->debugAdsrKnobCount() != 4) { continue; }

                    const auto graphBottom = card->debugEditorBounds().getBottom();
                    auto highestKnob = card->getHeight();
                    for (int k = 0; k < 4; ++k)
                    {
                        highestKnob = juce::jmin(highestKnob,
                                                 juce::jmin(card->debugAdsrKnob(k).getBounds().getY(),
                                                            card->debugAdsrKnobLabel(k).getBounds().getY()));
                    }
                    if (highestKnob - graphBottom < 20)
                    {
                        cramped.add("card " + juce::String(static_cast<int>(i)) + ": graph ends at "
                                    + juce::String(graphBottom) + ", knobs start at "
                                    + juce::String(highestKnob) + " - only "
                                    + juce::String(highestKnob - graphBottom) + " px between");
                    }
                }

                auto tightest = 10000;
                for (std::size_t i = 0; i < cards.size(); ++i)
                {
                    auto* card = cards[i];
                    if (card->debugAdsrKnobCount() != 4) { continue; }
                    auto highestKnob = card->getHeight();
                    for (int k = 0; k < 4; ++k)
                    {
                        highestKnob = juce::jmin(highestKnob,
                                                 juce::jmin(card->debugAdsrKnob(k).getBounds().getY(),
                                                            card->debugAdsrKnobLabel(k).getBounds().getY()));
                    }
                    tightest = juce::jmin(tightest,
                                          highestKnob - card->debugEditorBounds().getBottom());
                }

                check("EnvelopeKnobs_TheRowIsNotHardAgainstTheGraph",
                      ! cards.empty() && cramped.isEmpty(),
                      cramped.isEmpty()
                          ? "every knob row sits clear of its graph, tightest "
                                + juce::String(tightest) + " px"
                          : cramped.joinIntoString("; "));

                check("ModPanel_ScrollsPastTheBottomOfTheEnvelopeCards",
                      ! cards.empty() && lowest > 0 && height - lowest >= kTail,
                      cards.empty()
                          ? "no envelope cards on the mod panel"
                          : juce::String(static_cast<int>(cards.size())) + " cards, the lowest ending at "
                                + juce::String(lowest) + " in a panel " + juce::String(height)
                                + " tall - " + juce::String(height - lowest) + " px below it");
            }
        }
    }

    // ---- bypassing an envelope does not discard its shape -------------------
    //
    // Toggling BYPASS is a mute, not a reset: the curve has to be exactly where
    // it was when the card comes back.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        if (editor != nullptr)
        {
            std::vector<EnvelopeComponent*> cards;
            std::function<void(juce::Component&)> find = [&](juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* env = dynamic_cast<EnvelopeComponent*>(child)) { cards.push_back(env); }
                    find(*child);
                }
            };
            find(*editor);

            // A click on the GRAPH is not a click on the card background, so it
            // must not toggle the envelope off. The test drives the card's own
            // mouseUp rather than the editor's, because that is the handler
            // that decides - and it asked for the LAST cardInner row, which is
            // the knob row now that there is one.
            if (! cards.empty())
            {
                auto* card = cards[0];
                const auto onTheGraph = card->debugEditorBounds().getCentre().toFloat();
                const auto wasEnabled = processor.getEnvelopeEnabledParam(0).get();

                const auto click = juce::MouseEvent(
                    juce::Desktop::getInstance().getMainMouseSource(), onTheGraph,
                    juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, card, card,
                    juce::Time::getCurrentTime(), onTheGraph,
                    juce::Time::getCurrentTime(), 1, false);
                card->mouseUp(click);

                check("EnvelopeBypass_AClickOnTheGraphDoesNotToggleTheCard",
                      processor.getEnvelopeEnabledParam(0).get() == wasEnabled,
                      juce::String("clicking the graph at ") + onTheGraph.toString()
                          + " left the envelope "
                          + (processor.getEnvelopeEnabledParam(0).get() ? "on" : "off")
                          + ", was " + (wasEnabled ? "on" : "off"));
            }

            // Slot 1 is ENV 1. Edited the way a drag leaves it: a shape stored
            // with a bend, and the four parameters written back to match.
            EnvelopeSettings edited;
            edited.attackSeconds = 0.250f;
            edited.decaySeconds = 0.400f;
            edited.sustainLevel = 0.35f;
            edited.releaseSeconds = 0.700f;

            auto shape = px3::BreakpointEnvelope::fromAdsr(edited);
            shape.setCurve(0, 0.6);
            shape.setCurve(2, -0.4);
            processor.setShapedEnvelope(1, shape);

            const auto write = [](juce::AudioParameterFloat& parameter, float value)
            {
                parameter.beginChangeGesture();
                parameter.setValueNotifyingHost(parameter.convertTo0to1(value));
                parameter.endChangeGesture();
            };
            write(processor.getEnvelopeAttackParam(0), edited.attackSeconds);
            write(processor.getEnvelopeDecayParam(0), edited.decaySeconds);
            write(processor.getEnvelopeSustainParam(0), edited.sustainLevel);
            write(processor.getEnvelopeReleaseParam(0), edited.releaseSeconds);

            auto& enabled = processor.getEnvelopeEnabledParam(0);
            // Through ModPanel, which is what actually hands the stored shape
            // to the cards - refreshing the cards directly would only exercise
            // the half of the path that reads parameters.
            ModPanel* panel = nullptr;
            std::function<void(juce::Component&)> findPanel = [&](juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* p = dynamic_cast<ModPanel*>(child)) { panel = p; }
                    findPanel(*child);
                }
            };
            findPanel(*editor);

            const auto refresh = [&]
            {
                if (panel != nullptr) { panel->refreshFromParameters(); }
            };
            const auto setEnabled = [&](bool on)
            {
                enabled.beginChangeGesture();
                enabled.setValueNotifyingHost(on ? 1.0f : 0.0f);
                enabled.endChangeGesture();
            };

            refresh();
            const auto before = processor.getShapedEnvelope(1).toAdsr();

            setEnabled(false);
            refresh();

            // WHILE bypassed, which is where this went wrong: the graph was
            // drawn from what the VOICE runs, and a bypassed modulation
            // envelope runs a neutral contour that gets out of the way. Bypass
            // is a mute, so the curve has to stay exactly where it was.
            const auto whileOff = cards.empty() ? px3::BreakpointEnvelope()
                                                : cards[0]->debugEditor().getEnvelope();
            const auto offAdsr = whileOff.toAdsr();
            check("EnvelopeBypass_TheGraphKeepsItsShapeWhileBypassed",
                  ! cards.empty()
                      && std::abs(offAdsr.attackSeconds - before.attackSeconds) < 1.0e-4f
                      && std::abs(offAdsr.decaySeconds - before.decaySeconds) < 1.0e-4f
                      && std::abs(offAdsr.sustainLevel - before.sustainLevel) < 1.0e-4f
                      && std::abs(offAdsr.releaseSeconds - before.releaseSeconds) < 1.0e-4f,
                  "bypassed, the graph reads A " + fmt(offAdsr.attackSeconds, 3) + " D "
                      + fmt(offAdsr.decaySeconds, 3) + " S " + fmt(offAdsr.sustainLevel, 2)
                      + " R " + fmt(offAdsr.releaseSeconds, 3) + " (edited to A "
                      + fmt(before.attackSeconds, 3) + " D " + fmt(before.decaySeconds, 3)
                      + " S " + fmt(before.sustainLevel, 2) + " R "
                      + fmt(before.releaseSeconds, 3) + ")");

            // And the VOICE still gets the neutral contour, which is what
            // bypass means to the DSP - the two answers stay different.
            const auto played = processor.currentModEnvelopeSettings(0);
            check("EnvelopeBypass_TheVoiceStillGetsTheNeutralContour",
                  played.sustainLevel >= 1.0f - 1.0e-6f
                      && played.attackSeconds < 0.01f && played.releaseSeconds < 0.05f,
                  "a bypassed envelope plays A " + fmt(played.attackSeconds, 3) + " S "
                      + fmt(played.sustainLevel, 2) + " R " + fmt(played.releaseSeconds, 3));

            setEnabled(true);
            refresh();

            const auto after = processor.getShapedEnvelope(1);
            const auto readBack = after.toAdsr();

            // What the user is looking at, not only what the processor holds.
            const auto shown = cards.empty() ? px3::BreakpointEnvelope()
                                             : cards[0]->debugEditor().getEnvelope();
            check("EnvelopeBypass_TheGraphStillShowsTheEditedShape",
                  panel != nullptr && ! cards.empty()
                      && std::abs(shown.toAdsr().decaySeconds - before.decaySeconds) < 1.0e-4f
                      && std::abs(shown.getPoint(0).curveToNext - 0.6) < 1.0e-6,
                  "the graph reads a decay of " + fmt(shown.toAdsr().decaySeconds, 3)
                      + " s with a first curve of " + fmt(shown.getPoint(0).curveToNext, 2));

            // And a shape the four parameters CANNOT describe, which is the one
            // with no parameters to be rebuilt from if anything drops it.
            auto freeForm = px3::BreakpointEnvelope::fromAdsr(edited);
            // Adding points is a Breakpoint-mode capability.
            freeForm.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
            freeForm.addPoint(0.320, 0.85);
            processor.setShapedEnvelope(1, freeForm);
            refresh();

            setEnabled(false);
            refresh();
            setEnabled(true);
            refresh();

            const auto freeAfter = cards.empty() ? px3::BreakpointEnvelope()
                                                 : cards[0]->debugEditor().getEnvelope();
            check("EnvelopeBypass_AFreeFormShapeSurvivesItToo",
                  freeAfter.getPointCount() == freeForm.getPointCount(),
                  "a " + juce::String(freeForm.getPointCount()) + "-point shape comes back with "
                      + juce::String(freeAfter.getPointCount()) + " points");

            check("EnvelopeBypass_TheShapeSurvivesABypassAndBack",
                  std::abs(readBack.attackSeconds - before.attackSeconds) < 1.0e-4f
                      && std::abs(readBack.decaySeconds - before.decaySeconds) < 1.0e-4f
                      && std::abs(readBack.sustainLevel - before.sustainLevel) < 1.0e-4f
                      && std::abs(readBack.releaseSeconds - before.releaseSeconds) < 1.0e-4f
                      && std::abs(after.getPoint(0).curveToNext - 0.6) < 1.0e-6
                      && std::abs(after.getPoint(2).curveToNext + 0.4) < 1.0e-6,
                  "A " + fmt(readBack.attackSeconds, 3) + " D " + fmt(readBack.decaySeconds, 3)
                      + " S " + fmt(readBack.sustainLevel, 2) + " R "
                      + fmt(readBack.releaseSeconds, 3) + ", curves "
                      + fmt(after.getPoint(0).curveToNext, 2) + "/"
                      + fmt(after.getPoint(2).curveToNext, 2) + " (was A "
                      + fmt(before.attackSeconds, 3) + " D " + fmt(before.decaySeconds, 3)
                      + " S " + fmt(before.sustainLevel, 2) + " R "
                      + fmt(before.releaseSeconds, 3) + ", curves 0.60/-0.40)");
        }
    }

    // ---- a stage of no length is still not a discontinuity ------------------
    //
    // Letting the stages reach zero means the user can draw a vertical edge,
    // and a vertical edge in a gain envelope is the shape of a click. It is not
    // one here: the envelope's output smoother rounds the corner, so a zero
    // release falls about four times faster than a 10 ms one rather than
    // stepping. Measured, because "it sounds fine" is not a number.
    {
        constexpr double kRate = 48000.0;
        juce::StringArray steps;
        auto worstOverall = 0.0f;

        for (float release : { 0.0f, 0.001f, 0.010f })
        {
            EnvelopeSettings settings;
            settings.attackSeconds = 0.010f;
            settings.decaySeconds = 0.050f;
            settings.sustainLevel = 0.80f;
            settings.releaseSeconds = release;

            AmpEnvelope env;
            env.prepare(kRate);
            env.setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));
            env.noteOn();
            for (int i = 0; i < 4800; ++i) { env.getNextSample(); }

            auto previous = env.getNextSample();
            env.noteOff();
            auto worst = 0.0f;
            for (int i = 0; i < 480; ++i)
            {
                const auto value = env.getNextSample();
                worst = juce::jmax(worst, std::abs(value - previous));
                previous = value;
            }

            steps.add(fmt(release, 3) + " s -> " + fmt(worst, 4));
            worstOverall = juce::jmax(worstOverall, worst);
        }

        check("EnvelopeOverlap_AZeroLengthReleaseDoesNotStep",
              worstOverall < 0.05f,
              "largest one-sample step at note-off: " + steps.joinIntoString(", ")
                  + " (a hard cut from the 0.80 sustain would be 0.80)");
    }

    // ---- handles may share a spot, and the DSP gets what is drawn -----------
    //
    // A decay that begins the instant the attack ends is a stage of no length.
    // Two things used to deny it: the drawing nudged coincident handles apart,
    // so the picture showed a gap that was not there, and the decay parameter
    // had a 5 ms floor, so the round trip through it handed back a length the
    // graph was not showing.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        if (editor != nullptr)
        {
            ModPanel* panel = nullptr;
            std::vector<EnvelopeComponent*> cards;
            std::function<void(juce::Component&)> find = [&](juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* p = dynamic_cast<ModPanel*>(child)) { panel = p; }
                    if (auto* e = dynamic_cast<EnvelopeComponent*>(child)) { cards.push_back(e); }
                    find(*child);
                }
            };
            find(*editor);

            auto* card = cards.empty() ? nullptr : cards[0];
            if (card != nullptr && panel != nullptr)
            {
                auto& graph = const_cast<BreakpointEnvelopeEditor&>(card->debugEditor());

                const auto event = [&graph](juce::Point<float> at)
                {
                    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                                            juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                            &graph, &graph, juce::Time::getCurrentTime(), at,
                                            juce::Time::getCurrentTime(), 1, false);
                };
                const auto drag = [&](int index, float dx)
                {
                    const auto from = graph.drawnPointPosition(index);
                    graph.mouseDown(event(from));
                    graph.mouseDrag(event(from.translated(dx, 0.0f)));
                    graph.mouseUp(event(from.translated(dx, 0.0f)));
                };
                const auto reset = [&](float sustainLevel)
                {
                    EnvelopeSettings settings;
                    settings.attackSeconds = 0.300f;
                    settings.decaySeconds = 0.400f;
                    settings.sustainLevel = sustainLevel;
                    settings.releaseSeconds = 0.500f;
                    graph.setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));
                };

                // DECAY dragged onto ATTACK. Same time, same pixel, and both
                // handles drawn where their points actually are.
                reset(1.00f);
                drag(2, -4000.0f);
                const auto stacked = graph.getEnvelope();
                check("EnvelopeOverlap_TheDecayMayLandOnTheAttack",
                      std::abs(stacked.getPoint(2).timeSeconds
                               - stacked.getPoint(1).timeSeconds) < 1.0e-9
                          && graph.drawnPointPosition(2).getDistanceFrom(
                                 graph.drawnPointPosition(1)) < 0.01f
                          && graph.drawnPointPosition(1).getDistanceFrom(
                                 graph.pointToScreen(1)) < 0.01f,
                      "attack at " + fmt(stacked.getPoint(1).timeSeconds, 3) + " s and decay at "
                          + fmt(stacked.getPoint(2).timeSeconds, 3) + " s, drawn "
                          + fmt(graph.drawnPointPosition(2).getDistanceFrom(
                                    graph.drawnPointPosition(1)), 2) + " px apart");

                // What the DSP is handed matches: the round trip through the
                // parameters keeps the stage at no length.
                panel->refreshFromParameters();
                const auto played = card->debugEditor().getEnvelope();
                check("EnvelopeOverlap_AZeroLengthStageSurvivesTheRoundTrip",
                      std::abs(played.getPoint(2).timeSeconds
                               - played.getPoint(1).timeSeconds) < 1.0e-6
                          && processor.getEnvelopeDecayParam(0).get() < 1.0e-6f,
                      "after the round trip the decay is "
                          + fmt(played.getPoint(2).timeSeconds - played.getPoint(1).timeSeconds, 4)
                          + " s long and the parameter reads "
                          + fmt(processor.getEnvelopeDecayParam(0).get(), 4) + " s");

                // The buried handle is one drag away, not lost: a grab on the
                // shared spot takes the later point, and moving it uncovers
                // the one underneath.
                const auto shared = graph.drawnPointPosition(2);
                const auto hit = graph.grabAt(shared);
                drag(2, 60.0f);
                const auto separated = graph.getEnvelope();
                check("EnvelopeOverlap_TheHandleUnderneathIsOneDragAway",
                      hit.target == BreakpointEnvelopeEditor::Target::point && hit.index == 2
                          && separated.getPoint(2).timeSeconds
                                 > separated.getPoint(1).timeSeconds + 1.0e-6,
                      "grabbing the shared spot takes point " + juce::String(hit.index)
                          + ", and dragging it right leaves the attack at "
                          + fmt(separated.getPoint(1).timeSeconds, 3) + " s with the decay at "
                          + fmt(separated.getPoint(2).timeSeconds, 3) + " s");

                // RELEASE onto DECAY is the same story at the other end.
                reset(0.00f);
                drag(3, -4000.0f);
                const auto collapsed = graph.getEnvelope();
                check("EnvelopeOverlap_TheReleaseMayLandOnTheDecay",
                      std::abs(collapsed.getPoint(3).timeSeconds
                               - collapsed.getPoint(2).timeSeconds) < 1.0e-9
                          && graph.drawnPointPosition(3).getDistanceFrom(
                                 graph.drawnPointPosition(2)) < 0.01f,
                      "decay at " + fmt(collapsed.getPoint(2).timeSeconds, 3)
                          + " s and release ending at " + fmt(collapsed.getPoint(3).timeSeconds, 3)
                          + " s, drawn " + fmt(graph.drawnPointPosition(3).getDistanceFrom(
                                                   graph.drawnPointPosition(2)), 2) + " px apart");
            }
        }
    }

    // ---- the knobs under the graph ------------------------------------------
    //
    // ATTACK | DECAY | SUSTAIN | RELEASE, bound to the same parameters the
    // graph edits. Two views of one thing, so a drag has to move the knobs and
    // a knob has to move the graph - and neither may straighten a curve the
    // user drew.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        if (editor != nullptr)
        {
            std::vector<EnvelopeComponent*> cards;
            std::function<void(juce::Component&)> find = [&](juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* env = dynamic_cast<EnvelopeComponent*>(child)) { cards.push_back(env); }
                    find(*child);
                }
            };
            find(*editor);

            // Named by what carries them rather than by position in the list.
            const EnvelopeComponent* ampGraph = nullptr;
            std::function<void(juce::Component&)> findAmp = [&](juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* amp = dynamic_cast<AmpEnvelopeComponent*>(child))
                    {
                        ampGraph = amp->debugGraph();
                    }
                    findAmp(*child);
                }
            };
            findAmp(*editor);

            juce::StringArray names;
            if (ampGraph != nullptr)
            {
                for (int k = 0; k < ampGraph->debugAdsrKnobCount(); ++k)
                {
                    names.add(ampGraph->debugAdsrKnobName(k));
                }
            }

            check("EnvelopeKnobs_AmpEnvCarriesTheFour",
                  ampGraph != nullptr && ampGraph->debugAdsrKnobCount() == 4
                      && names == juce::StringArray({ "ATTACK", "DECAY", "SUSTAIN", "RELEASE" }),
                  ampGraph == nullptr ? "no AMP ENV card found"
                                      : "AMP ENV offers: " + names.joinIntoString(", "));

            // And every other envelope card too - AMP ENV and ENV 1-3 carry the
            // same four, because they are the same component asked for them.
            juce::StringArray without;
            for (std::size_t i = 0; i < cards.size(); ++i)
            {
                juce::StringArray theirs;
                for (int k = 0; k < cards[i]->debugAdsrKnobCount(); ++k)
                {
                    theirs.add(cards[i]->debugAdsrKnobName(k));
                }
                if (theirs != names)
                {
                    without.add("card " + juce::String(static_cast<int>(i)) + " offers "
                                + (theirs.isEmpty() ? juce::String("nothing")
                                                    : theirs.joinIntoString("/")));
                }
            }

            check("EnvelopeKnobs_EveryEnvelopeCardCarriesTheFour",
                  ! cards.empty() && without.isEmpty(),
                  cards.empty() ? "no envelope cards found"
                                : (without.isEmpty()
                                       ? juce::String(static_cast<int>(cards.size()))
                                             + " cards, each with ATTACK, DECAY, SUSTAIN, RELEASE"
                                       : without.joinIntoString("; ")));

            // The knob row sits below the graph and inside the card, on every
            // card that has one - which is the whole layout requirement.
            juce::StringArray misplaced;
            for (std::size_t i = 0; i < cards.size(); ++i)
            {
                auto* card = cards[i];
                if (card->debugAdsrKnobCount() != 4) { continue; }

                const auto graph = card->debugEditorBounds();
                for (int k = 0; k < 4; ++k)
                {
                    const auto knob = card->debugAdsrKnob(k).getBounds();
                    if (knob.isEmpty() || knob.getY() < graph.getBottom()
                        || ! card->getLocalBounds().contains(knob))
                    {
                        misplaced.add("card " + juce::String(static_cast<int>(i)) + " knob "
                                      + juce::String(k) + " at " + knob.toString()
                                      + ", graph ends at " + juce::String(graph.getBottom())
                                      + ", card is " + card->getLocalBounds().toString());
                    }
                }
            }

            // Styled like every other knob in the plugin: the editor's shared
            // rotary look-and-feel, not each card's own. A knob drawn by a
            // different one is a knob that looks like a different plugin.
            juce::StringArray unstyled;
            for (std::size_t i = 0; i < cards.size(); ++i)
            {
                auto* card = cards[i];
                for (int k = 0; k < card->debugAdsrKnobCount(); ++k)
                {
                    const auto& knob = card->debugAdsrKnob(k);
                    const auto& label = card->debugAdsrKnobLabel(k);
                    const auto& readout = card->debugAdsrKnobReadout(k);

                    const auto sharedLook = &knob.getLookAndFeel() != &juce::LookAndFeel::getDefaultLookAndFeel();
                    const auto chip = dynamic_cast<const px3::ui::ChipLabel*>(&label) != nullptr;
                    const auto captionColour = label.findColour(juce::Label::textColourId)
                                               == juce::Colour::fromRGB(232, 232, 232);
                    const auto readoutColour = readout.findColour(juce::Label::textColourId)
                                               == juce::Colour::fromRGB(218, 218, 228);

                    if (! sharedLook || ! chip || ! captionColour || ! readoutColour)
                    {
                        unstyled.add("card " + juce::String(static_cast<int>(i)) + " knob "
                                     + juce::String(k)
                                     + (sharedLook ? "" : " has the default look-and-feel")
                                     + (chip ? "" : " has a plain caption")
                                     + (captionColour ? "" : " caption colour")
                                     + (readoutColour ? "" : " readout colour"));
                    }
                }
            }

            check("EnvelopeKnobs_TheyAreStyledLikeEveryOtherKnob",
                  ! cards.empty() && unstyled.isEmpty(),
                  unstyled.isEmpty()
                      ? "shared rotary look-and-feel, chip captions and readouts throughout"
                      : unstyled.joinIntoString("; "));

            check("EnvelopeKnobs_TheRowSitsBelowTheGraphInsideTheCard",
                  misplaced.isEmpty(),
                  misplaced.isEmpty() ? "every knob row is under its graph and inside its card"
                                      : misplaced.joinIntoString("; "));
        }
    }

    // ---- the knobs and the graph are two views of one thing -----------------
    {
        EnvelopeSettings settings;
        settings.attackSeconds = 0.100f;
        settings.decaySeconds = 0.200f;
        settings.sustainLevel = 0.50f;
        settings.releaseSeconds = 0.300f;

        // A knob turn reaches the graph without straightening a bend. This is
        // the whole reason withAdsrApplied exists rather than fromAdsr.
        auto bent = px3::BreakpointEnvelope::fromAdsr(settings);
        bent.setCurve(0, 0.7);
        bent.setCurve(2, -0.5);

        EnvelopeSettings turned = settings;
        turned.decaySeconds = 0.450f;
        turned.sustainLevel = 0.20f;
        const auto applied = bent.withAdsrApplied(turned);
        const auto readBack = applied.toAdsr();

        check("EnvelopeKnobs_AKnobMovesTheGraphAndLeavesTheCurvesAlone",
              std::abs(readBack.decaySeconds - 0.450f) < 1.0e-4f
                  && std::abs(readBack.sustainLevel - 0.20f) < 1.0e-4f
                  && std::abs(applied.getPoint(0).curveToNext - 0.7) < 1.0e-9
                  && std::abs(applied.getPoint(2).curveToNext + 0.5) < 1.0e-9,
              "decay " + fmt(readBack.decaySeconds, 3) + " s, sustain "
                  + fmt(readBack.sustainLevel, 2) + ", curves "
                  + fmt(applied.getPoint(0).curveToNext, 2) + " and "
                  + fmt(applied.getPoint(2).curveToNext, 2));

        // Once a point has been ADDED there is no ADSR left to apply, so the
        // knobs stop writing rather than flattening the shape into four points.
        auto freeForm = px3::BreakpointEnvelope::fromAdsr(settings);
        freeForm.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        freeForm.addPoint(0.150, 0.8);
        const auto untouched = freeForm.withAdsrApplied(turned);
        check("EnvelopeKnobs_TheyStopWritingOnceTheShapeIsFreeForm",
              untouched.getPointCount() == freeForm.getPointCount()
                  && std::abs(untouched.getPoint(2).timeSeconds
                              - freeForm.getPoint(2).timeSeconds) < 1.0e-9,
              "a five-point shape comes back with "
                  + juce::String(untouched.getPointCount()) + " points, unchanged");

        // And the other direction: a shape that has been BENT still reports the
        // four numbers, so a drag keeps the knobs in step. isPlainAdsr, which
        // the write used to be gated on, says no the moment a curve is bent.
        check("EnvelopeKnobs_ABentShapeStillWritesTheParameters",
              bent.isAdsrSkeleton() && ! bent.isPlainAdsr()
                  && std::abs(bent.toAdsr().decaySeconds - 0.200f) < 1.0e-4f,
              "a bent skeleton reports a decay of " + fmt(bent.toAdsr().decaySeconds, 3)
                  + " s while isPlainAdsr says "
                  + juce::String(bent.isPlainAdsr() ? "yes" : "no"));
    }

    // ---- DECAY and SUSTAIN are one handle -----------------------------------
    //
    // They are the two coordinates of point 2, so the handle takes both axes:
    // sideways is the decay TIME, upwards the sustain LEVEL. This replaced a
    // separate sustain bar on a drawn held stretch, which needed a fifth
    // control standing for a duration the model does not have.
    {
        EnvelopeSettings settings;
        settings.attackSeconds = 0.05f;
        settings.decaySeconds = 0.10f;
        settings.sustainLevel = 0.60f;
        settings.releaseSeconds = 0.13f;

        BreakpointEnvelopeEditor editor;
        editor.setSize(400, 200);
        editor.setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));

        const auto event = [&editor](juce::Point<float> at)
        {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                                    juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                    &editor, &editor, juce::Time::getCurrentTime(), at,
                                    juce::Time::getCurrentTime(), 1, false);
        };

        const auto drag = [&](juce::Point<float> from, float dx, float dy)
        {
            editor.mouseDown(event(from));
            editor.mouseDrag(event(from.translated(dx, dy)));
            editor.mouseUp(event(from.translated(dx, dy)));
            return editor.getEnvelope().toAdsr();
        };

        const auto sustainIndex = editor.getEnvelope().getSustainPoint();
        const auto before = editor.getEnvelope().toAdsr();

        check("EnvelopeEditor_TheDecaySustainHandleIsOnTheSustainPoint",
              editor.grabAt(editor.drawnPointPosition(sustainIndex)).index == sustainIndex
                  && editor.roleLabelFor(sustainIndex) == "DECAY / SUSTAIN",
              "the handle at the sustain point grabs point "
                  + juce::String(editor.grabAt(editor.drawnPointPosition(sustainIndex)).index)
                  + " and names itself " + editor.roleLabelFor(sustainIndex));

        // Sideways only: the decay time moves and the level does not.
        const auto sideways = drag(editor.drawnPointPosition(sustainIndex), 30.0f, 0.0f);
        check("EnvelopeEditor_DraggingItSidewaysChangesTheDecayTime",
              sideways.decaySeconds > before.decaySeconds + 1.0e-4f
                  && std::abs(sideways.sustainLevel - before.sustainLevel) < 1.0e-4f,
              "decay " + fmt(before.decaySeconds, 3) + " -> " + fmt(sideways.decaySeconds, 3)
                  + " s with sustain still " + fmt(sideways.sustainLevel, 3));

        // Upwards only: the level moves and the decay time does not.
        const auto upwards = drag(editor.drawnPointPosition(sustainIndex), 0.0f, -25.0f);
        check("EnvelopeEditor_DraggingItUpChangesTheSustainLevel",
              upwards.sustainLevel > sideways.sustainLevel + 1.0e-4f
                  && std::abs(upwards.decaySeconds - sideways.decaySeconds) < 1.0e-4f,
              "sustain " + fmt(sideways.sustainLevel, 3) + " -> " + fmt(upwards.sustainLevel, 3)
                  + " with decay still " + fmt(upwards.decaySeconds, 3) + " s");

        // And both at once, which is the point of merging them.
        const auto both = drag(editor.drawnPointPosition(sustainIndex), 20.0f, 18.0f);
        check("EnvelopeEditor_DraggingItDiagonallyChangesBoth",
              both.decaySeconds > upwards.decaySeconds + 1.0e-4f
                  && both.sustainLevel < upwards.sustainLevel - 1.0e-4f,
              "decay " + fmt(upwards.decaySeconds, 3) + " -> " + fmt(both.decaySeconds, 3)
                  + " s and sustain " + fmt(upwards.sustainLevel, 3) + " -> "
                  + fmt(both.sustainLevel, 3));

        // Dragged hard left it stops at the attack rather than crossing it.
        drag(editor.drawnPointPosition(sustainIndex), -4000.0f, 0.0f);
        check("EnvelopeEditor_TheDecayNeverPassesTheAttack",
              editor.getEnvelope().getPoint(2).timeSeconds
                  >= editor.getEnvelope().getPoint(1).timeSeconds - 1.0e-9,
              "the decay point stops at the attack rather than crossing it");

        // Nothing is drawn between the decay and the release any more: the
        // curve runs from the sustain point straight into it.
        editor.setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));
        const auto shape = editor.getEnvelope();
        check("EnvelopeEditor_NothingIsDrawnBetweenTheDecayAndTheRelease",
              std::abs(editor.pointToScreen(sustainIndex).x
                       - editor.debugToScreen(shape.getPoint(sustainIndex).timeSeconds,
                                              shape.getPoint(sustainIndex).value).x) < 0.01f
                  && std::abs(editor.pointToScreen(3).x
                              - editor.debugToScreen(shape.getPoint(3).timeSeconds, 0.0).x) < 0.01f,
              "every point is drawn at its own time, with no stretch inserted");
    }

    // ---- every stage is its OWN control -------------------------------------
    //
    // This is the regression suite for the thing that went round in circles.
    // The rule it encodes, in one sentence: both envelopes offer the same three
    // handles, ATTACK and RELEASE change one thing each, and DECAY / SUSTAIN
    // changes the two coordinates of the one point it sits on.
    {
        const auto adsr = []
        {
            EnvelopeSettings settings;
            settings.attackSeconds = 0.10f;
            settings.decaySeconds = 0.20f;
            settings.sustainLevel = 0.50f;
            settings.releaseSeconds = 0.30f;
            return settings;
        }();

        struct Rig
        {
            const char* name;
            const char* perStage;
            const char* spacing;
            const char* grabs;
            juce::StringArray expected;
        };
        const Rig rigs[] = {
            { "AMP ENV",
              "AmpEnv_HasOneHandlePerStage",
              "AmpEnv_NoTwoHandlesShareASpot",
              "AmpEnv_EveryHandleGrabsItself",
              { "ATTACK", "DECAY / SUSTAIN", "RELEASE" } },
            { "ENV 1-3",
              "ModEnv_HasOneHandlePerStage",
              "ModEnv_NoTwoHandlesShareASpot",
              "ModEnv_EveryHandleGrabsItself",
              { "ATTACK", "DECAY / SUSTAIN", "RELEASE" } },
        };

        for (const auto& rig : rigs)
        {
            BreakpointEnvelopeEditor editor;
            editor.setSize(400, 200);
            editor.setEnvelope(px3::BreakpointEnvelope::fromAdsr(adsr));

            const auto name = juce::String(rig.name);

            // 1. Every named stage has a handle, and they are all different
            //    places on the screen.
            juce::StringArray found;
            std::vector<juce::Point<float>> places;
            for (int i = 1; i < editor.getEnvelope().getPointCount(); ++i)
            {
                const auto role = editor.roleLabelFor(i);
                if (role.isNotEmpty())
                {
                    found.add(role);
                    places.push_back(editor.drawnPointPosition(i));
                }
            }

            check(rig.perStage,
                  found == rig.expected,
                  name + " offers: " + found.joinIntoString(", ")
                      + "  (expected " + rig.expected.joinIntoString(", ") + ")");

            auto closest = 1.0e9f;
            for (std::size_t a = 0; a < places.size(); ++a)
            {
                for (auto b = a + 1; b < places.size(); ++b)
                {
                    closest = juce::jmin(closest, places[a].getDistanceFrom(places[b]));
                }
            }
            check(rig.spacing,
                  closest >= 6.0f,
                  name + "'s closest two handles are " + fmt(closest, 1) + " px apart");

            // 2. Each handle is reachable, and grabs ITSELF.
            juce::StringArray misgrabs;
            for (int i = 1; i < editor.getEnvelope().getPointCount(); ++i)
            {
                if (editor.roleLabelFor(i).isEmpty()) { continue; }
                const auto hit = editor.grabAt(editor.drawnPointPosition(i));
                if (hit.target != BreakpointEnvelopeEditor::Target::point || hit.index != i)
                {
                    misgrabs.add(editor.roleLabelFor(i));
                }
            }
            check(rig.grabs,
                  misgrabs.isEmpty(),
                  misgrabs.isEmpty() ? "every handle selects the stage it names"
                                     : "these grabbed something else: "
                                           + misgrabs.joinIntoString(", "));
        }
    }

    // ---- the two shapes, as the processor actually hands them out -----------
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        const auto amp = processor.currentAmpEnvelope();
        const auto mod = processor.currentModEnvelope(0);

        check("AmpEnv_HasNoHoldStage",
              amp.getPointCount() == 4 && amp.getSustainPoint() == 2,
              "AMP ENV is " + juce::String(amp.getPointCount())
                  + " points holding at index " + juce::String(amp.getSustainPoint())
                  + " - attack, decay, sustain, release");

        check("ModEnv_HasNoHoldStageEither",
              mod.getPointCount() == 4 && mod.getSustainPoint() == 2,
              "ENV 1 is " + juce::String(mod.getPointCount())
                  + " points holding at index " + juce::String(mod.getSustainPoint())
                  + " - attack, decay, sustain, release, the same as AMP ENV");

        // The label on the sustain point claims it sets two things. It has to.
        {
            EnvelopeSettings settings;
            settings.attackSeconds = 0.1f;
            settings.decaySeconds = 0.2f;
            settings.sustainLevel = 0.5f;
            settings.releaseSeconds = 0.3f;

            auto shape = px3::BreakpointEnvelope::fromAdsr(settings);
            const auto before = shape.toAdsr();

            // Sideways: decay only.
            shape.setPoint(2, shape.getPoint(2).timeSeconds + 0.1, shape.getPoint(2).value);
            const auto moved = shape.toAdsr();

            // Upwards: sustain only.
            shape.setPoint(2, shape.getPoint(2).timeSeconds, 0.8);
            const auto raised = shape.toAdsr();

            check("Envelope_TheSustainPointCarriesBothDecayTimeAndSustainLevel",
                  std::abs(moved.decaySeconds - (before.decaySeconds + 0.1f)) < 1.0e-3f
                      && std::abs(moved.sustainLevel - before.sustainLevel) < 1.0e-3f
                      && std::abs(raised.sustainLevel - 0.8f) < 1.0e-3f
                      && std::abs(raised.decaySeconds - moved.decaySeconds) < 1.0e-3f,
                  "sideways moved decay " + fmt(before.decaySeconds, 3) + " -> "
                      + fmt(moved.decaySeconds, 3) + " s at the same level; upwards moved "
                      "sustain " + fmt(moved.sustainLevel, 2) + " -> "
                      + fmt(raised.sustainLevel, 2) + " at the same time");
        }
    }

    // ---- the editor ---------------------------------------------------------
    {
        BreakpointEnvelopeEditor editor;
        editor.setSize(400, 200);

        EnvelopeSettings settings;
        settings.attackSeconds = 0.1f;
        settings.decaySeconds = 0.2f;
        settings.sustainLevel = 0.5f;
        settings.releaseSeconds = 0.3f;
        editor.setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));

        int changeCount = 0;
        editor.onEnvelopeChanged = [&changeCount](const px3::BreakpointEnvelope&) { ++changeCount; };

        // A mouse event the component will accept. Built once and moved,
        // because constructing one per interaction is most of the noise in a
        // test like this.
        const auto makeEvent = [&editor](juce::Point<float> position, int clicks,
                                         juce::ModifierKeys mods = juce::ModifierKeys())
        {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                    position, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                    &editor, &editor, juce::Time::getCurrentTime(),
                                    position, juce::Time::getCurrentTime(), clicks, false);
        };

        // ---- an INIT patch's hold point can actually be grabbed ----
        // HOLD defaults to zero, which is right, but it puts the hold point at
        // the same time and value as the attack point. One handle covering
        // another is one handle you cannot reach: before this, grabbing there
        // always returned the attack point and the hold was unreachable from
        // the moment the plugin loaded.
        {
            EnvelopeSettings init;
            init.attackSeconds = 0.1f;
            init.decaySeconds = 0.2f;
            init.sustainLevel = 0.5f;
            init.releaseSeconds = 0.3f;

            BreakpointEnvelopeEditor fresh;
            fresh.setSize(400, 200);
            fresh.setEnvelope(px3::BreakpointEnvelope::fromAdsr(init));

            const auto& shape = fresh.getEnvelope();
            const auto attackTime = shape.getPoint(1).timeSeconds;
            const auto holdTime = shape.getPoint(2).timeSeconds;

            const auto attackAt = fresh.drawnPointPosition(1);
            const auto holdAt = fresh.drawnPointPosition(2);
            const auto apart = holdAt.getDistanceFrom(attackAt);

            const auto grabbedHold = fresh.grabAt(holdAt);
            const auto grabbedAttack = fresh.grabAt(attackAt);

            check("EnvelopeEditor_HoldAndAttackDoNotShareOnePixelAtInit",
                  apart >= 6.0f,
                  "hold starts at the same time as attack (" + fmt(holdTime - attackTime, 3)
                      + " s apart) but is drawn " + fmt(apart, 1) + " px away");

            check("EnvelopeEditor_TheHoldPointCanBeGrabbedAtInit",
                  grabbedHold.target == BreakpointEnvelopeEditor::Target::point
                      && grabbedHold.index == 2,
                  grabbedHold.index == 2 ? "grabbing the hold handle selects the hold point"
                                         : "grabbing the hold handle selected point "
                                               + juce::String(grabbedHold.index));

            check("EnvelopeEditor_TheAttackPointIsStillGrabbable",
                  grabbedAttack.target == BreakpointEnvelopeEditor::Target::point
                      && grabbedAttack.index == 1,
                  "separating them did not cost the attack handle - point "
                      + juce::String(grabbedAttack.index));
        }

        // ---- every handle says what it changes ----
        {
            EnvelopeSettings plain;
            plain.attackSeconds = 0.1f;
            plain.decaySeconds = 0.2f;
            plain.sustainLevel = 0.5f;
            plain.releaseSeconds = 0.3f;

            BreakpointEnvelopeEditor labelled;
            labelled.setSize(400, 200);
            labelled.setEnvelope(px3::BreakpointEnvelope::fromAdsr(plain));

            juce::StringArray roles;
            for (int i = 1; i <= 3; ++i) { roles.add(labelled.roleLabelFor(i)); }

            check("EnvelopeEditor_EveryModEnvelopeHandleIsLabelled",
                  roles == juce::StringArray({ "ATTACK", "DECAY / SUSTAIN", "RELEASE" }),
                  "handles 1-3 of a mod envelope read: " + roles.joinIntoString(", "));

            // Both envelopes carry the same three handles: there is one
            // skeleton, so a label that appears on one and not the other would
            // mean the two had drifted apart.
            BreakpointEnvelopeEditor amp;
            amp.setSize(400, 200);
            amp.setEnvelope(px3::BreakpointEnvelope::fromAdsr(plain));

            juce::StringArray ampRoles;
            for (int i = 1; i <= 3; ++i) { ampRoles.add(amp.roleLabelFor(i)); }

            check("EnvelopeEditor_TheAmpEnvelopeHasTheSameThreeHandles",
                  amp.getEnvelope().getPointCount() == 4
                      && amp.getEnvelope().getSustainPoint() == 2
                      && ampRoles == roles,
                  juce::String(amp.getEnvelope().getPointCount()) + " points reading: "
                      + ampRoles.joinIntoString(", "));

            // And with no hold there is nothing to sit on top of the attack.
            check("EnvelopeEditor_NothingOverlapsTheAmpAttackHandle",
                  amp.drawnPointPosition(1) == amp.pointToScreen(1),
                  "the amp attack handle is drawn where it actually is, un-nudged");

            check("EnvelopeEditor_TheAnchorIsNotLabelled",
                  labelled.roleLabelFor(0).isEmpty(),
                  "the point at time zero is not a stage and carries no name");

            // Added points change what the handles mean, so the names stop
            // being true and are dropped rather than left saying the wrong
            // thing.
            auto edited = labelled.getEnvelope();
            // Adding points is a Breakpoint-mode capability.
            edited.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
            edited.addPoint(0.15, 0.8);
            labelled.setEnvelope(edited);
            check("EnvelopeEditor_LabelsStopOnceTheShapeIsNoLongerAdsr",
                  labelled.roleLabelFor(1).isEmpty() && labelled.roleLabelFor(2).isEmpty(),
                  "a " + juce::String(labelled.getEnvelope().getPointCount())
                      + "-point envelope has no ADSR roles to name");
        }

        // ---- the curve fills the width ----
        {
            const auto lastPoint = editor.pointToScreen(editor.getEnvelope().getPointCount() - 1);
            const auto firstPoint = editor.pointToScreen(0);
            const auto span = lastPoint.x - firstPoint.x;
            const auto available = 400.0f - 2.0f * 8.0f;   // width less the inset

            check("EnvelopeEditor_TheCurveFillsTheWidth",
                  span > available * 0.98f,
                  "the envelope spans " + fmt(span, 1) + " px of " + fmt(available, 1)
                      + " available");
        }

        // ---- hit testing ----
        {
            const auto onPoint = editor.pointToScreen(1);
            const auto hit = editor.grabAt(onPoint);
            check("EnvelopeEditor_FindsABreakpointUnderTheMouse",
                  hit.target == BreakpointEnvelopeEditor::Target::point && hit.index == 1,
                  "point 1 at " + onPoint.toString() + " was hit");

            // A few pixels off still counts: the grab radius is deliberately
            // larger than the dot, because a user aims at what they can see.
            const auto nearMiss = editor.grabAt(onPoint.translated(7.0f, 0.0f));
            check("EnvelopeEditor_GrabRadiusIsLargerThanTheDot",
                  nearMiss.target == BreakpointEnvelopeEditor::Target::point,
                  "7 px off a breakpoint still grabs it");

            const auto empty = editor.grabAt({ 5.0f, 5.0f });
            check("EnvelopeEditor_EmptySpaceHitsNothing",
                  empty.target == BreakpointEnvelopeEditor::Target::none,
                  "the top-left corner grabs nothing");
        }

        // ---- adding and removing ----
        {
            // Somewhere provably empty. A fixed coordinate is fragile here: the
            // curve handles sit ON the curve, and double-clicking one of those
            // straightens its segment rather than adding a point.
            const auto emptySpot = [&editor]
            {
                for (int y = 20; y < 180; y += 3)
                {
                    for (int x = 30; x < 370; x += 3)
                    {
                        const juce::Point<float> probe { static_cast<float>(x),
                                                         static_cast<float>(y) };
                        if (editor.grabAt(probe).target == BreakpointEnvelopeEditor::Target::none)
                        {
                            return probe;
                        }
                    }
                }
                return juce::Point<float>(200.0f, 100.0f);
            }();

            // Adding points by double-click is a Breakpoint-mode capability.
            {
                auto breakpointShape = editor.getEnvelope();
                breakpointShape.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
                editor.setEnvelope(breakpointShape);
            }

            const auto before = editor.getEnvelope().getPointCount();
            editor.mouseDoubleClick(makeEvent(emptySpot, 2));
            const auto afterAdd = editor.getEnvelope().getPointCount();
            check("EnvelopeEditor_DoubleClickingEmptySpaceAddsAPoint", afterAdd == before + 1,
                  juce::String(before) + " points, then " + juce::String(afterAdd));

            // And removing it again by double-clicking the point itself.
            auto addedIndex = -1;
            for (int i = 0; i < editor.getEnvelope().getPointCount(); ++i)
            {
                if (editor.getEnvelope().canRemovePoint(i)) { addedIndex = i; break; }
            }
            if (addedIndex >= 0)
            {
                editor.mouseDoubleClick(makeEvent(editor.pointToScreen(addedIndex), 2));
            }
            check("EnvelopeEditor_DoubleClickingAPointRemovesIt",
                  editor.getEnvelope().getPointCount() == before,
                  "back to " + juce::String(editor.getEnvelope().getPointCount()) + " points");

            // The structural points refuse, and refuse silently rather than
            // leaving an envelope that cannot be evaluated.
            const auto held = editor.getEnvelope().getPointCount();
            editor.mouseDoubleClick(makeEvent(editor.pointToScreen(0), 2));
            editor.mouseDoubleClick(makeEvent(
                editor.pointToScreen(editor.getEnvelope().getPointCount() - 1), 2));
            check("EnvelopeEditor_StructuralPointsSurviveADoubleClick",
                  editor.getEnvelope().getPointCount() == held,
                  "the anchor and the end are still there");
        }

        // ---- dragging a breakpoint ----
        {
            // Reset first, and grab the SUSTAIN point rather than the peak: the
            // peak is already at 1.0, so dragging it up measures the clamp. The
            // earlier version of this depended on whatever the add/remove tests
            // above happened to leave behind.
            editor.setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));

            const auto start = editor.pointToScreen(3);
            const auto before = editor.getEnvelope().getPoint(3);

            editor.mouseDown(makeEvent(start, 1));
            editor.mouseDrag(makeEvent(start.translated(20.0f, -20.0f), 1));
            editor.mouseUp(makeEvent(start.translated(20.0f, -20.0f), 1));

            const auto after = editor.getEnvelope().getPoint(3);
            // On an ADSR skeleton a breakpoint is a TIME, and only a time: the
            // one level in the shape belongs to the sustain handle. Dragging
            // diagonally moves it along the axis it owns and leaves the other
            // alone, which is what "separate controls" has to mean when the
            // mouse moves in two dimensions at once.
            check("EnvelopeEditor_AnAdsrBreakpointMovesInTimeOnly",
                  after.timeSeconds > before.timeSeconds
                      && std::abs(after.value - before.value) < 1.0e-9,
                  "dragged up and to the right: " + fmt(before.timeSeconds, 4) + "s/"
                      + fmt(before.value, 3) + " -> " + fmt(after.timeSeconds, 4) + "s/"
                      + fmt(after.value, 3));

            // A free-form envelope has no stages to name, so nothing is locked.
            {
                auto freeform = px3::BreakpointEnvelope::fromAdsr(settings);
                // Adding points is a Breakpoint-mode capability.
                freeform.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
                freeform.addPoint(0.15, 0.7);

                BreakpointEnvelopeEditor loose;
                loose.setSize(400, 200);
                loose.setEnvelope(freeform);

                // The point that was ADDED, found by position rather than by a
                // guessed index: inserting it renumbers everything after it,
                // and index 2 is the hold point, which sits on top of the
                // attack point when the hold is zero.
                auto addedIndex = 1;
                auto closestToAdded = 1.0e9;
                for (int i = 1; i + 1 < loose.getEnvelope().getPointCount(); ++i)
                {
                    const auto gap = std::abs(loose.getEnvelope().getPoint(i).timeSeconds - 0.15);
                    if (gap < closestToAdded) { closestToAdded = gap; addedIndex = i; }
                }

                const auto grabPoint = loose.drawnPointPosition(addedIndex);
                const auto was = loose.getEnvelope().getPoint(addedIndex);

                const auto ev = [&loose](juce::Point<float> at)
                {
                    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                            at, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f,
                                            0.0f, &loose, &loose, juce::Time::getCurrentTime(),
                                            at, juce::Time::getCurrentTime(), 1, false);
                };
                loose.mouseDown(ev(grabPoint));
                loose.mouseDrag(ev(grabPoint.translated(15.0f, -25.0f)));
                loose.mouseUp(ev(grabPoint.translated(15.0f, -25.0f)));

                const auto now = loose.getEnvelope().getPoint(addedIndex);
                check("EnvelopeEditor_AFreeFormPointStillMovesInBothAxes",
                      now.timeSeconds > was.timeSeconds && now.value > was.value,
                      "once a point is added the roles are gone and so is the lock: "
                          + fmt(was.timeSeconds, 3) + "s/" + fmt(was.value, 2) + " -> "
                          + fmt(now.timeSeconds, 3) + "s/" + fmt(now.value, 2));
            }

            check("EnvelopeEditor_DraggingReportsTheChange", changeCount > 0,
                  juce::String(changeCount) + " change callbacks fired");
        }

        // ---- bending a segment ----
        {
            // A rising segment and a falling one, because the sign of the bend
            // has to follow the mouse in both - a falling segment that bends
            // the wrong way under the cursor feels broken long before it looks
            // it.
            juce::StringArray wrongWay;
            for (const auto segment : { 0, 2 })
            {
                editor.setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));
                const auto handle = editor.grabAt(
                    editor.pointToScreen(segment).translated(0.0f, 0.0f));
                juce::ignoreUnused(handle);

                // Grab the curve handle for this segment.
                auto handlePosition = juce::Point<float>();
                for (int x = 0; x < 400; ++x)
                {
                    for (int y = 0; y < 200; y += 2)
                    {
                        const auto probe = editor.grabAt({ static_cast<float>(x),
                                                            static_cast<float>(y) });
                        if (probe.target == BreakpointEnvelopeEditor::Target::curve
                            && probe.index == segment)
                        {
                            handlePosition = { static_cast<float>(x), static_cast<float>(y) };
                            x = 400;
                            break;
                        }
                    }
                }

                editor.mouseDown(makeEvent(handlePosition, 1));
                editor.mouseDrag(makeEvent(handlePosition.translated(0.0f, -30.0f), 1));
                editor.mouseUp(makeEvent(handlePosition.translated(0.0f, -30.0f), 1));

                // Dragging UP must raise the curve, whichever way the segment runs.
                const auto& a = editor.getEnvelope().getPoint(segment);
                const auto& b = editor.getEnvelope().getPoint(segment + 1);
                const auto midpoint = a.value + (b.value - a.value)
                                                     * px3::BreakpointEnvelope::shape(0.5, a.curveToNext);
                const auto straight = 0.5 * (a.value + b.value);
                if (midpoint <= straight + 1.0e-6)
                {
                    wrongWay.add("segment " + juce::String(segment));
                }
            }

            check("EnvelopeEditor_DraggingUpBendsUpOnRisingAndFallingSegments",
                  wrongWay.isEmpty(),
                  wrongWay.isEmpty() ? "both a rising and a falling segment bend toward the mouse"
                                     : "bent away from the mouse on " + wrongWay.joinIntoString(", "));
        }

        // ---- keyboard ----
        {
            editor.setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));
            // Point 2, the sustain - the only point in the skeleton with a
            // level to nudge. Point 1 is the peak, already at 1.0 with nowhere
            // to go, and points 0 and 3 are anchored at silence, so testing
            // any of those would measure a clamp rather than the nudge.
            editor.mouseDown(makeEvent(editor.pointToScreen(2), 1));
            editor.mouseUp(makeEvent(editor.pointToScreen(2), 1));

            const auto before = editor.getEnvelope().getPoint(2).value;
            editor.keyPressed(juce::KeyPress(juce::KeyPress::upKey));
            const auto afterUp = editor.getEnvelope().getPoint(2).value;

            check("EnvelopeEditor_ArrowKeysNudgeTheSelectedPoint", afterUp > before,
                  fmt(before, 4) + " -> " + fmt(afterUp, 4));

            const auto pointsBefore = editor.getEnvelope().getPointCount();

            // Somewhere provably empty, rather than a hard-coded pixel. The
            // held stretch moved every handle along the axis, and a coordinate
            // chosen when the geometry was different landed ON one - so the
            // double-click removed a point instead of adding one.
            auto emptySpot = juce::Point<float>(150.0f, 90.0f);
            for (float x = 60.0f; x < 340.0f; x += 7.0f)
            {
                const auto candidate = juce::Point<float>(x, 90.0f);
                if (editor.grabAt(candidate).target == BreakpointEnvelopeEditor::Target::none)
                {
                    emptySpot = candidate;
                    break;
                }
            }

            editor.mouseDoubleClick(makeEvent(emptySpot, 2));

            // Adding a point ends the ADSR skeleton, which removes the held
            // stretch and moves every handle along the axis. Clicking the same
            // pixel again would no longer be clicking the new point, so it is
            // located afresh.
            auto addedAt = emptySpot;
            auto nearest = 1.0e9f;
            for (int i = 0; i < editor.getEnvelope().getPointCount(); ++i)
            {
                const auto where = editor.drawnPointPosition(i);
                const auto gap = where.getDistanceFrom(emptySpot);
                if (gap < nearest) { nearest = gap; addedAt = where; }
            }

            editor.mouseDown(makeEvent(addedAt, 1));
            editor.mouseUp(makeEvent(addedAt, 1));
            editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));

            check("EnvelopeEditor_DeleteRemovesTheSelectedPoint",
                  editor.getEnvelope().getPointCount() == pointsBefore,
                  "added then deleted, back to " + juce::String(pointsBefore));
        }

        // ---- one editor cannot reach another ----
        {
            BreakpointEnvelopeEditor first, second;
            first.setSize(400, 200);
            second.setSize(400, 200);

            // Both in Breakpoint mode, since the point of the test is that
            // adding a point to one leaves the other alone.
            auto breakpointShape = px3::BreakpointEnvelope::fromAdsr(settings);
            breakpointShape.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
            first.setEnvelope(breakpointShape);
            second.setEnvelope(breakpointShape);

            juce::Point<float> spot { 200.0f, 40.0f };
            for (int y = 20; y < 180 && first.grabAt(spot).target
                                            != BreakpointEnvelopeEditor::Target::none; y += 3)
            {
                spot = { 200.0f, static_cast<float>(y) };
                if (first.grabAt(spot).target == BreakpointEnvelopeEditor::Target::none) { break; }
            }

            first.mouseDoubleClick(juce::MouseEvent(
                juce::Desktop::getInstance().getMainMouseSource(), spot,
                juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &first, &first,
                juce::Time::getCurrentTime(), spot,
                juce::Time::getCurrentTime(), 2, false));

            check("EnvelopeEditor_EditingOneDoesNotTouchAnother",
                  first.getEnvelope().getPointCount() == 5
                      && second.getEnvelope().getPointCount() == 4,
                  "first has " + juce::String(first.getEnvelope().getPointCount())
                      + " points, second still has "
                      + juce::String(second.getEnvelope().getPointCount()));
        }
    }

    // ---- every handle sits on the curve it controls -------------------------
    //
    // A short ATTACK put the anchor within a handle's width of the attack
    // point, and the anchor - which is not a control and cannot be dragged -
    // nudged the attack handle a whole spacing to the RIGHT of the corner it
    // marks. The line turned in one place and the handle sat in another.
    //
    // Checked at rest and again after each handle has been dragged, because
    // "in the right spot" has to hold while the mouse is down as well.
    {
        const float attacks[] = { 0.001f, 0.005f, 0.020f, 0.200f, 2.000f, 4.000f };
        juce::StringArray offences;

        for (const auto attack : attacks)
        {
            EnvelopeSettings settings;
            settings.attackSeconds = attack;
            settings.decaySeconds = 0.300f;
            settings.sustainLevel = 0.60f;
            settings.releaseSeconds = 0.400f;

            BreakpointEnvelopeEditor editor;
            editor.setSize(600, 240);
            editor.setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));

            const auto makeEvent = [&editor](juce::Point<float> at, int clicks)
            {
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                                        juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                        &editor, &editor, juce::Time::getCurrentTime(), at,
                                        juce::Time::getCurrentTime(), clicks, false);
            };

            const auto handlesOnTheCurve = [&](const juce::String& when)
            {
                for (int i = 0; i < editor.getEnvelope().getPointCount(); ++i)
                {
                    const auto drawn = editor.drawnPointPosition(i);
                    const auto real = editor.pointToScreen(i);
                    if (drawn.getDistanceFrom(real) > 0.01f)
                    {
                        offences.add("attack " + fmt(attack, 3) + " s " + when + ": handle "
                                     + juce::String(i) + " drawn at " + fmt(drawn.x, 1) + ","
                                     + fmt(drawn.y, 1) + " but its point is at " + fmt(real.x, 1)
                                     + "," + fmt(real.y, 1));
                    }
                }
            };

            handlesOnTheCurve("at rest");

            // Drag each handle in turn and look again - a handle that only
            // agrees with its point when nothing is happening is no use.
            for (int i = 1; i < editor.getEnvelope().getPointCount(); ++i)
            {
                const auto from = editor.drawnPointPosition(i);
                const auto to = from.translated(-14.0f, -9.0f);
                editor.mouseDown(makeEvent(from, 1));
                editor.mouseDrag(makeEvent(to, 1));
                handlesOnTheCurve("dragging handle " + juce::String(i));
                editor.mouseUp(makeEvent(to, 1));
                handlesOnTheCurve("after dragging handle " + juce::String(i));
            }

        }

        check("EnvelopeEditor_EveryHandleSitsOnTheCurveItControls",
              offences.isEmpty(),
              offences.isEmpty()
                  ? "six attack times, every handle on its point at rest and through a drag"
                  : offences.joinIntoString("; "));
    }

    // The anchor is not a control: it cannot be grabbed, so it cannot take the
    // ATTACK handle away from the user on a short attack, and double-clicking
    // it neither removes it nor drops a new point on top of it.
    {
        EnvelopeSettings settings;
        settings.attackSeconds = 0.001f;
        settings.decaySeconds = 0.300f;
        settings.sustainLevel = 0.60f;
        settings.releaseSeconds = 0.400f;

        BreakpointEnvelopeEditor editor;
        editor.setSize(600, 240);
        editor.setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));

        // The anchor hands back nothing: it is pinned at time zero and value
        // zero, so a hit on it would be a handle that goes nowhere. The ATTACK
        // corner directly above it is still the attack.
        const auto onAnchor = editor.grabAt(editor.pointToScreen(0));
        const auto hit = editor.grabAt(editor.pointToScreen(1));

        const auto before = editor.getEnvelope().getPointCount();
        editor.mouseDoubleClick(juce::MouseEvent(
            juce::Desktop::getInstance().getMainMouseSource(), editor.pointToScreen(0),
            juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &editor, &editor,
            juce::Time::getCurrentTime(), editor.pointToScreen(0),
            juce::Time::getCurrentTime(), 2, false));

        check("EnvelopeEditor_TheAnchorIsNotAControl",
              onAnchor.target == BreakpointEnvelopeEditor::Target::none
                  && hit.target == BreakpointEnvelopeEditor::Target::point && hit.index == 1
                  && editor.getEnvelope().getPointCount() == before,
              "the anchor grabs nothing, a 1 ms attack grabs point "
                  + juce::String(hit.index) + ", and double-clicking the anchor leaves "
                  + juce::String(editor.getEnvelope().getPointCount()) + " points");
    }

    // ---- ATTACK moves in time along the top ---------------------------------
    //
    // The mirror of RELEASE, which moves in time along the bottom. The peak is
    // where the attack has FINISHED, so the handle is a duration and nothing
    // else; dragging it down would make one handle two controls and leave a
    // shape the four parameters can no longer describe.
    {
        EnvelopeSettings settings;
        settings.attackSeconds = 0.400f;
        settings.decaySeconds = 0.300f;
        settings.sustainLevel = 0.60f;
        settings.releaseSeconds = 0.500f;

        BreakpointEnvelopeEditor editor;
        editor.setSize(600, 240);
        editor.setEnvelope(px3::BreakpointEnvelope::fromAdsr(settings));

        const auto makeEvent = [&editor](juce::Point<float> at, int clicks)
        {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                                    juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                    &editor, &editor, juce::Time::getCurrentTime(), at,
                                    juce::Time::getCurrentTime(), clicks, false);
        };

        const auto from = editor.pointToScreen(1);
        const auto to = from.translated(60.0f, 70.0f);   // right AND a long way down
        editor.mouseDown(makeEvent(from, 1));
        editor.mouseDrag(makeEvent(to, 1));
        const auto duringDrag = editor.getEnvelope().getPoint(1);
        editor.mouseUp(makeEvent(to, 1));
        const auto afterDrag = editor.getEnvelope().getPoint(1);

        check("EnvelopeEditor_TheAttackHandleMovesInTimeAlongTheTop",
              duringDrag.value >= 1.0 - 1.0e-9 && afterDrag.value >= 1.0 - 1.0e-9
                  && afterDrag.timeSeconds > 0.400 + 1.0e-6,
              "dragging down and right leaves the peak at " + fmt(afterDrag.value, 3)
                  + " and the attack at " + fmt(afterDrag.timeSeconds, 3) + " s");

        // Arrow keys are the same control by another route.
        editor.mouseDown(makeEvent(editor.pointToScreen(1), 1));
        editor.mouseUp(makeEvent(editor.pointToScreen(1), 1));
        editor.keyPressed(juce::KeyPress(juce::KeyPress::downKey));
        check("EnvelopeEditor_ArrowKeysCannotPullTheAttackOffTheTop",
              editor.getEnvelope().getPoint(1).value >= 1.0 - 1.0e-9,
              "after a down-arrow the peak is at "
                  + fmt(editor.getEnvelope().getPoint(1).value, 3));

        // And a skeleton restored with a lowered peak comes back pinned, so an
        // envelope already stored that way is repaired rather than left short.
        px3::BreakpointEnvelope::Point raw[4] = { { 0.0, 0.0, 0.0 }, { 0.02, 0.756, 0.0 },
                                                  { 0.14, 0.7, 0.0 }, { 0.36, 0.0, 0.0 } };
        px3::BreakpointEnvelope restored;
        restored.setPoints(raw, 4, 2);
        check("EnvelopeEditor_ARestoredSkeletonComesBackWithItsPeakOnTheTop",
              restored.getPoint(1).value >= 1.0 - 1.0e-9,
              "a stored peak of 0.756 comes back at " + fmt(restored.getPoint(1).value, 3));
    }

    // ---- an envelope starts and ends at silence -----------------------------
    //
    // A curve that begins at 0.44 and ends at 0.68 is drawn faithfully - and
    // looks like the line has come away from the bottom of its own graph,
    // because it has. On AMP ENV it is also a click at note-on and a note that
    // never stops.
    //
    // The route in: add a point, which makes the shape free-form and unlocks
    // every point's LEVEL, drag the ends up, then remove the added point. The
    // skeleton is back to four points with a sustain bar, carrying levels the
    // skeleton could never have been given directly.
    {
        EnvelopeSettings adsr;
        adsr.attackSeconds = 0.020f;
        adsr.decaySeconds = 0.120f;
        adsr.sustainLevel = 0.7f;
        adsr.releaseSeconds = 0.220f;

        auto shape = px3::BreakpointEnvelope::fromAdsr(adsr);
        // Adding points is a Breakpoint-mode capability.
        shape.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        const auto added = shape.addPoint(0.060, 0.5);
        juce::ignoreUnused(added);

        shape.setPoint(0, 0.0, 0.44);
        shape.setPoint(shape.getPointCount() - 1,
                       shape.getPoint(shape.getPointCount() - 1).timeSeconds, 0.68);
        shape.removePoint(1);

        const auto last = shape.getPointCount() - 1;
        check("EnvelopeEditor_TheCurveStartsAndEndsOnTheBaseline",
              shape.getPoint(0).value <= 1.0e-9 && shape.getPoint(last).value <= 1.0e-9
                  && shape.getPoint(0).timeSeconds <= 1.0e-9,
              "after free-form editing the shape runs from " + fmt(shape.getPoint(0).value, 3)
                  + " to " + fmt(shape.getPoint(last).value, 3) + " over "
                  + juce::String(shape.getPointCount()) + " points");

        // The same has to hold for a shape restored from saved state, so an
        // envelope already stored this way comes back repaired rather than
        // floating.
        px3::BreakpointEnvelope::Point raw[4] = { { 0.0, 0.44, 0.0 }, { 0.02, 1.0, 0.0 },
                                                  { 0.14, 0.7, 0.0 }, { 0.36, 0.68, 0.0 } };
        px3::BreakpointEnvelope restored;
        restored.setPoints(raw, 4, 2);
        check("EnvelopeEditor_ARestoredEnvelopeIsBroughtBackToTheBaseline",
              restored.getPoint(0).value <= 1.0e-9
                  && restored.getPoint(restored.getPointCount() - 1).value <= 1.0e-9,
              "restored shape runs from " + fmt(restored.getPoint(0).value, 3) + " to "
                  + fmt(restored.getPoint(restored.getPointCount() - 1).value, 3));
    }

    // ---- the curve is drawn on the graph it is drawn in ---------------------
    //
    // Everything inside the editor - frame, grid, time rules, labels, curve -
    // is placed from plotArea(). If any one of them used a different rectangle
    // the picture would show a curve floating inside its own background, so
    // the invariants are checked against the rectangle rather than against
    // each other.
    {
        const auto shapes = [] {
            std::vector<std::pair<juce::String, px3::BreakpointEnvelope>> out;

            EnvelopeSettings adsr;
            adsr.attackSeconds = 0.020f;
            adsr.decaySeconds = 0.120f;
            adsr.sustainLevel = 0.7f;
            adsr.releaseSeconds = 0.220f;
            out.emplace_back("plain ADSR", px3::BreakpointEnvelope::fromAdsr(adsr));

            EnvelopeSettings slow;
            slow.attackSeconds = 4.000f;
            slow.decaySeconds = 1.000f;
            slow.sustainLevel = 0.35f;
            slow.releaseSeconds = 3.000f;
            out.emplace_back("slow ADSR", px3::BreakpointEnvelope::fromAdsr(slow));

            auto curved = px3::BreakpointEnvelope::fromAdsr(adsr);
            curved.setCurve(0, 0.8);
            curved.setCurve(1, -0.6);
            curved.setCurve(2, 0.4);
            out.emplace_back("curved", curved);

            auto freeForm = px3::BreakpointEnvelope::fromAdsr(adsr);
            freeForm.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
            freeForm.addPoint(0.060, 0.5);
            freeForm.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
            freeForm.addPoint(0.200, 0.9);
            out.emplace_back("free-form", freeForm);

            return out;
        }();

        juce::StringArray offences;

        for (const auto& [name, shape] : shapes)
        {
            for (const auto size : { juce::Point<int>(395, 119), juce::Point<int>(524, 89),
                                     juce::Point<int>(1650, 221), juce::Point<int>(200, 60) })
            {
                BreakpointEnvelopeEditor editor;
                editor.setSize(size.x, size.y);
                editor.setEnvelope(shape);

                const auto area = editor.debugPlotArea();
                const auto describe = [&](const juce::String& what)
                {
                    return name + " at " + juce::String(size.x) + "x" + juce::String(size.y)
                         + ": " + what;
                };

                // The value axis fills the plot area exactly: 0 on the bottom
                // edge, 1 on the top. A curve that floats inside its own
                // background is this pair of numbers disagreeing.
                if (std::abs(editor.debugToScreen(0.0, 0.0).y - area.getBottom()) > 0.01f)
                {
                    offences.add(describe("value 0 lands at y "
                                          + fmt(editor.debugToScreen(0.0, 0.0).y, 2)
                                          + ", not on the plot bottom " + fmt(area.getBottom(), 2)));
                }
                if (std::abs(editor.debugToScreen(0.0, 1.0).y - area.getY()) > 0.01f)
                {
                    offences.add(describe("value 1 lands at y "
                                          + fmt(editor.debugToScreen(0.0, 1.0).y, 2)
                                          + ", not on the plot top " + fmt(area.getY(), 2)));
                }

                // Every breakpoint sits where its own value says, so the
                // handles cannot drift from the line they mark.
                for (int i = 0; i < shape.getPointCount(); ++i)
                {
                    const auto drawn = editor.pointToScreen(i);
                    const auto wanted = area.getBottom()
                                      - area.getHeight()
                                            * static_cast<float>(shape.getPoint(i).value);
                    if (std::abs(drawn.y - wanted) > 0.01f)
                    {
                        offences.add(describe("point " + juce::String(i) + " drawn at y "
                                              + fmt(drawn.y, 2) + ", value says " + fmt(wanted, 2)));
                    }
                }

                // And the whole drawn curve stays inside the frame it is drawn
                // in - no part of it outside its own background.
                juce::Path curve;
                editor.buildCurvePath(curve);
                const auto bounds = curve.getBounds();
                if (! area.expanded(0.51f).contains(bounds))
                {
                    offences.add(describe("the curve covers " + bounds.toString()
                                          + " but the plot is " + area.toString()));
                }

                // The first and last points anchor the curve to the ends of the
                // axis, so the shape spans the graph rather than sitting in it.
                if (std::abs(bounds.getX() - area.getX()) > 0.51f
                    || std::abs(bounds.getRight() - area.getRight()) > 0.51f)
                {
                    offences.add(describe("the curve runs x " + fmt(bounds.getX(), 1) + ".."
                                          + fmt(bounds.getRight(), 1) + ", the plot "
                                          + fmt(area.getX(), 1) + ".." + fmt(area.getRight(), 1)));
                }
            }
        }

        check("EnvelopeEditor_TheCurveIsDrawnOnTheGraphItIsDrawnIn",
              offences.isEmpty(),
              offences.isEmpty()
                  ? juce::String(shapes.size() * 4) + " shape/size combinations, "
                        + "every curve, handle and axis on the plot rectangle"
                  : offences.joinIntoString("; "));
    }

    // ---- the editor sits inside the frame the card draws --------------------
    // It did not: the editor was positioned from computeGeometry BEFORE
    // layoutCardInner had run, so it got the previous layout's graph rectangle
    // and the curve drew outside the card's frame.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        if (editor != nullptr)
        {
            std::vector<EnvelopeComponent*> cards;
            std::function<void(juce::Component&)> find = [&](juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* env = dynamic_cast<EnvelopeComponent*>(child))
                    {
                        cards.push_back(env);
                    }
                    find(*child);
                }
            };
            find(*editor);

            // Panel bounds change after the first layout in normal use, and that
            // is the path that diverged: it repainted without re-placing the
            // children, so the frame moved and the editor did not.
            for (auto* card : cards)
            {
                card->setPanelContentBounds(card->getBounds().expanded(0, 12));
            }

            juce::StringArray misplaced;
            for (std::size_t i = 0; i < cards.size(); ++i)
            {
                const auto frame = cards[i]->debugGraphFrameBounds();
                const auto placed = cards[i]->debugEditorBounds();
                if (frame != placed)
                {
                    misplaced.add("card " + juce::String(static_cast<int>(i)) + ": frame "
                                  + frame.toString() + " vs editor " + placed.toString());
                }
            }

            check("EnvelopeCard_EditorSitsExactlyOnTheGraphFrame",
                  ! cards.empty() && misplaced.isEmpty(),
                  cards.empty() ? "no envelope cards found in the editor"
                                : (misplaced.isEmpty()
                                       ? juce::String(static_cast<int>(cards.size()))
                                             + " cards, every editor on its frame"
                                       : misplaced.joinIntoString("; ")));
        }
    }

    // ---- evaluation cost ----------------------------------------------------
    // Every active voice evaluates four of these per sample, so the per-sample
    // cost is multiplied by 64 voices and again by the sample rate before it
    // reaches the CPU meter.
    {
        auto shaped = px3::BreakpointEnvelope::fromAdsr(EnvelopeSettings {});
        for (int i = 0; i < 8; ++i)
        {
            shaped.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
            const auto added = shaped.addPoint(0.002 + i * 0.004, 0.4 + 0.05 * i);
            if (added >= 0) { shaped.setCurve(added, i % 2 == 0 ? 0.7 : -0.6); }
        }

        px3::BreakpointEnvelope::Snapshot snapshot;
        snapshot.rebuild(shaped, kSampleRate);

        constexpr int iterations = 2000000;
        volatile float sink = 0.0f;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < iterations; ++i)
        {
            sink = sink + snapshot.valueAtHeld((i % 4800) / kSampleRate);
        }
        const auto nanos = (juce::Time::getMillisecondCounterHiRes() - start)
                           * 1.0e6 / iterations;

        // A full 64-voice patch with all four envelopes running is 256
        // evaluations per sample; at 48 kHz that is 12.3 million a second, so
        // 20 ns each would be a quarter of the entire CPU budget.
        check("Envelope_EvaluationIsCheapEnoughForEveryVoice", nanos < 20.0,
              fmt(nanos, 2) + " ns per sample on a twelve-point envelope with curves "
              "on every segment");
    }

    // ---- instances are independent ------------------------------------------
    {
        auto first = px3::BreakpointEnvelope::fromAdsr(EnvelopeSettings {});
        auto second = first;
        // Adding points is a Breakpoint-mode capability.
        second.setMode(px3::BreakpointEnvelope::Mode::breakpoint);
        second.addPoint(0.01, 0.5);
        second.setCurve(0, 0.9);

        check("Envelope_CopiesDoNotShareState",
              first.getPointCount() == 4 && std::abs(first.getPoint(0).curveToNext) < 1.0e-12,
              "editing a copy left the original at "
                  + juce::String(first.getPointCount()) + " points with no curve");
    }
}

} // namespace px3tests
