#include "ModRouting.h"
#include "ModAmountControl.h"

#include "ParameterKnob.h"
#include "Theme.h"

#include <map>
#include <optional>
#include <set>

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
    // Sized by its initialisers and checked against kSourceCount: declared
    // [kSourceCount], a source added without a colour silently got a
    // zero-initialised one - transparent black - which is how MACRO 6 arrived
    // with a dark jack and no text colour.
    static const juce::Colour colours[] {
        juce::Colour(0xff4fd1c5), juce::Colour(0xff63b3ed), juce::Colour(0xff9f8cf0),   // LFO 1-3
        juce::Colour(0xff667eea),                                                       // LFO 4
        juce::Colour(0xfff6ad55), juce::Colour(0xfff26d6d), juce::Colour(0xffed64a6),   // ENV 1-3
        juce::Colour(0xffecc94b),                                                       // ENV 4
        juce::Colour(0xff7ddc8a), juce::Colour(0xffd6e05a), juce::Colour(0xff5fd3f3),   // MACRO 1-3
        juce::Colour(0xffe0a8ff), juce::Colour(0xffc9d1d3), juce::Colour(0xffe8c99a) }; // MACRO 4-6
    static_assert(std::size(colours) == static_cast<std::size_t>(kSourceCount),
                  "every modulation source needs a colour");
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
    const auto x = jackOnRight ? static_cast<float>(getWidth()) - size - 1.0f : 1.0f;
    return { x, (static_cast<float>(getHeight()) - size) * 0.5f, size, size };
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
    if (armed)
    {
        g.setColour(colour.withAlpha(0.9f));
        g.drawEllipse(jack.expanded(1.5f), 1.2f);
    }
    if (showLabel)
    {
        g.setColour(hover || armed ? kInk : kInk.withAlpha(0.82f));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        const auto text = jackOnRight ? Processor::graphSourceName(source).toUpperCase() : sourceShortName(source);
        auto area = jackOnRight ? getLocalBounds().withRight(juce::roundToInt(jack.getX()) - 6).withTrimmedLeft(4)
                                : getLocalBounds().withTrimmedLeft(juce::roundToInt(jack.getRight()) + 4);
        if (jackOnRight)
        {
            // Route count beside the jack: "3" reads faster than counting cables.
            if (routeCount > 0)
            {
                g.setColour(colour.withAlpha(0.95f));
                g.setFont(juce::FontOptions(10.0f));
                g.drawText(juce::String(routeCount), area.removeFromRight(18), juce::Justification::centredRight, false);
                g.setColour(hover || armed ? kInk : kInk.withAlpha(0.82f));
                g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            }
            // Identity colour as a small swatch: a second cue, never the only one.
            g.setColour(colour);
            g.fillRect(area.removeFromLeft(3).withSizeKeepingCentre(3, 12));
            area.removeFromLeft(6);
            g.setColour(hover || armed ? kInk : kInk.withAlpha(0.82f));
        }
        g.drawFittedText(text, area, juce::Justification::centredLeft, 1, 0.8f);
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
    const auto created = controller.end(e.getEventRelativeTo(&controller.getRoot()).position);
    if (created < 0 && ! e.mouseWasDraggedSinceMouseDown() && onClick) { onClick(source); }
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
        if (owner.hoverTarget.getComponent() != nullptr && ! owner.hoverRect.isEmpty())
        {
            const auto area = getLocalArea(&owner.root, owner.hoverRect).toFloat().expanded(2.0f);
            g.setColour(colour.withAlpha(0.18f));
            g.fillRect(area);
            g.setColour(colour);
            g.drawRect(area, 2.0f);
            const auto name = destinationName(owner.processor, owner.destinationAt(owner.current.roundToInt()));
            const auto label = Processor::graphSourceName(owner.dragSource) + "  >  " + name;
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            const auto width = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), label) + 16;
            auto tag = juce::Rectangle<float>(static_cast<float>(width), 20.0f)
                           .withCentre({ area.getCentreX(), area.getY() - 14.0f });
            tag = tag.constrainedWithin(getLocalBounds().toFloat());
            g.setColour(juce::Colour(0xee101314));
            g.fillRect(tag);
            g.setColour(colour);
            g.drawRect(tag, 1.0f);
            g.setColour(kInk);
            g.drawText(label, tag, juce::Justification::centred, false);
        }
        // Over a matrix destination row the plug lands in that row's input
        // jack (its centre), not at the pointer.
        auto end = owner.current;
        if (dynamic_cast<DestinationProvider*>(owner.hoverTarget.getComponent()) != nullptr && ! owner.hoverRect.isEmpty())
        {
            end = getLocalPoint(&owner.root, juce::Point<float>(static_cast<float>(owner.hoverRect.getX()) + 7.0f,
                                                               owner.hoverRect.toFloat().getCentreY()));
        }
        drawCable(g, owner.start, end, colour, 4.0f, 0.95f);
        drawPlug(g, owner.start, colour, 6.0f);
        drawPlug(g, end, colour, 7.0f);
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
    destinationAt(rootPosition.roundToInt(), &target, &hoverRect);
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

