#include "FxRack.h"

#include "FxChain.h"
#include "FxChainLayout.h"
#include "Theme.h"
#include "UIConfig.h"

#include <algorithm>

namespace px3::ui
{
namespace th = px3::ui::theme;

namespace
{
constexpr std::array<FxDomain, 3> kDomains { { FxDomain::instrument, FxDomain::send, FxDomain::master } };

std::size_t slotOf(FxDomain domain) noexcept
{
    switch (domain)
    {
        case FxDomain::instrument: return 0;
        case FxDomain::send:       return 1;
        case FxDomain::master:     return 2;
    }
    return 1;
}

// The grip: two columns of three dots. Drawn, not typed, so it is the same
// glyph on every platform font.
void drawGrip(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    g.setColour(colour);
    const auto dot = 2.0f;
    const auto pitch = 4.0f;
    const auto x0 = area.getCentreX() - (pitch + dot) * 0.5f;
    const auto y0 = area.getCentreY() - (2.0f * pitch + dot) * 0.5f;
    for (int column = 0; column < 2; ++column)
        for (int row = 0; row < 3; ++row)
            g.fillRect(x0 + static_cast<float>(column) * pitch, y0 + static_cast<float>(row) * pitch, dot, dot);
}

void drawText(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
              th::Type type, juce::Colour colour, juce::Justification justification = juce::Justification::centredLeft)
{
    g.setColour(colour);
    g.setFont(th::font(type));
    g.drawText(text, area, justification, false);
}

int textWidthOf(const juce::String& text, th::Type type)
{
    return juce::roundToInt(std::ceil(th::textWidth(text, type))) + 2;
}

juce::String sectionTitle(FxDomain domain)
{
    switch (domain)
    {
        case FxDomain::instrument: return "INSTRUMENT";
        case FxDomain::send:       return "SEND FX";
        case FxDomain::master:     return "MASTER";
    }
    return {};
}

juce::String sectionSubtitle(FxDomain domain)
{
    switch (domain)
    {
        case FxDomain::instrument: return "Processes the complete instrument";
        case FxDomain::send:       return "Processes signal sent to the FX bus";
        case FxDomain::master:     return "Processes the finished mix: dry signal + FX return";
    }
    return {};
}

juce::String sectionTooltip(FxDomain domain)
{
    switch (domain)
    {
        case FxDomain::instrument:
            return "Runs on the whole instrument, before the dry/send split. ANALOG runs inside every voice; "
                   "VIBE is one insert on the summed instrument, so the dry signal and the FX bus are both vibed. "
                   "Send amounts do not affect these, and they cannot join the send chain.";
        case FxDomain::send:
            return "The sources' FX sends (set per source on the MIX page) feed one FX bus. These effects process "
                   "that bus in series: left to right, row after row. Drag a card by its handle to change the order. "
                   "The bus then passes the FX BUS EQ / COMP and the FX RETURN fader and joins the dry signal.";
        case FxDomain::master:
            return "Master inserts: they process the finished mix (dry signal + FX return), LUCY then SPREAD. "
                   "Send amounts do not affect them, and they cannot be reordered.";
    }
    return {};
}
} // namespace

// ============================================================================
// FxRackRail
// ============================================================================

FxRackRail::FxRackRail(FxRackCanvas& owner, int stage)
    : canvas(owner), stageId(stage)
{
    setOpaque(true);
}

void FxRackRail::setDomain(FxDomain domain, int sendPosition, int sendCount)
{
    if (stageDomain == domain && position == sendPosition && count == sendCount) { return; }
    stageDomain = domain;
    position = sendPosition;
    count = sendCount;
    setMouseCursor(hasHandle() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void FxRackRail::setHighlighted(bool shouldHighlight)
{
    if (highlighted == shouldHighlight) { return; }
    highlighted = shouldHighlight;
    repaint();
}

void FxRackRail::setActive(bool isActive)
{
    if (active == isActive) { return; }
    active = isActive;
    repaint();
}

void FxRackRail::setDragging(bool isDragging)
{
    if (dragging == isDragging) { return; }
    dragging = isDragging;
    repaint();
}

juce::Rectangle<int> FxRackRail::handleBounds() const
{
    if (! hasHandle()) { return {}; }
    return { 0, 0, 22, getHeight() };
}

juce::String FxRackRail::tagText() const
{
    return FxRackCanvas::tagFor(stageDomain, stageId, position);
}

void FxRackRail::paint(juce::Graphics& g)
{
    const auto lit = highlighted || dragging;
    auto face = th::colour::rail;
    if (dragging)      face = face.brighter(0.35f);
    else if (lit)      face = face.brighter(0.18f);
    g.fillAll(face);

    auto area = getLocalBounds();
    if (hasHandle())
    {
        const auto grip = area.removeFromLeft(22);
        // The handle is always there, quiet until the card is hovered: a send
        // card always says it can move, and says it louder when it is about to.
        drawGrip(g, grip.toFloat(), lit ? th::colour::textPrimary : th::colour::textDim);
    }
    else
    {
        area.removeFromLeft(7);
    }

    const auto tagColour = ! active ? th::colour::textDim
                                    : (lit ? th::colour::textLabel : th::colour::textSecondary);
    if (! active)
    {
        drawText(g, "BYPASSED", area.removeFromRight(64).withTrimmedRight(7), th::Type::secondary,
                 th::colour::textSecondary, juce::Justification::centredRight);
    }
    drawText(g, tagText(), area, th::Type::secondary, tagColour);
}

void FxRackRail::mouseDown(const juce::MouseEvent& event)
{
    if (hasHandle()) { canvas.railPressed(stageId, event.getEventRelativeTo(&canvas).getPosition()); }
}

void FxRackRail::mouseDrag(const juce::MouseEvent& event)
{
    if (hasHandle()) { canvas.railDragged(event.getEventRelativeTo(&canvas).getPosition()); }
}

void FxRackRail::mouseUp(const juce::MouseEvent& event)
{
    if (hasHandle()) { canvas.railReleased(event.getEventRelativeTo(&canvas).getPosition()); }
}

void FxRackRail::mouseEnter(const juce::MouseEvent&) { canvas.setHoveredStage(stageId); }

void FxRackRail::mouseExit(const juce::MouseEvent&)
{
    if (canvas.hoveredStage() == stageId) { canvas.setHoveredStage(-1); }
}

juce::String FxRackRail::getTooltip()
{
    auto text = FxRackCanvas::tooltipFor(stageDomain, stageId);
    if (hasHandle())
    {
        text = "Position " + juce::String(position) + " of " + juce::String(count) + " in the send chain. " + text;
    }
    return text;
}

// ============================================================================
// FxRackCanvas
// ============================================================================

FxRackCanvas::FxRackCanvas()
{
    for (std::size_t i = 0; i < sections.size(); ++i)
    {
        sections[i].domain = kDomains[i];
    }

    busSend.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    busSend.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    busSend.setRange(0.0, 1.0);
    busSend.setTooltip(busSendTooltip());
    busSend.setName("fx.rack.busSend");
    busSend.onValueChange = [this] { repaint(sendValueBounds); };
    addAndMakeVisible(busSend);
}

FxRackCanvas::~FxRackCanvas()
{
    busSend.setLookAndFeel(nullptr);
    for (auto* card : watchedCards)
    {
        card->removeMouseListener(&hoverWatcher);
    }
}

juce::String FxRackCanvas::busSendLabel()
{
    return juce::String(juce::CharPointer_UTF8("SEND \xe2\x86\x92 FX BUS"));
}

juce::String FxRackCanvas::busSendTooltip()
{
    return busSendLabel() + ": how much of the source signal is routed into the FX bus. "
           "A master trim over every source's FX send (each source's own send is on the MIX page). "
           "The dry signal is not affected.";
}

juce::String FxRackCanvas::tagFor(FxDomain domain, int stage, int sendPosition)
{
    switch (domain)
    {
        case FxDomain::instrument: return stage == px3::fxStageAnalog ? "PER VOICE" : "INSTRUMENT INSERT";
        case FxDomain::send:       return "SEND " + juce::String(sendPosition);
        case FxDomain::master:     return "MASTER INSERT";
    }
    return {};
}

juce::String FxRackCanvas::tooltipFor(FxDomain domain, int stage)
{
    switch (domain)
    {
        case FxDomain::instrument:
            return stage == px3::fxStageAnalog
                       ? "Runs inside every voice, ahead of everything else on this page. Fixed position."
                       : "One insert on the whole instrument, before the dry/send split: the dry signal and the FX bus "
                         "are both vibed. Fixed position; not part of the send chain.";
        case FxDomain::send:
            return "Processes the FX bus. Drag the handle to reorder.";
        case FxDomain::master:
            return "Processes the finished mix, after the FX return. Fixed position; not part of the send chain.";
    }
    return {};
}

void FxRackCanvas::setUIConfig(std::shared_ptr<const UIConfig> config)
{
    uiConfig = std::move(config);
    const Style defaults;
    rackStyle = defaults;
    if (uiConfig != nullptr)
    {
        const auto i = [this](const char* key, int fallback) { return uiConfig->getInt(juce::String("fx.rack.") + key, fallback); };
        const auto c = [this](const char* key, juce::Colour fallback) { return uiConfig->getColour(juce::String("fx.rack.") + key, fallback); };
        rackStyle.padX = i("padX", defaults.padX);
        rackStyle.padTop = i("padTop", defaults.padTop);
        rackStyle.padBottom = i("padBottom", defaults.padBottom);
        rackStyle.leadIn = i("leadIn", defaults.leadIn);
        rackStyle.gap = i("gap", defaults.gap);
        rackStyle.rowGap = i("rowGap", defaults.rowGap);
        rackStyle.sectionGap = i("sectionGap", defaults.sectionGap);
        rackStyle.headerHeight = i("headerHeight", defaults.headerHeight);
        rackStyle.headerGap = i("headerGap", defaults.headerGap);
        rackStyle.railHeight = i("railHeight", defaults.railHeight);
        rackStyle.minCardWidth = i("minCardWidth", defaults.minCardWidth);
        rackStyle.maxCardWidth = i("maxCardWidth", defaults.maxCardWidth);
        rackStyle.returnBandHeight = i("returnBandHeight", defaults.returnBandHeight);
        rackStyle.endCapWidth = i("endCapWidth", defaults.endCapWidth);
        rackStyle.defaultCardHeight = i("cardHeight.default", defaults.defaultCardHeight);
        rackStyle.tweenMs = i("tweenMs", defaults.tweenMs);
        rackStyle.arrowColour = c("arrowColour", defaults.arrowColour);
        rackStyle.placeholderColour = c("placeholderColour", defaults.placeholderColour);
        rackStyle.dropRegionColour = c("dropRegionColour", defaults.dropRegionColour);
    }
    repaint();
}

FxRackCanvas::Section& FxRackCanvas::section(FxDomain domain) { return sections[slotOf(domain)]; }
const FxRackCanvas::Section& FxRackCanvas::section(FxDomain domain) const { return sections[slotOf(domain)]; }

void FxRackCanvas::setSections(std::vector<Stage> instrument, std::vector<Stage> send, std::vector<Stage> master)
{
    section(FxDomain::instrument).stages = std::move(instrument);
    section(FxDomain::send).stages = std::move(send);
    section(FxDomain::master).stages = std::move(master);

    std::vector<juce::Component*> cards;
    for (const auto& s : sections)
    {
        for (const auto& stage : s.stages)
        {
            if (stage.card == nullptr) { continue; }
            cards.push_back(stage.card);
            if (stage.card->getParentComponent() != this) { addAndMakeVisible(*stage.card); }

            auto& rail = rails[stage.id];
            if (rail == nullptr)
            {
                rail = std::make_unique<FxRackRail>(*this, stage.id);
                addAndMakeVisible(*rail);
            }
        }
    }

    for (auto* card : cards)
    {
        if (std::find(watchedCards.begin(), watchedCards.end(), card) == watchedCards.end())
        {
            card->addMouseListener(&hoverWatcher, true);
            watchedCards.push_back(card);
        }
    }

    updateRails();
}

void FxRackCanvas::updateRails()
{
    for (const auto& s : sections)
    {
        const auto count = static_cast<int>(s.stages.size());
        for (int i = 0; i < count; ++i)
        {
            const auto& stage = s.stages[static_cast<std::size_t>(i)];
            if (auto* rail = railFor(stage.id))
            {
                rail->setDomain(s.domain, i + 1, count);
                rail->setActive(stage.active);
            }
        }
    }
}

void FxRackCanvas::setStageActive(int stageId, bool active)
{
    for (auto& s : sections)
    {
        for (auto& stage : s.stages)
        {
            if (stage.id == stageId) { stage.active = active; }
        }
    }
    if (auto* rail = railFor(stageId)) { rail->setActive(active); }
}

const FxRackCanvas::Stage* FxRackCanvas::findStage(int stageId) const
{
    for (const auto& s : sections)
        for (const auto& stage : s.stages)
            if (stage.id == stageId) return &stage;
    return nullptr;
}

bool FxRackCanvas::hasStage(int stageId) const { return findStage(stageId) != nullptr; }

FxDomain FxRackCanvas::domainOf(int stageId) const
{
    for (const auto& s : sections)
        for (const auto& stage : s.stages)
            if (stage.id == stageId) return s.domain;
    return FxDomain::send;
}

std::vector<int> FxRackCanvas::stagesIn(FxDomain domain) const
{
    std::vector<int> ids;
    for (const auto& stage : section(domain).stages) { ids.push_back(stage.id); }
    return ids;
}

juce::Rectangle<int> FxRackCanvas::sectionBounds(FxDomain domain) const { return section(domain).bounds; }
juce::Rectangle<int> FxRackCanvas::headerBounds(FxDomain domain) const { return section(domain).header; }

juce::Rectangle<int> FxRackCanvas::restingSlot(int stageId) const
{
    for (const auto& s : sections)
        for (std::size_t i = 0; i < s.stages.size() && i < s.cells.size(); ++i)
            if (s.stages[i].id == stageId) return s.cells[i];
    return {};
}

FxRackRail* FxRackCanvas::railFor(int stageId) const
{
    const auto it = rails.find(stageId);
    return it != rails.end() ? it->second.get() : nullptr;
}

int FxRackCanvas::cardHeightFor(const Stage& stage) const
{
    if (uiConfig == nullptr || stage.styleKey.isEmpty()) { return rackStyle.defaultCardHeight; }
    return uiConfig->getInt("fx.rack.cardHeight." + stage.styleKey, rackStyle.defaultCardHeight);
}

void FxRackCanvas::placeStage(int stageId, juce::Rectangle<int> slot, bool animate)
{
    const auto* stage = findStage(stageId);
    auto* rail = railFor(stageId);
    if (stage == nullptr || stage->card == nullptr || rail == nullptr) { return; }

    const auto railBounds = slot.withHeight(rackStyle.railHeight);
    const auto cardBounds = slot.withTrimmedTop(rackStyle.railHeight);

    auto& animator = juce::Desktop::getInstance().getAnimator();
    const auto move = [&](juce::Component& c, juce::Rectangle<int> target)
    {
        // A move slides; anything that changes size is placed outright, so a
        // card never re-lays its controls out on every frame of a tween.
        if (animate && motionEnabled && rackStyle.tweenMs > 0 && c.getBounds().getWidth() == target.getWidth()
            && c.getBounds().getHeight() == target.getHeight() && c.getBounds() != target)
        {
            animator.animateComponent(&c, target, 1.0f, rackStyle.tweenMs, false, 1.6, 0.0);
            return;
        }
        if (animator.isAnimating(&c)) { animator.cancelAnimation(&c, false); }
        c.setBounds(target);
    };
    move(*stage->card, cardBounds);
    move(*rail, railBounds);
}

int FxRackCanvas::layoutForWidth(int width, bool animate)
{
    const auto& st = rackStyle;
    laidWidth = width;

    const auto x0 = st.padX + st.leadIn;
    const auto available = juce::jmax(1, width - x0 - st.padX);
    laidColumns = fxRackColumns(available, st.minCardWidth, st.gap);
    laidCardWidth = fxRackCardWidth(available, laidColumns, st.gap, st.minCardWidth, st.maxCardWidth);

    auto y = st.padTop;
    returnBounds = {};
    capBounds = {};
    sendLabelBounds = {};
    sendValueBounds = {};
    auto anySection = false;

    for (auto& s : sections)
    {
        s.cells.clear();
        s.rowOfCell.clear();
        s.header = {};
        s.bounds = {};
        s.cardArea = {};
        if (s.stages.empty()) { continue; }

        if (anySection) { y += st.sectionGap; }
        anySection = true;
        const auto top = y;

        s.header = { st.padX, y, width - 2 * st.padX, st.headerHeight };
        y += st.headerHeight + st.headerGap;

        // One height for every card in a section: the tallest card's. Rows
        // then line up, and a reorder is a pure move - no card changes size
        // because it changed row.
        auto tallest = 0;
        for (const auto& stage : s.stages) { tallest = juce::jmax(tallest, cardHeightFor(stage)); }
        const std::vector<int> heights(s.stages.size(), st.railHeight + tallest);

        const auto flow = fxRackFlow({ x0, y }, laidCardWidth, laidColumns, st.gap, st.rowGap, heights);
        s.cells = flow.cells;
        s.rowOfCell = flow.rowOfCell;
        for (const auto& cell : s.cells) { s.cardArea = s.cardArea.isEmpty() ? cell : s.cardArea.getUnion(cell); }
        y += flow.height;

        if (s.domain == FxDomain::send && ! s.cells.empty())
        {
            returnBounds = { st.padX, y, width - 2 * st.padX, st.returnBandHeight };
            y += st.returnBandHeight;
        }

        if (s.domain == FxDomain::master && ! s.cells.empty())
        {
            const auto last = s.cells.back();
            const auto room = width - st.padX - last.getRight();
            const auto cardTop = last.getY() + st.railHeight;
            capBounds = room >= st.endCapWidth
                            ? juce::Rectangle<int>(last.getRight(), cardTop, st.endCapWidth, last.getHeight() - st.railHeight)
                            : juce::Rectangle<int>(last.getX(), last.getBottom(), last.getWidth(), st.returnBandHeight);
            if (room < st.endCapWidth) { y += st.returnBandHeight; }
        }

        s.bounds = { st.padX, top, width - 2 * st.padX, y - top };

        if (s.domain == FxDomain::send)
        {
            // The bus send sits at the right of the SEND FX header: label,
            // knob, value. It is the one control on the page that is not an
            // effect's, so it lives with the bus rather than on a card.
            auto right = s.header.withTrimmedBottom(1);
            sendValueBounds = right.removeFromRight(40);
            const auto knobSize = juce::jmin(26, right.getHeight() - 4);
            const auto knob = right.removeFromRight(knobSize + 6).withSizeKeepingCentre(knobSize, knobSize);
            busSend.setBounds(knob);
            sendLabelBounds = right.removeFromRight(textWidthOf(busSendLabel(), th::Type::label) + 8);
        }
    }

    busSend.setVisible(! section(FxDomain::send).stages.empty());

    for (const auto& s : sections)
    {
        for (std::size_t i = 0; i < s.stages.size() && i < s.cells.size(); ++i)
        {
            if (s.stages[i].id == draggedStage) { continue; }
            placeStage(s.stages[i].id, s.cells[i], animate);
        }
    }

    updateRails();
    repaint();
    return y + st.padBottom;
}

// ---- hover -----------------------------------------------------------------

void FxRackCanvas::cardHover(juce::Component* component, bool entered)
{
    // Which card the event belongs to: the component itself or an ancestor.
    for (const auto& s : sections)
    {
        for (const auto& stage : s.stages)
        {
            if (stage.card == nullptr) { continue; }
            if (component != stage.card && ! stage.card->isParentOf(component)) { continue; }

            if (entered) { setHoveredStage(stage.id); }
            else if (hovered == stage.id && ! stage.card->isMouseOver(true))
            {
                setHoveredStage(-1);
            }
            return;
        }
    }
}

void FxRackCanvas::setHoveredStage(int stageId)
{
    if (hovered == stageId) { return; }
    if (auto* old = railFor(hovered)) { old->setHighlighted(false); }
    hovered = stageId;
    if (auto* now = railFor(hovered)) { now->setHighlighted(true); }
}

// ---- dragging --------------------------------------------------------------

void FxRackCanvas::railPressed(int stageId, juce::Point<int> position)
{
    if (domainOf(stageId) != FxDomain::send || ! hasStage(stageId)) { return; }
    pressedStage = stageId;
    pressPosition = position;
    grabOffset = position - restingSlot(stageId).getPosition();
}

void FxRackCanvas::previewSendOrder(const std::vector<int>& order)
{
    auto& s = section(FxDomain::send);
    for (std::size_t i = 0; i < order.size() && i < s.cells.size(); ++i)
    {
        if (order[i] == draggedStage) { continue; }
        placeStage(order[i], s.cells[i], true);
    }
}

void FxRackCanvas::railDragged(juce::Point<int> position)
{
    if (pressedStage < 0) { return; }

    auto& s = section(FxDomain::send);
    if (draggedStage < 0)
    {
        // A press has to travel before it becomes a drag, or the tremor in an
        // ordinary click would reorder the chain.
        if (position.getDistanceFrom(pressPosition) < kDragThresholdPx) { return; }
        draggedStage = pressedStage;
        previewOrder = stagesIn(FxDomain::send);
        const auto it = std::find(previewOrder.begin(), previewOrder.end(), draggedStage);
        insertionIndex = static_cast<int>(std::distance(previewOrder.begin(), it));

        if (auto* rail = railFor(draggedStage))
        {
            rail->setDragging(true);
            // Keeps drag events coming while the cursor rests at the edge of
            // the view, so the page can scroll under a held card.
            rail->beginDragAutoRepeat(40);
        }
        if (const auto* stage = findStage(draggedStage); stage != nullptr && stage->card != nullptr)
        {
            stage->card->toFront(false);
        }
        if (auto* rail = railFor(draggedStage)) { rail->toFront(false); }
    }

    if (auto* view = findParentComponentOfClass<juce::Viewport>())
    {
        const auto inView = view->getLocalPoint(this, position);
        view->autoScroll(inView.x, inView.y, 28, 14);
    }

    // The card follows the cursor, held inside the send section: the chain is
    // the only place it can go, and the bounds say so while it moves.
    const auto slot = restingSlot(draggedStage);
    auto topLeft = position - grabOffset;
    const auto area = s.cardArea;
    topLeft.x = juce::jlimit(area.getX() - rackStyle.leadIn, juce::jmax(area.getX(), laidWidth - rackStyle.padX - slot.getWidth()), topLeft.x);
    topLeft.y = juce::jlimit(area.getY() - rackStyle.headerGap, juce::jmax(area.getY(), area.getBottom() - slot.getHeight()), topLeft.y);
    const auto lifted = slot.withPosition(topLeft);
    placeStage(draggedStage, lifted, false);

    const auto target = fxRackInsertionIndex(s.cells, lifted.getCentre());
    if (target >= 0 && target != insertionIndex)
    {
        auto order = previewOrder;
        order.erase(std::remove(order.begin(), order.end(), draggedStage), order.end());
        order.insert(order.begin() + juce::jlimit(0, static_cast<int>(order.size()), target), draggedStage);
        previewOrder = order;
        insertionIndex = target;
        previewSendOrder(previewOrder);
    }

    repaint(s.bounds.expanded(24));
}

void FxRackCanvas::railReleased(juce::Point<int>)
{
    const auto wasDragging = draggedStage >= 0;
    const auto dropped = draggedStage;
    const auto order = previewOrder;
    pressedStage = -1;
    draggedStage = -1;
    insertionIndex = -1;
    previewOrder.clear();
    if (auto* rail = railFor(dropped)) { rail->setDragging(false); }
    juce::Component::beginDragAutoRepeat(0);

    if (! wasDragging) { return; }

    if (order != stagesIn(FxDomain::send) && onSendOrderChanged != nullptr)
    {
        // Reported, not applied. The panel is handed the new order back
        // (through the processor) and lays out from it.
        onSendOrderChanged(order);
    }

    // Settle: the dropped card slides from where it was let go into its cell,
    // the rest finish their shuffle. If nothing changed they slide home.
    layoutForWidth(laidWidth, true);
}

// ---- painting --------------------------------------------------------------

void FxRackCanvas::drawArrow(juce::Graphics& g, float x1, float x2, float y) const
{
    if (x2 - x1 < 8.0f) { return; }
    g.setColour(rackStyle.arrowColour);
    g.fillRect(juce::Rectangle<float>(x1, y - 0.5f, x2 - x1 - 3.0f, 1.0f));
    juce::Path head;
    head.startNewSubPath(x2 - 5.0f, y - 3.5f);
    head.lineTo(x2, y);
    head.lineTo(x2 - 5.0f, y + 3.5f);
    g.strokePath(head, juce::PathStrokeType(1.2f, juce::PathStrokeType::mitered));
}

void FxRackCanvas::paintHeader(juce::Graphics& g, const Section& s) const
{
    auto area = s.header;
    // A rule under the header: the section's edge, quiet enough that the page
    // is not a stack of boxes.
    g.setColour(th::colour::railEdge);
    g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);

    auto text = area.withTrimmedBottom(1);
    const auto title = sectionTitle(s.domain);
    const auto titleWidth = textWidthOf(title, th::Type::heading);
    drawText(g, title, text.removeFromLeft(titleWidth), th::Type::heading, th::colour::textPrimary);
    text.removeFromLeft(10);

    if (s.domain == FxDomain::send)
    {
        // The bus this section is: a small outlined tag, not a button.
        const juce::String bus = "FX BUS";
        const auto chip = text.removeFromLeft(textWidthOf(bus, th::Type::secondary) + 12)
                              .withSizeKeepingCentre(textWidthOf(bus, th::Type::secondary) + 12, 16);
        g.setColour(th::colour::textDim);
        g.drawRect(chip, 1);
        drawText(g, bus, chip, th::Type::secondary, th::colour::textLabel, juce::Justification::centred);
        text.removeFromLeft(10);
    }

    const auto subtitle = sectionSubtitle(s.domain);
    g.setFont(juce::FontOptions(11.0f));
    g.setColour(th::colour::textSecondary);
    const auto subtitleWidth = juce::roundToInt(std::ceil(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), subtitle))) + 2;
    g.drawText(subtitle, text.removeFromLeft(subtitleWidth), juce::Justification::centredLeft, false);

