#pragma once

#include <JuceHeader.h>

#include "Theme.h"

#include <functional>
#include <vector>

// Four modulators of one kind - LFO 1-4, or ENV 1-4 - shown one at a time
// under a strip of tabs.
//
// The cards themselves are unchanged: each page is a complete LfoComponent or
// EnvelopeComponent, alive and attached whether or not it is the one showing,
// so selecting a tab changes only what is visible. Every source's patch-bay
// jack sits on its tab, so a modulator can be patched without switching to it.
//
// The tabs are the top menu's: a flat key on the rail, a lifted face with the
// identity colour as an underline when selected, a faint wash on hover, 1 px
// seams.
class ModulatorTabs final : public juce::Component
{
public:
    ModulatorTabs(juce::String namePrefix, juce::Colour accentIn)
        : prefix(std::move(namePrefix)), accent(accentIn)
    {
        setName(prefix + " TABS");
    }

    // A page, shown when its tab is selected. Adopted as a child.
    void addPage(juce::Component& card)
    {
        pages.push_back(&card);
        sockets.push_back(nullptr);
        addChildComponent(card);
        card.setVisible(static_cast<int>(pages.size()) - 1 == selected);
        resized();
    }

    // The jack for page `index`, placed at the right end of its tab.
    void setSocket(int index, juce::Component* socket)
    {
        if (! juce::isPositiveAndBelow(index, static_cast<int>(sockets.size()))) { return; }
        sockets[static_cast<std::size_t>(index)] = socket;
        if (socket != nullptr) { addAndMakeVisible(*socket); }
        resized();
    }

    void setAccent(juce::Colour colour)
    {
        accent = colour;
        repaint(tabStrip());
    }

    void setTabHeight(int height)
    {
        tabHeight = juce::jmax(16, height);
        resized();
    }

    int getSelected() const noexcept { return selected; }
    int getNumPages() const noexcept { return static_cast<int>(pages.size()); }
    juce::Component* getPage(int index) const
    {
        return juce::isPositiveAndBelow(index, getNumPages()) ? pages[static_cast<std::size_t>(index)] : nullptr;
    }

    void setSelected(int index, bool notify = true)
    {
        index = juce::jlimit(0, juce::jmax(0, getNumPages() - 1), index);
        if (index == selected) { return; }
        selected = index;
        for (int i = 0; i < getNumPages(); ++i) { pages[static_cast<std::size_t>(i)]->setVisible(i == selected); }
        repaint(tabStrip());
        if (notify && onSelectionChanged != nullptr) { onSelectionChanged(selected); }
    }

    std::function<void(int)> onSelectionChanged;

    // The tab for page `index`, in this component's coordinates.
    juce::Rectangle<int> tabBounds(int index) const
    {
        const auto count = juce::jmax(1, getNumPages());
        const auto strip = tabStrip();
        // Whole pixels with 1 px seams, the last tab taking the remainder, so
        // the strip ends exactly at the panel's edge.
        const auto seams = count - 1;
        const auto width = (strip.getWidth() - seams) / count;
        const auto x = strip.getX() + index * (width + 1);
        const auto w = index == count - 1 ? strip.getRight() - x : width;
        return { x, strip.getY(), w, strip.getHeight() };
    }

    // Where the selected page goes.
    juce::Rectangle<int> pageArea() const
    {
        return getLocalBounds().withTrimmedTop(tabHeight + 1);
    }

    void resized() override
    {
        const auto area = pageArea();
        for (auto* page : pages) { page->setBounds(area); }
        for (int i = 0; i < static_cast<int>(sockets.size()); ++i)
        {
            if (auto* socket = sockets[static_cast<std::size_t>(i)])
            {
                socket->setBounds(socketBounds(i));
                socket->toFront(false);
            }
        }
    }

    void paint(juce::Graphics& g) override
    {
        namespace th = px3::ui::theme;
        for (int i = 0; i < getNumPages(); ++i)
        {
            const auto tab = tabBounds(i).toFloat();
            const auto on = i == selected;
            g.setColour(on ? juce::Colour(0xff2a3036) : juce::Colour(0xff1b1f23));
            g.fillRect(tab);
            if (i == hovered && ! on)
            {
                g.setColour(juce::Colours::white.withAlpha(0.05f));
                g.fillRect(tab);
            }
            if (on)
            {
                g.setColour(juce::Colours::white.withAlpha(0.10f));
                g.fillRect(tab.withHeight(1.0f));
                g.setColour(accent);
                g.fillRect(tab.withTop(tab.getBottom() - th::space::accentBar));
            }

            auto text = tab.reduced(10.0f, 0.0f);
            if (sockets[static_cast<std::size_t>(i)] != nullptr)
            {
                text.setRight(static_cast<float>(socketBounds(i).getX()) - 4.0f);
            }
            th::drawLabel(g, prefix + " " + juce::String(i + 1), text, th::Type::heading,
                          on ? th::colour::textPrimary : th::colour::textSecondary,
                          juce::Justification::centredLeft);
        }
    }

    void mouseMove(const juce::MouseEvent& event) override { setHovered(tabAt(event.getPosition())); }
    void mouseExit(const juce::MouseEvent&) override { setHovered(-1); }
    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto tab = tabAt(event.getPosition());
        if (tab >= 0) { setSelected(tab); }
    }

    int tabAt(juce::Point<int> position) const
    {
        for (int i = 0; i < getNumPages(); ++i)
        {
            if (tabBounds(i).contains(position)) { return i; }
        }
        return -1;
    }

private:
    juce::Rectangle<int> tabStrip() const { return getLocalBounds().withHeight(tabHeight); }

    // The jack: square, the tab's height less a margin, at the tab's right end
    // - the same size and inset as the card power buttons it used to sit
    // opposite.
    juce::Rectangle<int> socketBounds(int index) const
    {
        const auto tab = tabBounds(index);
        const auto size = juce::jmax(10, tab.getHeight() - 8);
        return { tab.getRight() - size - 6, tab.getCentreY() - size / 2, size, size };
    }

    void setHovered(int index)
    {
        if (hovered == index) { return; }
        hovered = index;
        repaint(tabStrip());
    }

    juce::String prefix;
    juce::Colour accent;
    std::vector<juce::Component*> pages;
    std::vector<juce::Component*> sockets;
    int selected { 0 };
    int hovered { -1 };
    int tabHeight { 28 };
};
