#pragma once

#include "UILayout.h"

#if PX3_UI_DESIGNER

#include <array>
#include <functional>
#include <vector>

class UILayoutDesignerWindow final : public juce::DocumentWindow
{
public:
    struct Callbacks
    {
        struct BindingOption
        {
            juce::String id;
            juce::String label;
        };

        std::vector<BindingOption> bindingOptions;
        std::function<juce::String(const juce::String&,
                                   px3::ui::InstrumentSceneNodeKind,
                                   const juce::String&,
                                   const juce::String&)> presentationChanged;
        std::function<void(const juce::String&)> selectionChanged;
        std::function<juce::String(const juce::String&, juce::Rectangle<float>)> boundsChanged;
        std::function<juce::String(const juce::String&, int)> orderChanged;
        std::function<juce::String(const juce::String&, const juce::String&)> styleChanged;
        std::function<juce::String(const juce::String&, px3::ui::InstrumentSceneLayoutMode)> layoutChanged;
        std::function<juce::String(const juce::String&, const juce::String&)> parentChanged;
        std::function<juce::String(const juce::String&, float, float, int)> flowChanged;
        std::function<juce::String(const juce::String&, juce::Point<float>, juce::Point<float>)> sizeConstraintsChanged;
        std::function<juce::String(const juce::String&, bool)> visibilityChanged;
        std::function<void()> beginEdit;
        std::function<void()> endEdit;
        std::function<void()> save;
        std::function<bool()> undo;
        std::function<bool()> redo;
        std::function<void()> close;
    };

    UILayoutDesignerWindow(const px3::ui::InstrumentSceneDocument& document, Callbacks callbacks);

    void setDocument(const px3::ui::InstrumentSceneDocument& document);
    void setSelectedRegion(const juce::String& id);
    void setStatus(const juce::String& text);
    void closeButtonPressed() override;
    void resized() override;

private:
    void refreshInspector();
    void applyBoundsFromInspector();
    void applyOrderFromInspector();
    void applyStyleFromInspector();
    void applyLayoutFromInspector();
    void applyParentFromInspector();
    void applyFlowFromInspector();
    void applySizeConstraintsFromInspector();
    void applyNodePresentationFromInspector();
    void beginPropertyEdit();
    void endPropertyEdit();

    const px3::ui::InstrumentSceneDocument& document;
    Callbacks callbacks;
    juce::ComboBox regionSelector;
    juce::Label regionLabel;
    juce::ComboBox parentSelector;
    juce::Label parentLabel;
    juce::ComboBox layoutSelector;
    juce::Label layoutLabel;
    juce::ComboBox styleSelector;
    juce::Label styleLabel;
    juce::ComboBox kindSelector;
    juce::Label kindLabel;
    juce::TextEditor labelEditor;
    juce::Label labelEditorLabel;
    juce::ComboBox bindingSelector;
    juce::Label bindingLabel;
    juce::ToggleButton visibilityToggle { "Visible" };
    std::array<juce::Label, 12> propertyLabels;
    std::array<juce::Slider, 12> propertySliders;
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::TextButton saveButton { "Save Layout" };
    juce::Label statusLabel;
    juce::StringArray regionIds;
    juce::StringArray parentIds;
    juce::StringArray styleIds;
    juce::StringArray bindingIds;
    juce::String selectedRegionId;
    bool suppressCallbacks { false };
    bool propertyEditActive { false };
};

class UILayoutSelectionOverlay final : public juce::Component
{
public:
    using BoundsChanged = std::function<void(juce::Rectangle<float>)>;

    explicit UILayoutSelectionOverlay(BoundsChanged callback);

    void setSelection(juce::Rectangle<int> parentBounds,
                      juce::Rectangle<float> normalizedBounds,
                      bool active);
    bool hitTest(int x, int y) override;
    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

    std::function<void()> onEditBegin;
    std::function<void()> onEditEnd;

private:
    juce::Rectangle<float> normalizedFromPixels(juce::Rectangle<float> bounds) const;
    void updateFrame();

    BoundsChanged boundsChanged;
    juce::Rectangle<int> parentBounds;
    juce::Rectangle<float> normalizedBounds;
    juce::Rectangle<float> frameBounds;
    juce::Rectangle<float> dragStartBounds;
    juce::Point<float> dragStartPoint;
    bool active { false };
    bool dragging { false };
    bool resizeLeft { false };
    bool resizeRight { false };
    bool resizeTop { false };
    bool resizeBottom { false };
};

#endif