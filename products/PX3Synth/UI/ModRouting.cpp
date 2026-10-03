#include "ModRouting.h"

#include "ParameterKnob.h"

#include <cmath>

namespace px3::ui::modrouting
{
namespace
{
using Processor = PX3SynthAudioProcessor;
constexpr int kLfos = Processor::kLfoSourceCount;
constexpr int kEnvs = Processor::kEnvelopeSourceCount;

const juce::Colour kPanelFill { 0xff15191a };
const juce::Colour kInk { 0xffe6e9e7 };
const juce::Colour kDimInk { 0xff8e9794 };

juce::Path cablePath(juce::Point<float> a, juce::Point<float> b)
{
    // A hanging patch cable: the control points drop below both ends, more for
    // a longer cable, so it reads as a cable rather than a wire diagram.
    const auto sag = juce::jlimit(18.0f, 90.0f, a.getDistanceFrom(b) * 0.22f);
    juce::Path path;
    path.startNewSubPath(a);
    path.cubicTo(a.x + (b.x - a.x) * 0.3f, a.y + sag, b.x - (b.x - a.x) * 0.3f, b.y + sag, b.x, b.y);
    return path;
}

void drawCable(juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour colour,
               float thickness, float alpha)
{
    const auto path = cablePath(a, b);
    g.setColour(juce::Colours::black.withAlpha(0.35f * alpha));
    g.strokePath(path, juce::PathStrokeType(thickness + 2.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded),
                 juce::AffineTransform::translation(0.0f, 1.5f));
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(path, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colour.brighter(0.6f).withAlpha(0.35f * alpha));
    g.strokePath(path, juce::PathStrokeType(juce::jmax(1.0f, thickness * 0.3f)),
                 juce::AffineTransform::translation(0.0f, -thickness * 0.2f));
}

void drawPlug(juce::Graphics& g, juce::Point<float> at, juce::Colour colour, float radius)
{
    g.setColour(juce::Colour(0xff0b0d0e));
    g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(at));
    g.setColour(colour);
    g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(at), 2.0f);
    g.fillEllipse(juce::Rectangle<float>(radius * 0.8f, radius * 0.8f).withCentre(at));
}

} // namespace

juce::Colour sourceColour(int source)
{
    static const juce::Colour colours[kSourceCount] {
        juce::Colour(0xff4fd1c5), juce::Colour(0xff63b3ed), juce::Colour(0xff9f8cf0),   // LFO 1-3
        juce::Colour(0xfff6ad55), juce::Colour(0xfff26d6d), juce::Colour(0xffed64a6),   // ENV 1-3
        juce::Colour(0xff7ddc8a), juce::Colour(0xffd6e05a), juce::Colour(0xff5fd3f3),   // MACRO 1-3
        juce::Colour(0xffe0a8ff), juce::Colour(0xffc9d1d3) };                         // MACRO 4-5
    return juce::isPositiveAndBelow(source, kSourceCount) ? colours[source] : kDimInk;
}

juce::String sourceShortName(int source)
{
    if (juce::isPositiveAndBelow(source, kLfos)) { return "LFO " + juce::String(source + 1); }
    if (juce::isPositiveAndBelow(source - kLfos, kEnvs)) { return "ENV " + juce::String(source - kLfos + 1); }
    if (juce::isPositiveAndBelow(source - kLfos - kEnvs, Processor::kMacroCount))
    {
        return "M" + juce::String(source - kLfos - kEnvs + 1);
    }
    return {};
}

bool sourceIsBipolar(int source) { return juce::isPositiveAndBelow(source, kLfos); }

juce::String RouteInfo::key() const
{
    const char* kinds[] { "g", "c", "m" };
    return juce::String(kinds[static_cast<int>(kind)]) + juce::String(slot) + ":" + juce::String(source) + ">" + destination;
}

