#include "UILayoutDesigner.h"

#if PX3_UI_DESIGNER

#include <algorithm>
#include <cmath>

namespace
{
px3::ui::InstrumentSceneLayoutMode layoutModeForIndex(int index)
{
    switch (index)
    {
        case 1: return px3::ui::InstrumentSceneLayoutMode::row;
        case 2: return px3::ui::InstrumentSceneLayoutMode::column;
        case 3: return px3::ui::InstrumentSceneLayoutMode::grid;
        case 4: return px3::ui::InstrumentSceneLayoutMode::overlay;
        default: return px3::ui::InstrumentSceneLayoutMode::absolute;
    }
}
}

UILayoutDesignerWindow::UILayoutDesignerWindow(const px3::ui::InstrumentSceneDocument& documentIn,
                                               Callbacks callbacksIn)
    : juce::DocumentWindow("PX3 UI DESIGNER",
                           juce::Colour::fromRGB(27, 30, 32),
                           juce::DocumentWindow::allButtons),
      document(documentIn),
      callbacks(std::move(callbacksIn))
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(360, 620, 780, 900);
    setAlwaysOnTop(true);
    setBounds(72, 72, 520, 640);

    regionLabel.setText("NODE", juce::dontSendNotification);
    regionLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(220, 224, 222));
    addAndMakeVisible(regionLabel);

    styleLabel.setText("STYLE", juce::dontSendNotification);
    styleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(174, 183, 180));
    styleLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    addAndMakeVisible(styleLabel);
    addAndMakeVisible(styleSelector);
    styleSelector.onChange = [this] { applyStyleFromInspector(); };

    addAndMakeVisible(visibilityToggle);
    visibilityToggle.onClick = [this]
    {
        if (suppressCallbacks || callbacks.visibilityChanged == nullptr || selectedRegionId.isEmpty())
        {
            return;
        }
        beginPropertyEdit();
        setStatus(callbacks.visibilityChanged(selectedRegionId, visibilityToggle.getToggleState()));
        endPropertyEdit();
    };

    addAndMakeVisible(regionSelector);
    regionSelector.onChange = [this]
    {
        if (suppressCallbacks)
        {
            return;
        }
        const auto index = regionSelector.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow(index, regionIds.size()))
        {
            return;
        }
        selectedRegionId = regionIds[index];
        refreshInspector();
        if (callbacks.selectionChanged != nullptr)
        {
            callbacks.selectionChanged(selectedRegionId);
        }
    };

    parentLabel.setText("PARENT", juce::dontSendNotification);
    parentLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(174, 183, 180));
    parentLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    addAndMakeVisible(parentLabel);
    addAndMakeVisible(parentSelector);
    parentSelector.onChange = [this] { applyParentFromInspector(); };

    layoutLabel.setText("LAYOUT", juce::dontSendNotification);
    layoutLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(174, 183, 180));
    layoutLabel.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    addAndMakeVisible(layoutLabel);
    layoutSelector.addItem("Absolute", 1);
    layoutSelector.addItem("Row", 2);
    layoutSelector.addItem("Column", 3);
    layoutSelector.addItem("Grid", 4);
    layoutSelector.addItem("Overlay", 5);
    addAndMakeVisible(layoutSelector);
    layoutSelector.onChange = [this] { applyLayoutFromInspector(); };

    constexpr std::array<const char*, 8> propertyNames {
        "X", "Y", "WIDTH", "HEIGHT", "ORDER", "FLEX GROW", "SPACING", "GRID COLUMNS"
    };
    for (std::size_t i = 0; i < propertySliders.size(); ++i)
    {
        auto& label = propertyLabels[i];
        label.setText(propertyNames[i], juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(174, 183, 180));
        label.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        addAndMakeVisible(label);

        auto& slider = propertySliders[i];
        slider.setSliderStyle(juce::Slider::LinearBar);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
        const auto maximum = i < 4 ? 1.0 : (i == 4 || i == 7 ? 32.0 : (i == 5 ? 8.0 : 160.0));
        const auto step = i < 4 ? 0.001 : ((i == 4 || i == 7 || i == 6) ? 1.0 : 0.1);
        slider.setRange(0.0, maximum, step);
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour::fromRGB(235, 238, 235));
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour::fromRGB(17, 19, 20));
        slider.onDragStart = [this] { beginPropertyEdit(); };
        slider.onDragEnd = [this] { endPropertyEdit(); };
        slider.onValueChange = [this, i]
        {
            if (suppressCallbacks)
            {
                return;
            }

            const auto ownTransaction = ! propertyEditActive;
            if (ownTransaction)
            {
                beginPropertyEdit();
            }

            if (i == 4)
            {
                applyOrderFromInspector();
            }
            else if (i >= 5)
            {
                applyFlowFromInspector();
            }
            else
            {
                applyBoundsFromInspector();
            }

            if (ownTransaction)
            {
                endPropertyEdit();
            }
        };
        addAndMakeVisible(slider);
    }

    for (auto* button : { &undoButton, &redoButton, &saveButton })
    {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 51, 52));
        button->setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(232, 236, 232));
        addAndMakeVisible(*button);
    }

    undoButton.onClick = [this]
    {
        if (callbacks.undo != nullptr && callbacks.undo())
        {
            refreshInspector();
            setStatus("Undid layout edit");
        }
    };
    redoButton.onClick = [this]
    {
        if (callbacks.redo != nullptr && callbacks.redo())
        {
            refreshInspector();
            setStatus("Redid layout edit");
        }
    };
    saveButton.onClick = [this]
    {
        if (callbacks.save != nullptr)
        {
            callbacks.save();
        }
    };

    statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(169, 190, 179));
    statusLabel.setFont(juce::FontOptions(10.5f));
    addAndMakeVisible(statusLabel);

    setDocument(document);
}

