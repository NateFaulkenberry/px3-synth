#include "OscillatorComponent.h"

#include "CardInner.h"

#include "UIConfig.h"

#include <cmath>
#include <vector>

OscillatorComponent::OscillatorComponent(juce::ToggleButton& enabledButtonIn,
                                                                                                             juce::Label& enabledLabelIn,
                                                                                                             juce::Slider& pitchIn,
                                                                                                             juce::Label& pitchLabelIn,
                                                                                                             juce::Label& pitchValueLabelIn,
                                                                                                             juce::Slider& macroAIn,
                                                                                                             juce::Slider& macroBIn,
                                                                                                             juce::Slider& macroCIn,
                                                                                                             juce::Label& macroALabelIn,
                                                                                                             juce::Label& macroBLabelIn,
                                                                                                             juce::Label& macroCLabelIn,
                                                                                                             juce::ComboBox& modeBoxIn,
                                                                                                             juce::Label& modeLabelIn,
                                                                                                             juce::ComboBox& vowelBoxIn,
                                                                                                             juce::Label& vowelLabelIn,
                                                                                                             juce::Colour accentIn)
        : enabledButton(enabledButtonIn),
            enabledLabel(enabledLabelIn),
            pitch(pitchIn),
            pitchLabel(pitchLabelIn),
            pitchValueLabel(pitchValueLabelIn),
            macroA(macroAIn),
      macroB(macroBIn),
      macroC(macroCIn),
      macroALabel(macroALabelIn),
      macroBLabel(macroBLabelIn),
      macroCLabel(macroCLabelIn),
      modeBox(modeBoxIn),
      modeLabel(modeLabelIn),
      vowelBox(vowelBoxIn),
      vowelLabel(vowelLabelIn),
      accent(accentIn)
{
        addAndMakeVisible(enabledButton);
        addAndMakeVisible(enabledLabel);
    addAndMakeVisible(pitch);
    addAndMakeVisible(pitchLabel);
    addAndMakeVisible(pitchValueLabel);
    addAndMakeVisible(macroA);
    addAndMakeVisible(macroB);
    addAndMakeVisible(macroC);
    addAndMakeVisible(macroALabel);
    addAndMakeVisible(macroBLabel);
    addAndMakeVisible(macroCLabel);
    addAndMakeVisible(modeBox);
    addAndMakeVisible(modeLabel);
    addAndMakeVisible(vowelBox);
    addAndMakeVisible(vowelLabel);

    applyModeUi();
    applyEnabledUi();
}

void OscillatorComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void OscillatorComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    card.setConfig(uiConfig);
    resized();
    repaint();
}

void OscillatorComponent::refreshFromParameters(bool enabled, int modeIndex, int vowelIndex)
{
    currentEnabled = enabled;
    enabledButton.setToggleState(enabled, juce::dontSendNotification);

    if (modeBox.getSelectedItemIndex() != modeIndex)
    {
        modeBox.setSelectedItemIndex(modeIndex, juce::dontSendNotification);
    }

    if (vowelBox.getSelectedItemIndex() != vowelIndex)
    {
        vowelBox.setSelectedItemIndex(vowelIndex, juce::dontSendNotification);
    }

    if (modeIndex != lastModeIndex)
    {
        applyModeUi();
    }

    applyEnabledUi();
}

void OscillatorComponent::advanceAnimation(float deltaPhase)
{
    if (!currentEnabled)
    {
        return;
    }

    phase += deltaPhase;
    if (phase > juce::MathConstants<float>::twoPi)
    {
        phase -= juce::MathConstants<float>::twoPi;
    }

    repaint();
}