juce::String ModDragController::destinationAt(juce::Point<int> rootPosition, juce::Component** target,
                                              juce::Rectangle<int>* targetRect) const
{
    juce::String found;
    juce::Component* foundComponent = nullptr;
    juce::Rectangle<int> foundRect;
    // Deepest visible hit wins, through every container and viewport, so a
    // knob anywhere in the editor is a target with no list to maintain.
    std::function<void(juce::Component&)> walk = [&](juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (child == nullptr || ! child->isVisible() || child == overlay.get()) { continue; }
            const auto local = child->getLocalPoint(&root, rootPosition);
            if (! child->getLocalBounds().contains(local)) { continue; }
            juce::String id;
            auto rect = child->getLocalBounds();
            if (auto* slider = dynamic_cast<juce::Slider*>(child)) { id = px3::ui::parameterIdOf(*slider); }
            if (id.isEmpty()) { id = child->getProperties()[destinationProperty].toString(); }
            if (id.isEmpty())
            {
                if (auto* provider = dynamic_cast<DestinationProvider*>(child))
                {
                    id = provider->destinationAtPoint(local);
                    rect = provider->destinationBounds(id);
                }
            }
            if (id.isNotEmpty() && processor.isGraphDestination(id))
            {
                found = id;
                foundComponent = child;
                foundRect = root.getLocalArea(child, rect);
            }
            walk(*child);
        }
    };
    walk(root);
    if (target != nullptr) { *target = foundComponent; }
    if (targetRect != nullptr) { *targetRect = foundRect; }
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
    g.fillRect(area);
    g.setColour(juce::Colour(0xff2b3133));
    g.drawRect(area, 1.0f);
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
// The MOD page: the modulation matrix as a patch bay.
namespace
{
namespace th = px3::ui::theme;
const juce::Colour kMatrixAccent { 0xff8f7cf0 };   // the modulation family's purple
constexpr float kBand = px3::ui::theme::space::minTitleBand;   // same title band as every powered card

juce::Path linkPath(juce::Point<float> a, juce::Point<float> b)
{
    // A short patch lead between two columns: leaves and lands horizontally.
    const auto dx = juce::jmax(10.0f, std::abs(b.x - a.x) * 0.55f);
    juce::Path path;
    path.startNewSubPath(a);
    path.cubicTo(a.x + dx, a.y, b.x - dx, b.y, b.x, b.y);
    return path;
}

bool routeIsBipolar(const RouteInfo& route)
{
    using P = px3::synth::ModulationPolarity;
    return route.polarity == P::bipolar || (route.polarity == P::native && sourceIsBipolar(route.source));
}

juce::String groupLabelFor(const px3::synth::ParameterCatalogEntry& entry)
{
    // The catalog's group path (e.g. Voice > Filter 1): the innermost two
    // levels name the module well enough to scan.
    juce::StringArray parts;
    for (const auto& name : entry.groupNames) { parts.add(name); }
    while (parts.size() > 2) { parts.remove(0); }
    return parts.isEmpty() ? juce::String("Other") : parts.joinIntoString(" / ").toUpperCase();
}
} // namespace

//------------------------------------------------------------------------------
class ModRoutingPanel::Header final : public juce::Component
{
public:
    explicit Header(ModRoutingPanel& ownerIn) : owner(ownerIn)
    {
        setComponentID("mod.routing.title");
        setInterceptsMouseClicks(false, false);
    }
    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour(th::colour::rail);
        g.fillRect(area);
        area.removeFromLeft(11.0f);
        th::drawLabel(g, "MODULATION MATRIX", area.removeFromLeft(170.0f), th::Type::heading,
                      th::colour::textPrimary, juce::Justification::centredLeft);
        const auto graphRoutes = std::count_if(owner.routes.begin(), owner.routes.end(),
                                               [](const RouteInfo& r) { return r.kind == RouteInfo::Kind::graph; });
        const auto count = juce::String(static_cast<int>(graphRoutes)) + " / "
                           + juce::String(Processor::kGraphRouteSlots) + " SLOTS";
        th::drawLabel(g, count, area.removeFromRight(120.0f).withTrimmedRight(8.0f), th::Type::secondary,
                      th::colour::textSecondary, juce::Justification::centredRight);
        juce::String hint;
        if (owner.armedSource >= 0)
        {
            hint = Processor::graphSourceName(owner.armedSource).toUpperCase()
                   + " ARMED - click destinations to patch, click the jack again to disarm";
        }
        else
        {
            hint = "Drag a jack onto a destination or any knob  -  click a jack to arm it  -  "
                   "Cmd/Alt-drag, wheel or arrows for fine amounts, double-click to type";
        }
        th::drawLabel(g, hint, area, th::Type::secondary,
                      owner.armedSource >= 0 ? sourceColour(owner.armedSource) : th::colour::textDim,
                      juce::Justification::centredLeft);
    }

private:
    ModRoutingPanel& owner;
};

//------------------------------------------------------------------------------
class ModRoutingPanel::SourceColumn final : public juce::Component
{
public:
    SourceColumn(ModRoutingPanel& ownerIn, ModDragController& controller) : owner(ownerIn)
    {
        setComponentID("mod.routing.patch");
        for (int source = 0; source < kSourceCount; ++source)
        {
            auto socket = std::make_unique<ModSourceSocket>(controller, source, true);
            socket->setJackOnRight(true);
            socket->onClick = [this](int s) { owner.armSource(owner.armedSource == s ? -1 : s); };
            socket->setTooltip("Drag " + Processor::graphSourceName(source)
                               + " onto a destination or any knob. Click to arm it for click-to-patch.");
            addAndMakeVisible(*socket);
            sockets.push_back(std::move(socket));
        }
    }

    ModSourceSocket& socket(int source) { return *sockets[static_cast<std::size_t>(source)]; }

