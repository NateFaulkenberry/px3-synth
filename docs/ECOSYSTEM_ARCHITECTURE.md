# PX3 Ecosystem Architecture

How this repository is organised now that PX3 Synth is a product inside it
rather than the whole of it.

---

## 1. The shape

```
shared/                      code any PX3 product may use
  DSP/                       Mood Delay Reverb Doom Lucy Chorus
                             StereoSpread Vibe Filter Analog Core
  UI/
    Components/              Card ChipLabel BypassButton PianoKeyboard
                             VuMeter RoundedRect ModalBackdrop ...
    Style/                   UIConfig, UIConfigManager, UIConfig.json
    Fx/                      the FX editors, which are already product-neutral
  Infrastructure/
    Update/                  provider, registry, service, semantic version
    Settings/                the global preference file
  Assets/                    ecosystem assets (the uninstaller icon)

products/
  PX3Synth/                  everything only the Synth needs
    DSP/ UI/ Preset/ Assets/

updater/                     the helper application
tools/                       diagnostics and benchmarks
tests/Tests/                 the component suite
cmake/PX3Product.cmake       px3_add_product()
```

Nothing under `shared/` includes anything from `products/`. That is the rule the
boundary rests on, and it is checkable: a grep for `products/` inside `shared/`
should return nothing.

---

## 2. Why the DSP was already shared

Every FX class - Mood, Delay, Reverb, Doom, Lucy, Chorus, StereoSpread, Vibe,
AnalogEngine, VoiceFilter, StftEngine, OutputCeiling, BusAnalyser - includes
nothing but its own headers. None has ever known about `PluginProcessor`.

So the migration moved them; it did not extract them. That is why no audio
behaviour changed, and it is the reason a standalone FX product is a wrapper
rather than a reimplementation.

The same turned out to be true of more UI than expected: only 11 of 104 UI
files include `PluginProcessor.h`, and the FX **editors** are not among them.
`MoodComponent`, `DelayComponent`, `ReverbComponent`, `VibeComponent` and
`FilterComponent` sit in `shared/UI/Fx` beside the DSP they drive.

---

## 3. Why shared code is not a static library

`JuceHeader.h` is generated **per target** and carries that target's
`JucePlugin_*` identity. A single static library built against one product's
header would bake that product's identity into every other product linking it.

So `shared/` is a source list compiled into each product, not a library. The
boundary comes from the two lists and the include paths:

```cmake
set(PX3_SHARED_SOURCES ...)   # shared/
set(PX3_SYNTH_SOURCES  ...)   # products/PX3Synth/
set(PX3_ALL_SOURCES ${PX3_SHARED_SOURCES} ${PX3_SYNTH_SOURCES})
```

A product target compiles `PX3_SHARED_SOURCES` plus its own. The tools and the
test suite compile `PX3_ALL_SOURCES`, because they build the Synth in full.

---

## 4. Product identity

Identity exists in two halves that must agree.

**Build side** - `cmake/PX3Product.cmake`:

```cmake
px3_add_product(
    TARGET       PX3Synth
    PRODUCT_NAME "PX3 Synth"
    BUNDLE_ID    "com.px3.px3synth"
    PLUGIN_CODE  SyP1
    IS_SYNTH     TRUE
    FORMATS      AU VST3 Standalone
    SOURCES      ${PX3_SYNTH_SOURCES})
```

**Runtime side** - `ProductRegistry::Registration`: product id, display name,
version provider, bundle id, which formats, and the installer component id.

| | convention | note |
|---|---|---|
| product id | `px3-<name>` | `px3-synth` |
| bundle id | `com.px3.<name>` | the Synth is **grandfathered** as `com.px3.px3synth` |
| manufacturer code | `SyPr` | one manufacturer, many products |
| plugin code | unique, four characters | `SyP1` for the Synth |

`PLUGIN_CODE` must be unique across the ecosystem. Two products sharing one is
how a DAW ends up loading the wrong plug-in.

The Synth's bundle id and plugin code are **not** changed to match the newer
convention, and must not be: they are what is already installed on people's
machines, and changing them orphans every existing install and session.

---

## 5. Version

`PX3_VERSION` in the top-level `CMakeLists.txt` is the single source of truth.
It reaches JUCE as `ProjectInfo::versionString`, and everything else -
`px3::version::string()`, the Settings panel, the updater's comparison, the
installer's filenames - reads it from there. Nothing keeps a second copy.

---

## 6. Adding a product: PX3 Mood, concretely

1. **Sources.** `products/PX3Mood/` with a `PluginProcessor` and `PluginEditor`
   that wrap `shared/DSP/Mood` and `shared/UI/Fx/MoodComponent`. Do not copy
   either - the shared implementation is the canonical one, and the wrapper is
   thin by design.

2. **Source list**, beside the Synth's:
   ```cmake
   set(PX3_MOOD_SOURCES products/PX3Mood/PluginProcessor.cpp ...)
   ```

3. **Target**, one call:
   ```cmake
   px3_add_product(
       TARGET       PX3Mood
       PRODUCT_NAME "PX3 Mood"
       BUNDLE_ID    com.px3.mood
       PLUGIN_CODE  MdP1
       FORMATS      AU VST3        # no Standalone: an effect does not need one
       SOURCES      ${PX3_MOOD_SOURCES})
   ```

4. **Include path**: add `products/PX3Mood` to `PX3_INCLUDE_DIRS`.

5. **Register it** so the updater and installer can see it:
   ```cpp
   ProductRegistry::Registration mood;
   mood.productId = "px3-mood";
   mood.displayName = "PX3 Mood";
   mood.versionProvider = [] { return px3::version::string(); };
   mood.bundleId = "com.px3.mood";
   mood.hasStandalone = false;
   mood.installerComponentId = "px3.mood";
   ```

6. **Tests** under `tests/Tests/`, covering the wrapper. The DSP is already
   covered by the Synth's suite, and that coverage now protects both consumers.

Nothing in that list touches PX3 Synth.

### Is an effect worth extracting?

Ask what it needs. An effect that reads only its input buffer is a candidate.
One that reaches into per-voice state, the modulation matrix or the Synth's
internal signal path is not a conventional insert, and forcing it into one
would change what it does. Assess before extracting, and it is a perfectly
good answer that something stays Synth-only.

---

## 7. Installer

`scripts/build-release.sh` builds per-product component packages and combines
them with `productbuild`. Products install to the locations hosts expect:

```
/Library/Audio/Plug-Ins/Components     AU
/Library/Audio/Plug-Ins/VST3           VST3
/Applications                          standalone, where a product has one
```

No product depends on another product's install location. The only deliberately
shared installed component is the update helper, which ships inside the
standalone.

---

## 8. Updater

Product-agnostic by construction: it works in terms of product id, installed
version, available version, release and installer, and knows nothing about the
Synth. See `docs/midi-mapping-design.md`'s sibling sections and the update
tests for the detail. Adding a product to the updater is a registry entry.