std::vector<RouteInfo> collectRoutes(const Processor& processor)
{
    std::vector<RouteInfo> routes;
    for (int lfo = 0; lfo < kLfos; ++lfo)
    {
        const auto id = processor.getLfoAssignmentParameterId(lfo);
        if (id.isEmpty() || id == "none") { continue; }
        RouteInfo r;
        r.kind = RouteInfo::Kind::card;
        r.slot = lfo;
        r.source = lfo;
        r.destination = id;
        r.depth = processor.getLfoAmountParam(lfo).get();
        routes.push_back(r);
    }
    for (int env = 0; env < kEnvs; ++env)
    {
        const auto id = processor.getEnvelopeAssignmentParameterId(env);
        if (id.isEmpty() || id == "none") { continue; }
        RouteInfo r;
        r.kind = RouteInfo::Kind::card;
        r.slot = kLfos + env;
        r.source = kLfos + env;
        r.destination = id;
        r.depth = processor.getEnvelopeAmountParam(env).get();
        routes.push_back(r);
    }
    for (int macro = 0; macro < Processor::kMacroCount; ++macro)
    {
        for (const auto& destination : processor.getMacroDestinations(macro))
        {
            RouteInfo r;
            r.kind = RouteInfo::Kind::macro;
            r.slot = macro;
            r.source = kLfos + kEnvs + macro;
            r.destination = destination.parameterId;
            r.depth = destination.depth;
            routes.push_back(r);
        }
    }
    for (int slot = 0; slot < Processor::kGraphRouteSlots; ++slot)
    {
        const auto route = processor.getGraphRoute(slot);
        if (route.source < 0 || route.destination.isEmpty()) { continue; }
        RouteInfo r;
        r.kind = RouteInfo::Kind::graph;
        r.slot = slot;
        r.source = route.source;
        r.destination = route.destination;
        r.depth = processor.getGraphRouteDepthParam(slot).get();
        r.polarity = route.polarity;
        r.curve = route.curve;
        routes.push_back(r);
    }
    return routes;
}

int createRoute(Processor& processor, int source, const juce::String& destination, juce::String& error,
                float initialDepth)
{
    if (! juce::isPositiveAndBelow(source, kSourceCount) || ! processor.isGraphDestination(destination))
    {
        error = "That control cannot be modulated.";
        return -1;
    }
    auto freeSlot = -1;
    for (int slot = 0; slot < Processor::kGraphRouteSlots; ++slot)
    {
        const auto route = processor.getGraphRoute(slot);
        if (route.source == source && route.destination == destination) { return slot; }
        if (freeSlot < 0 && route.source < 0 && route.destination.isEmpty()) { freeSlot = slot; }
    }
    if (freeSlot < 0)
    {
        error = "All " + juce::String(Processor::kGraphRouteSlots) + " routes are in use.";
        return -1;
    }
    Processor::GraphRouteConfiguration route;
    route.source = source;
    route.destination = destination;
    if (! processor.setGraphRoute(freeSlot, route, error)) { return -1; }
    auto& depth = processor.getGraphRouteDepthParam(freeSlot);
    depth.beginChangeGesture();
    depth.setValueNotifyingHost(depth.convertTo0to1(initialDepth));
    depth.endChangeGesture();
    return freeSlot;
}

bool removeRoute(Processor& processor, const RouteInfo& route)
{
    switch (route.kind)
    {
        case RouteInfo::Kind::graph:
        {
            juce::String error;
            return processor.setGraphRoute(route.slot, {}, error);
        }
        case RouteInfo::Kind::card:
            return route.source < kLfos ? processor.setLfoAssignmentIndex(route.source, 0)
                                        : processor.setEnvelopeAssignmentIndex(route.source - kLfos, 0);
        case RouteInfo::Kind::macro:
            if (processor.isMacroDestination(route.slot, route.destination))
            {
                return ! processor.toggleMacroDestination(route.slot, route.destination);
            }
            return false;
    }
    return false;
}

juce::String destinationName(const Processor& processor, const juce::String& id)
{
    if (const auto* entry = processor.getParameterCatalog().find(id)) { return entry->name; }
    return id;
}

juce::Range<float> sweepRange(const RouteInfo& route, float base)
{
    using P = px3::synth::ModulationPolarity;
    const auto bipolar = route.polarity == P::bipolar
                      || (route.polarity == P::native && sourceIsBipolar(route.source));
    const auto depth = juce::jlimit(-1.0f, 1.0f, route.depth);
    float lo = base, hi = base;
    if (bipolar)
    {
        lo = base - std::abs(depth) * 0.5f;
        hi = base + std::abs(depth) * 0.5f;
    }
    else if (depth >= 0.0f)
    {
        hi = base + depth * (1.0f - base);
    }
    else
    {
        lo = base + depth * base;
    }
    return { juce::jlimit(0.0f, 1.0f, lo), juce::jlimit(0.0f, 1.0f, hi) };
}

//==============================================================================
ModSourceSocket::ModSourceSocket(ModDragController& controllerIn, int sourceIn, bool showLabelIn)
    : controller(controllerIn), source(sourceIn), showLabel(showLabelIn)
{
    setComponentID("mod.source." + juce::String(sourceIn));
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    setTooltip("Drag " + Processor::graphSourceName(sourceIn) + " onto any knob to modulate it");
}