    void setCounts(const std::vector<RouteInfo>& newRoutes)
    {
        std::array<int, kSourceCount> counts {};
        for (const auto& route : newRoutes)
        {
            if (juce::isPositiveAndBelow(route.source, kSourceCount)) { ++counts[static_cast<std::size_t>(route.source)]; }
        }
        for (int s = 0; s < kSourceCount; ++s)
        {
            sockets[static_cast<std::size_t>(s)]->setRouteCount(counts[static_cast<std::size_t>(s)]);
            sockets[static_cast<std::size_t>(s)]->setArmed(owner.armedSource == s);
        }
    }

    void resized() override
    {
        // Three groups (LFO, ENV, MACRO), each under a printed sub-heading.
        auto area = getLocalBounds().withTrimmedTop(static_cast<int>(kBand) + 2).reduced(6, 2);
        constexpr int headings = 3;
        const auto headingH = 14;
        const auto rowH = juce::jlimit(18, 38, (area.getHeight() - headings * headingH) / kSourceCount);
        headingRows.clear();
        for (int source = 0; source < kSourceCount; ++source)
        {
            if (source == 0 || source == kLfos || source == kLfos + kEnvs)
            {
                headingRows.push_back(area.removeFromTop(headingH));
            }
            sockets[static_cast<std::size_t>(source)]->setBounds(area.removeFromTop(rowH));
        }
    }

    void paint(juce::Graphics& g) override
    {
        th::drawModulePanel(g, getLocalBounds().toFloat(), "SOURCES", kMatrixAccent, true, kBand);
        const char* names[] { "LFO", "ENVELOPE", "MACRO" };
        for (std::size_t i = 0; i < headingRows.size() && i < 3; ++i)
        {
            const auto r = headingRows[i].toFloat();
            th::drawLabel(g, names[i], r, th::Type::secondary, th::colour::textDim, juce::Justification::centredLeft);
            g.setColour(th::colour::railEdge);
            g.drawHorizontalLine(juce::roundToInt(r.getCentreY()), r.getX() + 64.0f, r.getRight());
        }
    }

private:
    ModRoutingPanel& owner;
    std::vector<std::unique_ptr<ModSourceSocket>> sockets;
    std::vector<juce::Rectangle<int>> headingRows;
};

//------------------------------------------------------------------------------
// A route's amount: the shared control (ModAmountControl.h), also used by the
// macro depth panel.
using AmountControl = px3::ui::ModAmountControl;


//------------------------------------------------------------------------------
using px3::ui::UnpatchGlyph;


