#pragma once

// Reverb presets: type-specific starting points for synth material, shared by
// the Synth's Reverb card and PX3 Reverb. A preset sets the type's controls
// (never MIX, which is the player's balance, and never the bypass); every knob
// stays live afterwards. Values are written in real units and converted with
// the same mapping the DSP uses (ReverbMapping.h).
//
// Each preset is meant to show off what its algorithm is for; the evaluation
// that kept or rejected them is in docs/REVERB_DSP_DESIGN.md.

#include "ReverbMapping.h"
#include "ReverbParameters.h"
#include "ReverbTypes.h"

#include <array>
#include <cmath>
#include <vector>

namespace px3::reverb
{
struct Preset
{
    const char* name;
    int type;
    const char* purpose;
    // Real units. Negative = leave at the control's default.
    float decaySeconds { -1.0f };
    float preDelayMs { -1.0f };
    float size { -1.0f };          // normalised 0..1
    float damping { -1.0f };
    float lowMultiplier { -1.0f };
    float diffusion { -1.0f };
    float modulation { -1.0f };
    float width { -1.0f };
    float early { -1.0f };
    float shimmer { -1.0f };
    float drip { -1.0f };
    float shape { -1.0f };
};

// The normalised value a preset gives one control.
inline float presetValue(const Preset& p, Control c) noexcept
{
    const auto pick = [&](float v) { return v >= 0.0f ? v : spec(c).defaultValue; };
    switch (c)
    {
        case Control::decay:      return p.decaySeconds > 0.0f ? decayNormalised(p.type, p.decaySeconds) : spec(c).defaultValue;
        case Control::preDelay:   return p.preDelayMs >= 0.0f ? preDelayNormalised(p.preDelayMs) : spec(c).defaultValue;
        case Control::low:        return p.lowMultiplier > 0.0f ? lowNormalised(p.lowMultiplier) : spec(c).defaultValue;
        case Control::size:       return pick(p.size);
        case Control::damping:    return pick(p.damping);
        case Control::diffusion:  return pick(p.diffusion);
        case Control::modulation: return pick(p.modulation);
        case Control::width:      return pick(p.width);
        case Control::early:      return pick(p.early);
        case Control::shimmer:    return pick(p.shimmer);
        case Control::drip:       return pick(p.drip);
        default:                  return pick(p.shape);
    }
}

namespace detail
{
inline Preset make(const char* name, int type, const char* purpose) { Preset p; p.name = name; p.type = type; p.purpose = purpose; return p; }
}

inline const std::vector<Preset>& allPresets()
{
    static const std::vector<Preset> presets = []
    {
        using detail::make;
        std::vector<Preset> v;
        auto add = [&v](Preset p) { v.push_back(p); };
        Preset p;

        // ---- ROOM: early reflections first; the tail is support -----------
        p = make("Tight Room", room, "Puts a dry synth in a small room without an audible tail: reflections, little decay");
        p.decaySeconds = 0.3f; p.preDelayMs = 0.0f; p.size = 0.3f; p.damping = 0.45f; p.early = 0.75f; p.lowMultiplier = 0.8f; add(p);
        p = make("Synth Room", room, "An all-purpose room for leads and polys: a short, even tail behind clear reflections");
        p.decaySeconds = 0.75f; p.preDelayMs = 6.0f; p.size = 0.5f; p.damping = 0.35f; p.early = 0.5f; p.lowMultiplier = 0.9f; p.modulation = 0.3f; add(p);
        p = make("Small Studio", room, "A treated live room: dark, quick, for plucks and keys that need air but not space");
        p.decaySeconds = 0.5f; p.preDelayMs = 3.0f; p.size = 0.35f; p.damping = 0.6f; p.early = 0.6f; p.lowMultiplier = 0.75f; p.diffusion = 0.8f; add(p);
        p = make("Wide Room", room, "A bigger, wider room for stereo synths and arpeggios");
        // Revised after screening: at SIZE 0.75 / WIDTH 1 a sparse melody summed
        // to mono lost 6.3 dB (inter-ear reflection timing cancelling single
        // tones). The width now comes from the late field, not the reflections.
        p.decaySeconds = 1.0f; p.preDelayMs = 10.0f; p.size = 0.65f; p.damping = 0.3f; p.early = 0.3f; p.width = 0.9f; p.modulation = 0.4f; add(p);
        p = make("Dark Room", room, "Warm and close; tames bright saws and FM without dulling the dry sound");
        p.decaySeconds = 1.1f; p.preDelayMs = 4.0f; p.size = 0.55f; p.damping = 0.8f; p.early = 0.5f; p.lowMultiplier = 1.0f; add(p);
        p = make("Drifty Room", room, "A room whose tail moves: for static pads and drones that need life");
        p.decaySeconds = 1.4f; p.preDelayMs = 8.0f; p.size = 0.65f; p.damping = 0.4f; p.early = 0.35f; p.modulation = 0.75f; add(p);

        // ---- HALL: the late field --------------------------------------------
        p = make("Synth Hall", hall, "The default concert-hall wash for leads and chords");
        p.decaySeconds = 2.4f; p.preDelayMs = 20.0f; p.size = 0.55f; p.damping = 0.35f; p.lowMultiplier = 1.0f; p.early = 0.4f; p.modulation = 0.35f; add(p);
        p = make("Wide Pad", hall, "Spacious late field and full width behind pads; little early energy so the pad stays in front");
        p.decaySeconds = 4.5f; p.preDelayMs = 35.0f; p.size = 0.8f; p.damping = 0.4f; p.diffusion = 0.85f; p.modulation = 0.5f; p.width = 1.0f; p.early = 0.2f; p.lowMultiplier = 0.9f; add(p);
        p = make("Lead Throw", hall, "A long pre-delay keeps a lead's articulation clear of its own tail");
        p.decaySeconds = 2.0f; p.preDelayMs = 60.0f; p.size = 0.6f; p.damping = 0.4f; p.early = 0.25f; p.lowMultiplier = 0.8f; add(p);
        p = make("Dark Hall", hall, "A big, warm hall: air absorption in a large space, for bright sources");
        p.decaySeconds = 3.5f; p.preDelayMs = 25.0f; p.size = 0.7f; p.damping = 0.75f; p.lowMultiplier = 1.15f; p.early = 0.4f; add(p);
        p = make("Bright Hall", hall, "An open, airy hall for dull or filtered sounds");
        p.decaySeconds = 2.8f; p.preDelayMs = 18.0f; p.size = 0.6f; p.damping = 0.12f; p.lowMultiplier = 0.8f; p.early = 0.45f; add(p);
        p = make("Epic Synth", hall, "A huge, long hall for sustained leads and swells");
        p.decaySeconds = 8.0f; p.preDelayMs = 45.0f; p.size = 1.0f; p.damping = 0.45f; p.lowMultiplier = 0.85f; p.modulation = 0.55f; p.early = 0.3f; add(p);

        // ---- PLATE: dense, immediate, bright ---------------------------------
        p = make("Short Decay", plate, "A dense, immediate plate under a second: shine on plucks and percussion without a tail");
        p.decaySeconds = 0.7f; p.preDelayMs = 0.0f; p.size = 0.4f; p.damping = 0.2f; p.lowMultiplier = 0.75f; p.diffusion = 0.8f; add(p);
        p = make("Synth Plate", plate, "The classic plate for leads and polys");
        p.decaySeconds = 1.8f; p.preDelayMs = 10.0f; p.size = 0.5f; p.damping = 0.25f; p.lowMultiplier = 0.85f; add(p);
        p = make("Vocal-ish Synth", plate, "For vowel and formant patches: pre-delay and a lean low end, the vocal-plate recipe");
        p.decaySeconds = 2.2f; p.preDelayMs = 30.0f; p.size = 0.55f; p.damping = 0.3f; p.lowMultiplier = 0.7f; p.modulation = 0.45f; add(p);
        p = make("Bright Plate", plate, "Undamped and sparkling: for dark or low-passed sounds");
        p.decaySeconds = 2.0f; p.preDelayMs = 8.0f; p.size = 0.5f; p.damping = 0.05f; p.lowMultiplier = 0.8f; add(p);
        p = make("Dark Plate", plate, "A damped plate for bright, buzzy sources");
        p.decaySeconds = 2.5f; p.preDelayMs = 10.0f; p.size = 0.55f; p.damping = 0.7f; p.lowMultiplier = 0.9f; add(p);
        p = make("Long Plate", plate, "A large plate held long, for slow chords");
        p.decaySeconds = 5.0f; p.preDelayMs = 15.0f; p.size = 0.85f; p.damping = 0.35f; p.lowMultiplier = 0.8f; p.modulation = 0.5f; add(p);
        p = make("Percussive Plate", plate, "Small, tight and wide: snaps on FM percussion and drums");
        p.decaySeconds = 1.0f; p.preDelayMs = 0.0f; p.size = 0.25f; p.damping = 0.3f; p.lowMultiplier = 0.6f; p.width = 1.0f; add(p);

        // ---- CLOUD: ambient; SHIMMER is pitch-shifted regeneration ------------
        p = make("Soft Cloud", cloud, "A gentle ambient wash with no shimmer");
        p.decaySeconds = 6.0f; p.preDelayMs = 20.0f; p.size = 0.45f; p.damping = 0.45f; p.diffusion = 0.75f; p.modulation = 0.45f; p.shimmer = 0.0f; p.lowMultiplier = 0.85f; add(p);
        p = make("Synth Cloud", cloud, "A long, evolving cloud for pads and arpeggios");
        p.decaySeconds = 10.0f; p.preDelayMs = 25.0f; p.size = 0.6f; p.damping = 0.4f; p.diffusion = 0.8f; p.modulation = 0.55f; p.shimmer = 0.0f; p.lowMultiplier = 0.8f; add(p);
        p = make("Shimmer Wash", cloud, "Octave-up regeneration rising out of the cloud: the classic shimmer pad");
        p.decaySeconds = 12.0f; p.preDelayMs = 30.0f; p.size = 0.6f; p.damping = 0.35f; p.diffusion = 0.8f; p.modulation = 0.5f; p.shimmer = 0.55f; p.lowMultiplier = 0.7f; add(p);
        p = make("Octave Bloom", cloud, "Heavy shimmer and full bloom: the tail climbs octave on octave");
        p.decaySeconds = 18.0f; p.preDelayMs = 40.0f; p.size = 0.75f; p.damping = 0.3f; p.diffusion = 0.9f; p.modulation = 0.55f; p.shimmer = 0.8f; p.lowMultiplier = 0.6f; add(p);
        p = make("Ethereal Pad", cloud, "A light touch of shimmer under a long, moving tail");
        p.decaySeconds = 20.0f; p.preDelayMs = 35.0f; p.size = 0.7f; p.damping = 0.4f; p.diffusion = 0.85f; p.modulation = 0.75f; p.shimmer = 0.3f; p.lowMultiplier = 0.75f; add(p);
        p = make("Infinite", cloud, "Near-endless sustain (60 s) for drones and freezes");
        p.decaySeconds = 60.0f; p.preDelayMs = 20.0f; p.size = 0.8f; p.damping = 0.5f; p.diffusion = 0.85f; p.modulation = 0.6f; p.shimmer = 0.0f; p.lowMultiplier = 0.6f; add(p);
        p = make("Dark Cloud", cloud, "A long, dark, low wash under bright material");
        p.decaySeconds = 14.0f; p.preDelayMs = 20.0f; p.size = 0.65f; p.damping = 0.85f; p.diffusion = 0.8f; p.modulation = 0.45f; p.lowMultiplier = 1.0f; add(p);

        // ---- SPRING -------------------------------------------------------------
        p = make("Surf Tank", spring, "A drippy guitar-amp tank: strong chirp, medium decay");
        p.decaySeconds = 2.5f; p.preDelayMs = 0.0f; p.size = 0.5f; p.damping = 0.3f; p.drip = 0.85f; p.diffusion = 0.6f; add(p);
        p = make("Dub Spring", spring, "Long and dark, for dub stabs and echoes");
        p.decaySeconds = 4.5f; p.preDelayMs = 0.0f; p.size = 0.75f; p.damping = 0.55f; p.drip = 0.65f; p.diffusion = 0.5f; add(p);
        p = make("Short Spring", spring, "A short, tight spring that adds twang without a wash");
        p.decaySeconds = 1.2f; p.preDelayMs = 0.0f; p.size = 0.3f; p.damping = 0.35f; p.drip = 0.4f; p.diffusion = 0.5f; add(p);
        p = make("Bright Twang", spring, "Open and splashy for retro leads");
        p.decaySeconds = 2.2f; p.preDelayMs = 0.0f; p.size = 0.5f; p.damping = 0.1f; p.drip = 0.9f; p.diffusion = 0.8f; add(p);
        p = make("Dark Tank", spring, "A muffled spring that sits under a mix");
        p.decaySeconds = 3.0f; p.preDelayMs = 0.0f; p.size = 0.6f; p.damping = 0.75f; p.drip = 0.6f; p.diffusion = 0.4f; add(p);

        // ---- GATED --------------------------------------------------------------
        p = make("80s Gate", gated, "The gated drum room: a flat burst that stops dead");
        p.decaySeconds = 0.3f; p.preDelayMs = 0.0f; p.size = 0.5f; p.damping = 0.3f; p.diffusion = 0.8f; p.shape = 0.5f; add(p);
        p = make("Tight Gate", gated, "A short gate that thickens percussion without a tail");
        p.decaySeconds = 0.15f; p.preDelayMs = 0.0f; p.size = 0.4f; p.damping = 0.35f; p.diffusion = 0.8f; p.shape = 0.55f; add(p);
        p = make("Reverse Swell", gated, "A rising, reversed-sounding swell into each hit");
        p.decaySeconds = 0.45f; p.preDelayMs = 0.0f; p.size = 0.6f; p.damping = 0.35f; p.diffusion = 0.9f; p.shape = 0.0f; add(p);
        p = make("Big Snap", gated, "A longer, falling burst for big electronic snares and claps");
        p.decaySeconds = 0.5f; p.preDelayMs = 0.0f; p.size = 0.7f; p.damping = 0.25f; p.diffusion = 0.85f; p.shape = 0.75f; add(p);
        return v;
    }();
    return presets;
}

inline std::vector<Preset> presetsForType(int type)
{
    std::vector<Preset> out;
    for (const auto& p : allPresets()) if (p.type == type) out.push_back(p);
    return out;
}

inline const Preset* findPreset(int type, const juce::String& name)
{
    for (const auto& p : allPresets())
        if (p.type == type && name == p.name) return &p;
    return nullptr;
}

inline void applyPresetToSettings(const Preset& p, ReverbSettings& s) noexcept
{
    s.algorithmIndex = p.type;
    for (int c = 0; c < kControlCount; ++c)
        settingsField(s, static_cast<Control>(c)) = presetValue(p, static_cast<Control>(c));
}

// Whether the live controls still hold the preset's values: false once the
// player has moved any knob the type reads. `current(c)` returns the
// control's normalised value.
template <typename Current>
inline bool presetMatches(const Preset& p, int type, Current&& current)
{
    if (type != p.type) return false;
    for (int c = 0; c < kControlCount; ++c)
    {
        const auto control = static_cast<Control>(c);
        if (! isLive(control, type)) continue;
        if (std::abs(current(control) - presetValue(p, control)) > 2.0e-3f) return false;
    }
    return true;
}
} // namespace px3::reverb