void UILayoutDesignerWindow::setDocument(const px3::ui::InstrumentSceneDocument& documentIn)
{
    juce::ignoreUnused(documentIn);
    const auto previousSelection = selectedRegionId;
    regionIds.clear();
    styleIds.clear();
    parentIds.clear();
    regionSelector.clear(juce::dontSendNotification);
    styleSelector.clear(juce::dontSendNotification);
    parentSelector.clear(juce::dontSendNotification);

    std::vector<const px3::ui::InstrumentSceneNode*> children;
    for (const auto& region : document.getNodes())
    {
        children.push_back(&region);
    }
    std::sort(children.begin(), children.end(), [](const auto* a, const auto* b)
    {
        if (a->parentId != b->parentId) { return a->parentId < b->parentId; }
        if (a->order != b->order) { return a->order < b->order; }
        return a->id < b->id;
    });

    auto itemId = 1;
    for (const auto* region : children)
    {
        regionIds.add(region->id);
        regionSelector.addItem(region->id, itemId++);
    }

    itemId = 1;
    for (const auto& style : document.getStyleTokens())
    {
        styleIds.add(style.id);
        styleSelector.addItem(style.id, itemId++);
    }

    parentIds.add(juce::String());
    parentSelector.addItem("ROOT", 1);
    itemId = 2;
    for (const auto& node : document.getNodes())
    {
        parentIds.add(node.id);
        parentSelector.addItem(node.id, itemId++);
    }

    if (regionIds.contains(previousSelection))
    {
        setSelectedRegion(previousSelection);
    }
    else if (! regionIds.isEmpty())
    {
        setSelectedRegion(regionIds[0]);
    }
    else
    {
        selectedRegionId.clear();
        refreshInspector();
    }
}

void UILayoutDesignerWindow::setSelectedRegion(const juce::String& id)
{
    const auto index = regionIds.indexOf(id);
    if (index < 0)
    {
        return;
    }

    selectedRegionId = id;
    suppressCallbacks = true;
    regionSelector.setSelectedId(index + 1, juce::dontSendNotification);
    suppressCallbacks = false;
    refreshInspector();
}