//------------------------------------------------------------------------------
// One connection: (plug) SOURCE ~ [ AMOUNT ] POLARITY CURVE ~~ DESTINATION [x] (plug)
class ModRoutingPanel::Row final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Row(ModRoutingPanel& ownerIn, const RouteInfo& routeIn)
        : owner(ownerIn), route(routeIn), depth(sourceColour(routeIn.source), routeIsBipolar(routeIn))
    {
        setComponentID("mod.routing.row." + route.key());
        auto& proc = owner.processor;
        destinationText = destinationName(proc, route.destination);
        if (const auto* entry = proc.getParameterCatalog().find(route.destination))
        {
            destinationGroup = groupLabelFor(*entry);
        }
        originText = route.kind == RouteInfo::Kind::card ? "CARD"
                     : route.kind == RouteInfo::Kind::macro ? "MACRO" : "S" + juce::String(route.slot + 1);
        setTooltip(Processor::graphSourceName(route.source) + " > " + destinationText + "  ("
                   + (route.kind == RouteInfo::Kind::card ? juce::String("card ASSIGN")
                      : route.kind == RouteInfo::Kind::macro ? juce::String("macro destination")
                                                              : "matrix slot " + juce::String(route.slot + 1))
                   + ")");

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
        depth.textFromValueFunction = [](double v) { return juce::String(v * 100.0, 1) + "%"; };
        depth.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue() / 100.0; };
        depth.onDragStart = [this] { owner.select(route.key()); };
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
            box->setEnabled(route.kind == RouteInfo::Kind::graph);
            box->onChange = [this] { applyShape(); };
            addAndMakeVisible(*box);
        }

        remove.setComponentID("mod.routing.remove");
        remove.setTooltip("Unpatch (or select the route and press Delete)");
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
    // Where the cables attach, in this row's coordinates.
    juce::Point<float> sourcePlug() const { return { 11.0f, static_cast<float>(getHeight()) * 0.5f }; }
    juce::Point<float> destinationPlug() const { return { static_cast<float>(getWidth()) - 9.0f, static_cast<float>(getHeight()) * 0.5f }; }

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
        const auto colour = sourceColour(route.source);
        auto area = getLocalBounds().toFloat();
        g.setColour(selected ? th::colour::selection.withAlpha(0.55f) : (isMouseOver(true) ? th::colour::hover : juce::Colours::transparentBlack));
        g.fillRect(area);
        g.setColour(th::colour::panelEdge.withAlpha(0.8f));
        g.drawHorizontalLine(getHeight() - 1, 0.0f, area.getRight());
        g.setColour(colour);
        g.fillRect(area.withWidth(selected ? 3.0f : 2.0f));

        const auto mid = area.getCentreY();
        // The lead runs the length of the strip, under the controls, so the
        // row reads as one cable from source plug to destination plug.
        g.setColour(colour.withAlpha(0.55f + 0.4f * juce::jlimit(0.0f, 1.0f, std::abs(route.depth))));
        const auto leadFrom = sourceNameArea.getRight() + 2.0f;
        g.fillRect(juce::Rectangle<float>(leadFrom, mid - 1.0f, static_cast<float>(depth.getX()) - leadFrom, 2.0f));
        const auto textW = th::textWidth(destinationText, th::Type::label);
        const auto leadTo = juce::jmax(destinationArea.getX(), destinationArea.getRight() - textW - 8.0f);
        const auto afterShape = static_cast<float>(curve.getRight()) + 2.0f;
        if (leadTo > afterShape) { g.fillRect(juce::Rectangle<float>(afterShape, mid - 1.0f, leadTo - afterShape, 2.0f)); }

        drawPlug(g, sourcePlug(), colour, 5.0f);
        drawPlug(g, destinationPlug(), colour, 5.0f);

        th::drawLabel(g, sourceShortName(route.source), sourceNameArea, th::Type::label, th::colour::textPrimary,
                      juce::Justification::centredLeft);
        auto dest = destinationArea;
        if (showOrigin)
        {
            // Slot / origin tag printed on the lead, on a small plate.
            auto tag = juce::Rectangle<float>(36.0f, 14.0f).withCentre({ dest.getX() + 24.0f, mid });
            g.setColour(th::colour::panelBottom);
            g.fillRect(tag);
            th::drawLabel(g, originText, tag, th::Type::secondary, th::colour::textSecondary);
        }
        if (showGroup && destinationGroup.isNotEmpty())
        {
            auto group = dest.removeFromTop(dest.getHeight() * 0.42f);
            th::drawLabel(g, destinationGroup, group, th::Type::secondary, th::colour::textSecondary,
                          juce::Justification::bottomRight);
            th::drawLabel(g, destinationText, dest, th::Type::label, th::colour::textPrimary, juce::Justification::topRight);
        }
        else
        {
            th::drawLabel(g, destinationText, dest, th::Type::label, th::colour::textPrimary, juce::Justification::centredRight);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4, 0);
        const auto h = getHeight();
        const auto controlH = juce::jmin(20, h - 6);
        const auto centred = [&](juce::Rectangle<int> r) { return r.withSizeKeepingCentre(r.getWidth(), controlH); };
        const auto wide = getWidth() >= 640;
        area.removeFromLeft(14);   // the source plug
        sourceNameArea = area.removeFromLeft(wide ? 50 : 42).toFloat();
        area.removeFromLeft(wide ? 14 : 8);
        depth.setBounds(centred(area.removeFromLeft(wide ? 132 : 112)));
        area.removeFromLeft(4);
        polarity.setBounds(centred(area.removeFromLeft(wide ? 86 : 74)));
        area.removeFromLeft(2);
        curve.setBounds(centred(area.removeFromLeft(wide ? 74 : 66)));
        area.removeFromRight(16);   // the destination plug
        // Unpatch: after the destination it removes, just inside its plug.
        remove.setBounds(centred(area.removeFromRight(16)));
        area.removeFromRight(4);
        area.removeFromLeft(wide ? 16 : 8);
        destinationArea = area.toFloat();
        showOrigin = destinationArea.getWidth() > 190.0f;
        showGroup = h >= 28;
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        owner.select(route.key());
        owner.grabKeyboardFocus();
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    void applyShape()
    {
        if (route.kind != RouteInfo::Kind::graph) { return; }
        auto configuration = owner.processor.getGraphRoute(route.slot);
        configuration.polarity = static_cast<px3::synth::ModulationPolarity>(juce::jlimit(0, 2, polarity.getSelectedId() - 1));
        configuration.curve = static_cast<px3::synth::ModulationCurve>(juce::jlimit(0, 2, curve.getSelectedId() - 1));
        juce::String error;
        owner.processor.setGraphRoute(route.slot, configuration, error);
        route.polarity = configuration.polarity;
        route.curve = configuration.curve;
        depth.setBipolar(routeIsBipolar(route));
        owner.select(route.key());
    }

    ModRoutingPanel& owner;
    RouteInfo route;
    AmountControl depth;
    juce::ComboBox polarity, curve;
    UnpatchGlyph remove;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    juce::String destinationText, destinationGroup, originText;
    juce::Rectangle<float> sourceNameArea, destinationArea;
    bool showOrigin { true };
    bool showGroup { true };
};

//------------------------------------------------------------------------------
class ModRoutingPanel::ListContent final : public juce::Component
{
public:
    std::vector<std::unique_ptr<Row>> rows;
    int rowHeight { 30 };
    void paint(juce::Graphics& g) override
    {
        // The unused part of the bay reads as empty slots, like an unpatched
        // panel, rather than as a blank page.
        const auto clip = g.getClipBounds();
        for (int y = static_cast<int>(rows.size()) * rowHeight; y < clip.getBottom(); y += rowHeight)
        {
            if (y + rowHeight < clip.getY()) { continue; }
            const auto mid = static_cast<float>(y) + static_cast<float>(rowHeight) * 0.5f;
            g.setColour(th::colour::panelEdge.withAlpha(0.5f));
            g.drawHorizontalLine(y + rowHeight - 1, 0.0f, static_cast<float>(getWidth()));
            g.setColour(th::colour::textDim.withAlpha(0.35f));
            g.drawEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre({ 25.0f, mid }), 1.0f);
            g.drawEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre({ static_cast<float>(getWidth()) - 9.0f, mid }), 1.0f);
            const float dashes[] { 3.0f, 5.0f };
            g.drawDashedLine(juce::Line<float>(36.0f, mid, static_cast<float>(getWidth()) - 20.0f, mid), dashes, 2, 1.0f);
        }
        if (! rows.empty()) { return; }
        th::drawLabel(g, "NO CONNECTIONS", getLocalBounds().toFloat().withHeight(60.0f), th::Type::label,
                      th::colour::textSecondary);
        th::drawLabel(g, "Drag a source jack onto a destination (right) or onto any knob",
                      getLocalBounds().toFloat().withTrimmedTop(44.0f).withHeight(20.0f), th::Type::secondary,
                      th::colour::textDim);
    }
};