    if (s.domain == FxDomain::send)
    {
        text.removeFromLeft(14);
        const auto grip = text.removeFromLeft(10);
        if (! sendLabelBounds.isEmpty() && grip.getRight() + 110 > sendLabelBounds.getX()) { return; }
        drawGrip(g, grip.toFloat(), th::colour::textDim);
        text.removeFromLeft(4);
        drawText(g, "DRAG TO REORDER", text.withRight(juce::jmax(text.getX(), sendLabelBounds.getX() - 8)),
                 th::Type::secondary, th::colour::textDim);

        // The bus send: a label that says what it is, and its value.
        drawText(g, busSendLabel(), sendLabelBounds, th::Type::label, th::colour::textLabel,
                 juce::Justification::centredRight);
        drawText(g, juce::String(juce::roundToInt(busSend.getValue() * 100.0)) + "%", sendValueBounds,
                 th::Type::value, th::colour::textValue, juce::Justification::centredLeft);
    }
}

void FxRackCanvas::paintFlow(juce::Graphics& g, const Section& s) const
{
    const auto& st = rackStyle;
    for (std::size_t i = 0; i < s.cells.size(); ++i)
    {
        const auto cell = s.cells[i];
        const auto y = static_cast<float>(cell.getY() + st.railHeight) + static_cast<float>(cell.getHeight() - st.railHeight) * 0.5f;
        const auto rowStart = i == 0 || s.rowOfCell[i] != s.rowOfCell[i - 1];
        const auto rowEnd = i + 1 == s.cells.size() || s.rowOfCell[i + 1] != s.rowOfCell[i];

        if (rowStart)
        {
            const auto gutter = static_cast<float>(cell.getX() - st.leadIn + 2);
            if (i == 0)
            {
                // The section's input.
                drawArrow(g, gutter, static_cast<float>(cell.getX() - 2), y);
            }
            else
            {
                // A wrapped row picks the chain up again from the left: a small
                // hook into the card, the answer to the drop under the last card
                // of the row above.
                g.setColour(st.arrowColour);
                const auto top = y - 10.0f;
                g.fillRect(juce::Rectangle<float>(gutter, top, 1.0f, y - top));
                drawArrow(g, gutter, static_cast<float>(cell.getX() - 2), y);
            }
        }

        if (! rowEnd)
        {
            drawArrow(g, static_cast<float>(cell.getRight() + 3), static_cast<float>(s.cells[i + 1].getX() - 3), y);
        }
        else if (i + 1 < s.cells.size())
        {
            // The row continues below: a short drop from the last card into
            // the row gap, so the eye is sent down, never back along the row.
            const auto x = static_cast<float>(cell.getCentreX());
            const auto top = static_cast<float>(cell.getBottom() + 3);
            const auto bottom = static_cast<float>(cell.getBottom() + st.rowGap - 5);
            g.setColour(st.arrowColour);
            g.fillRect(juce::Rectangle<float>(x - 0.5f, top, 1.0f, bottom - top - 2.0f));
            juce::Path head;
            head.startNewSubPath(x - 3.5f, bottom - 5.0f);
            head.lineTo(x, bottom);
            head.lineTo(x + 3.5f, bottom - 5.0f);
            g.strokePath(head, juce::PathStrokeType(1.2f, juce::PathStrokeType::mitered));
        }
    }
}