juce::Rectangle<float> ModSourceSocket::jackBounds() const
{
    const auto size = juce::jmin(static_cast<float>(getHeight()) - 2.0f, showLabel ? 20.0f : 18.0f);
    return { 1.0f, (static_cast<float>(getHeight()) - size) * 0.5f, size, size };
}

void ModSourceSocket::setRouteCount(int count)
{
    if (count != routeCount)
    {
        routeCount = count;
        repaint();
    }
}

void ModSourceSocket::paint(juce::Graphics& g)
{
    const auto colour = sourceColour(source);
    const auto jack = jackBounds();
    const auto hover = isMouseOver() || controller.getDragSource() == source;
    // Hex-nut jack: dark nut, metal collar, coloured ring, black hole.
    g.setColour(juce::Colour(0xff2a2f31));
    g.fillEllipse(jack);
    g.setColour(colour.withAlpha(hover ? 1.0f : 0.85f));
    g.drawEllipse(jack.reduced(1.2f), hover ? 2.4f : 1.8f);
    g.setColour(juce::Colour(0xff050606));
    g.fillEllipse(jack.reduced(jack.getWidth() * 0.3f));
    if (routeCount > 0)
    {
        g.setColour(colour);
        g.fillEllipse(jack.reduced(jack.getWidth() * 0.38f));
    }
    if (showLabel)
    {
        g.setColour(hover ? kInk : kInk.withAlpha(0.82f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawFittedText(sourceShortName(source),
                         getLocalBounds().withTrimmedLeft(juce::roundToInt(jack.getRight()) + 4),
                         juce::Justification::centredLeft, 1);
    }
}

void ModSourceSocket::mouseDown(const juce::MouseEvent&)
{
    // The cable is plugged into the jack, so it starts at the jack's centre -
    // not wherever on the socket the click happened to land.
    const auto centre = jackBounds().getCentre();
    controller.begin(source, controller.getRoot().getLocalPoint(this, centre));
}

void ModSourceSocket::mouseDrag(const juce::MouseEvent& e)
{
    controller.move(e.getEventRelativeTo(&controller.getRoot()).position);
}

void ModSourceSocket::mouseUp(const juce::MouseEvent& e)
{
    controller.end(e.getEventRelativeTo(&controller.getRoot()).position);
    repaint();
}

//==============================================================================
class ModDragController::Overlay final : public juce::Component, private juce::Timer
{
public:
    explicit Overlay(ModDragController& ownerIn) : owner(ownerIn)
    {
        setInterceptsMouseClicks(false, false);
        setComponentID("mod.drag.overlay");
    }
    void start() { startTimerHz(30); }
    void stop() { stopTimer(); }
    void paint(juce::Graphics& g) override
    {
        if (! owner.isDragging()) { return; }
        const auto colour = sourceColour(owner.dragSource);
        if (auto* target = owner.hoverTarget.getComponent())
        {
            const auto area = getLocalArea(target, target->getLocalBounds()).toFloat().expanded(3.0f);
            g.setColour(colour.withAlpha(0.18f));
            g.fillRoundedRectangle(area, 6.0f);
            g.setColour(colour);
            g.drawRoundedRectangle(area, 6.0f, 2.0f);
            const auto name = destinationName(owner.processor, owner.destinationAt(owner.current.roundToInt()));
            const auto label = Processor::graphSourceName(owner.dragSource) + "  >  " + name;
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            const auto width = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), label) + 16;
            auto tag = juce::Rectangle<float>(static_cast<float>(width), 20.0f)
                           .withCentre({ area.getCentreX(), area.getY() - 14.0f });
            tag = tag.constrainedWithin(getLocalBounds().toFloat());
            g.setColour(juce::Colour(0xee101314));
            g.fillRoundedRectangle(tag, 4.0f);
            g.setColour(colour);
            g.drawRoundedRectangle(tag, 4.0f, 1.0f);
            g.setColour(kInk);
            g.drawText(label, tag, juce::Justification::centred, false);
        }
        drawCable(g, owner.start, owner.current, colour, 4.0f, 0.95f);
        drawPlug(g, owner.start, colour, 6.0f);
        drawPlug(g, owner.current, colour, 7.0f);
    }

private:
    void timerCallback() override
    {
        // Spring-loaded section tabs: dwelling on a tab while dragging opens it,
        // so a source on the MOD page can be dropped on an OSC or FILTER knob.
        if (! owner.isDragging() || owner.hoverTab < 0) { return; }
        if (juce::Time::getMillisecondCounterHiRes() - owner.hoverTabSince > 380.0 && owner.host.selectSection)
        {
            const auto tab = owner.hoverTab;
            owner.hoverTab = -1;
            owner.host.selectSection(tab);
            toFront(false);
        }
    }
    ModDragController& owner;
};