// The ROUTES module: column legend over a scrolling list of strips.
class RoutesArea final : public juce::Component
{
public:
    class Port final : public juce::Viewport
    {
    public:
        std::function<void()> onScroll;
        void visibleAreaChanged(const juce::Rectangle<int>&) override { if (onScroll) { onScroll(); } }
    };

    // The scene places this column directly, so the panel's resized() does not
    // always run when it changes size: the column asks for its rows to be
    // refitted itself, or they keep a stale width and the right-hand plugs
    // fall outside it.
    std::function<void()> onResized;

    RoutesArea()
    {
        setComponentID("mod.routing.list");
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(8);
        addAndMakeVisible(viewport);
    }
    void resized() override
    {
        viewport.setBounds(getLocalBounds().withTrimmedTop(static_cast<int>(kBand) + kLegend));
        if (onResized) { onResized(); }
    }
    void paint(juce::Graphics& g) override
    {
        th::drawModulePanel(g, getLocalBounds().toFloat(), "ROUTES", kMatrixAccent, true, kBand);
        auto legend = getLocalBounds().withTrimmedTop(static_cast<int>(kBand)).withHeight(kLegend).reduced(4, 0).toFloat();
        const auto wide = getWidth() >= 640;
        legend.removeFromLeft(30.0f);
        th::drawLabel(g, "SOURCE", legend.removeFromLeft(wide ? 64.0f : 50.0f), th::Type::secondary,
                      th::colour::textDim, juce::Justification::centredLeft);
        th::drawLabel(g, "AMOUNT", legend.removeFromLeft(wide ? 136.0f : 116.0f), th::Type::secondary,
                      th::colour::textDim);
        th::drawLabel(g, "POLARITY", legend.removeFromLeft(wide ? 88.0f : 76.0f), th::Type::secondary,
                      th::colour::textDim);
        th::drawLabel(g, "CURVE", legend.removeFromLeft(wide ? 76.0f : 68.0f), th::Type::secondary,
                      th::colour::textDim);
        th::drawLabel(g, "DESTINATION", legend.withTrimmedRight(18.0f), th::Type::secondary, th::colour::textDim,
                      juce::Justification::centredRight);
        g.setColour(th::colour::panelEdge.withAlpha(0.8f));
        g.drawHorizontalLine(juce::roundToInt(kBand) + kLegend - 1, 1.0f, static_cast<float>(getWidth() - 1));
    }
    static constexpr int kLegend = 16;
    Port viewport;
};

//------------------------------------------------------------------------------
// Every modulatable parameter, from the processor's catalog (no hard-coded
// list), grouped by module, collapsible and searchable. Custom-painted: a
// few hundred destinations cost one component.
class ModRoutingPanel::DestinationBrowser final : public juce::Component
{
public:
    class List final : public juce::Component, public DestinationProvider, public juce::SettableTooltipClient
    {
    public:
        explicit List(DestinationBrowser& ownerIn) : owner(ownerIn)
        {
            setComponentID("mod.routing.dest.list");
            setTooltip("Drop a source here to patch it. Click a group to fold it.");
        }

        juce::String destinationAtPoint(juce::Point<int> local) const override
        {
            const auto index = local.y / kRow;
            if (! juce::isPositiveAndBelow(index, static_cast<int>(owner.visibleItems.size()))) { return {}; }
            const auto& item = owner.items[static_cast<std::size_t>(owner.visibleItems[static_cast<std::size_t>(index)])];
            return item.header ? juce::String() : item.id;
        }
        juce::Rectangle<int> destinationBounds(const juce::String& id) const override
        {
            for (int row = 0; row < static_cast<int>(owner.visibleItems.size()); ++row)
            {
                const auto& item = owner.items[static_cast<std::size_t>(owner.visibleItems[static_cast<std::size_t>(row)])];
                if (! item.header && item.id == id) { return { 0, row * kRow, getWidth(), kRow }; }
            }
            return {};
        }

        void paint(juce::Graphics& g) override
        {
            const auto clip = g.getClipBounds();
            const auto first = juce::jmax(0, clip.getY() / kRow);
            const auto last = juce::jmin(static_cast<int>(owner.visibleItems.size()), clip.getBottom() / kRow + 1);
            for (int row = first; row < last; ++row)
            {
                const auto& item = owner.items[static_cast<std::size_t>(owner.visibleItems[static_cast<std::size_t>(row)])];
                auto r = juce::Rectangle<int>(0, row * kRow, getWidth(), kRow).toFloat();
                if (item.header)
                {
                    g.setColour(th::colour::rail);
                    g.fillRect(r);
                    const auto folded = owner.collapsed.count(item.group) != 0 && owner.filter.isEmpty();
                    g.setColour(th::colour::textSecondary);
                    g.setFont(th::font(th::Type::secondary));
                    g.drawText(juce::CharPointer_UTF8(folded ? "\xe2\x96\xb8" : "\xe2\x96\xbe"),
                               r.removeFromLeft(14.0f), juce::Justification::centred, false);
                    th::drawLabel(g, item.group, r.withTrimmedRight(30.0f), th::Type::secondary, th::colour::textLabel,
                                  juce::Justification::centredLeft);
                    th::drawLabel(g, juce::String(item.count), r.removeFromRight(28.0f), th::Type::secondary,
                                  th::colour::textDim, juce::Justification::centredRight);
                    continue;
                }
                const auto selected = item.id == owner.highlightId;
                const auto hovered = row == hoverRow;
                if (selected || hovered)
                {
                    g.setColour(selected ? th::colour::selection.withAlpha(0.7f) : th::colour::hover);
                    g.fillRect(r);
                }
                // Routed: one dot per source patched here, in the source's colour.
                auto dots = r.removeFromRight(8.0f + 7.0f * static_cast<float>(item.sources.size()));
                dots.removeFromRight(4.0f);
                for (auto it = item.sources.rbegin(); it != item.sources.rend(); ++it)
                {
                    g.setColour(sourceColour(*it));
                    g.fillEllipse(dots.removeFromRight(7.0f).withSizeKeepingCentre(5.0f, 5.0f));
                }
                r.removeFromLeft(14.0f);
                // Input jack ring on the left edge where the cable lands.
                g.setColour(item.sources.empty() ? th::colour::textDim.withAlpha(0.5f) : sourceColour(item.sources.front()));
                g.drawEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({ 7.0f, r.getCentreY() }), 1.2f);
                th::drawLabel(g, item.name, r, th::Type::value,
                              item.sources.empty() ? th::colour::textLabel : th::colour::textValue,
                              juce::Justification::centredLeft);
            }
        }

