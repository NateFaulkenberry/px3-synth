// Renders each FX card's BACKGROUND to a PNG, with nothing on it.
//
// For designing artwork to layer into the inner card background: to draw
// something that sits inside a card you need the card's own shape and palette
// as a starting point, at the size it is actually drawn, with its rounded
// corners transparent rather than matted onto a guessed colour.
//
// The backgrounds come from the components themselves rather than from a
// description of them. FxCardComponent::paint draws the card and nothing else -
// its knobs, labels, boxes and power button are child components, and painting
// a component does not paint its children - so calling paint() on one is
// exactly "the card with nothing on it". Delay and Mood are not cards but paint
// their own card the same way, so they export through the same call.
//
// Titles are left off: the cards are constructed with an empty title, so the
// title bar's styling is present and its text is not.

#include <JuceHeader.h>

#include "FxCardComponent.h"
#include "DelayComponent.h"
#include "MoodComponent.h"
#include "UIConfig.h"
#include "UIConfigManager.h"
#include "Card.h"

#include "../products/PX3Reverb/PluginProcessor.h"
#include "../products/PX3Doom/PluginProcessor.h"
#include "../products/PX3Lucy/PluginProcessor.h"
#include "../products/PX3Chorus/PluginProcessor.h"
#include "../products/PX3Spread/PluginProcessor.h"
#include "../products/PX3Delay/PluginProcessor.h"
#include "../products/PX3Mood/PluginProcessor.h"

namespace
{
// The size a card is drawn at: one cell of the Synth's FX grid, which is also
// what a standalone effect's window opens at.
constexpr int kCardWidth = 318;
constexpr int kCardHeight = 500;

// Rendered at 2x and left at that size. Artwork drawn over these will be
// scaled by whatever uses it, and starting from more pixels than the card has
// costs nothing here and cannot be recovered later.
constexpr int kScale = 2;

std::shared_ptr<const UIConfig> loadConfig(juce::String& error)
{
    const auto file = UIConfigManager::findShippingConfigFile();
    if (! file.existsAsFile())
    {
        error = "no UIConfig.json found - every card would export in code defaults";
        return {};
    }

    return UIConfig::fromJsonText(file.loadFileAsString(), error);
}

// The ground an FX card is drawn on - the Synth's FX page, and the window a
// standalone effect opens. Cards are translucent over it: Reverb's background
// is 12% opaque, its border 59%. Rendered onto nothing those fills composite
// against nothing and the palette washes out to almost white, which is the
// card's alpha faithfully reproduced and not the card as anyone has seen it.
const juce::Colour kPanelGround = juce::Colour::fromRGB(18, 20, 24);

bool writePng(juce::Component& component,
              const juce::String& styleKey,
              const juce::File& destination,
              const std::shared_ptr<const UIConfig>& config)
{
    // Transparent, and cleared: the corners a card rounds off must come out as
    // alpha rather than as whatever colour happened to be behind them.
    juce::Image image(juce::Image::ARGB, kCardWidth * kScale, kCardHeight * kScale, true);

    {
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(static_cast<float>(kScale)));
        component.setBounds(0, 0, kCardWidth, kCardHeight);

        // The ground, but only INSIDE the card's own shape, so the palette is
        // the one on screen while everything outside the rounded rectangle
        // stays transparent.
        //
        // The shape comes from a CardHost of our own, given the same style key
        // and config the component gives its: that is how the component works
        // out where to draw, so it is the same rectangle and the same radius
        // rather than a guess that has to be kept in step.
        px3::ui::CardHost shape;
        shape.setStyleKey(styleKey);
        shape.setConfig(config);
        shape.layout({ 0, 0, kCardWidth, kCardHeight });

        g.setColour(kPanelGround);
        g.fillRoundedRectangle(shape.bounds(), shape.style().border.radius);

        // paint(), not paintEntireComponent(): the second walks the children,
        // and the children are the controls this is meant to be without.
        component.paint(g);
    }

    destination.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(destination.createOutputStream());
    if (stream == nullptr) { return false; }

    juce::PNGImageFormat png;
    return png.writeImageToStream(image, *stream);
}