ModDragController::ModDragController(Processor& processorIn, juce::Component& rootIn, Host hostIn)
    : processor(processorIn), root(rootIn), host(std::move(hostIn)), overlay(std::make_unique<Overlay>(*this))
{
    root.addChildComponent(*overlay);
}

ModDragController::~ModDragController()
{
    root.removeChildComponent(overlay.get());
}

void ModDragController::begin(int source, juce::Point<float> rootPosition)
{
    dragSource = source;
    start = current = rootPosition;
    hoverTarget = nullptr;
    hoverTab = -1;
    lastError.clear();
    overlay->setBounds(root.getLocalBounds());
    overlay->setVisible(true);
    overlay->toFront(false);
    overlay->start();
    overlay->repaint();
}

void ModDragController::move(juce::Point<float> rootPosition)
{
    if (! isDragging()) { return; }
    current = rootPosition;
    juce::Component* target = nullptr;
    destinationAt(rootPosition.roundToInt(), &target);
    hoverTarget = target;
    const auto tab = host.sectionTabAt ? host.sectionTabAt(rootPosition.roundToInt()) : -1;
    if (tab != hoverTab)
    {
        hoverTab = tab;
        hoverTabSince = juce::Time::getMillisecondCounterHiRes();
    }
    overlay->repaint();
}

int ModDragController::end(juce::Point<float> rootPosition)
{
    if (! isDragging()) { return -1; }
    move(rootPosition);
    const auto source = dragSource;
    const auto destination = destinationAt(rootPosition.roundToInt());
    cancel();
    if (destination.isEmpty()) { return -1; }
    const auto slot = createRoute(processor, source, destination, lastError);
    if (slot >= 0 && host.routeCreated)
    {
        RouteInfo info;
        info.slot = slot;
        info.source = source;
        info.destination = destination;
        host.routeCreated(info);
    }
    return slot;
}

void ModDragController::cancel()
{
    dragSource = -1;
    hoverTarget = nullptr;
    hoverTab = -1;
    overlay->stop();
    overlay->setVisible(false);
}

juce::String ModDragController::destinationAt(juce::Point<int> rootPosition, juce::Component** target) const
{
    juce::String found;
    juce::Component* foundComponent = nullptr;
    // Deepest visible hit wins, through every container and viewport, so a
    // knob anywhere in the editor is a target with no list to maintain.
    std::function<void(juce::Component&)> walk = [&](juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (child == nullptr || ! child->isVisible() || child == overlay.get()) { continue; }
            if (! child->getLocalBounds().contains(child->getLocalPoint(&root, rootPosition))) { continue; }
            juce::String id;
            if (auto* slider = dynamic_cast<juce::Slider*>(child)) { id = px3::ui::parameterIdOf(*slider); }
            if (id.isEmpty()) { id = child->getProperties()[destinationProperty].toString(); }
            if (id.isNotEmpty() && processor.isGraphDestination(id))
            {
                found = id;
                foundComponent = child;
            }
            walk(*child);
        }
    };
    walk(root);
    if (target != nullptr) { *target = foundComponent; }
    return found;
}

//==============================================================================
ModPatchBar::ModPatchBar(ModDragController& controller)
{
    setComponentID("patchbar");
    title.setText("MOD", juce::dontSendNotification);
    title.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, kDimInk);
    title.setJustificationType(juce::Justification::centredRight);
    title.setTooltip("Modulation sources: drag a jack onto any knob to patch it");
    addAndMakeVisible(title);
    for (int source = 0; source < kSourceCount; ++source)
    {
        sockets.push_back(std::make_unique<ModSourceSocket>(controller, source));
        addAndMakeVisible(*sockets.back());
    }
}

void ModPatchBar::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(juce::Colour(0xff111415));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(juce::Colour(0xff2b3133));
    g.drawRoundedRectangle(area, 4.0f, 1.0f);
}

void ModPatchBar::resized()
{
    auto area = getLocalBounds().reduced(6, 2);
    title.setBounds(area.removeFromLeft(44));
    area.removeFromLeft(8);
    for (auto& socket : sockets) { socket->setBounds(area.removeFromLeft(62)); }
}