        void mouseMove(const juce::MouseEvent& e) override { setHover(e.y / kRow); }
        void mouseExit(const juce::MouseEvent&) override { setHover(-1); }
        void mouseUp(const juce::MouseEvent& e) override
        {
            const auto row = e.y / kRow;
            if (! juce::isPositiveAndBelow(row, static_cast<int>(owner.visibleItems.size()))) { return; }
            const auto& item = owner.items[static_cast<std::size_t>(owner.visibleItems[static_cast<std::size_t>(row)])];
            if (item.header)
            {
                owner.toggleGroup(item.group);
                return;
            }
            owner.destinationClicked(item.id);
        }

        static constexpr int kRow = 18;

    private:
        void setHover(int row)
        {
            if (row == hoverRow) { return; }
            if (hoverRow >= 0) { repaint(0, hoverRow * kRow, getWidth(), kRow); }
            hoverRow = row;
            if (hoverRow >= 0) { repaint(0, hoverRow * kRow, getWidth(), kRow); }
        }
        DestinationBrowser& owner;
        int hoverRow { -1 };
    };

    struct Item
    {
        bool header { false };
        juce::String group, id, name, searchText;
        int count { 0 };
        std::vector<int> sources;
    };

    DestinationBrowser(ModRoutingPanel& ownerIn) : owner(ownerIn), list(*this)
    {
        setComponentID("mod.routing.dest");
        search.setComponentID("mod.routing.dest.search");
        search.setTextToShowWhenEmpty("Search destinations", th::colour::textDim);
        search.setFont(th::font(th::Type::value));
        search.onTextChange = [this] { setFilter(search.getText()); };
        search.onEscapeKey = [this] { search.clear(); setFilter({}); };
        addAndMakeVisible(search);
        viewport.setViewedComponent(&list, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(8);
        addAndMakeVisible(viewport);
        buildItems();
        setFilter({});
    }
    ~DestinationBrowser() override { viewport.setViewedComponent(nullptr, false); }

    void buildItems()
    {
        // In catalog order, so groups follow the instrument's own structure.
        juce::StringArray groups;
        std::map<juce::String, std::vector<Item>> byGroup;
        for (const auto& entry : owner.processor.getParameterCatalog().entries())
        {
            if (! owner.processor.isGraphDestination(entry.id)) { continue; }
            Item item;
            item.group = groupLabelFor(entry);
            item.id = entry.id;
            item.name = entry.name;
            item.searchText = (item.group + " " + entry.name + " " + entry.id).toLowerCase();
            groups.addIfNotAlreadyThere(item.group);
            byGroup[item.group].push_back(std::move(item));
        }
        items.clear();
        destinationCount = 0;
        for (const auto& group : groups)
        {
            Item heading;
            heading.header = true;
            heading.group = group;
            heading.count = static_cast<int>(byGroup[group].size());
            items.push_back(heading);
            for (auto& item : byGroup[group]) { items.push_back(std::move(item)); ++destinationCount; }
        }
    }

    void setFilter(const juce::String& text)
    {
        filter = text.trim().toLowerCase();
        if (search.getText().trim().toLowerCase() != filter) { search.setText(text, false); }
        rebuildVisible();
    }

    void rebuildVisible()
    {
        visibleItems.clear();
        juce::StringArray tokens;
        tokens.addTokens(filter, " ", {});
        tokens.removeEmptyStrings();
        int headerIndex = -1;
        bool headerAdded = false;
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            const auto& item = items[static_cast<std::size_t>(i)];
            if (item.header) { headerIndex = i; headerAdded = false; continue; }
            auto match = true;
            for (const auto& token : tokens) { match = match && item.searchText.contains(token); }
            if (! match) { continue; }
            if (! headerAdded) { visibleItems.push_back(headerIndex); headerAdded = true; }
            if (tokens.isEmpty() && collapsed.count(item.group) != 0) { continue; }
            visibleItems.push_back(i);
        }
        list.setSize(juce::jmax(1, viewport.getMaximumVisibleWidth()),
                     juce::jmax(1, static_cast<int>(visibleItems.size()) * List::kRow));
        list.repaint();
        owner.repaint();
    }

    void toggleGroup(const juce::String& group)
    {
        if (collapsed.count(group) != 0) { collapsed.erase(group); } else { collapsed.insert(group); }
        rebuildVisible();
    }

    void destinationClicked(const juce::String& id)
    {
        if (owner.armedSource >= 0)
        {
            owner.patchArmedSourceTo(id);
            return;
        }
        // Otherwise select the first route that lands here.
        for (const auto& route : owner.routes)
        {
            if (route.destination == id) { owner.select(route.key()); return; }
        }
        setHighlight(id);
    }

    void setRoutes(const std::vector<RouteInfo>& newRoutes)
    {
        for (auto& item : items) { item.sources.clear(); }
        for (const auto& route : newRoutes)
        {
            for (auto& item : items)
            {
                if (! item.header && item.id == route.destination
                    && std::find(item.sources.begin(), item.sources.end(), route.source) == item.sources.end())
                {
                    item.sources.push_back(route.source);
                }
            }
        }
        list.repaint();
    }

    void setHighlight(const juce::String& id)
    {
        if (highlightId == id) { return; }
        highlightId = id;
        list.repaint();
    }

    // The row's rect in this component's coordinates, scrolling it into view.
    juce::Rectangle<int> reveal(const juce::String& id)
    {
        if (id.isEmpty()) { return {}; }
        for (const auto& item : items)
        {
            if (! item.header && item.id == id && collapsed.count(item.group) != 0) { toggleGroup(item.group); break; }
        }
        auto row = list.destinationBounds(id);
        if (row.isEmpty()) { return {}; }
        const auto view = viewport.getViewArea();
        if (! view.contains(row))
        {
            viewport.setViewPosition(0, juce::jmax(0, row.getY() - viewport.getHeight() / 3));
        }
        return getLocalArea(&list, row);
    }

    // The row's left edge in this component's coordinates, if it is on screen.
    std::optional<juce::Point<float>> visibleAnchor(const juce::String& id) const
    {
        const auto row = list.destinationBounds(id);
        if (row.isEmpty() || ! viewport.getViewArea().contains(row.getCentre())) { return std::nullopt; }
        const auto r = getLocalArea(&list, row).toFloat();
        return juce::Point<float>(r.getX() + 7.0f, r.getCentreY());
    }

    void resized() override
    {
        auto area = getLocalBounds().withTrimmedTop(static_cast<int>(kBand) + 1);
        search.setBounds(area.removeFromTop(22).reduced(4, 1));
        area.removeFromTop(2);
        viewport.setBounds(area);
        list.setSize(juce::jmax(1, viewport.getMaximumVisibleWidth()), list.getHeight());
    }

    void paint(juce::Graphics& g) override
    {
        th::drawModulePanel(g, getLocalBounds().toFloat(), "DESTINATIONS", kMatrixAccent, true, kBand);
        th::drawLabel(g, juce::String(destinationCount), getLocalBounds().toFloat().withHeight(kBand).withTrimmedTop(2.0f)
                                                          .withTrimmedRight(8.0f).removeFromRight(40.0f),
                      th::Type::secondary, th::colour::textDim, juce::Justification::centredRight);
    }

    ModRoutingPanel& owner;
    List list;
    RoutesArea::Port viewport;
    juce::TextEditor search;
    std::vector<Item> items;
    std::vector<int> visibleItems;
    std::set<juce::String> collapsed;
    juce::String filter;
    juce::String highlightId;
    int destinationCount { 0 };
};