void FxRackCanvas::paintReturn(juce::Graphics& g) const
{
    const auto& s = section(FxDomain::send);
    if (returnBounds.isEmpty() || s.cells.empty()) { return; }

    // The chain's endpoint: not a card, a destination. A drop from the last
    // send card, then where the bus goes - its fixed EQ / COMP and the return.
    const auto last = s.cells.back();
    const auto x = static_cast<float>(last.getCentreX());
    const auto top = static_cast<float>(last.getBottom() + 3);
    const auto mid = static_cast<float>(returnBounds.getCentreY());
    g.setColour(rackStyle.arrowColour);
    g.fillRect(juce::Rectangle<float>(x - 0.5f, top, 1.0f, mid + 4.0f - top));
    juce::Path head;
    head.startNewSubPath(x - 3.5f, mid - 1.0f);
    head.lineTo(x, mid + 4.0f);
    head.lineTo(x + 3.5f, mid - 1.0f);
    g.strokePath(head, juce::PathStrokeType(1.2f, juce::PathStrokeType::mitered));

    const auto via = juce::String(juce::CharPointer_UTF8("FX BUS EQ / COMP  \xe2\x80\xba"));
    const juce::String endpoint = "FX RETURN";
    const auto viaWidth = textWidthOf(via, th::Type::secondary);
    const auto endWidth = textWidthOf(endpoint, th::Type::label);
    const auto total = viaWidth + 6 + endWidth;
    auto textX = juce::roundToInt(x) + 10;
    if (textX + total > returnBounds.getRight()) { textX = juce::roundToInt(x) - 10 - total; }
    auto row = juce::Rectangle<int>(textX, returnBounds.getY(), total, returnBounds.getHeight());
    drawText(g, via, row.removeFromLeft(viaWidth), th::Type::secondary, th::colour::textSecondary);
    row.removeFromLeft(6);
    drawText(g, endpoint, row, th::Type::label, th::colour::textLabel);
}

