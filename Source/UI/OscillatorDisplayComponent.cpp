#include "OscillatorDisplayComponent.h"

#include <cmath>
#include <vector>

OscillatorDisplayComponent::OscillatorDisplayComponent(juce::Slider& macroAIn,
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
    : macroA(macroAIn),
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
}

void OscillatorDisplayComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void OscillatorDisplayComponent::refreshFromSelections(int modeIndex, int vowelIndex)
{
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
}

void OscillatorDisplayComponent::advanceAnimation(float deltaPhase)
{
    phase += deltaPhase;
    if (phase > juce::MathConstants<float>::twoPi)
    {
        phase -= juce::MathConstants<float>::twoPi;
    }

    repaint();
}

void OscillatorDisplayComponent::resized()
{
    auto area = getLocalBounds().reduced(2, 0);
    auto left = area.removeFromLeft(area.getWidth() / 2).reduced(0, 0);
    auto right = area.reduced(0, 0);

    layoutMacroControls(right);

    auto modeRow = juce::Rectangle<int>(left.getX(), left.getBottom() - 22, left.getWidth(), 18);
    auto vowelRow = juce::Rectangle<int>(right.getX(), right.getBottom() - 44, right.getWidth(), 18);

    auto modeLabelArea = modeRow.removeFromLeft(52);
    modeLabel.setBounds(modeLabelArea);
    modeBox.setBounds(modeRow.reduced(1, 0));

    auto vowelLabelArea = vowelRow.removeFromLeft(52);
    vowelLabel.setBounds(vowelLabelArea);
    vowelBox.setBounds(vowelRow.reduced(1, 0));
}

