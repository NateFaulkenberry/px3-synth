#include "TestSupport.h"

// testCardStyle, testCardInner

namespace px3tests
{


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
            { "gloss.topRadius",   R"({"gloss":{"topRadius":17}})",
              [](const CardStyle& s) { return static_cast<double>(s.gloss.topRadius.resolve(100.0f, 0.0f)); } },
            { "gloss.bottomRadius",R"({"gloss":{"bottomRadius":19}})",
              [](const CardStyle& s) { return static_cast<double>(s.gloss.bottomRadius.resolve(100.0f, 0.0f)); } },
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
        // All three properties are configurable and all three are read.
        juce::String error;
        auto config = UIConfig::fromJsonText(R"({"cards":{
            "defaults":{"disabled":{"saturation":0.0,"dim":0.75,"darken":0.45}},
            "partial":{"disabled":{"saturation":0.6,"dim":0.9,"darken":0.2}}}})", error);

        const auto base = CardStyle::fromConfig(config.get(), "cards.defaults", "cards.defaults");
        const auto partial = CardStyle::fromConfig(config.get(), "cards.defaults", "cards.partial");

        // And darken must actually darken: a white card has no saturation to
        // remove, so without it a bypassed Sub Osc stayed bright white.
        CardStyle white;
        white.border.colour = juce::Colours::white;
        white.disabled = base.disabled;
        const auto bypassed = white.disabledVariant();

        check("CardStyle_DisabledAppearanceIsConfigurable",
              juce::approximatelyEqual(base.disabled.saturation, 0.0f)
                  && juce::approximatelyEqual(base.disabled.dim, 0.75f)
                  && juce::approximatelyEqual(base.disabled.darken, 0.45f)
                  && juce::approximatelyEqual(partial.disabled.saturation, 0.6f)
                  && juce::approximatelyEqual(partial.disabled.dim, 0.9f)
                  && juce::approximatelyEqual(partial.disabled.darken, 0.2f)
                  && bypassed.border.colour.getBrightness() < 0.6f,
              "defaults 0.0/0.75/0.45, override 0.6/0.9/0.2; white bypasses to brightness "
                  + fmt(bypassed.border.colour.getBrightness(), 2));
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


    // ---- per-side padding and margin ---------------------------------------
    {
        // "padding": 4 sets all four edges; "paddingTop": 0 then overrides one.
        // Trimming a single edge is the common case, and rewriting the whole
        // {top,right,bottom,left} object to change one number is not workable.
        auto rowFor = [](const char* json)
        {
            juce::String error;
            const auto config = UIConfig::fromJsonText(json, error);
            CardInner inner;
            inner.setStylePath("cards.probe.cardInner");
            inner.setConfig(config);
            inner.setRowCount(1);
            inner.layout({ 0, 0, 200, 200 });
            return inner.rowContent(0);
        };

        const auto plain = rowFor(R"({"cards":{"probe":{"cardInner":{
            "margin":0,"padding":0,"rows":{"row1":{"height":"100%"}}}}}})");
        check("CardInner_NoPaddingIsTheWholeBox",
              plain.getX() == 0 && plain.getY() == 0
                  && plain.getWidth() == 200 && plain.getHeight() == 200,
              "");

        const auto uniform = rowFor(R"({"cards":{"probe":{"cardInner":{
            "margin":0,"padding":10,"rows":{"row1":{"height":"100%"}}}}}})");
        check("CardInner_GenericPaddingStillSetsAllFourSides",
              uniform.getX() == 10 && uniform.getY() == 10
                  && uniform.getWidth() == 180 && uniform.getHeight() == 180,
              "x " + juce::String(uniform.getX()) + " w " + juce::String(uniform.getWidth()));

        // One side at a time, each on top of a generic value, so the override
        // has to actually replace rather than add.
        struct Case { const char* key; int x; int y; int w; int h; };
        const std::array<Case, 4> cases { {
            { "paddingTop",    10,  0, 180, 190 },
            { "paddingBottom", 10, 10, 180, 190 },
            { "paddingLeft",    0, 10, 190, 180 },
            { "paddingRight",  10, 10, 190, 180 },
        } };

        juce::StringArray wrong;
        for (const auto& c : cases)
        {
            const juce::String json = juce::String(R"({"cards":{"probe":{"cardInner":{
                "margin":0,"padding":10,")") + c.key + R"(":0,"rows":{"row1":{"height":"100%"}}}}}})";
            const auto r = rowFor(json.toRawUTF8());
            if (r.getX() != c.x || r.getY() != c.y || r.getWidth() != c.w || r.getHeight() != c.h)
            {
                wrong.add(juce::String(c.key) + " gave " + r.toString());
            }
        }

        check("CardInner_EachPaddingSideOverridesTheGenericValue", wrong.isEmpty(),
              wrong.isEmpty() ? "paddingTop/Right/Bottom/Left each override padding alone"
                              : wrong.joinIntoString("; "));

        // The same treatment on margin, and on a ROW rather than the container.
        const auto marginSide = rowFor(R"({"cards":{"probe":{"cardInner":{
            "margin":10,"marginLeft":0,"padding":0,"rows":{"row1":{"height":"100%"}}}}}})");
        check("CardInner_MarginSidesWorkToo",
              marginSide.getX() == 0 && marginSide.getWidth() == 190,
              "x " + juce::String(marginSide.getX()) + " w " + juce::String(marginSide.getWidth()));

        const auto rowSide = rowFor(R"({"cards":{"probe":{"cardInner":{
            "margin":0,"padding":0,"rows":{"default":{"padding":8},
            "row1":{"height":"100%","paddingTop":0}}}}}})");
        check("CardInner_ARowCanOverrideOneEdgeOfTheDefaultRow",
              rowSide.getY() == 0 && rowSide.getX() == 8 && rowSide.getHeight() == 192,
              "y " + juce::String(rowSide.getY()) + " x " + juce::String(rowSide.getX())
                  + " h " + juce::String(rowSide.getHeight()));
    }

    // ---- The percentage chain ---------------------------------------------
    {
        const auto config = configFrom(R"({"cards":{"probe":{"cardInner":{
            "margin":0,"padding":0,"direction":"column","gap":0,
            "rows":{"default":{"height":"33%"},
                    "row1":{"height":"30%"},"row2":{"height":"30%"},"row3":{"height":"40%"}}}}}})");

        CardInner inner;
        inner.setStylePath("cards.probe.cardInner");
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
        const auto config = configFrom(R"({"cards":{"probe":{"cardInner":{
            "margin":10,"padding":20,"direction":"column","gap":0,
            "rows":{"row1":{"height":"50%"},"row2":{"height":"50%"}}}}}})");

        CardInner inner;
        inner.setStylePath("cards.probe.cardInner");
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
        const auto config = configFrom(R"({"cards":{"probe":{"cardInner":{
            "margin":0,"padding":{"top":0,"right":15,"bottom":0,"left":15},
            "rows":{"row1":{"height":"100%"}}}}}})");

        CardInner inner;
        inner.setStylePath("cards.probe.cardInner");
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
        const auto config = configFrom(R"({"cards":{"probe":{"cardInner":{
            "margin":0,"padding":0,
            "rows":{"row1":{"height":"100%","margin":5,"padding":10}}}}}})");

        CardInner inner;
        inner.setStylePath("cards.probe.cardInner");
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
        const auto config = configFrom(R"({"cards":{"probe":{"cardInner":{
            "margin":0,"padding":0,
            "rows":{"row1":{"height":"80%"},"row2":{"height":"80%"}}}}}})");

        CardInner inner;
        inner.setStylePath("cards.probe.cardInner");
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
        const auto config = configFrom(R"({"cards":{"probe":{"cardInner":{
            "rows":{"row1":{"direction":"column","wrap":"wrap",
                            "justifyContent":"space-between","alignItems":"flex-start",
                            "alignContent":"flex-end","gap":8}}}}}})");

        CardInner inner;
        inner.setStylePath("cards.probe.cardInner");
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
        const auto config = configFrom(R"({"cards":{"probe":{"cardInner":{
            "margin":0,"padding":0,
            "rows":{"row1":{"height":"100%","gap":20}}}}}})");

        CardInner inner;
        inner.setStylePath("cards.probe.cardInner");
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

    // ---- Oscillator row 2, every mode --------------------------------------
    {
        // The oscillator's macro knobs change count with the mode: 0 for the
        // plain waveforms, 1 for NOISE / PINK NOISE / SUPER SAW / PWM /
        // WAVETABLE, 2 or 3 for the rest. Whatever the count, every knob has to
        // be laid out BY the row - inside its bounds, not overlapping a
        // neighbour, and never larger than the pitch knob it sits beside.
        //
        // This mirrors OscillatorComponent::resized()'s row 2 exactly.
        const auto config = configFrom(R"({"cards":{"probe":{"cardInner":{
            "margin":0,"padding":4,"gap":2,
            "rows":{"row1":{"height":"22%"},"row2":{"height":"36%","gap":4},
                    "row3":{"height":"42%"}}}}}})");

        juce::StringArray problems;
        for (int macroCount = 0; macroCount <= 3; ++macroCount)
        {
            CardInner inner;
            inner.setStylePath("cards.probe.cardInner");
            inner.setConfig(config);
            inner.setRowCount(3);
            inner.layout({ 0, 0, 232, 300 });

            auto flex = inner.rowFlex(1);
            const auto gap = inner.rowGap(1);
            const auto row = inner.rowContent(1);
            const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

            std::vector<float> natural { 72.0f };
            natural.insert(natural.end(), (std::size_t) macroCount, 60.0f);
            const auto widths = px3::ui::fitRowItemWidths(natural, gap.left + gap.right,
                                                          static_cast<float>(juce::jmax(1, row.getWidth())));
            for (const auto width : widths)
            {
                flex.items.add(juce::FlexItem(width, cellHeight).withMargin(gap));
            }
            flex.performLayout(row.toFloat());

            juce::Component pitch;
            pitch.setVisible(true);
            std::vector<std::unique_ptr<juce::Component>> macros;

            const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
            px3::ui::layoutLabelledControl(cell(0), { nullptr, &pitch, nullptr,
                                                      px3::ui::ControlShape::square, 16, 16, 56 },
                                           inner.rowControl(1));

            for (int i = 0; i < macroCount; ++i)
            {
                auto knob = std::make_unique<juce::Component>();
                knob->setVisible(true);
                px3::ui::layoutLabelledControl(cell(i + 1), { nullptr, knob.get(), nullptr,
                                                              px3::ui::ControlShape::square, 18, 0, 56 },
                                               inner.rowControl(1));
                macros.push_back(std::move(knob));
            }

            const auto label = "macros=" + juce::String(macroCount);

            if (! row.contains(pitch.getBounds()))
            {
                problems.add(label + " pitch outside the row");
            }
            for (std::size_t i = 0; i < macros.size(); ++i)
            {
                const auto b = macros[i]->getBounds();
                if (! row.contains(b))
                {
                    problems.add(label + " macro " + juce::String((int) i) + " outside the row");
                }
                if (b.getWidth() > pitch.getWidth())
                {
                    problems.add(label + " macro " + juce::String((int) i) + " bigger than pitch ("
                                 + juce::String(b.getWidth()) + " > " + juce::String(pitch.getWidth()) + ")");
                }
                if (b.intersects(pitch.getBounds()))
                {
                    problems.add(label + " macro " + juce::String((int) i) + " overlaps pitch");
                }
                for (std::size_t j = i + 1; j < macros.size(); ++j)
                {
                    if (b.intersects(macros[j]->getBounds()))
                    {
                        problems.add(label + " macros " + juce::String((int) i) + "/"
                                     + juce::String((int) j) + " overlap");
                    }
                }
            }
        }

        check("CardInner_OscillatorRowHoldsEveryMacroCount",
              problems.isEmpty(),
              problems.isEmpty() ? "0-3 macros all inside the row, no overlaps, none larger than pitch"
                                 : problems.joinIntoString("; "));
    }

    // ---- Top menu bar ------------------------------------------------------
    {
        // The bar's section buttons fill their row: equal shares of the width
        // and its full height. They are the plugin's primary navigation, so
        // they take the strip rather than floating in a band inside it.
        const auto config = configFrom(R"({"topMenu":{"sections":{"flex":{
            "direction":"row","justifyContent":"space-between","alignItems":"stretch","gap":6}}}})");

        const juce::Rectangle<int> row { 0, 0, 300, 40 };
        const auto flexStyle = px3::ui::FlexStyle::readLayered(config.get(), { "topMenu.sections.flex" }, {});
        auto box = flexStyle.toFlexBox();
        const auto gap = flexStyle.gapMargin();

        constexpr int count = 6;
        // Mirrors TopMenuBar: the row is widened by half a gap on each side so
        // the outer half-margins fall outside it and the buttons sit flush.
        const auto laidOutWidth = static_cast<float>(row.getWidth()) + gap.left + gap.right;
        const std::vector<float> natural((std::size_t) count, laidOutWidth / static_cast<float>(count));
        const auto widths = px3::ui::fitRowItemWidths(natural, gap.left + gap.right, laidOutWidth);
        for (const auto w : widths)
        {
            auto item = juce::FlexItem(w, static_cast<float>(row.getHeight())).withMargin(gap);
            item.flexGrow = 1.0f;
            box.items.add(item);
        }
        box.performLayout(row.toFloat().expanded(gap.left, 0.0f));

        juce::StringArray problems;
        juce::Rectangle<float> union_;
        for (int i = 0; i < count; ++i)
        {
            const auto b = box.items.getReference(i).currentBounds;
            union_ = union_.isEmpty() ? b : union_.getUnion(b);

            if (std::abs(b.getHeight() - static_cast<float>(row.getHeight())) > 1.0f)
            {
                problems.add("button " + juce::String(i) + " is "
                             + fmt(b.getHeight(), 1) + "px tall, not the row's "
                             + juce::String(row.getHeight()));
            }
            if (i > 0)
            {
                const auto prev = box.items.getReference(i - 1).currentBounds;
                if (std::abs(b.getWidth() - prev.getWidth()) > 1.5f)
                {
                    problems.add("buttons " + juce::String(i - 1) + "/" + juce::String(i)
                                 + " differ in width");
                }
            }
        }

        // And they span the row, rather than leaving it part-filled.
        if (union_.getWidth() < static_cast<float>(row.getWidth()) - 2.0f)
        {
            problems.add("buttons span only " + fmt(union_.getWidth(), 1)
                         + " of " + juce::String(row.getWidth()) + "px");
        }

        check("TopMenu_SectionButtonsFillTheirRow",
              problems.isEmpty(),
              problems.isEmpty() ? "6 equal buttons, full height, spanning the row"
                                 : problems.joinIntoString("; "));
    }

    {
        // rowHeight 0 means "fill the bar". That is what lets the buttons take
        // the strip's whole height instead of a fixed band inside it.
        const auto config = configFrom(R"({"topMenu":{"layout":{"rowHeight":0}}})");
        const auto rowHeight = config->getInt("topMenu.layout.rowHeight", 32);
        const juce::Rectangle<int> bar { 0, 0, 600, 44 };
        const auto row = rowHeight > 0 ? bar.withSizeKeepingCentre(bar.getWidth(), rowHeight) : bar;

        check("TopMenu_ZeroRowHeightFillsTheBar",
              rowHeight == 0 && row == bar,
              "rowHeight 0 -> row is the full bar (" + row.toString() + ")");
    }

    // ---- The power slot ----------------------------------------------------
    {
        // The power toggle is pinned to cardInner's corner and is NOT part of
        // any row: it must not move when a row's contents change, and it must
        // not consume row space.
        const auto config = configFrom(R"({"cards":{"probe":{"cardInner":{
            "margin":0,"padding":10,"gap":0,
            "power":{"x":-4,"y":-2,"size":25},
            "rows":{"row1":{"height":"50%"},"row2":{"height":"50%"}}}}}})");
        CardInner inner;
        inner.setStylePath("cards.probe.cardInner");
        inner.setConfig(config);
        inner.setRowCount(2);
        inner.layout({ 0, 0, 200, 300 });

        const auto power = inner.powerBounds();
        const auto content = inner.content();
        const auto rowsUntouched = inner.rowContent(0).getHeight() == 140
                                && inner.rowContent(0).getY() == content.getY();

        check("CardInner_PowerSlotIsOutsideTheFlexFlow",
              power.getX() == content.getX() - 4 && power.getY() == content.getY() - 2
                  && power.getWidth() == 25 && power.getHeight() == 25 && rowsUntouched,
              "power at " + power.toString() + ", row 1 still "
                  + juce::String(inner.rowContent(0).getHeight()) + "px at the content top");
    }

    // ---- Level meter -------------------------------------------------------
    {
        // A meter must actually reach empty when the signal stops.
        //
        // Its fall is exponential, so it approaches zero without arriving, and
        // the first segment lights for ANY level above zero - which left one
        // green lamp on indefinitely after a note ended.
        MixerLevelMeter meter;

        // Ring it up to full, then feed silence.
        for (int i = 0; i < 60; ++i)
        {
            meter.setLevel(1.0f);
        }

        auto framesToSilence = -1;
        for (int i = 0; i < 600; ++i)
        {
            meter.setLevel(0.0f);
            if (meter.displayLevelForTest() <= 0.0f)
            {
                framesToSilence = i;
                break;
            }
        }

        // At 30 Hz, 600 frames is 20 seconds - far longer than any meter should
        // take, so this only fails if it never gets there at all.
        check("Meter_ClearsCompletelyWhenTheSignalStops",
              framesToSilence >= 0,
              framesToSilence >= 0
                  ? "empty after " + juce::String(framesToSilence) + " frames of silence"
                  : "still lit after 600 frames");
    }

    // ---- Keyword spellings -------------------------------------------------
    {
        // "flex-start" is the CSS spelling and the one to reach for. But every
        // property NAME here is camelCase, so "flexStart" is the natural guess
        // for a value, and it used to fall back silently. All spellings that
        // differ only by case or separators resolve to the same thing.
        auto justifyFor = [&](const char* spelling)
        {
            const juce::String json = juce::String(R"({"cards":{"probe":{"cardInner":{"rows":{"row1":{"justifyContent":")")
                                    + spelling + R"("}}}}}})";
            juce::String error;
            auto config = UIConfig::fromJsonText(json, error);
            CardInner inner;
            inner.setStylePath("cards.probe.cardInner");
            inner.setConfig(std::move(config));
            inner.setRowCount(1);
            inner.layout({ 0, 0, 200, 100 });
            return inner.style().rows[0].flex.justifyContent;
        };

        const auto kebab = justifyFor("flex-start");
        const auto camel = justifyFor("flexStart");
        const auto snake = justifyFor("flex_start");
        const auto bare  = justifyFor("start");
        const auto typo  = justifyFor("flexstrat");

        check("CardInner_KeywordSpellingsAreEquivalent",
              kebab == JustifyContent::start && camel == kebab && snake == kebab && bare == kebab
                  && typo == JustifyContent::centre,
              "flex-start / flexStart / flex_start / start all agree; an unknown value falls back");
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
        const auto config = configFrom(R"({"cards":{"probe":{"cardInner":{
            "margin":0,"padding":0,"gap":0,
            "rows":{"row1":{"height":"100%","wrap":"wrap","gap":6}}}}}})");
        CardInner inner;
        inner.setStylePath("cards.probe.cardInner");
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
        const px3::ui::ControlStyle style;
        px3::ui::layoutLabelledControl(cell, { nullptr, &knob, nullptr,
                                               px3::ui::ControlShape::square, 0, 0, 0 }, style);
        px3::ui::layoutLabelledControl(cell, { nullptr, &dropdown, nullptr,
                                               px3::ui::ControlShape::stretch, 0, 0, 24 }, style);

        check("CardInner_ControlShapeDecidesKnobVersusDropdown",
              knob.getWidth() == 60 && knob.getHeight() == 60
                  && dropdown.getWidth() == 120 && dropdown.getHeight() == 24,
              "knob " + knob.getBounds().toString() + ", dropdown " + dropdown.getBounds().toString());
    }

    {
        // Label above, control, readout below - stacked as a GROUP and centred,
        // not label-pinned-top and readout-pinned-bottom. The distance between
        // a label and its control is now `control.gap`, a value, rather than
        // whatever space happened to be left over.
        juce::Component label, knob, readout;
        label.setVisible(true);
        knob.setVisible(true);
        readout.setVisible(true);

        px3::ui::ControlStyle tight;
        tight.gap = 4.0f;

        px3::ui::layoutLabelledControl({ 0, 0, 100, 100 },
                                       { &label, &knob, &readout,
                                         px3::ui::ControlShape::square, 16, 14, 30 },
                                       tight);

        const auto labelToKnob = knob.getY() - label.getBottom();
        const auto knobToReadout = readout.getY() - knob.getBottom();
        const auto group = label.getBounds().getUnion(readout.getBounds());
        const auto centred = std::abs(group.getCentreY() - 50) <= 1;

        check("CardInner_ControlGapIsAValueNotLeftoverSpace",
              labelToKnob == 4 && knobToReadout == 4 && centred
                  && knob.getWidth() == 30 && knob.getHeight() == 30,
              "gaps " + juce::String(labelToKnob) + "/" + juce::String(knobToReadout)
                  + "px, group centred at " + juce::String(group.getCentreY()));
    }

    {
        // space-between reproduces the old spread exactly - label at the top,
        // readout at the bottom, control between - so nothing was lost by
        // defaulting to centred.
        juce::Component label, knob, readout;
        label.setVisible(true);
        knob.setVisible(true);
        readout.setVisible(true);

        px3::ui::ControlStyle spread;
        spread.gap = 0.0f;
        spread.justifyContent = px3::ui::JustifyContent::spaceBetween;

        px3::ui::layoutLabelledControl({ 0, 0, 100, 100 },
                                       { &label, &knob, &readout,
                                         px3::ui::ControlShape::square, 16, 14, 30 },
                                       spread);

        check("CardInner_SpaceBetweenReproducesTheOldSpread",
              label.getY() == 0 && readout.getBottom() == 100
                  && knob.getY() > label.getBottom() && knob.getBottom() < readout.getY(),
              "label at " + juce::String(label.getY()) + ", knob at " + juce::String(knob.getY())
                  + ", readout ends at " + juce::String(readout.getBottom()));
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
        const juce::String baseInner = R"("power":{"x":1,"y":2,"size":20},
            "margin":2,"padding":3,"display":"flex","direction":"column",
            "wrap":"nowrap","justifyContent":"center","alignItems":"center","alignContent":"center","gap":4,
            "rows":{"row1":{"height":"20%","margin":1,"padding":1,"display":"flex","direction":"row",
                            "wrap":"nowrap","justifyContent":"center","alignItems":"center",
                            "alignContent":"center","gap":3,
                            "control":{"direction":"column","justifyContent":"center",
                                       "alignItems":"center","gap":4,
                                       "labelHeight":8,"readoutHeight":9,"size":11}},
                    "row2":{"height":"20%"},"row3":{"height":"20%"}}})";

        auto fingerprint = [&](const juce::String& innerJson)
        {
            const auto config = configFrom((R"({"cards":{"probe":{"cardInner":{)"
                                            + innerJson + R"(}}}})").toRawUTF8());
            CardInner inner;
            inner.setStylePath("cards.probe.cardInner");
            inner.setConfig(config);
            inner.setRowCount(3);
            inner.layout({ 0, 0, 240, 300 });

            juce::String out = inner.content().toString() + "|" + inner.powerBounds().toString();
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

            // The control style is layout too, and it reaches the screen through
            // layoutLabelledControl - so the fingerprint has to lay a cell out.
            const auto& cs = style.rows[(size_t) i].control;
            juce::Component label, control, readout;
            label.setVisible(true); control.setVisible(true); readout.setVisible(true);
            px3::ui::layoutLabelledControl(inner.rowContent(i),
                                           { &label, &control, &readout,
                                             px3::ui::ControlShape::square, 12, 12, 0 },
                                           cs);
            out += "|" + label.getBounds().toString() + "/" + control.getBounds().toString()
                 + "/" + readout.getBounds().toString();
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
            { "control.direction",      R"("direction":"row")" },
            { "control.justifyContent", R"("justifyContent":"flex-start")" },
            { "control.alignItems",     R"("alignItems":"flex-end")" },
            { "control.gap",            R"("gap":15)" },
            { "control.labelHeight",    R"("labelHeight":21)" },
            { "control.readoutHeight",  R"("readoutHeight":23)" },
            { "control.size",           R"("size":19)" },
            { "power.x",    R"("x":13)" },
            { "power.y",    R"("y":14)" },
            { "power.size", R"("size":31)" },
        };

        juce::StringArray inert;
        for (const auto& probe : probes)
        {
            juce::String variant = baseInner;
            if (probe.first.startsWith("power."))
            {
                const auto key = probe.first.fromFirstOccurrenceOf(".", false, false);
                const auto blockStart = variant.indexOf("\"power\"");
                const auto keyStart = variant.indexOf(blockStart, "\"" + key + "\":");
                const auto keyEnd = variant.indexOfAnyOf(",}", keyStart, false);
                if (blockStart < 0 || keyStart < 0 || keyEnd < 0)
                {
                    inert.add(probe.first + " (probe did not match)");
                    continue;
                }
                variant = variant.substring(0, keyStart) + probe.second + variant.substring(keyEnd);
            }
            else if (probe.first.startsWith("control."))
            {
                const auto key = probe.first.fromFirstOccurrenceOf(".", false, false);
                const auto blockStart = variant.indexOf("\"control\"");
                const auto keyStart = variant.indexOf(blockStart, "\"" + key + "\":");
                const auto keyEnd = variant.indexOfAnyOf(",}", keyStart, false);
                if (blockStart < 0 || keyStart < 0 || keyEnd < 0)
                {
                    inert.add(probe.first + " (probe did not match)");
                    continue;
                }
                variant = variant.substring(0, keyStart) + probe.second + variant.substring(keyEnd);
            }
            else if (probe.first.startsWith("row."))
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
        const auto config = configFrom(R"({"cards":{"probe":{"cardInner":{
            "margin":0,"padding":0,"gap":0,
            "rows":{"row1":{"height":"25%"},
                    "row2":{"height":"50%","display":"none"},
                    "row3":{"height":"25%"}}}}}})");
        CardInner inner;
        inner.setStylePath("cards.probe.cardInner");
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
        const auto config = configFrom(R"({"cards":{"probe":{}}})");
        CardInner inner;
        inner.setStylePath("cards.probe.cardInner");
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
            const juce::String json = juce::String(R"({"cards":{"probe":{"cardInner":{
                "margin":0,"padding":0,"rows":{"row1":{"height":")") + h1 + R"("},"row2":{"height":"10%"}}}}}})";
            return UIConfig::fromJsonText(json, error);
        };

        CardInner inner;
        inner.setStylePath("cards.probe.cardInner");
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

} // namespace px3tests