void FxRackCanvas::paintEndCap(juce::Graphics& g) const
{
    const auto& s = section(FxDomain::master);
    if (capBounds.isEmpty() || s.cells.empty()) { return; }

    // The end of the line: an arrow into a bar, and OUT. The master section
    // terminates the page; nothing follows it.
    const auto last = s.cells.back();
    if (capBounds.getX() >= last.getRight())
    {
        const auto y = static_cast<float>(capBounds.getCentreY());
        const auto barX = static_cast<float>(capBounds.getX() + rackStyle.gap);
        drawArrow(g, static_cast<float>(capBounds.getX() + 3), barX - 3.0f, y);
        g.setColour(th::colour::textSecondary);
        g.fillRect(juce::Rectangle<float>(barX, y - 14.0f, 2.0f, 28.0f));
        drawText(g, "OUT", juce::Rectangle<int>(juce::roundToInt(barX) + 6, juce::roundToInt(y) - 8, 40, 16),
                 th::Type::label, th::colour::textLabel);
    }
    else
    {
        const auto x = static_cast<float>(last.getCentreX());
        const auto top = static_cast<float>(last.getBottom() + 3);
        const auto bar = static_cast<float>(capBounds.getCentreY());
        g.setColour(rackStyle.arrowColour);
        g.fillRect(juce::Rectangle<float>(x - 0.5f, top, 1.0f, bar - top - 3.0f));
        g.setColour(th::colour::textSecondary);
        g.fillRect(juce::Rectangle<float>(x - 14.0f, bar, 28.0f, 2.0f));
        drawText(g, "OUT", juce::Rectangle<int>(juce::roundToInt(x) + 18, juce::roundToInt(bar) - 8, 40, 16),
                 th::Type::label, th::colour::textLabel);
    }
}