void OscillatorComponent::resized()
{
    // Per-instance style block, so "cards.osc2" can differ from "cards.osc1"
    // while both run this same code.
    card.setStyleKey("osc" + juce::String(instanceIndex));
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    // Per component TYPE, not per instance: osc1, osc2 and osc3 share one
    // layout definition and cannot drift apart.
    inner.setStylePath("cards.osc.cardInner");
    inner.setConfig(uiConfig);
    inner.setRowCount(3);
    inner.layout(card.contentBelowTitle());

    using px3::ui::ControlShape;

    // Row 1: bypass and mode, plus the vowel selector on the modes that have
    // one. Visibility is behaviour, not style, so a hidden vowel box drops out
    // of the row here rather than through a `display` property in UIConfig -
    // and the two remaining controls take the width back.
    {
        auto flex = inner.rowFlex(0);
        const auto gap = inner.rowGap(0);
        const auto row = inner.rowContent(0);
        const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));
        const auto showVowel = vowelBox.isVisible();

        flex.items.add(juce::FlexItem(46.0f, cellHeight).withMargin(gap));
        flex.items.add(juce::FlexItem(showVowel ? 84.0f : 116.0f, cellHeight).withMargin(gap));
        if (showVowel)
        {
            flex.items.add(juce::FlexItem(84.0f, cellHeight).withMargin(gap));
        }
        flex.performLayout(row.toFloat());

        const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
        px3::ui::layoutLabelledControl(cell(0), &enabledLabel, &enabledButton, nullptr,
                                       14, 0, ControlShape::square, 22);
        px3::ui::layoutLabelledControl(cell(1), &modeLabel, &modeBox, nullptr,
                                       14, 0, ControlShape::stretch, 24);
        if (showVowel)
        {
            px3::ui::layoutLabelledControl(cell(2), &vowelLabel, &vowelBox, nullptr,
                                           14, 0, ControlShape::stretch, 24);
        }
        else
        {
            vowelLabel.setBounds(0, 0, 0, 0);
            vowelBox.setBounds(0, 0, 0, 0);
        }
    }

    // Row 2: the pitch knob and whichever macro knobs the current mode uses.
    // The macros share the space left over, which is what the hand-rolled
    // even-split this component used to do by hand.
    {
        auto flex = inner.rowFlex(1);
        const auto gap = inner.rowGap(1);
        const auto row = inner.rowContent(1);
        const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

        const std::array<juce::Slider*, 3> macroSliders { &macroA, &macroB, &macroC };
        const std::array<juce::Label*, 3> macroLabels { &macroALabel, &macroBLabel, &macroCLabel };

        flex.items.add(juce::FlexItem(72.0f, cellHeight).withMargin(gap));

        std::vector<int> visibleMacros;
        for (int i = 0; i < 3; ++i)
        {
            if (macroSliders[static_cast<std::size_t>(i)]->isVisible())
            {
                visibleMacros.push_back(i);
                auto item = juce::FlexItem(60.0f, cellHeight).withMargin(gap);
                item.flexGrow = 1.0f;
                flex.items.add(item);
            }
        }

        flex.performLayout(row.toFloat());

        const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
        px3::ui::layoutLabelledControl(cell(0), &pitchLabel, &pitch, &pitchValueLabel,
                                       16, 16, ControlShape::square, 56);

        for (std::size_t i = 0; i < visibleMacros.size(); ++i)
        {
            const auto index = static_cast<std::size_t>(visibleMacros[i]);
            px3::ui::layoutLabelledControl(cell(static_cast<int>(i) + 1),
                                           macroLabels[index], macroSliders[index], nullptr,
                                           18, 0, ControlShape::square, 86);
        }
    }

    // Row 3 is the wave graph, which paint() draws rather than a child
    // component owning it.
}

void OscillatorComponent::setInstanceIndex(int oneBasedIndex)
{
    const auto clamped = juce::jlimit(1, 8, oneBasedIndex);
    if (instanceIndex != clamped)
    {
        instanceIndex = clamped;
        resized();
        repaint();
    }
}

void OscillatorComponent::setPanelContentBounds(juce::Rectangle<int> panelContent)
{
    card.setPanelContentBounds(panelContent);
    resized();
    repaint();
}