void ModPatchBar::refresh(const std::vector<RouteInfo>& routes)
{
    std::array<int, kSourceCount> counts {};
    for (const auto& route : routes)
    {
        if (juce::isPositiveAndBelow(route.source, kSourceCount)) { ++counts[static_cast<std::size_t>(route.source)]; }
    }
    for (int source = 0; source < kSourceCount; ++source)
    {
        sockets[static_cast<std::size_t>(source)]->setRouteCount(counts[static_cast<std::size_t>(source)]);
    }
}

//==============================================================================
class ModRoutingPanel::PatchView final : public juce::Component
{
public:
    PatchView(ModRoutingPanel& ownerIn, ModDragController& controller) : owner(ownerIn)
    {
        setComponentID("mod.routing.patch");
        for (int source = 0; source < kSourceCount; ++source)
        {
            sockets.push_back(std::make_unique<ModSourceSocket>(controller, source));
            addAndMakeVisible(*sockets.back());
        }
    }

    void setRoutes(const std::vector<RouteInfo>& latest)
    {
        juce::StringArray ids;
        for (const auto& route : latest) { ids.addIfNotAlreadyThere(route.destination); }
        if (ids != destinationIds)
        {
            destinationIds = ids;
            nodes.clear();
            for (const auto& id : ids)
            {
                auto node = std::make_unique<juce::Label>();
                node->setText(destinationName(owner.processor, id), juce::dontSendNotification);
                node->setFont(juce::FontOptions(11.5f));
                node->setColour(juce::Label::textColourId, kInk);
                node->setColour(juce::Label::backgroundColourId, juce::Colour(0xff1d2224));
                node->setColour(juce::Label::outlineColourId, juce::Colour(0xff394144));
                node->setMinimumHorizontalScale(0.7f);
                node->getProperties().set(destinationProperty, id);
                node->setTooltip(id);
                node->setInterceptsMouseClicks(false, false);
                addAndMakeVisible(*node);
                nodes.push_back(std::move(node));
            }
            resized();
        }
        counts.fill(0);
        for (const auto& route : latest) { ++counts[static_cast<std::size_t>(juce::jlimit(0, kSourceCount - 1, route.source))]; }
        for (int s = 0; s < kSourceCount; ++s) { sockets[static_cast<std::size_t>(s)]->setRouteCount(counts[static_cast<std::size_t>(s)]); }
        repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8, 6);
        auto sourceColumn = area.removeFromLeft(78);
        const auto rowH = juce::jlimit(14, 26, sourceColumn.getHeight() / kSourceCount);
        sourceColumn = sourceColumn.withSizeKeepingCentre(sourceColumn.getWidth(), rowH * kSourceCount);
        for (auto& socket : sockets) { socket->setBounds(sourceColumn.removeFromTop(rowH)); }
        auto destinationColumn = area.removeFromRight(juce::jmin(190, area.getWidth() / 2));
        if (nodes.empty()) { return; }
        const auto count = static_cast<int>(nodes.size());
        const auto pitch = juce::jlimit(12, 30, destinationColumn.getHeight() / count);
        auto column = destinationColumn.withSizeKeepingCentre(destinationColumn.getWidth(), pitch * count);
        for (auto& node : nodes) { node->setBounds(column.removeFromTop(pitch).reduced(0, pitch > 18 ? 3 : 1)); }
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xff0e1112));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff2b3133));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 5.0f, 1.0f);
        if (owner.routes.empty())
        {
            g.setColour(kDimInk);
            g.setFont(juce::FontOptions(12.0f));
            g.drawFittedText("Drag a source jack onto any knob to patch it.\nDwell on a tab while dragging to reach other pages.",
                             getLocalBounds().withTrimmedLeft(96).reduced(8), juce::Justification::centred, 3);
        }
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        for (const auto& route : owner.routes)
        {
            const auto a = anchorOf(route.source);
            const auto b = nodeAnchor(route.destination);
            if (a.isOrigin() || b.isOrigin()) { continue; }
            const auto selected = route.key() == owner.selectedKey;
            const auto weight = 0.45f + 0.55f * std::abs(route.depth);
            drawCable(g, a, b, sourceColour(route.source), selected ? 4.0f : 2.6f,
                      selected ? 1.0f : 0.5f + 0.4f * weight);
            drawPlug(g, b, sourceColour(route.source), 4.5f);
            drawPlug(g, a, sourceColour(route.source), 4.0f);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Click a cable to select its route (the topmost, i.e. last drawn).
        juce::String hit;
        for (const auto& route : owner.routes)
        {
            const auto a = anchorOf(route.source);
            const auto b = nodeAnchor(route.destination);
            if (a.isOrigin() || b.isOrigin()) { continue; }
            juce::Path stroke;
            juce::PathStrokeType(10.0f).createStrokedPath(stroke, cablePath(a, b));
            if (stroke.contains(e.position)) { hit = route.key(); }
        }
        owner.select(hit);
        owner.grabKeyboardFocus();
    }