void OscillatorDisplayComponent::paint(juce::Graphics& g)
{
    auto oscSplit = getLocalBounds().reduced(8, 0);
    auto oscVizRect = oscSplit.removeFromLeft(oscSplit.getWidth() / 2).reduced(4, 2);
    oscVizRect.removeFromBottom(28);

    if (oscVizRect.getWidth() <= 40 || oscVizRect.getHeight() <= 24)
    {
        return;
    }

    const auto modeIndex = juce::jmax(0, modeBox.getSelectedItemIndex());
    const auto macroAValue = static_cast<float>(macroA.getValue());
    const auto macroBValue = static_cast<float>(macroB.getValue());
    const auto macroCValue = static_cast<float>(macroC.getValue());

    auto viz = oscVizRect.toFloat();
    g.setColour(juce::Colour::fromRGBA(12, 16, 26, 170));
    g.fillRoundedRectangle(viz, 7.0f);
    g.setColour(juce::Colour::fromRGBA(145, 198, 255, 80));
    g.drawRoundedRectangle(viz, 7.0f, 1.0f);

    const auto left = viz.getX() + 6.0f;
    const auto right = viz.getRight() - 6.0f;
    const auto top = viz.getY() + 5.0f;
    const auto bottom = viz.getBottom() - 5.0f;
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
    g.setColour(accent.withAlpha(juce::jlimit(0.2f, 1.0f, glowAlpha * 0.6f)));
    g.strokePath(wave,
                 juce::PathStrokeType(3.0f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
    g.setColour(juce::Colour::fromRGB(170, 228, 255));
    g.strokePath(wave,
                 juce::PathStrokeType(1.35f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 120));
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("Mode Visual", oscVizRect.removeFromTop(14), juce::Justification::centredTop);
}

void OscillatorDisplayComponent::applyModeUi()
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

    resized();
    repaint();
}

void OscillatorDisplayComponent::layoutMacroControls(const juce::Rectangle<int>& area)
{
    constexpr int labelHeight = 22;
    constexpr int minGap = 8;

    auto right = area.reduced(2, 0);
    right.removeFromBottom(52);

    const std::array<juce::Slider*, 3> sliders { &macroA, &macroB, &macroC };
    const std::array<juce::Label*, 3> labels { &macroALabel, &macroBLabel, &macroCLabel };

    std::vector<int> visibleIndices;
    for (int i = 0; i < 3; ++i)
    {
        if (sliders[static_cast<std::size_t>(i)]->isVisible())
        {
            visibleIndices.push_back(i);
        }
    }

    if (visibleIndices.empty())
    {
        return;
    }

    const auto setTextSizedLabel = [&right](juce::Label* label, const juce::Rectangle<int>& rowBounds, int knobCenterX)
    {
        if (label == nullptr)
        {
            return;
        }

        const auto approxTextWidth = static_cast<int>(label->getText().length()) * 8 + 16;
        const auto textWidth = juce::jlimit(42,
                                            right.getWidth() - 6,
                                            approxTextWidth);
        const auto left = juce::jlimit(rowBounds.getX(),
                                       rowBounds.getRight() - textWidth,
                                       knobCenterX - textWidth / 2);
        label->setBounds(left, rowBounds.getY(), textWidth, 18);
    };

    const auto visibleCount = static_cast<int>(visibleIndices.size());

    if (visibleCount == 3)
    {
        const auto baseKnobSize = juce::jlimit(38, 62, juce::jmin(right.getWidth() - 14, (right.getHeight() - 14) / 2));
        constexpr int centerGap = 12;
        constexpr int verticalGap = 6;

        auto work = right;
        auto topRow = work.removeFromTop(work.getHeight() / 2 + 4);
        work.removeFromTop(verticalGap);
        auto bottomRow = work;

        auto topBody = topRow.reduced(2, 1);
        auto topLabel = topBody.removeFromTop(labelHeight);
        topBody.removeFromTop(8);
        auto topKnob = juce::Rectangle<int>(baseKnobSize, baseKnobSize).withCentre(topBody.getCentre());

        setTextSizedLabel(labels[static_cast<std::size_t>(visibleIndices[0])], topLabel, topKnob.getCentreX());
        sliders[static_cast<std::size_t>(visibleIndices[0])]->setBounds(topKnob);

        auto leftCell = bottomRow.removeFromLeft((bottomRow.getWidth() - centerGap) / 2).reduced(1, 1);
        bottomRow.removeFromLeft(centerGap);
        auto rightCell = bottomRow.reduced(1, 1);
        const std::array<juce::Rectangle<int>, 2> cells { leftCell, rightCell };

        for (int idx = 0; idx < 2; ++idx)
        {
            auto cell = cells[static_cast<std::size_t>(idx)];
            auto labelRow = cell.removeFromTop(labelHeight);
            cell.removeFromTop(8);
            const auto cellKnobSize = juce::jlimit(36,
                                                   baseKnobSize,
                                                   juce::jmin(cell.getWidth() - 2, cell.getHeight() - 2));
            auto knob = juce::Rectangle<int>(cellKnobSize, cellKnobSize).withCentre(cell.getCentre());
            const auto index = visibleIndices[static_cast<std::size_t>(idx + 1)];
            setTextSizedLabel(labels[static_cast<std::size_t>(index)], labelRow, knob.getCentreX());
            sliders[static_cast<std::size_t>(index)]->setBounds(knob);
        }

        return;
    }

    if (visibleCount == 2)
    {
        constexpr int gap = 34;
        auto row = right.reduced(0, 4);
        auto labelRow = row.removeFromTop(labelHeight);
        row.removeFromTop(8);

        const auto pairWidth = juce::jmax(2, row.getWidth() - gap);
        const auto cellWidth = juce::jmax(1, pairWidth / 2);
        const auto rowLeft = row.getX() + (row.getWidth() - (cellWidth * 2 + gap)) / 2;

        auto leftCell = juce::Rectangle<int>(rowLeft, row.getY(), cellWidth, row.getHeight()).reduced(2, 0);
        auto rightCell = juce::Rectangle<int>(rowLeft + cellWidth + gap,
                                              row.getY(),
                                              cellWidth,
                                              row.getHeight()).reduced(2, 0);
        const std::array<juce::Rectangle<int>, 2> cells { leftCell, rightCell };

        const auto maxByWidth = juce::jmax(36, cellWidth - 4);
        const auto maxByHeight = juce::jmax(36, row.getHeight() - 2);
        const auto knobSize = juce::jlimit(38, 72, juce::jmin(maxByWidth, maxByHeight));

        for (int idx = 0; idx < 2; ++idx)
        {
            const auto index = visibleIndices[static_cast<std::size_t>(idx)];
            auto knob = juce::Rectangle<int>(knobSize, knobSize).withCentre(cells[static_cast<std::size_t>(idx)].getCentre());
            setTextSizedLabel(labels[static_cast<std::size_t>(index)], labelRow, knob.getCentreX());
            sliders[static_cast<std::size_t>(index)]->setBounds(knob);
        }

        return;
    }

    const auto rowHeight = juce::jmax(62, (right.getHeight() - juce::jmax(0, visibleCount - 1) * minGap) / visibleCount);
    const auto maxKnobByHeight = juce::jmax(44, rowHeight - labelHeight - 8);
    const auto maxKnobByWidth = juce::jmax(40, right.getWidth() - 12);
    const auto knobSize = juce::jlimit(44, 78, juce::jmin(maxKnobByWidth, maxKnobByHeight));

    auto y = right.getY();
    for (int v = 0; v < visibleCount; ++v)
    {
        const auto index = visibleIndices[static_cast<std::size_t>(v)];
        auto rowBounds = juce::Rectangle<int>(right.getX(), y, right.getWidth(), rowHeight);
        auto labelBounds = rowBounds.removeFromTop(labelHeight);
        rowBounds.removeFromTop(6);
        auto knob = juce::Rectangle<int>(knobSize, knobSize).withCentre(rowBounds.getCentre());

        setTextSizedLabel(labels[static_cast<std::size_t>(index)], labelBounds, knob.getCentreX());
        sliders[static_cast<std::size_t>(index)]->setBounds(knob);

        y += rowHeight + minGap;
    }
}