void OscillatorComponent::paint(juce::Graphics& g)
{
    // Same card implementation as Sub Osc, differing only in configuration and
    // in what the component puts inside it. The title is the component's own
    // content and is drawn by the card, not by the parent panel.
    const auto effectiveAccent = currentEnabled ? accent : juce::Colour::fromRGBA(150, 150, 150, 180);

    const auto title = "OSC " + juce::String(instanceIndex);
    if (currentEnabled)
    {
        card.draw(g, title);
    }
    else
    {
        card.drawInactive(g, title);
    }

    // The graph is row 3. This used to re-derive the whole vertical stack that
    // resized() had just walked, including the vowel-box branch, which meant
    // two copies of one layout kept in step by hand.
    const auto graph = inner.rowContent(2).toFloat().reduced(0.0f, 2.0f);

    if (graph.getWidth() <= 40 || graph.getHeight() <= 24)
    {
        return;
    }

    const auto modeIndex = juce::jmax(0, modeBox.getSelectedItemIndex());
    const auto macroAValue = static_cast<float>(macroA.getValue());
    const auto macroBValue = static_cast<float>(macroB.getValue());
    const auto macroCValue = static_cast<float>(macroC.getValue());

    g.setColour(juce::Colour::fromRGBA(12, 16, 26, 170));
    g.fillRoundedRectangle(graph, 7.0f);
    g.setColour(effectiveAccent.withAlpha(0.32f));
    g.drawRoundedRectangle(graph, 7.0f, 1.0f);

    const auto left = graph.getX() + 6.0f;
    const auto right = graph.getRight() - 6.0f;
    const auto top = graph.getY() + 5.0f;
    const auto bottom = graph.getBottom() - 5.0f;
    const auto mid = (top + bottom) * 0.5f;
    const auto width = juce::jmax(1.0f, right - left);
    const auto height = juce::jmax(1.0f, bottom - top);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 28));
    for (int gx = 1; gx < 6; ++gx)
    {
        const auto x = left + width * (static_cast<float>(gx) / 6.0f);
        g.drawLine(x, top, x, bottom, 0.7f);
    }
    g.drawLine(left, mid, right, mid, 0.9f);

    juce::Path wave;
    wave.startNewSubPath(left, mid);

    for (int s = 0; s <= 72; ++s)
    {
        const auto t = static_cast<float>(s) / 72.0f;
        const auto samplePhase = t * juce::MathConstants<float>::twoPi + phase;
        float y = 0.0f;

        switch (modeIndex)
        {
            case 0:
                y = std::sin(samplePhase);
                break;
            case 1:
                y = 2.0f * t - 1.0f;
                break;
            case 2:
                y = std::sin(samplePhase) >= 0.0f ? 1.0f : -1.0f;
                break;
            case 3:
                y = 1.0f - 4.0f * std::abs(t - 0.5f);
                break;
            case 4:
            case 5:
            {
                const auto n = std::sin(samplePhase * 13.0f + t * 31.0f) * 0.7f
                               + std::sin(samplePhase * 29.0f + t * 19.0f) * 0.3f;
                y = modeIndex == 5 ? n * 0.55f : n;
                break;
            }
            case 6:
                y = (std::sin(samplePhase)
                     + std::sin(samplePhase * (1.0f + 0.04f + macroAValue * 0.2f))
                     + std::sin(samplePhase * (1.0f - 0.05f - macroAValue * 0.18f)))
                    * 0.33f;
                break;
            case 7:
            {
                const auto widthNorm = juce::jlimit(0.1f, 0.9f, 0.1f + macroAValue * 0.8f);
                y = t < widthNorm ? 1.0f : -1.0f;
                break;
            }
            case 8:
                y = std::sin(samplePhase * (1.0f + macroAValue * 4.0f)) * 0.6f
                    + std::sin(samplePhase * (3.0f + macroBValue * 5.0f)) * 0.35f;
                break;
            case 9:
            case 18:
                y = std::sin(samplePhase) * 0.62f + std::sin(samplePhase * 2.0f) * 0.22f + std::sin(samplePhase * 3.0f) * 0.16f;
                break;
            case 10:
                y = std::sin(samplePhase) * 0.45f + std::sin(samplePhase * (2.0f + macroBValue * 2.0f)) * 0.33f + std::sin(samplePhase * 4.0f) * 0.22f;
                break;
            case 11:
                y = std::sin(samplePhase + std::sin(samplePhase * (1.0f + macroAValue * 4.0f)) * (macroBValue * 3.0f));
                break;
            case 12:
                y = std::sin(std::fmod(samplePhase * (1.0f + macroAValue * 6.0f), juce::MathConstants<float>::twoPi));
                break;
            case 13:
            case 15:
            case 16:
                y = std::sin(samplePhase * (1.0f + macroAValue * 2.0f)) * (0.7f - t * 0.35f)
                    + std::sin(samplePhase * (5.0f + macroBValue * 8.0f)) * 0.18f;
                break;
            case 14:
                y = std::sin(samplePhase) * 0.55f + std::sin(samplePhase * 2.0f) * 0.30f + std::sin(samplePhase * 4.0f) * 0.15f;
                break;
            case 17:
                y = std::sin(samplePhase * (1.0f + macroBValue * 1.8f)) * 0.6f + std::sin(samplePhase * 7.0f + t * 9.0f) * 0.25f;
                break;
            case 19:
                y = std::sin(samplePhase * (1.0f + macroAValue * 2.3f)
                             + std::sin(samplePhase * (2.0f + macroBValue * 5.0f)) * (0.7f + macroCValue * 2.4f));
                break;
            default:
                y = std::sin(samplePhase);
                break;
        }

        y = juce::jlimit(-1.0f, 1.0f, y);
        const auto px = left + t * width;
        const auto py = mid - y * (height * 0.40f);
        if (s == 0)
        {
            wave.startNewSubPath(px, py);
        }
        else
        {
            wave.lineTo(px, py);
        }
    }

    const auto glowAlpha = juce::jlimit(0.20f, 0.86f, 0.20f + 0.22f * (macroAValue + macroBValue + macroCValue));
    g.setColour(effectiveAccent.withAlpha(juce::jlimit(0.2f, 1.0f, glowAlpha * 0.6f)));
    g.strokePath(wave,
                 juce::PathStrokeType(3.0f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
    const auto waveDetailColour = currentEnabled ? juce::Colour::fromRGB(170, 228, 255)
                                                 : juce::Colour::fromRGB(178, 178, 178);
    g.setColour(waveDetailColour);
    g.strokePath(wave,
                 juce::PathStrokeType(1.35f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 120));
    g.setFont(juce::FontOptions(10.0f));
    auto titleRow = graph.toNearestInt().removeFromTop(14);
    g.drawText("Mode Visual", titleRow, juce::Justification::centredTop);
}

void OscillatorComponent::applyModeUi()
{
    const auto modeIndex = juce::jmax(0, modeBox.getSelectedItemIndex());
    if (modeIndex == lastModeIndex)
    {
        return;
    }

    lastModeIndex = modeIndex;

    struct ModeUi
    {
        const char* a;
        const char* b;
        const char* c;
        int count;
        bool showVowel;
    };
        if (!currentEnabled)
        {
            return;
        }


    static const std::array<ModeUi, 20> modeUi { {
        { "", "", "", 0, false },
        { "", "", "", 0, false },
        { "", "", "", 0, false },
        { "", "", "", 0, false },
        { "COLOR", "", "", 1, false },
        { "COLOR", "", "", 1, false },
        { "DETUNE", "SPREAD", "", 2, false },
        { "WIDTH", "", "", 1, false },
        { "POSITION", "", "", 1, false },
        { "TILT", "ODD/EVEN", "ROLL", 3, false },
        { "MORPH", "COLOR", "", 2, true },
        { "RATIO", "INDEX", "", 2, false },
        { "SYNC", "DRIVE", "", 2, false },
        { "DECAY", "BRIGHT", "", 2, false },
        { "TONE", "CLICK", "", 2, false },
        { "BITS", "RATE", "", 2, false },
        { "DECAY", "MATERIAL", "", 2, false },
        { "TRANS", "BODY", "CHAOS", 3, false },
        { "SPREAD", "ODD/EVEN", "ROLL", 3, false },
        { "MORPH", "CHAR", "MOVE", 3, false }
    } };

    const auto ui = modeUi[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(modeUi.size()) - 1, modeIndex))];

    const std::array<juce::Slider*, 3> sliders { &macroA, &macroB, &macroC };
    const std::array<juce::Label*, 3> labels { &macroALabel, &macroBLabel, &macroCLabel };
    const std::array<const char*, 3> texts { ui.a, ui.b, ui.c };

    for (int i = 0; i < 3; ++i)
    {
        const auto show = i < ui.count;
        sliders[static_cast<std::size_t>(i)]->setVisible(show);
        labels[static_cast<std::size_t>(i)]->setVisible(show);
        labels[static_cast<std::size_t>(i)]->setText(show ? texts[static_cast<std::size_t>(i)] : "", juce::dontSendNotification);
        const auto tooltipText = show ? juce::String(texts[static_cast<std::size_t>(i)]) : juce::String();
        labels[static_cast<std::size_t>(i)]->setTooltip(tooltipText);
        sliders[static_cast<std::size_t>(i)]->setTooltip(tooltipText);
    }

    vowelBox.setVisible(ui.showVowel);
    vowelLabel.setVisible(ui.showVowel);

    applyEnabledUi();
    resized();
    repaint();
}

void OscillatorComponent::applyEnabledUi()
{
    modeBox.setEnabled(currentEnabled);
    modeLabel.setEnabled(currentEnabled);
    vowelBox.setEnabled(currentEnabled && vowelBox.isVisible());
    vowelLabel.setEnabled(currentEnabled && vowelLabel.isVisible());
    pitch.setEnabled(currentEnabled);
    pitchLabel.setEnabled(currentEnabled);
    pitchValueLabel.setEnabled(currentEnabled);

    const std::array<juce::Slider*, 3> sliders { &macroA, &macroB, &macroC };
    const std::array<juce::Label*, 3> labels { &macroALabel, &macroBLabel, &macroCLabel };

    for (int i = 0; i < 3; ++i)
    {
        const auto visible = sliders[static_cast<std::size_t>(i)]->isVisible();
        sliders[static_cast<std::size_t>(i)]->setEnabled(currentEnabled && visible);
        labels[static_cast<std::size_t>(i)]->setEnabled(currentEnabled && visible);
    }

    repaint();
}