private:
    juce::Point<float> anchorOf(int source) const
    {
        if (! juce::isPositiveAndBelow(source, kSourceCount)) { return {}; }
        const auto& socket = *sockets[static_cast<std::size_t>(source)];
        // From the right edge of the source column, so cables never cross the labels.
        return { static_cast<float>(socket.getRight()) + 4.0f, getLocalArea(&socket, socket.jackBounds()).getCentreY() };
    }
    juce::Point<float> nodeAnchor(const juce::String& id) const
    {
        const auto index = destinationIds.indexOf(id);
        if (index < 0) { return {}; }
        const auto bounds = nodes[static_cast<std::size_t>(index)]->getBounds().toFloat();
        return { bounds.getX() - 5.0f, bounds.getCentreY() };
    }

    ModRoutingPanel& owner;
    std::vector<std::unique_ptr<ModSourceSocket>> sockets;
    std::vector<std::unique_ptr<juce::Label>> nodes;
    juce::StringArray destinationIds;
    std::array<int, kSourceCount> counts {};
};

//==============================================================================
class ModRoutingPanel::Row final : public juce::Component
{
public:
    Row(ModRoutingPanel& ownerIn, const RouteInfo& routeIn) : owner(ownerIn), route(routeIn)
    {
        setComponentID("mod.routing.row." + route.key());
        auto& proc = owner.processor;
        name.setText(Processor::graphSourceName(route.source) + "  >  " + destinationName(proc, route.destination),
                     juce::dontSendNotification);
        name.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        name.setColour(juce::Label::textColourId, kInk);
        name.setMinimumHorizontalScale(0.6f);
        name.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(name);

        origin.setFont(juce::FontOptions(10.0f));
        origin.setColour(juce::Label::textColourId, kDimInk);
        origin.setJustificationType(juce::Justification::centredRight);
        origin.setInterceptsMouseClicks(false, false);
        origin.setText(route.kind == RouteInfo::Kind::card ? "CARD ASSIGN"
                       : route.kind == RouteInfo::Kind::macro ? "MACRO" : "SLOT " + juce::String(route.slot + 1),
                       juce::dontSendNotification);
        addAndMakeVisible(origin);

        depth.setComponentID("mod.routing.depth");
        depth.setSliderStyle(juce::Slider::LinearBar);
        depth.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        depth.setColour(juce::Slider::trackColourId, sourceColour(route.source).withAlpha(0.75f));
        depth.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1d2224));
        depth.setColour(juce::Slider::textBoxTextColourId, kInk);
        depth.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff394144));
        depth.setTooltip("Route depth");
        depth.setDoubleClickReturnValue(true, 0.0);
        if (route.kind == RouteInfo::Kind::graph)
        {
            attachment = std::make_unique<juce::SliderParameterAttachment>(proc.getGraphRouteDepthParam(route.slot), depth, nullptr);
        }
        else if (route.kind == RouteInfo::Kind::card)
        {
            auto& parameter = route.source < kLfos ? proc.getLfoAmountParam(route.source)
                                                   : proc.getEnvelopeAmountParam(route.source - kLfos);
            attachment = std::make_unique<juce::SliderParameterAttachment>(parameter, depth, nullptr);
        }
        else
        {
            depth.setRange(-1.0, 1.0, 0.0);
            depth.setValue(route.depth, juce::dontSendNotification);
            depth.onValueChange = [this]
            {
                owner.processor.setMacroDestinationDepth(route.slot, route.destination, static_cast<float>(depth.getValue()));
            };
        }
        depth.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; };
        depth.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue() / 100.0; };
        depth.updateText();
        addAndMakeVisible(depth);

        polarity.setComponentID("mod.routing.polarity");
        polarity.addItemList({ "NATIVE", "UNIPOLAR", "BIPOLAR" }, 1);
        polarity.setSelectedId(static_cast<int>(route.polarity) + 1, juce::dontSendNotification);
        polarity.setTooltip("Polarity: NATIVE keeps the source's own range");
        curve.setComponentID("mod.routing.curve");
        curve.addItemList({ "LINEAR", "SQUARE", "SQ ROOT" }, 1);
        curve.setSelectedId(static_cast<int>(route.curve) + 1, juce::dontSendNotification);
        curve.setTooltip("Response curve");
        for (auto* box : { &polarity, &curve })
        {
            box->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1d2224));
            box->setColour(juce::ComboBox::textColourId, kInk);
            box->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff394144));
            box->setEnabled(route.kind == RouteInfo::Kind::graph);
            box->onChange = [this] { applyShape(); };
            addAndMakeVisible(*box);
        }

        remove.setComponentID("mod.routing.remove");
        remove.setButtonText("X");
        remove.setTooltip("Remove this route (or select it and press Delete)");
        remove.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2f31));
        remove.setColour(juce::TextButton::textColourOffId, kInk);
        remove.onClick = [this]
        {
            removeRoute(owner.processor, route);
            // Rebuilt on the next tick, not from inside this row's own click.
            juce::Component::SafePointer<ModRoutingPanel> panel(&owner);
            juce::MessageManager::callAsync([panel] { if (panel != nullptr) { panel->refresh(); } });
        };
        addAndMakeVisible(remove);
    }

    const RouteInfo& getRoute() const { return route; }
    juce::Slider& getDepth() { return depth; }

    void syncDepth(float value)
    {
        if (route.kind == RouteInfo::Kind::macro && ! depth.isMouseButtonDown()
            && std::abs(depth.getValue() - value) > 1.0e-4)
        {
            depth.setValue(value, juce::dontSendNotification);
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto selected = owner.selectedKey == route.key();
        const auto area = getLocalBounds().toFloat().reduced(1.0f, 2.0f);
        g.setColour(selected ? juce::Colour(0xff253033) : juce::Colour(0xff1a1f20));
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour(selected ? sourceColour(route.source) : juce::Colour(0xff2b3133));
        g.drawRoundedRectangle(area, 4.0f, selected ? 1.6f : 1.0f);
        g.setColour(sourceColour(route.source));
        g.fillRoundedRectangle(area.withWidth(4.0f), 2.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10, 5);
        auto top = area.removeFromTop(18);
        remove.setBounds(top.removeFromRight(22));
        top.removeFromRight(6);
        origin.setBounds(top.removeFromRight(72));
        name.setBounds(top);
        area.removeFromTop(4);
        auto bottom = area.removeFromTop(20);
        curve.setBounds(bottom.removeFromRight(82));
        bottom.removeFromRight(4);
        polarity.setBounds(bottom.removeFromRight(88));
        bottom.removeFromRight(6);
        depth.setBounds(bottom);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        owner.select(route.key());
        owner.grabKeyboardFocus();
    }

private:
    void applyShape()
    {
        if (route.kind != RouteInfo::Kind::graph) { return; }
        auto configuration = owner.processor.getGraphRoute(route.slot);
        configuration.polarity = static_cast<px3::synth::ModulationPolarity>(juce::jlimit(0, 2, polarity.getSelectedId() - 1));
        configuration.curve = static_cast<px3::synth::ModulationCurve>(juce::jlimit(0, 2, curve.getSelectedId() - 1));
        juce::String error;
        owner.processor.setGraphRoute(route.slot, configuration, error);
        owner.select(route.key());
    }

    ModRoutingPanel& owner;
    RouteInfo route;
    juce::Label name, origin;
    juce::Slider depth;
    juce::ComboBox polarity, curve;
    juce::TextButton remove;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

class ModRoutingPanel::ListContent final : public juce::Component
{
public:
    std::vector<std::unique_ptr<Row>> rows;
    juce::String emptyText;
    void paint(juce::Graphics& g) override
    {
        if (! rows.empty()) { return; }
        g.setColour(kDimInk);
        g.setFont(juce::FontOptions(12.0f));
        g.drawFittedText(emptyText, getLocalBounds().reduced(10), juce::Justification::centredTop, 3);
    }
};

//==============================================================================
ModRoutingPanel::ModRoutingPanel(ModDragController& controllerIn)
    : controller(controllerIn), processor(controllerIn.getProcessor())
{
    setComponentID("mod.routing");
    setWantsKeyboardFocus(true);
    title.setText("ROUTING", juce::dontSendNotification);
    title.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, kInk);
    title.setJustificationType(juce::Justification::centredLeft);
    title.setComponentID("mod.routing.title");
    addAndMakeVisible(title);
    patchView = std::make_unique<PatchView>(*this, controller);
    addAndMakeVisible(*patchView);
    listContent = std::make_unique<ListContent>();
    listContent->emptyText = "No routes yet. Drag a jack (here, on an LFO/ENV card or in the MOD strip "
                             "under the panels) onto a knob.";
    listViewport.setComponentID("mod.routing.list");
    listViewport.setViewedComponent(listContent.get(), false);
    listViewport.setScrollBarsShown(true, false);
    listViewport.setScrollBarThickness(8);
    addAndMakeVisible(listViewport);
    refresh();
}