void UILayoutDesignerWindow::setStatus(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
}

void UILayoutDesignerWindow::closeButtonPressed()
{
    if (callbacks.close != nullptr)
    {
        callbacks.close();
    }
}

void UILayoutDesignerWindow::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto row = area.removeFromTop(30);
    regionLabel.setBounds(row.removeFromLeft(66));
    row.removeFromLeft(8);
    regionSelector.setBounds(row);
    area.removeFromTop(8);
    row = area.removeFromTop(30);
    parentLabel.setBounds(row.removeFromLeft(66));
    row.removeFromLeft(8);
    parentSelector.setBounds(row);
    area.removeFromTop(8);
    row = area.removeFromTop(30);
    layoutLabel.setBounds(row.removeFromLeft(66));
    row.removeFromLeft(8);
    layoutSelector.setBounds(row);
    area.removeFromTop(8);
    row = area.removeFromTop(30);
    styleLabel.setBounds(row.removeFromLeft(66));
    row.removeFromLeft(8);
    visibilityToggle.setBounds(row.removeFromRight(88));
    row.removeFromRight(8);
    styleSelector.setBounds(row);
    area.removeFromTop(12);

    constexpr int rowHeight = 34;
    for (std::size_t i = 0; i < propertySliders.size(); ++i)
    {
        auto propertyRow = area.removeFromTop(rowHeight);
        propertyLabels[i].setBounds(propertyRow.removeFromLeft(74));
        propertySliders[i].setBounds(propertyRow);
        area.removeFromTop(5);
    }

    area.removeFromTop(6);
    statusLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);
    auto buttons = area.removeFromTop(30);
    saveButton.setBounds(buttons.removeFromLeft(112));
    buttons.removeFromLeft(8);
    undoButton.setBounds(buttons.removeFromLeft(72));
    buttons.removeFromLeft(6);
    redoButton.setBounds(buttons.removeFromLeft(72));
}

void UILayoutDesignerWindow::refreshInspector()
{
    const auto* region = document.findNode(selectedRegionId);
    const auto bounds = region != nullptr ? region->bounds : juce::Rectangle<float>(0.0f, 0.0f, 1.0f, 1.0f);
    suppressCallbacks = true;
    visibilityToggle.setEnabled(region != nullptr);
    visibilityToggle.setToggleState(region != nullptr && region->visible, juce::dontSendNotification);
    const auto styleIndex = region != nullptr ? styleIds.indexOf(region->styleToken) : -1;
    styleSelector.setSelectedId(styleIndex >= 0 ? styleIndex + 1 : 0, juce::dontSendNotification);
    const auto parentIndex = region != nullptr ? parentIds.indexOf(region->parentId) : -1;
    parentSelector.setSelectedId(parentIndex >= 0 ? parentIndex + 1 : 0, juce::dontSendNotification);
    layoutSelector.setSelectedId(region != nullptr ? static_cast<int>(region->layout) + 1 : 1,
                                 juce::dontSendNotification);
    propertySliders[0].setValue(bounds.getX(), juce::dontSendNotification);
    propertySliders[1].setValue(bounds.getY(), juce::dontSendNotification);
    propertySliders[2].setValue(bounds.getWidth(), juce::dontSendNotification);
    propertySliders[3].setValue(bounds.getHeight(), juce::dontSendNotification);
    propertySliders[4].setValue(region != nullptr ? region->order : 0, juce::dontSendNotification);
    propertySliders[5].setValue(region != nullptr ? region->flexGrow : 1.0f, juce::dontSendNotification);
    propertySliders[6].setValue(region != nullptr ? region->spacing : 0.0f, juce::dontSendNotification);
    propertySliders[7].setValue(region != nullptr ? region->gridColumns : 0, juce::dontSendNotification);
    suppressCallbacks = false;
}

