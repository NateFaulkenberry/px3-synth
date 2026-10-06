#pragma once

#include <JuceHeader.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

class UIConfig;

namespace px3::ui
{

// Where an FX stage processes, which is what the FX page is organised by.
//
//   instrument - ahead of the dry/send split: ANALOG (inside every voice) and
//                VIBE (one insert on the whole instrument). Fixed order.
//   send       - the FX bus: the stages the sources' sends feed, in series,
//                and the only stages that can be reordered.
//   master     - the finished mix (dry + FX return): LUCY then SPREAD. Fixed.
enum class FxDomain { instrument, send, master };

class FxRackCanvas;

// The thin tab above every card on the FX page. It carries the card's
// metadata - which domain it belongs to, its position in the send chain - and,
// on a send card only, the drag handle. Non-send cards have a tag and no
// handle, so "can this move?" is answered by the absence of the grip rather
// than by a refusal after the fact.
class FxRackRail final : public juce::Component,
                         public juce::TooltipClient
{
public:
    FxRackRail(FxRackCanvas& owner, int stage);

    void setDomain(FxDomain domain, int sendPosition, int sendCount);
    void setHighlighted(bool highlighted);
    void setActive(bool active);
    void setDragging(bool dragging);

    int stage() const noexcept { return stageId; }
    FxDomain domain() const noexcept { return stageDomain; }
    bool hasHandle() const noexcept { return stageDomain == FxDomain::send; }
    bool isHighlighted() const noexcept { return highlighted; }
    // The grip, in rail coordinates. Empty on a rail without one.
    juce::Rectangle<int> handleBounds() const;
    juce::String tagText() const;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    juce::String getTooltip() override;

private:
    FxRackCanvas& canvas;
    int stageId { 0 };
    FxDomain stageDomain { FxDomain::send };
    int position { 0 };
    int count { 0 };
    bool highlighted { false };
    bool active { true };
    bool dragging { false };
};

// The scrolling content of the FX page: three sections - INSTRUMENT, SEND FX,
// MASTER - each a left-to-right row of cards that wraps onto further rows.
//
// The canvas paints the page furniture (section headers, the arrows between
// cards, continuation marks where a row wraps, the FX RETURN endpoint, the
// master output cap) and owns the rails. It does not own the cards; FxPanel
// hands it the components and the order to show them in.
//
// Reordering is done by dragging a send card's rail. The canvas reports the
// order the user asked for and does not keep it: the processor is the
// authority, and the new order comes back through setSections like any other.
class FxRackCanvas final : public juce::Component,
                           public juce::TooltipClient
{
public:
    // Everything about the page's geometry and furniture, read from "fx.rack"
    // in UIConfig.json.
    struct Style
    {
        int padX { 12 };
        int padTop { 8 };
        int padBottom { 18 };
        int leadIn { 18 };          // gutter left of each row: the incoming arrow
        int gap { 18 };             // between cards in a row: the arrow
        int rowGap { 24 };          // between wrapped rows: the continuation mark
        int sectionGap { 18 };
        int headerHeight { 32 };
        int headerGap { 6 };
        int railHeight { 18 };
        int minCardWidth { 236 };
        int maxCardWidth { 292 };
        int returnBandHeight { 30 };
        int endCapWidth { 58 };
        int defaultCardHeight { 340 };
        int tweenMs { 160 };
        juce::Colour arrowColour { juce::Colour::fromRGBA(255, 255, 255, 64) };
        juce::Colour placeholderColour { juce::Colour::fromRGBA(255, 255, 255, 80) };
        juce::Colour dropRegionColour { juce::Colour::fromRGBA(255, 255, 255, 8) };
    };

    struct Stage
    {
        int id { 0 };
        juce::Component* card { nullptr };
        juce::String name;
        juce::String styleKey;   // cards.<key>, and fx.rack.cardHeight.<key>
        bool active { true };
    };

    FxRackCanvas();
    ~FxRackCanvas() override;

    void setUIConfig(std::shared_ptr<const UIConfig> config);
    const Style& style() const noexcept { return rackStyle; }

    // The three sections' stages, in the order they process. Cards are made
    // children of the canvas here.
    void setSections(std::vector<Stage> instrument, std::vector<Stage> send, std::vector<Stage> master);
    void setStageActive(int stage, bool active);

    // Lays the page out across `width` and returns the height it needs.
    // `animate` slides cards that only changed position (a reorder) instead of
    // jumping them; a resize always places them directly.
    int layoutForWidth(int width, bool animate);

    // The single bus send: how much source signal goes into the FX bus. The
    // editor attaches it to mix.send.fx.level.
    juce::Slider& busSendKnob() noexcept { return busSend; }

    // Raised with the send stages in their new order when a drag ends in a
    // different place. Not applied here.
    std::function<void(const std::vector<int>&)> onSendOrderChanged;

    // ---- rail gestures (FxRackRail forwards in canvas coordinates) ----------
    void railPressed(int stage, juce::Point<int> position);
    void railDragged(juce::Point<int> position);
    void railReleased(juce::Point<int> position);
    void setHoveredStage(int stage);

    // ---- state, for the panel and for tests ---------------------------------
    std::vector<int> stagesIn(FxDomain domain) const;
    bool hasStage(int stage) const;
    FxDomain domainOf(int stage) const;
    juce::Rectangle<int> sectionBounds(FxDomain domain) const;
    juce::Rectangle<int> headerBounds(FxDomain domain) const;
    // Where a stage's rail + card rest when nothing is moving.
    juce::Rectangle<int> restingSlot(int stage) const;
    FxRackRail* railFor(int stage) const;
    juce::Rectangle<int> fxReturnBounds() const noexcept { return returnBounds; }
    juce::Rectangle<int> endCapBounds() const noexcept { return capBounds; }
    juce::Rectangle<int> busSendLabelBounds() const noexcept { return sendLabelBounds; }
    int cardWidth() const noexcept { return laidCardWidth; }
    int columns() const noexcept { return laidColumns; }
    bool isDragging() const noexcept { return draggedStage >= 0; }
    int dragInsertionIndex() const noexcept { return insertionIndex; }
    int hoveredStage() const noexcept { return hovered; }
    // Off in tests that read positions straight after a reorder.
    void setMotionEnabled(bool enabled) noexcept { motionEnabled = enabled; }

    static juce::String tagFor(FxDomain domain, int stage, int sendPosition);
    static juce::String tooltipFor(FxDomain domain, int stage);
    static juce::String busSendLabel();
    static juce::String busSendTooltip();

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& event) override;
    juce::String getTooltip() override;

private:
    struct Section
    {
        FxDomain domain { FxDomain::send };
        std::vector<Stage> stages;
        juce::Rectangle<int> header;
        juce::Rectangle<int> bounds;      // header to last row (and the return band)
        juce::Rectangle<int> cardArea;    // union of the resting cells
        std::vector<juce::Rectangle<int>> cells;
        std::vector<int> rowOfCell;
    };