void FxRackCanvas::paintDrag(juce::Graphics& g) const
{
    if (draggedStage < 0) { return; }
    const auto& s = section(FxDomain::send);

    // The valid drop region: the send chain, faintly lit, and nowhere else.
    g.setColour(rackStyle.dropRegionColour);
    g.fillRect(s.cardArea.expanded(rackStyle.gap / 2, rackStyle.rowGap / 3));

    // Where the card will land.
    if (insertionIndex >= 0 && insertionIndex < static_cast<int>(s.cells.size()))
    {
        const auto cell = s.cells[static_cast<std::size_t>(insertionIndex)].toFloat().reduced(1.5f);
        g.setColour(rackStyle.placeholderColour.withMultipliedAlpha(0.18f));
        g.fillRect(cell);
        juce::Path outline;
        outline.addRectangle(cell);
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType(1.0f, juce::PathStrokeType::mitered).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(rackStyle.placeholderColour);
        g.fillPath(dashed);
    }

    // The lifted card's shadow, under it.
    if (const auto* stage = findStage(draggedStage); stage != nullptr && stage->card != nullptr)
    {
        auto lifted = stage->card->getBounds();
        if (auto* rail = railFor(draggedStage)) { lifted = lifted.getUnion(rail->getBounds()); }
        juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 16, { 0, 6 }).drawForRectangle(g, lifted);
    }
}