ModRoutingPanel::~ModRoutingPanel()
{
    listViewport.setViewedComponent(nullptr, false);
}

juce::Component& ModRoutingPanel::getTitle() { return title; }
juce::Component& ModRoutingPanel::getPatchView() { return *patchView; }
juce::Component& ModRoutingPanel::getList() { return listViewport; }
int ModRoutingPanel::getRowCount() const { return static_cast<int>(listContent->rows.size()); }
juce::Component* ModRoutingPanel::getRow(int index)
{
    return juce::isPositiveAndBelow(index, getRowCount()) ? listContent->rows[static_cast<std::size_t>(index)].get() : nullptr;
}

void ModRoutingPanel::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(kPanelFill);
    g.fillRoundedRectangle(area, 8.0f);
    g.setColour(juce::Colour(0xff2f3739));
    g.drawRoundedRectangle(area, 8.0f, 1.0f);
}

void ModRoutingPanel::resized()
{
    auto area = getLocalBounds().reduced(12, 10);
    title.setBounds(area.removeFromTop(22));
    area.removeFromTop(6);
    patchView->setBounds(area.removeFromTop(area.getHeight() * 46 / 100));
    area.removeFromTop(8);
    listViewport.setBounds(area);
    rebuildRows();
}

void ModRoutingPanel::refresh()
{
    auto latest = collectRoutes(processor);
    juce::String latestSignature;
    for (const auto& route : latest)
    {
        latestSignature << route.key() << '/' << static_cast<int>(route.polarity) << static_cast<int>(route.curve) << ';';
    }
    if (latestSignature != signature)
    {
        routes = std::move(latest);
        signature = latestSignature;
        auto stillThere = false;
        for (const auto& route : routes) { stillThere = stillThere || route.key() == selectedKey; }
        if (! stillThere) { selectedKey.clear(); }
        rebuildRows();
        patchView->setRoutes(routes);
        return;
    }
    auto depthsMoved = false;
    for (std::size_t i = 0; i < routes.size() && i < latest.size(); ++i)
    {
        depthsMoved = depthsMoved || std::abs(routes[i].depth - latest[i].depth) > 1.0e-3f;
        routes[i].depth = latest[i].depth;
    }
    for (std::size_t i = 0; i < listContent->rows.size() && i < routes.size(); ++i)
    {
        listContent->rows[i]->syncDepth(routes[i].depth);
    }
    if (depthsMoved) { patchView->repaint(); }
}