    // Follows the cursor over a card's own controls, which the canvas never
    // sees, so hovering anywhere on a send card reveals its handle.
    struct HoverWatcher final : public juce::MouseListener
    {
        explicit HoverWatcher(FxRackCanvas& ownerIn) : owner(ownerIn) {}
        void mouseEnter(const juce::MouseEvent& e) override { owner.cardHover(e.eventComponent, true); }
        void mouseExit(const juce::MouseEvent& e) override { owner.cardHover(e.eventComponent, false); }
        FxRackCanvas& owner;
    };

    void cardHover(juce::Component* component, bool entered);
    Section& section(FxDomain domain);
    const Section& section(FxDomain domain) const;
    const Stage* findStage(int stage) const;
    int cardHeightFor(const Stage& stage) const;
    void placeStage(int stage, juce::Rectangle<int> slot, bool animate);
    void updateRails();
    void previewSendOrder(const std::vector<int>& order);

    void paintHeader(juce::Graphics& g, const Section& s) const;
    void paintFlow(juce::Graphics& g, const Section& s) const;
    void paintReturn(juce::Graphics& g) const;
    void paintEndCap(juce::Graphics& g) const;
    void paintDrag(juce::Graphics& g) const;
    void drawArrow(juce::Graphics& g, float x1, float x2, float y) const;

    std::shared_ptr<const UIConfig> uiConfig;
    Style rackStyle;
    std::array<Section, 3> sections;
    std::map<int, std::unique_ptr<FxRackRail>> rails;
    HoverWatcher hoverWatcher { *this };
    std::vector<juce::Component*> watchedCards;

    juce::Slider busSend;
    juce::Rectangle<int> sendLabelBounds;
    juce::Rectangle<int> sendValueBounds;
    juce::Rectangle<int> returnBounds;
    juce::Rectangle<int> capBounds;
    int laidWidth { 0 };
    int laidCardWidth { 0 };
    int laidColumns { 1 };
    bool motionEnabled { true };

    int hovered { -1 };
    int pressedStage { -1 };
    int draggedStage { -1 };
    int insertionIndex { -1 };
    juce::Point<int> pressPosition;
    juce::Point<int> grabOffset;
    std::vector<int> previewOrder;

    static constexpr int kDragThresholdPx = 4;
};

} // namespace px3::ui