void FxRackCanvas::paint(juce::Graphics& g)
{
    for (const auto& s : sections)
    {
        if (s.stages.empty()) { continue; }
        if (g.clipRegionIntersects(s.header)) { paintHeader(g, s); }
        if (g.clipRegionIntersects(s.bounds.expanded(0, rackStyle.rowGap))) { paintFlow(g, s); }
    }
    if (g.clipRegionIntersects(returnBounds)) { paintReturn(g); }
    paintEndCap(g);
    paintDrag(g);
}

void FxRackCanvas::mouseMove(const juce::MouseEvent&)
{
    if (hovered >= 0) { setHoveredStage(-1); }
}

juce::String FxRackCanvas::getTooltip()
{
    const auto at = getMouseXYRelative();
    if (sendLabelBounds.contains(at) || sendValueBounds.contains(at)) { return busSendTooltip(); }
    if (returnBounds.contains(at))
    {
        return "FX RETURN: the FX bus leaves the chain through its fixed FX BUS EQ / COMP (set on the MIX page) and "
               "the FX RETURN fader, then joins the dry signal and goes on to MASTER.";
    }
    if (capBounds.contains(at)) { return "The main output: everything above has been applied."; }
    for (const auto& s : sections)
    {
        if (s.header.contains(at)) { return sectionTooltip(s.domain); }
    }
    return {};
}

} // namespace px3::ui
