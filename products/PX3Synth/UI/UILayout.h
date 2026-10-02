#pragma once

#include <JuceHeader.h>

#include <vector>

namespace px3::ui
{
enum class InstrumentSceneNodeKind
{
    container,
    parameterControl,
    display,
    decoration
};

enum class InstrumentSceneLayoutMode
{
    absolute,
    row,
    column,
    grid,
    overlay
};

struct InstrumentSceneNode
{
    juce::String id;
    juce::String parentId;
    InstrumentSceneNodeKind kind { InstrumentSceneNodeKind::container };
    InstrumentSceneLayoutMode layout { InstrumentSceneLayoutMode::absolute };
    juce::String bindingId;
    juce::String styleToken;
    juce::String label;
    juce::Rectangle<float> bounds;
    juce::Point<float> minimumSize;
    juce::Point<float> maximumSize;
    float flexGrow { 1.0f };
    float spacing { 0.0f };
    int gridColumns { 0 };
    int order { 0 };
    bool visible { true };
};

struct InstrumentSceneStyleToken
{
    juce::String id;
    juce::String background { "#171A1C" };
    juce::String foreground { "#E6E9E7" };
    juce::String accent { "#68A9C8" };
    juce::String texture { "none" };
    float knobDiameter { 54.0f };
    float labelSize { 11.0f };
    float borderRadius { 4.0f };
    float borderWidth { 1.0f };
};

class InstrumentSceneDocument final
{
public:
    static constexpr int currentSchemaVersion = 1;

    bool loadJson(const juce::String& text, juce::String& error);
    juce::String toJson() const;

    bool addStyleToken(const InstrumentSceneStyleToken& token, juce::String& error);
    bool addNode(const InstrumentSceneNode& region, juce::String& error);
    bool setNodeStyleToken(const juce::String& id,
                           const juce::String& styleToken,
                           juce::String& error);
    bool setLayoutMode(const juce::String& id,
                       InstrumentSceneLayoutMode layout,
                       juce::String& error);
    bool setFlowProperties(const juce::String& id,
                           float flexGrow,
                           float spacing,
                           int gridColumns,
                           juce::String& error);
    bool setParentNode(const juce::String& id,
                       const juce::String& parentId,
                       juce::String& error);
    bool setBounds(const juce::String& id,
                   juce::Rectangle<float> bounds,
                   juce::String& error);
    bool setOrder(const juce::String& id, int order, juce::String& error);
    bool setVisible(const juce::String& id, bool visible, juce::String& error);

    void beginTransaction();
    bool commitTransaction();
    void cancelTransaction();
    bool undo();
    bool redo();

    const InstrumentSceneNode* findNode(const juce::String& id) const noexcept;
    bool isNodeVisible(const juce::String& id) const noexcept;
    const InstrumentSceneStyleToken* findStyleToken(const juce::String& id) const noexcept;
    const std::vector<InstrumentSceneNode>& getNodes() const noexcept { return nodes; }
    const std::vector<InstrumentSceneStyleToken>& getStyleTokens() const noexcept { return styleTokens; }
    juce::Rectangle<float> resolveBounds(const juce::String& id,
                                         juce::Rectangle<float> rootBounds) const;

private:
    bool validate(juce::String& error) const;
    bool restoreSnapshot(const juce::String& snapshot);

    std::vector<InstrumentSceneNode> nodes;
    std::vector<InstrumentSceneStyleToken> styleTokens;
    juce::String transactionSnapshot;
    std::vector<juce::String> undoSnapshots;
    std::vector<juce::String> redoSnapshots;
};
}