//==============================================================================
ModRoutingPanel::ModRoutingPanel(ModDragController& controllerIn)
    : controller(controllerIn), processor(controllerIn.getProcessor())
{
    setComponentID("mod.routing");
    setWantsKeyboardFocus(true);
    header = std::make_unique<Header>(*this);
    addAndMakeVisible(*header);
    sourceColumn = std::make_unique<SourceColumn>(*this, controller);
    addAndMakeVisible(*sourceColumn);
    routesArea = std::make_unique<RoutesArea>();
    listContent = std::make_unique<ListContent>();
    routesArea->viewport.setViewedComponent(listContent.get(), false);
    routesArea->viewport.onScroll = [this] { repaint(); };
    routesArea->onResized = [this] { fitRowsToRoutesArea(); };
    addAndMakeVisible(*routesArea);
    destinations = std::make_unique<DestinationBrowser>(*this);
    destinations->viewport.onScroll = [this] { repaint(); };
    addAndMakeVisible(*destinations);
    refresh();
}

ModRoutingPanel::~ModRoutingPanel()
{
    routesArea->viewport.setViewedComponent(nullptr, false);
}

juce::Component& ModRoutingPanel::getTitle() { return *header; }
juce::Component& ModRoutingPanel::getPatchView() { return *sourceColumn; }
juce::Component& ModRoutingPanel::getList() { return *routesArea; }
juce::Component& ModRoutingPanel::getDestinations() { return *destinations; }
int ModRoutingPanel::getRowCount() const { return static_cast<int>(listContent->rows.size()); }
juce::Component* ModRoutingPanel::getRow(int index)
{
    return juce::isPositiveAndBelow(index, getRowCount()) ? listContent->rows[static_cast<std::size_t>(index)].get() : nullptr;
}
int ModRoutingPanel::getDestinationCount() const { return destinations->destinationCount; }
void ModRoutingPanel::setDestinationFilter(const juce::String& text) { destinations->setFilter(text); }
juce::Rectangle<int> ModRoutingPanel::revealDestination(const juce::String& id) { return destinations->reveal(id); }

void ModRoutingPanel::paint(juce::Graphics& g)
{
    // The seams between the three modules show the chassis.
    g.fillAll(th::colour::chassis);
}

juce::Point<float> ModRoutingPanel::sourceAnchor(int source) const
{
    if (! juce::isPositiveAndBelow(source, kSourceCount)) { return {}; }
    auto& socket = sourceColumn->socket(source);
    return getLocalArea(&socket, socket.jackBounds()).getCentre();
}