void UILayoutDesignerWindow::applyBoundsFromInspector()
{
    if (callbacks.boundsChanged == nullptr || selectedRegionId.isEmpty())
    {
        return;
    }

    const auto bounds = juce::Rectangle<float>(static_cast<float>(propertySliders[0].getValue()),
                                                static_cast<float>(propertySliders[1].getValue()),
                                                static_cast<float>(propertySliders[2].getValue()),
                                                static_cast<float>(propertySliders[3].getValue()));
    setStatus(callbacks.boundsChanged(selectedRegionId, bounds));
}

void UILayoutDesignerWindow::applyOrderFromInspector()
{
    if (callbacks.orderChanged == nullptr || selectedRegionId.isEmpty())
    {
        return;
    }
    setStatus(callbacks.orderChanged(selectedRegionId,
                                     static_cast<int>(propertySliders[4].getValue())));
}

void UILayoutDesignerWindow::applyStyleFromInspector()
{
    if (suppressCallbacks || callbacks.styleChanged == nullptr || selectedRegionId.isEmpty())
    {
        return;
    }
    const auto index = styleSelector.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow(index, styleIds.size()))
    {
        return;
    }

    beginPropertyEdit();
    setStatus(callbacks.styleChanged(selectedRegionId, styleIds[index]));
    endPropertyEdit();
}

void UILayoutDesignerWindow::applyLayoutFromInspector()
{
    if (suppressCallbacks || callbacks.layoutChanged == nullptr || selectedRegionId.isEmpty())
    {
        return;
    }
    beginPropertyEdit();
    setStatus(callbacks.layoutChanged(selectedRegionId,
                                      layoutModeForIndex(layoutSelector.getSelectedId() - 1)));
    endPropertyEdit();
}

void UILayoutDesignerWindow::applyParentFromInspector()
{
    if (suppressCallbacks || callbacks.parentChanged == nullptr || selectedRegionId.isEmpty())
    {
        return;
    }
    const auto index = parentSelector.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow(index, parentIds.size()))
    {
        return;
    }
    beginPropertyEdit();
    setStatus(callbacks.parentChanged(selectedRegionId, parentIds[index]));
    endPropertyEdit();
}

void UILayoutDesignerWindow::applyFlowFromInspector()
{
    if (callbacks.flowChanged == nullptr || selectedRegionId.isEmpty())
    {
        return;
    }
    setStatus(callbacks.flowChanged(selectedRegionId,
                                    static_cast<float>(propertySliders[5].getValue()),
                                    static_cast<float>(propertySliders[6].getValue()),
                                    static_cast<int>(propertySliders[7].getValue())));
}

void UILayoutDesignerWindow::beginPropertyEdit()
{
    if (propertyEditActive)
    {
        return;
    }
    propertyEditActive = true;
    if (callbacks.beginEdit != nullptr)
    {
        callbacks.beginEdit();
    }
}

void UILayoutDesignerWindow::endPropertyEdit()
{
    if (! propertyEditActive)
    {
        return;
    }
    propertyEditActive = false;
    if (callbacks.endEdit != nullptr)
    {
        callbacks.endEdit();
    }
}

