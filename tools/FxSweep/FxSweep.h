#pragma once

// FX sweep: every control of every effect, moved through its whole range with
// that effect on and every other effect off, measured at the instrument's
// output.
//
// One definition, two hosts. The spec, the material, the settings and the
// measurements live here; a host only has to supply a Renderer that makes a
// fresh instance, applies settings by PARAMETER NAME (normalised 0..1), plays
// the material and returns the output. PX3Tests' `fxsweep` mode renders the
// processor directly (the standalone path); tools/FxSweep/PluginHostSweep
// loads the built AU / VST3 through JUCE's hosting (the plug-in path). Same
// names, same values, same numbers - so the two can be compared row by row.
//
// Names, not ids: a hosted AU or VST3 exposes its parameters' names but not
// the processor's string ids.

#include <JuceHeader.h>

#include "ReverbParameters.h"
#include "ReverbPresets.h"

#include <cmath>
#include <functional>
#include <map>
#include <vector>

namespace px3::fxsweep
{
struct Setting
{
    juce::String name;
    float value { 0.0f };       // normalised
    double atSeconds { -1.0 };  // < 0: before prepare; otherwise at the first block at or after this time
};
using Settings = std::vector<Setting>;

// A control moved linearly while the chord is held, set at each block start.
struct Ramp
{
    juce::String name;
    float from { 0.0f };
    float to { 1.0f };
    double fromSeconds { 0.35 };
    double toSeconds { 1.0 };
};

struct Note
{
    double on;
    double off;
    int note;
    float velocity;
};

struct Audio
{
    std::vector<float> left, right;
    double sampleRate { 48000.0 };
};

// Fresh instance at defaults -> the untimed settings in order -> prepare -> a
// playhead at kBpm, playing from bar 1 -> the material for kSeconds, each
// timed setting at the first block at or after its time, the ramp if any.
using Renderer = std::function<Audio(const Settings&, const Ramp*)>;

inline constexpr double kSeconds = 2.6;
inline constexpr double kBpm = 120.0;

// A held four-note chord (sustained material), then short low/high plucks (a
// drum-like pattern with gaps for repeats and tails to sound in).
inline std::vector<Note> material()
{
    std::vector<Note> notes { { 0.02, 1.10, 48, 0.85f }, { 0.02, 1.10, 55, 0.85f },
                              { 0.02, 1.10, 60, 0.85f }, { 0.02, 1.10, 64, 0.85f } };
    const int pattern[] = { 72, 43, 67, 48, 76, 45, 69, 50 };
    for (int i = 0; i < 8; ++i)
    {
        const auto at = 1.25 + 0.13 * i;
        notes.push_back({ at, at + 0.05, pattern[i], 1.0f });
    }
    return notes;
}

// Every effect and insert off. The console is left as it ships (on). MASTER
// is brought down (the shipped patch already sits at the output ceiling's
// knee, which would hide what a boost does) and OSC 1 is panned off centre,
// so stereo controls have a stereo signal to act on.
inline Settings everythingOff()
{
    Settings s { { "Master Gain", 0.35f }, { "Osc 1 Pan", 0.3f } };
    for (const auto* fx : { "Analog", "Vibe", "Drive", "Chorus", "Doom", "Delay", "Mood", "Reverb", "Lucy", "Spread" })
    {
        s.push_back({ juce::String(fx) + " Enabled", 0.0f });
    }
    for (const auto* bus : { "Dry Bus", "FX Bus" })
    {
        s.push_back({ juce::String(bus) + " EQ Enabled", 0.0f });
        s.push_back({ juce::String(bus) + " Comp Enabled", 0.0f });
    }
    return s;
}

struct Section
{
    juce::String label;
    juce::StringArray prefixes;   // which parameters belong to it, by name
    juce::String enable;          // empty: nothing to switch (the mixer)
    Settings active;              // what makes it audible
};

inline std::vector<Section> sections()
{
    return {
        { "ANALOG", { "Analog " }, "Analog Enabled", { { "Analog Amount", 0.8f } } },
        { "VIBE", { "Vibe " }, "Vibe Enabled", {} },
        { "DRIVE", { "Drive " }, "Drive Enabled", { { "Drive Mix", 1.0f } } },
        { "CHORUS", { "Chorus " }, "Chorus Enabled", { { "Chorus Amount", 0.8f } } },
        { "DOOM", { "Doom " }, "Doom Enabled", { { "Doom Mix", 0.7f } } },
        { "DELAY", { "Delay ", "Granular " }, "Delay Enabled", { { "Delay Amount", 0.5f } } },
        { "MOOD", { "Mood " }, "Mood Enabled", {} },
        { "REVERB", { "Reverb" }, "Reverb Enabled", { { "Reverb", 0.5f } } },
        { "LUCY", { "Lucy " }, "Lucy Enabled", { { "Lucy Global", 0.8f }, { "Lucy Loss", 0.7f } } },
        { "SPREAD", { "Spread " }, "Spread Enabled", { { "Spread Amount", 0.8f } } },
        { "DRY EQ", { "Dry Bus EQ" }, "Dry Bus EQ Enabled", { { "Dry Bus EQ 2 Gain", 0.8f } } },
        { "DRY COMP", { "Dry Bus Comp" }, "Dry Bus Comp Enabled", {} },
        { "FX EQ", { "FX Bus EQ" }, "FX Bus EQ Enabled", { { "FX Bus EQ 2 Gain", 0.8f }, { "Reverb Enabled", 1.0f }, { "Reverb", 0.6f } } },
        { "FX COMP", { "FX Bus Comp" }, "FX Bus Comp Enabled", { { "Reverb Enabled", 1.0f }, { "Reverb", 0.6f } } },
        { "CONSOLE", { "Console " }, "Console Enabled", {} },
        // The sends need something on the FX bus to be heard: a reverb, which
        // answers at once and has a tail.
        { "SENDS", { "FX Send", "Sub FX Send", "Osc 1 FX Send", "Osc 2 FX Send", "Osc 3 FX Send", "FX Return" }, {},
          { { "Reverb Enabled", 1.0f }, { "Reverb", 0.6f }, { "Chorus Enabled", 1.0f }, { "Chorus Amount", 0.8f } } },
    };
}

inline float choice(int index, int count) { return count > 1 ? static_cast<float>(index) / static_cast<float>(count - 1) : 0.0f; }

// What a control needs before it can do anything - not a default the sweep
// has chosen to hide, but the thing the control is FOR (a gate threshold needs
// the gate on). Anything with no entry is swept with only its section active.
inline Settings companionsFor(const juce::String& name)
{
    using namespace px3::reverb;
    if (name.startsWith("Reverb "))
    {
        for (int c = 0; c < kControlCount; ++c)
        {
            if (name == kParameterSpecs[c].name)
            {
                for (int type = 0; type < kTypeCount; ++type)
                {
                    if (isLive(static_cast<Control>(c), type)) { return { { "Reverb Mode", choice(type, kTypeCount) } }; }
                }
            }
        }
        return {};
    }
    static const std::map<juce::String, Settings> table {
        { "Lucy Gate Threshold", { { "Lucy Gate", 1.0f } } },
        { "Lucy Freezer", { { "Lucy Freeze", 0.5f } } },
        { "Lucy Freq", { { "Lucy Filter", 0.6f } } },
        { "Lucy Slope", { { "Lucy Filter", 0.6f } } },
        { "Lucy Filter Invert", { { "Lucy Filter", 0.6f } } },
        { "Lucy Decay", { { "Lucy Verb", 0.6f } } },
        { "Lucy Verb Post", { { "Lucy Verb", 0.6f } } },
        { "Delay Tape Wobble", { { "Delay Algorithm", choice(1, 7) } } },
        { "Delay Tape Quality", { { "Delay Algorithm", choice(1, 7) } } },
        { "Delay Tape Slip", { { "Delay Algorithm", choice(1, 7) } } },
        { "Delay Mod Depth", { { "Delay Algorithm", choice(5, 7) } } },
        { "Lucy Spread", { { "Lucy Verb", 0.6f } } },
        { "Doom Cross Source", { { "Doom Cross", 0.8f } } },
        // A send to a source that is not playing has nothing to send.
        { "Sub FX Send", { { "Sub Osc Enabled", 1.0f } } },
        { "Osc 2 FX Send", { { "Osc 2 Enabled", 1.0f } } },
        { "Osc 3 FX Send", { { "Osc 3 Enabled", 1.0f } } },
    };
    if (const auto it = table.find(name); it != table.end()) { return it->second; }

    // An EQ band's frequency, Q and type do nothing at 0 dB.
    if (name.contains(" EQ ") && ! name.endsWith("Gain") && ! name.endsWith("Enabled"))
    {
        const auto band = name.upToLastOccurrenceOf(" ", false, false);
        return { { band + " Gain", 0.85f } };
    }
    return {};
}

// The situations a control is tried in; it is live if it moves the output in
// any of them. Most controls have one (their companions). DOOM's looper plays
// back what it heard before it was engaged, so its controls are tried with the
// looper switched on AFTER the chord (at 1.15 s), in each loop mode.
inline std::vector<Settings> contextsFor(const juce::String& name)
{
    const juce::StringArray looper { "Doom Loop Half", "Doom Loop Length", "Doom Loop Modify", "Doom Loop Mode",
                                     "Doom Overdub", "Doom Fade", "Doom Spread", "Doom Routing", "Doom Blend",
                                     "Doom Freeze" };
    if (looper.contains(name))
    {
        std::vector<Settings> out;
        for (int mode = 0; mode < 3; ++mode)
        {
            Settings s { { "Doom Looper Active", 0.0f }, { "Doom Looper Active", 1.0f, 1.15 } };
            if (name != "Doom Loop Mode") { s.push_back({ "Doom Loop Mode", choice(mode, 3) }); }
            if (name == "Doom Fade") { s.push_back({ "Doom Overdub", 0.6f }); }
            if (name == "Doom Blend") { s.push_back({ "Doom Routing", 1.0f }); }
            out.push_back(s);
            if (name == "Doom Loop Mode") { break; }
        }
        if (name == "Doom Spread")
        {
            for (int wet = 0; wet < 3; ++wet) { out.push_back({ { "Doom Wet Mode", choice(wet, 3) } }); }
        }
        return out;
    }
    return { companionsFor(name) };
}

// The controls whose whole range the plug-in is known to make no difference
// to at this material, with the reason. Anything else that is dead is a bug.
inline std::map<juce::String, juce::String> knownInert()
{
    return {
        { "Lucy Speed", "known dead: decisionFrames only gates packets (declined fix)" },
        { "Lucy Weighting", "known dead: tilt only acts inside the LOSS coverage strip (declined fix)" },
        { "Lucy Auto Gain", "known dead: the coder barely changes energy at these settings (declined fix)" },
        { "Dry Bus Comp Meter", "display only: chooses what the VU shows" },
        { "FX Bus Comp Meter", "display only: chooses what the VU shows" },
    };
}

// Controls whose sweep is allowed a larger step than their still settings,
// with the reason.
inline std::map<juce::String, juce::String> allowedToStep()
{
    return {
        { "Mood Clock", "CLOCK is quantised to semitones by design: crossing one changes the internal rate at once "
                        "(largest step 0.024 against 0.010 still)" },
        { "Doom Clock", "CLOCK steps through harmonised ratios unless CLOCK SMOOTH is on; with 1024-sample host blocks "
                        "a sweep's crest reached 0.4 against 0.2 still (0.2 at 512)" },
    };
}

// Controls allowed to take the output far down at one end, with the reason.
inline std::map<juce::String, juce::String> allowedToSilence()
{
    return {
        { "Lucy Freq", "a narrow band placed where the material has nothing (the band filter's makeup is capped at +30 dB)" },
        { "Lucy Slope", "the 96 dB slope's eight stacked sections at FILTER 0.6: -21 dB at 48 kHz, -29 dB at 44.1 kHz" },
    };
}

// ---- measurement -------------------------------------------------------------

inline double rms(const Audio& a, double from = 0.05)
{
    const auto first = static_cast<std::size_t>(from * a.sampleRate);
    double e = 0.0;
    std::size_t n = 0;
    for (std::size_t i = first; i < a.left.size(); ++i, ++n)
    {
        e += static_cast<double>(a.left[i]) * a.left[i] + static_cast<double>(a.right[i]) * a.right[i];
    }
    return std::sqrt(e / static_cast<double>(n > 0 ? 2 * n : 1));
}

inline double differenceDb(const Audio& a, const Audio& b, double from = 0.05)
{
    const auto first = static_cast<std::size_t>(from * a.sampleRate);
    double e = 0.0, ref = 0.0;
    for (std::size_t i = first; i < a.left.size() && i < b.left.size(); ++i)
    {
        const auto dl = static_cast<double>(a.left[i]) - b.left[i];
        const auto dr = static_cast<double>(a.right[i]) - b.right[i];
        e += dl * dl + dr * dr;
        ref += static_cast<double>(a.left[i]) * a.left[i] + static_cast<double>(a.right[i]) * a.right[i];
    }
    return 10.0 * std::log10(juce::jmax(1.0e-30, e) / juce::jmax(1.0e-30, ref));
}

inline double peak(const Audio& a)
{
    double p = 0.0;
    for (const auto v : a.left) { p = juce::jmax(p, static_cast<double>(std::abs(v))); }
    for (const auto v : a.right) { p = juce::jmax(p, static_cast<double>(std::abs(v))); }
    return p;
}

inline bool finite(const Audio& a)
{
    for (const auto v : a.left) { if (! std::isfinite(v)) { return false; } }
    for (const auto v : a.right) { if (! std::isfinite(v)) { return false; } }
    return true;
}

// How much a window's largest sample step stands out from its own level: the
// largest, over 10 ms windows, of (max |step|) / (RMS + 1e-3). A click is a
// step far out of proportion to what is around it; a louder setting just has
// proportionally larger steps.
inline double stepCrest(const Audio& a, double fromSeconds, double toSeconds)
{
    const auto window = static_cast<std::size_t>(0.010 * a.sampleRate);
    const auto from = static_cast<std::size_t>(juce::jmax(1.0, fromSeconds * a.sampleRate));
    const auto to = juce::jmin(a.left.size(), static_cast<std::size_t>(toSeconds * a.sampleRate));
    double worst = 0.0;
    for (auto start = from; start + window <= to; start += window)
    {
        double step = 0.0, e = 0.0;
        for (auto i = start; i < start + window; ++i)
        {
            step = juce::jmax(step, static_cast<double>(std::abs(a.left[i] - a.left[i - 1])),
                              static_cast<double>(std::abs(a.right[i] - a.right[i - 1])));
            e += static_cast<double>(a.left[i]) * a.left[i] + static_cast<double>(a.right[i]) * a.right[i];
        }
        worst = juce::jmax(worst, step / (std::sqrt(e / static_cast<double>(2 * window)) + 1.0e-3));
    }
    return worst;
}

// The largest sample-to-sample step in a window, both channels.
inline double maxStep(const Audio& a, double fromSeconds, double toSeconds)
{
    const auto from = static_cast<std::size_t>(juce::jmax(1.0, fromSeconds * a.sampleRate));
    const auto to = juce::jmin(a.left.size(), static_cast<std::size_t>(toSeconds * a.sampleRate));
    double worst = 0.0;
    for (std::size_t i = from; i < to; ++i)
    {
        worst = juce::jmax(worst, static_cast<double>(std::abs(a.left[i] - a.left[i - 1])),
                           static_cast<double>(std::abs(a.right[i] - a.right[i - 1])));
    }
    return worst;
}

inline double toDb(double gain) { return juce::Decibels::gainToDecibels(gain, -300.0); }

struct ParameterInfo
{
    juce::String name;
    float defaultValue { 0.0f };
    int steps { 0 };          // 0 or > 64: continuous
    juce::StringArray texts;  // the value's text at each swept position (filled by the host)
};

struct ValueResult
{
    float value { 0.0f };
    juce::String text;
    double vsOffDb { 0.0 };       // difference from the effect switched off
    double vsDefaultDb { 0.0 };   // difference from the control at its default
    double levelDb { 0.0 };       // RMS re the effect switched off
    double peak { 0.0 };
    bool finite { true };
};

struct ControlResult
{
    juce::String section, name;
    std::vector<ValueResult> values;
    double maxVsDefaultDb { -300.0 };
    double rampStep { 0.0 }, staticStep { 0.0 };   // step crest swept / static, continuous controls only
    bool swept { false };
    juce::StringArray flags;   // DEAD, NAN, CLIP, CEILING (info), SILENCES, CLICK
};

struct SectionResult
{
    juce::String label;
    double onVsOffDb { -300.0 };
    std::vector<ControlResult> controls;
};

inline bool isContinuous(const ParameterInfo& p) { return p.steps <= 1 || p.steps > 64; }

inline std::vector<float> sweepValues(const ParameterInfo& p)
{
    if (isContinuous(p)) { return { 0.0f, 0.5f, 1.0f }; }
    std::vector<float> v;
    for (int i = 0; i < p.steps; ++i) { v.push_back(choice(i, p.steps)); }
    return v;
}

inline bool isLevelControl(const juce::String& name)
{
    for (const auto* word : { "Level", "Gain", "Output", "Input", "Mix", "Amount", "Return", "Send", "Global", "Limiter",
                              "Intensity", "Threshold" })
    {
        if (name.contains(word)) { return true; }
    }
    return false;
}

inline Settings join(Settings a, const Settings& b)
{
    for (const auto& s : b) { a.push_back(s); }
    return a;
}

// Runs every section. `params` is the instance's parameter list (name,
// default, step count); `log` gets one line per control as it finishes.
inline std::vector<SectionResult> run(const std::vector<ParameterInfo>& params, const Renderer& render,
                                      const std::function<void(const juce::String&)>& log,
                                      const juce::StringArray& onlySections = {})
{
    std::vector<SectionResult> out;
    const auto base = everythingOff();
    for (const auto& section : sections())
    {
        if (! onlySections.isEmpty() && ! onlySections.contains(section.label)) { continue; }
        SectionResult sr;
        sr.label = section.label;

        const auto activeBase = join(section.enable.isNotEmpty() ? join(base, { { section.enable, 1.0f } }) : base, section.active);
        // The "off" reference: the section's enable off (its other settings as
        // when on, so only the switch differs); for the mixer, sends closed.
        Settings offRef = join(base, section.active);
        if (section.enable.isNotEmpty()) { offRef.push_back({ section.enable, 0.0f }); }
        else
        {
            for (const auto& name : section.prefixes) { offRef.push_back({ name, 0.0f }); }
        }
        const auto offAudio = render(offRef, nullptr);
        const auto onAudio = render(activeBase, nullptr);
        sr.onVsOffDb = differenceDb(offAudio, onAudio);

        for (const auto& p : params)
        {
            auto belongs = false;
            for (const auto& prefix : section.prefixes)
            {
                belongs = belongs || (p.name == prefix || p.name.startsWith(prefix));
            }
            if (! belongs || p.name == section.enable) { continue; }

            // Tried in each of its contexts; the one where it does the most is
            // reported, and a flag from any of them is kept.
            ControlResult best;
            juce::StringArray flags;
            for (const auto& context : contextsFor(p.name))
            {
                ControlResult cr;
                cr.section = section.label;
                cr.name = p.name;
                const auto withContext = join(activeBase, context);
                const auto ref = render(withContext, nullptr);
                std::map<float, Audio> kept;   // 0, 0.5, 1 for the click check
                for (const auto value : sweepValues(p))
                {
                    const auto audio = render(join(withContext, { { p.name, value } }), nullptr);
                    ValueResult vr;
                    vr.value = value;
                    vr.vsOffDb = differenceDb(offAudio, audio);
                    vr.vsDefaultDb = differenceDb(ref, audio);
                    vr.levelDb = toDb(rms(audio) / juce::jmax(1.0e-12, rms(offAudio)));
                    vr.peak = peak(audio);
                    vr.finite = finite(audio);
                    cr.maxVsDefaultDb = juce::jmax(cr.maxVsDefaultDb, vr.vsDefaultDb);
                    if (! vr.finite) { flags.addIfNotAlreadyThere("NAN"); }
                    // The output ceiling is asymptotic to full scale: a sample
                    // AT full scale is the ceiling saturated (a level control at
                    // its top), one PAST it is a clip.
                    if (vr.peak > 1.0) { flags.addIfNotAlreadyThere("CLIP"); }
                    if (vr.peak >= 0.999) { flags.addIfNotAlreadyThere("CEILING"); }
                    if (vr.levelDb < -24.0 && ! isLevelControl(p.name) && allowedToSilence().count(p.name) == 0)
                    {
                        flags.addIfNotAlreadyThere("SILENCES");
                    }
                    if (isContinuous(p)) { kept[value] = audio; }
                    cr.values.push_back(vr);
                }

                // Swept while the chord is held: the steps it makes must not
                // stand out from those of the settings it moves between.
                if (isContinuous(p))
                {
                    Ramp ramp { p.name, 0.0f, 1.0f };
                    const auto swept = render(withContext, &ramp);
                    cr.swept = true;
                    cr.rampStep = stepCrest(swept, ramp.fromSeconds, ramp.toSeconds);
                    double staticAbsolute = 0.0;
                    for (const auto& [value, audio] : kept)
                    {
                        juce::ignoreUnused(value);
                        cr.staticStep = juce::jmax(cr.staticStep, stepCrest(audio, ramp.fromSeconds, ramp.toSeconds));
                        staticAbsolute = juce::jmax(staticAbsolute, maxStep(audio, ramp.fromSeconds, ramp.toSeconds));
                    }
                    if (! finite(swept)) { flags.addIfNotAlreadyThere("NAN"); }
                    // Out of proportion to its surroundings AND larger than any
                    // step the still settings make: a level dip mid-sweep (two
                    // detuned copies beating) raises the crest without a click.
                    const auto sweptAbsolute = maxStep(swept, ramp.fromSeconds, ramp.toSeconds);
                    if (cr.rampStep > 2.0 * cr.staticStep && sweptAbsolute > 1.5 * staticAbsolute && sweptAbsolute > 0.01
                        && allowedToStep().count(p.name) == 0)
                    {
                        flags.addIfNotAlreadyThere("CLICK");
                    }
                }
                if (best.name.isEmpty() || cr.maxVsDefaultDb > best.maxVsDefaultDb) { best = std::move(cr); }
            }
            best.flags = flags;
            if (best.maxVsDefaultDb < -60.0) { best.flags.addIfNotAlreadyThere("DEAD"); }

            juce::String line;
            line << section.label << " | " << p.name << " | max vs default " << juce::String(best.maxVsDefaultDb, 1) << " dB |";
            for (const auto& v : best.values)
            {
                line << " " << juce::String(v.value, 2) << ":" << juce::String(v.vsDefaultDb, 1) << "/" << juce::String(v.levelDb, 1);
            }
            if (best.swept) { line << " | sweep crest " << juce::String(best.rampStep, 1) << " vs " << juce::String(best.staticStep, 1); }
            if (! best.flags.isEmpty()) { line << " | " << best.flags.joinIntoString(","); }
            if (log) { log(line); }
            sr.controls.push_back(std::move(best));
        }
        out.push_back(std::move(sr));
    }
    return out;
}

// One row per (section, control, value): what both hosts write, so two runs can
// be compared with a diff or a spreadsheet.
inline juce::String toTsv(const std::vector<SectionResult>& results)
{
    juce::String t;
    t << "section\tcontrol\tvalue\tvs_off_db\tvs_default_db\tlevel_db\tpeak\tfinite\tsweep_crest\tstatic_crest\tflags\n";
    for (const auto& s : results)
    {
        t << s.label << "\t(section on)\t1\t" << juce::String(s.onVsOffDb, 2) << "\t\t\t\t\t\t\t\n";
        for (const auto& c : s.controls)
        {
            for (const auto& v : c.values)
            {
                t << s.label << "\t" << c.name << "\t" << juce::String(v.value, 3) << "\t" << juce::String(v.vsOffDb, 2)
                  << "\t" << juce::String(v.vsDefaultDb, 2) << "\t" << juce::String(v.levelDb, 2) << "\t"
                  << juce::String(v.peak, 4) << "\t" << (v.finite ? 1 : 0) << "\t" << juce::String(c.rampStep, 5) << "\t"
                  << juce::String(c.staticStep, 5) << "\t" << c.flags.joinIntoString(",") << "\n";
            }
        }
    }
    return t;
}

// The reverb card's presets, as the parameter values they set.
inline std::vector<std::pair<juce::String, Settings>> reverbPresetSettings()
{
    using namespace px3::reverb;
    std::vector<std::pair<juce::String, Settings>> out;
    for (int type = 0; type < kTypeCount; ++type)
    {
        for (const auto& preset : presetsForType(type))
        {
            Settings s { { "Reverb Mode", choice(type, kTypeCount) } };
            for (int c = 0; c < kControlCount; ++c)
            {
                s.push_back({ kParameterSpecs[c].name, presetValue(preset, static_cast<Control>(c)) });
            }
            out.push_back({ juce::String(kTypeNames[type]) + "/" + preset.name, s });
        }
    }
    return out;
}
} // namespace px3::fxsweep