void ModRoutingPanel::rebuildRows()
{
    // Only the routes changed: the scroll position is kept.
    const auto scroll = listViewport.getViewPosition();
    listContent->rows.clear();
    const auto width = juce::jmax(1, listViewport.getWidth() - listViewport.getScrollBarThickness() - 2);
    constexpr int rowHeight = 56;
    int y = 0;
    for (const auto& route : routes)
    {
        auto row = std::make_unique<Row>(*this, route);
        row->setBounds(0, y, width, rowHeight);
        y += rowHeight;
        listContent->addAndMakeVisible(*row);
        listContent->rows.push_back(std::move(row));
    }
    listContent->setSize(width, juce::jmax(y, listViewport.getHeight()));
    listViewport.setViewPosition(scroll);
    listContent->repaint();
}

void ModRoutingPanel::select(const juce::String& routeKey)
{
    if (selectedKey == routeKey) { return; }
    selectedKey = routeKey;
    for (auto& row : listContent->rows)
    {
        row->repaint();
        if (row->getRoute().key() == selectedKey)
        {
            listViewport.setViewPosition(0, juce::jlimit(0, juce::jmax(0, listContent->getHeight() - listViewport.getHeight()),
                                                          row->getY() - listViewport.getHeight() / 2));
        }
    }
    patchView->repaint();
}

bool ModRoutingPanel::removeSelected()
{
    for (const auto& route : routes)
    {
        if (route.key() != selectedKey) { continue; }
        const auto removed = removeRoute(processor, route);
        refresh();
        return removed;
    }
    return false;
}

bool ModRoutingPanel::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        removeSelected();
        return true;
    }
    return false;
}
} // namespace px3::ui::modrouting