void ModRoutingPanel::paintOverChildren(juce::Graphics& g)
{
    // Leads across the seams: source jack -> route strip -> destination row.
    // Only strips on screen are drawn; nothing here runs unless the page is
    // visible and something changed (routes, selection, scroll).
    auto& viewport = routesArea->viewport;
    const auto listArea = getLocalArea(&viewport, viewport.getLocalBounds());
    const auto destArea = getLocalArea(destinations.get(), destinations->viewport.getBounds());
    for (const auto& row : listContent->rows)
    {
        const auto inList = getLocalArea(listContent.get(), row->getBounds());
        if (! listArea.intersects(inList)) { continue; }
        const auto& route = row->getRoute();
        const auto colour = sourceColour(route.source);
        const auto selected = route.key() == selectedKey;
        const auto alpha = selected ? 1.0f : 0.7f;
        const auto width = selected ? 2.6f : 1.6f;
        const auto plug = getLocalPoint(row.get(), row->sourcePlug());
        if (listArea.contains(plug.roundToInt()))
        {
            const auto from = sourceAnchor(route.source);
            g.setColour(colour.withAlpha(alpha));
            g.saveState();
            g.reduceClipRegion(getLocalBounds().withLeft(juce::roundToInt(from.x)));
            g.strokePath(linkPath(from, plug), juce::PathStrokeType(width));
            g.restoreState();
        }
        if (auto anchor = destinations->visibleAnchor(route.destination))
        {
            const auto to = getLocalPoint(destinations.get(), *anchor);
            const auto out = getLocalPoint(row.get(), row->destinationPlug());
            if (listArea.contains(out.roundToInt()) && destArea.contains(to.roundToInt()))
            {
                g.setColour(colour.withAlpha(alpha * 0.85f));
                g.strokePath(linkPath(out, to), juce::PathStrokeType(width));
            }
        }
    }
}

void ModRoutingPanel::resized()
{
    // Fallback when the scene does not place the parts.
    auto area = getLocalBounds();
    header->setBounds(area.removeFromTop(24));
    area.removeFromTop(1);
    sourceColumn->setBounds(area.removeFromLeft(196));
    area.removeFromLeft(1);
    destinations->setBounds(area.removeFromRight(juce::jlimit(250, 400, area.getWidth() * 30 / 100)));
    area.removeFromRight(1);
    routesArea->setBounds(area);
    rebuildRows();
}

void ModRoutingPanel::armSource(int source)
{
    armedSource = juce::isPositiveAndBelow(source, kSourceCount) ? source : -1;
    sourceColumn->setCounts(routes);
    header->repaint();
}

int ModRoutingPanel::patchArmedSourceTo(const juce::String& destination)
{
    if (armedSource < 0) { return -1; }
    juce::String error;
    const auto slot = createRoute(processor, armedSource, destination, error);
    if (slot >= 0)
    {
        refresh();
        RouteInfo key;
        key.slot = slot;
        key.source = armedSource;
        key.destination = destination;
        select(key.key());
    }
    return slot;
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
        sourceColumn->setCounts(routes);
        destinations->setRoutes(routes);
        header->repaint();
        repaint();
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
    if (depthsMoved) { listContent->repaint(); }
}

int ModRoutingPanel::routeRowWidth() const
{
    const auto& viewport = routesArea->viewport;
    return juce::jmax(1, viewport.getWidth() - viewport.getScrollBarThickness());
}

void ModRoutingPanel::fitRowsToRoutesArea()
{
    if (listContent == nullptr) { return; }
    const auto width = routeRowWidth();
    if (listContent->getWidth() == width) { return; }
    for (auto& row : listContent->rows) { row->setSize(width, row->getHeight()); }
    listContent->setSize(width, juce::jmax(listContent->getHeight(), routesArea->viewport.getHeight()));
    repaint();
}

void ModRoutingPanel::rebuildRows()
{
    // Only the routes changed: the scroll position is kept.
    auto& viewport = routesArea->viewport;
    const auto scroll = viewport.getViewPosition();
    listContent->rows.clear();
    const auto width = routeRowWidth();
    constexpr int rowHeight = 30;
    int y = 0;
    for (const auto& route : routes)
    {
        auto row = std::make_unique<Row>(*this, route);
        row->setBounds(0, y, width, rowHeight);
        y += rowHeight;
        listContent->addAndMakeVisible(*row);
        listContent->rows.push_back(std::move(row));
    }
    listContent->setSize(width, juce::jmax(y, viewport.getHeight()));
    viewport.setViewPosition(scroll);
    listContent->repaint();
}

void ModRoutingPanel::select(const juce::String& routeKey)
{
    if (selectedKey == routeKey) { return; }
    selectedKey = routeKey;
    auto& viewport = routesArea->viewport;
    juce::String destination;
    for (auto& row : listContent->rows)
    {
        row->repaint();
        if (row->getRoute().key() == selectedKey)
        {
            destination = row->getRoute().destination;
            if (! viewport.getViewArea().contains(row->getBounds()))
            {
                viewport.setViewPosition(0, juce::jlimit(0, juce::jmax(0, listContent->getHeight() - viewport.getHeight()),
                                                         row->getY() - viewport.getHeight() / 2));
            }
        }
    }
    // The destination it lands on lights up in the browser.
    destinations->setHighlight(destination);
    destinations->reveal(destination);
    repaint();
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
    if (key.getKeyCode() == juce::KeyPress::escapeKey && armedSource >= 0)
    {
        armSource(-1);
        return true;
    }
    return false;
}
} // namespace px3::ui::modrouting