// How much of the image is actually painted, and whether the corners came out
// transparent. Reported per card because "it wrote a file" is not the same as
// "it wrote the card".
juce::String describe(const juce::File& file)
{
    juce::PNGImageFormat png;
    std::unique_ptr<juce::FileInputStream> stream(file.createInputStream());
    if (stream == nullptr) { return "unreadable"; }

    const auto image = png.decodeImage(*stream);
    if (! image.isValid()) { return "not a valid image"; }

    auto opaque = 0;
    auto total = 0;
    for (int y = 0; y < image.getHeight(); y += 4)
    {
        for (int x = 0; x < image.getWidth(); x += 4)
        {
            ++total;
            if (image.getPixelAt(x, y).getAlpha() > 8) { ++opaque; }
        }
    }

    const auto corner = image.getPixelAt(0, 0).getAlpha();
    const auto centre = image.getPixelAt(image.getWidth() / 2, image.getHeight() / 2);

    return juce::String(image.getWidth()) + "x" + juce::String(image.getHeight())
         + ", " + juce::String(total > 0 ? (opaque * 100) / total : 0) + "% painted"
         + ", corner alpha " + juce::String(corner)
         + ", centre " + centre.toDisplayString(true);
}
} // namespace

// The card WITH its controls, for checking a layout by looking at it.
//
// Separate from the background export and off by default: the backgrounds are
// the deliverable, and a render with knobs on it is a working tool for the
// person changing where those knobs go.
bool writePreview(juce::AudioProcessorEditor& editor, const juce::File& destination)
{
    const auto bounds = editor.getLocalBounds();
    if (bounds.isEmpty()) { return false; }

    juce::Image image(juce::Image::ARGB, bounds.getWidth() * kScale, bounds.getHeight() * kScale, true);

    {
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(static_cast<float>(kScale)));
        // Everything this time: the controls are the point.
        editor.paintEntireComponent(g, true);
    }

    destination.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(destination.createOutputStream());
    if (stream == nullptr) { return false; }

    juce::PNGImageFormat png;
    return png.writeImageToStream(image, *stream);
}

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // The first argument that is not a flag is the output directory. Taking
    // argv[1] blindly wrote the renders into a folder called "--preview".
    juce::String outputName = "card-backgrounds";
    for (int i = 1; i < argc; ++i)
    {
        const juce::String argument(argv[i]);
        if (! argument.startsWith("--")) { outputName = argument; break; }
    }

    const auto output = juce::File::getCurrentWorkingDirectory().getChildFile(outputName);
    output.createDirectory();

    juce::String error;
    const auto config = loadConfig(error);
    if (config == nullptr)
    {
        std::cout << "error: " << error << std::endl;
        return 1;
    }

    std::cout << "writing to " << output.getFullPathName() << std::endl;

    auto failures = 0;

    // The five card-shaped effects. Empty titles, no rows: a card with rows
    // still paints the same background, but declaring none makes it plain that
    // nothing but the card is being drawn.
    const std::array<std::pair<const char*, const char*>, 5> cards { {
        { "doom", "PX3-Doom" },
        { "lucy", "PX3-Lucy" },
        { "chorus", "PX3-Chorus" },
        { "stereoSpread", "PX3-Spread" },
        { "reverb", "PX3-Reverb" },
    } };

    for (const auto& [styleKey, name] : cards)
    {
        px3::ui::FxCardComponent card(styleKey, {});
        card.setUIConfig(config);

        const auto file = output.getChildFile(juce::String(name) + "-background.png");
        if (writePng(card, styleKey, file, config)) { std::cout << "  " << name << "  " << describe(file) << std::endl; }
        else                      { std::cout << "  " << name << "  FAILED" << std::endl; ++failures; }
    }

    // Delay and Mood paint their own card. Their components take references to
    // the controls they lay out, so the controls have to exist - they are never
    // drawn, because paint() does not touch children.
    {
        juce::ToggleButton bypass;
        juce::Slider amount, time, feedback;
        juce::Label amountLabel, timeLabel, feedbackLabel;
        juce::ComboBox algorithm, sync, mode;
        juce::Label algorithmLabel, syncLabel, modeLabel;

        DelayComponent delay(bypass, amount, amountLabel, algorithm, algorithmLabel,
                                      sync, syncLabel, mode, modeLabel,
                                      time, timeLabel, feedback, feedbackLabel,
                                      juce::Colour::fromRGB(132, 210, 255));
        delay.setUIConfig(config);

        const auto file = output.getChildFile("PX3-Delay-background.png");
        if (writePng(delay, "delay", file, config)) { std::cout << "  PX3-Delay  " << describe(file) << std::endl; }
        else                       { std::cout << "  PX3-Delay  FAILED" << std::endl; ++failures; }
    }

    {
        juce::ToggleButton bypass;
        px3::ui::ToggleChipButton freeze;
        juce::Slider mix, clock, wetTime, wetModify, loopLength, loopModify, feedback, spread, degrade;
        juce::Label mixL, clockL, wetTimeL, wetModifyL, loopLengthL, loopModifyL, feedbackL, spreadL, degradeL;
        juce::ComboBox routing, wetMode, loopMode;
        juce::Label routingL, wetModeL, loopModeL;

        MoodComponent mood(bypass, freeze,
                                    mix, mixL, clock, clockL,
                                    wetTime, wetTimeL, wetModify, wetModifyL,
                                    loopLength, loopLengthL, loopModify, loopModifyL,
                                    feedback, feedbackL, spread, spreadL,
                                    degrade, degradeL,
                                    routing, routingL, wetMode, wetModeL,
                                    loopMode, loopModeL,
                                    juce::Colour::fromRGB(198, 140, 255));
        mood.setUIConfig(config);

        const auto file = output.getChildFile("PX3-Mood-background.png");
        if (writePng(mood, "mood", file, config)) { std::cout << "  PX3-Mood  " << describe(file) << std::endl; }
        else                      { std::cout << "  PX3-Mood  FAILED" << std::endl; ++failures; }
    }

    // --preview also renders each standalone effect complete, so a layout
    // change can be looked at rather than inferred from coordinates.
    auto wantsPreview = false;
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String(argv[i]) == "--preview") { wantsPreview = true; }
    }

    if (wantsPreview)
    {
        const auto preview = [&](const juce::String& name, juce::AudioProcessor& processor)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            if (editor == nullptr) { return; }

            const auto file = output.getChildFile(name + "-preview.png");
            std::cout << "  " << name << " preview  "
                      << (writePreview(*editor, file) ? describe(file) : juce::String("FAILED"))
                      << std::endl;
        };

        // Bypassed as well as active, so "does artwork grey out" is a thing
        // that can be looked at rather than reasoned about.
        {
            PX3LucyAudioProcessor bypassed;
            for (auto* parameter : bypassed.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                {
                    if (ranged->getParameterID() == "lucyEnabled") { ranged->setValueNotifyingHost(0.0f); }
                    if (ranged->getParameterID() == "lucyGlobal")  { ranged->setValueNotifyingHost(1.0f); }
                }
            }

            std::unique_ptr<juce::AudioProcessorEditor> editor(bypassed.createEditor());
            if (editor != nullptr)
            {
                const auto file = output.getChildFile("PX3-Lucy-bypassed.png");
                std::cout << "  PX3-Lucy bypassed  "
                          << (writePreview(*editor, file) ? describe(file) : juce::String("FAILED"))
                          << std::endl;
            }
        }

        PX3ReverbAudioProcessor reverb;   preview("PX3-Reverb", reverb);
        PX3DoomAudioProcessor doom;       preview("PX3-Doom", doom);
        PX3LucyAudioProcessor lucy;       preview("PX3-Lucy", lucy);
        PX3ChorusAudioProcessor chorus;   preview("PX3-Chorus", chorus);
        PX3SpreadAudioProcessor spread;   preview("PX3-Spread", spread);
        PX3DelayAudioProcessor delayFx;   preview("PX3-Delay", delayFx);
        PX3MoodAudioProcessor moodFx;     preview("PX3-Mood", moodFx);
    }

    std::cout << (failures == 0 ? "all backgrounds written" : "some backgrounds failed") << std::endl;
    return failures == 0 ? 0 : 1;
}