UILayoutSelectionOverlay::UILayoutSelectionOverlay(BoundsChanged callback)
    : boundsChanged(std::move(callback))
{
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void UILayoutSelectionOverlay::setSelection(juce::Rectangle<int> parentBoundsIn,
                                            juce::Rectangle<float> normalizedBoundsIn,
                                            bool activeIn)
{
    parentBounds = parentBoundsIn;
    normalizedBounds = normalizedBoundsIn;
    active = activeIn && ! parentBounds.isEmpty();
    updateFrame();
    repaint();
}

bool UILayoutSelectionOverlay::hitTest(int x, int y)
{
    return active && frameBounds.contains(static_cast<float>(x), static_cast<float>(y));
}

void UILayoutSelectionOverlay::paint(juce::Graphics& graphics)
{
    if (! active || frameBounds.isEmpty())
    {
        return;
    }

    const auto line = frameBounds.reduced(1.0f);
    graphics.setColour(juce::Colour::fromRGB(255, 197, 74));
    graphics.drawRect(line, 2.0f);

    constexpr float handleSize = 9.0f;
    for (const auto x : { line.getX(), line.getCentreX(), line.getRight() })
    {
        for (const auto y : { line.getY(), line.getCentreY(), line.getBottom() })
        {
            const juce::Rectangle<float> handle(handleSize, handleSize);
            graphics.fillRect(handle.withCentre({ x, y }));
        }
    }
}

void UILayoutSelectionOverlay::mouseDown(const juce::MouseEvent& event)
{
    dragging = true;
    dragStartBounds = frameBounds;
    dragStartPoint = event.position;

    constexpr float edge = 10.0f;
    resizeLeft = event.position.x <= frameBounds.getX() + edge;
    resizeRight = event.position.x >= frameBounds.getRight() - edge;
    resizeTop = event.position.y <= frameBounds.getY() + edge;
    resizeBottom = event.position.y >= frameBounds.getBottom() - edge;
    if (! resizeLeft && ! resizeRight && ! resizeTop && ! resizeBottom)
    {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }

    if (onEditBegin != nullptr)
    {
        onEditBegin();
    }
}

void UILayoutSelectionOverlay::mouseDrag(const juce::MouseEvent& event)
{
    if (! dragging)
    {
        return;
    }

    const auto delta = event.position - dragStartPoint;
    auto next = dragStartBounds;
    if (resizeLeft) { next.setLeft(next.getX() + delta.x); }
    if (resizeRight) { next.setRight(next.getRight() + delta.x); }
    if (resizeTop) { next.setTop(next.getY() + delta.y); }
    if (resizeBottom) { next.setBottom(next.getBottom() + delta.y); }
    if (! resizeLeft && ! resizeRight && ! resizeTop && ! resizeBottom)
    {
        next.setPosition(next.getPosition() + delta);
    }

    const auto parent = parentBounds.toFloat();
    constexpr float minSize = 48.0f;
    next.setWidth(juce::jmax(minSize, next.getWidth()));
    next.setHeight(juce::jmax(minSize, next.getHeight()));
    next.setX(juce::jlimit(parent.getX(), parent.getRight() - next.getWidth(), next.getX()));
    next.setY(juce::jlimit(parent.getY(), parent.getBottom() - next.getHeight(), next.getY()));
    frameBounds = next;
    normalizedBounds = normalizedFromPixels(frameBounds);
    if (boundsChanged != nullptr)
    {
        boundsChanged(normalizedBounds);
    }
    repaint();
}

void UILayoutSelectionOverlay::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    if (! dragging)
    {
        return;
    }
    dragging = false;
    if (onEditEnd != nullptr)
    {
        onEditEnd();
    }
}

juce::Rectangle<float> UILayoutSelectionOverlay::normalizedFromPixels(juce::Rectangle<float> bounds) const
{
    const auto parent = parentBounds.toFloat();
    if (parent.getWidth() <= 0.0f || parent.getHeight() <= 0.0f)
    {
        return normalizedBounds;
    }

    return { (bounds.getX() - parent.getX()) / parent.getWidth(),
             (bounds.getY() - parent.getY()) / parent.getHeight(),
             bounds.getWidth() / parent.getWidth(),
             bounds.getHeight() / parent.getHeight() };
}

void UILayoutSelectionOverlay::updateFrame()
{
    if (! active)
    {
        frameBounds = {};
        return;
    }

    const auto parent = parentBounds.toFloat();
    frameBounds = { parent.getX() + normalizedBounds.getX() * parent.getWidth(),
                    parent.getY() + normalizedBounds.getY() * parent.getHeight(),
                    normalizedBounds.getWidth() * parent.getWidth(),
                    normalizedBounds.getHeight() * parent.getHeight() };
}

#endif