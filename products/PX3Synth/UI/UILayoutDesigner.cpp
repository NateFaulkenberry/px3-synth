#include "UILayoutDesigner.h"

#if PX3_UI_DESIGNER

namespace px3::ui
{
namespace le = layoutedit;

namespace
{
const juce::Colour kSelect { 0xffffd23f };
const juce::Colour kHover { 0xff4fd1ff };
const juce::Colour kPanel { 0xff1b1e21 };

juce::String edgesText(const Edges& e)
{
    const auto n = [](float v) { return Length::px(v).toString(); };
    if (e.top == e.right && e.right == e.bottom && e.bottom == e.left) { return n(e.top); }
    return n(e.top) + " " + n(e.right) + " " + n(e.bottom) + " " + n(e.left);
}

bool parseEdges(const juce::String& text, Edges& out)
{
    auto parts = juce::StringArray::fromTokens(text.trim(), " ,", "");
    parts.removeEmptyStrings();
    std::vector<float> v;
    for (const auto& p : parts)
    {
        if (! p.containsOnly("0123456789.-")) { return false; }
        v.push_back(p.getFloatValue());
    }
    if (v.size() == 1) { out = { v[0], v[0], v[0], v[0] }; return true; }
    if (v.size() == 2) { out = { v[0], v[1], v[0], v[1] }; return true; }
    if (v.size() == 4) { out = { v[0], v[1], v[2], v[3] }; return true; }
    return false;
}

juce::String tracksText(const std::vector<Length>& t)
{
    juce::StringArray s;
    for (const auto& l : t) { s.add(l.toString()); }
    return s.joinIntoString(" ");
}

bool parseTracks(const juce::String& text, std::vector<Length>& out)
{
    auto parts = juce::StringArray::fromTokens(text.trim(), " ,", "");
    parts.removeEmptyStrings();
    out.clear();
    for (const auto& p : parts)
    {
        Length l;
        if (! Length::fromString(p, l, true)) { return false; }
        out.push_back(l);
    }
    return true;
}

bool parseNumber(const juce::String& text, float& out)
{
    const auto t = text.trim();
    if (t.isEmpty() || ! t.containsOnly("0123456789.-")) { return false; }
    out = t.getFloatValue();
    return true;
}

juce::String numberText(float v) { return Length::px(v).toString(); }

// One editable property of a node.
struct Property
{
    enum class Type { text, choice, toggle, readOnly };
    juce::String key;
    Type type { Type::text };
    juce::StringArray choices;
    std::function<juce::String(const InstrumentSceneNode&)> get;
    std::function<bool(InstrumentSceneNode&, const juce::String&)> set;
};

template <typename E>
Property enumProperty(const juce::String& key, std::initializer_list<E> values, E InstrumentSceneNode::*member)
{
    Property p;
    p.key = key;
    p.type = Property::Type::choice;
    for (auto v : values) { p.choices.add(toString(v)); }
    p.get = [member](const InstrumentSceneNode& n) { return toString(n.*member); };
    p.set = [member](InstrumentSceneNode& n, const juce::String& t) { return parse(t, n.*member); };
    return p;
}

Property lengthProperty(const juce::String& key, Length InstrumentSceneNode::*member)
{
    return { key, Property::Type::text, {},
             [member](const InstrumentSceneNode& n) { return (n.*member).toString(); },
             [member](InstrumentSceneNode& n, const juce::String& t) { return Length::fromString(t, n.*member, false); } };
}

Property floatProperty(const juce::String& key, float InstrumentSceneNode::*member)
{
    return { key, Property::Type::text, {},
             [member](const InstrumentSceneNode& n) { return numberText(n.*member); },
             [member](InstrumentSceneNode& n, const juce::String& t) { return parseNumber(t, n.*member); } };
}

Property intProperty(const juce::String& key, int InstrumentSceneNode::*member)
{
    return { key, Property::Type::text, {},
             [member](const InstrumentSceneNode& n) { return juce::String(n.*member); },
             [member](InstrumentSceneNode& n, const juce::String& t)
             {
                 float v = 0;
                 if (! parseNumber(t, v)) { return false; }
                 n.*member = juce::roundToInt(v);
                 return true;
             } };
}

Property boolProperty(const juce::String& key, bool InstrumentSceneNode::*member)
{
    return { key, Property::Type::toggle, {},
             [member](const InstrumentSceneNode& n) { return juce::String(n.*member ? "true" : "false"); },
             [member](InstrumentSceneNode& n, const juce::String& t) { n.*member = t == "true"; return true; } };
}

Property stringProperty(const juce::String& key, juce::String InstrumentSceneNode::*member)
{
    return { key, Property::Type::text, {},
             [member](const InstrumentSceneNode& n) { return n.*member; },
             [member](InstrumentSceneNode& n, const juce::String& t) { n.*member = t.trim(); return true; } };
}

Property edgesProperty(const juce::String& key, Edges InstrumentSceneNode::*member)
{
    return { key, Property::Type::text, {},
             [member](const InstrumentSceneNode& n) { return edgesText(n.*member); },
             [member](InstrumentSceneNode& n, const juce::String& t) { return parseEdges(t, n.*member); } };
}

Property tracksProperty(const juce::String& key, std::vector<Length> InstrumentSceneNode::*member)
{
    return { key, Property::Type::text, {},
             [member](const InstrumentSceneNode& n) { return tracksText(n.*member); },
             [member](InstrumentSceneNode& n, const juce::String& t) { return parseTracks(t, n.*member); } };
}

// Only what applies to this node in its current role.
std::vector<Property> propertiesFor(const InstrumentSceneDocument& doc, int index)
{
    using N = InstrumentSceneNode;
    using K = InstrumentSceneNodeKind;
    std::vector<Property> props;
    const auto& n = doc.getNodes()[static_cast<std::size_t>(index)];
    const auto role = le::roleOf(doc, index);

    props.push_back({ "id", Property::Type::readOnly, {}, [](const N& x) { return x.id; }, nullptr });
    props.push_back({ "role", Property::Type::readOnly, {}, [&doc, index](const N&) { return le::describe(le::roleOf(doc, index)); }, nullptr });
    props.push_back(enumProperty("kind", { K::container, K::panel, K::card, K::control, K::button, K::label, K::display, K::decoration }, &N::kind));
    props.push_back(stringProperty("label", &N::label));
    props.push_back(stringProperty("style", &N::styleToken));
    props.push_back(boolProperty("visible", &N::visible));
    props.push_back(boolProperty("locked", &N::locked));
    if (n.kind == K::control || n.kind == K::button || n.kind == K::label)
    {
        props.push_back(stringProperty("binding", &N::bindingId));
    }

    // As a container.
    props.push_back(enumProperty("layout", { InstrumentSceneLayoutMode::none, InstrumentSceneLayoutMode::flex, InstrumentSceneLayoutMode::grid }, &N::layout));
    if (n.layout == InstrumentSceneLayoutMode::flex)
    {
        props.push_back(enumProperty("direction", { LayoutDirection::row, LayoutDirection::column }, &N::direction));
        props.push_back(boolProperty("wrap", &N::wrap));
        props.push_back(enumProperty("justify", { LayoutJustify::start, LayoutJustify::center, LayoutJustify::end, LayoutJustify::spaceBetween, LayoutJustify::spaceAround, LayoutJustify::spaceEvenly }, &N::justify));
    }
    if (n.layout == InstrumentSceneLayoutMode::grid)
    {
        props.push_back(tracksProperty("columns", &N::gridColumns));
        props.push_back(tracksProperty("rows", &N::gridRows));
    }
    if (n.layout != InstrumentSceneLayoutMode::none)
    {
        props.push_back(enumProperty("alignItems", { LayoutAlign::start, LayoutAlign::center, LayoutAlign::end, LayoutAlign::stretch }, &N::alignItems));
        props.push_back(floatProperty("gapX", &N::gapX));
        props.push_back(floatProperty("gapY", &N::gapY));
    }
    if (n.layout != InstrumentSceneLayoutMode::none || ! doc.childrenOf(index).empty())
    {
        props.push_back(edgesProperty("padding", &N::padding));
    }

    // As a child.
    if (role != le::Role::root)
    {
        props.push_back(enumProperty("position", { LayoutPosition::relative, LayoutPosition::absolute }, &N::position));
    }
    switch (role)
    {
        case le::Role::root: break;
        case le::Role::flexChild:
            props.push_back(lengthProperty("basis", &N::basis));
            props.push_back(floatProperty("grow", &N::grow));
            props.push_back(floatProperty("shrink", &N::shrink));
            props.push_back(intProperty("order", &N::order));
            props.push_back(enumProperty("alignSelf", { LayoutAlign::automatic, LayoutAlign::start, LayoutAlign::center, LayoutAlign::end, LayoutAlign::stretch }, &N::alignSelf));
            break;
        case le::Role::gridChild:
            props.push_back(intProperty("column", &N::gridColumn));
            props.push_back(intProperty("row", &N::gridRow));
            props.push_back(intProperty("columnSpan", &N::columnSpan));
            props.push_back(intProperty("rowSpan", &N::rowSpan));
            props.push_back(intProperty("order", &N::order));
            props.push_back(enumProperty("alignSelf", { LayoutAlign::automatic, LayoutAlign::start, LayoutAlign::center, LayoutAlign::end, LayoutAlign::stretch }, &N::alignSelf));
            break;
        case le::Role::absolute:
            props.push_back(lengthProperty("x", &N::x));
            props.push_back(lengthProperty("y", &N::y));
            break;
        case le::Role::overlayChild:
            props.push_back(enumProperty("alignSelf", { LayoutAlign::automatic, LayoutAlign::start, LayoutAlign::center, LayoutAlign::end, LayoutAlign::stretch }, &N::alignSelf));
            break;
    }
    if (role != le::Role::root)
    {
        props.push_back(lengthProperty("width", &N::width));
        props.push_back(lengthProperty("height", &N::height));
        props.push_back(lengthProperty("minWidth", &N::minWidth));
        props.push_back(lengthProperty("maxWidth", &N::maxWidth));
        props.push_back(lengthProperty("minHeight", &N::minHeight));
        props.push_back(lengthProperty("maxHeight", &N::maxHeight));
        props.push_back(edgesProperty("margin", &N::margin));
        props.push_back(floatProperty("aspect", &N::aspect));
    }
    return props;
}

juce::String signatureOf(const InstrumentSceneDocument& doc, int index)
{
    if (index < 0) { return {}; }
    const auto& n = doc.getNodes()[static_cast<std::size_t>(index)];
    return n.id + "|" + le::describe(le::roleOf(doc, index)) + "|" + toString(n.layout) + "|" + toString(n.kind)
         + "|" + juce::String(doc.childrenOf(index).empty() ? 0 : 1);
}
}

// ============================================================================
// The overlay on the live editor.

class LayoutDesignerOverlay final : public juce::Component
{
public:
    explicit LayoutDesignerOverlay(LayoutDesigner& d) : designer(d)
    {
        setWantsKeyboardFocus(true);
        setAlwaysOnTop(true);
    }

    void paint(juce::Graphics& g) override
    {
        auto& doc = designer.getDocument();
        doc.resolve(designer.getEditor().getLocalBounds().toFloat());

        if (! designer.editMode)
        {
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.fillRect(getLocalBounds().removeFromTop(16).removeFromRight(170));
            g.setColour(kSelect);
            g.setFont(11.0f);
            g.drawText("LAYOUT DESIGNER: PLAY MODE", getLocalBounds().removeFromTop(16).removeFromRight(166),
                       juce::Justification::centredRight);
        }

        if (hover >= 0 && hover != designer.getSelected() && doc.isShown(hover))
        {
            g.setColour(kHover.withAlpha(0.9f));
            g.drawRect(doc.rectOf(hover), 1.0f);
        }

        const auto sel = designer.getSelected();
        if (sel < 0 || ! doc.isShown(sel)) { return; }
        const auto r = doc.rectOf(sel);
        const auto& node = doc.getNodes()[static_cast<std::size_t>(sel)];

        // Siblings and children, faintly: the structure the edit happens in.
        g.setColour(kSelect.withAlpha(0.25f));
        for (auto c : doc.childrenOf(sel))
        {
            if (doc.isShown(c)) { g.drawRect(doc.rectOf(c), 1.0f); }
        }

        g.setColour(node.locked ? juce::Colours::orangered : kSelect);
        g.drawRect(r, 2.0f);
        if (! node.locked)
        {
            for (const auto& h : handleRects(r))
            {
                g.fillRect(h);
            }
        }

        juce::String tag = node.id + "  " + juce::String(juce::roundToInt(r.getWidth())) + "x"
                         + juce::String(juce::roundToInt(r.getHeight())) + "  " + le::describe(le::roleOf(doc, sel))
                         + (node.locked ? "  LOCKED" : "");
        g.setFont(11.0f);
        const auto tw = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), tag) + 10;
        auto tagArea = juce::Rectangle<float>(r.getX(), r.getY() - 16.0f, static_cast<float>(tw), 15.0f);
        if (tagArea.getY() < 0) { tagArea.setY(r.getY() + 2.0f); }
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.fillRect(tagArea);
        g.setColour(kSelect);
        g.drawText(tag, tagArea.reduced(4, 0), juce::Justification::centredLeft);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        auto& doc = designer.getDocument();
        const auto h = le::hitTest(doc, e.position, [this](int i) { return designer.isEligible(i); });
        if (h != hover)
        {
            hover = h;
            repaint();
        }
        setMouseCursor(cursorFor(handleUnder(e.position)));
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hover = -1;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        auto& doc = designer.getDocument();
        pendingHandle = le::Handle::none;
        deferredSelect = -1;

        // A handle of the current selection takes precedence over selection.
        const auto handle = handleUnder(e.position);
        if (handle != le::Handle::none && handle != le::Handle::move)
        {
            pendingHandle = handle;
            pendingId = doc.getNodes()[static_cast<std::size_t>(designer.getSelected())].id;
            return;
        }

        const auto hit = le::hitTest(doc, e.position, [this](int i) { return designer.isEligible(i); });
        if (hit < 0) { return; }
        auto target = hit;
        if (e.mods.isAltDown() || e.mods.isCommandDown())
        {
            // Climb: the parent of the hit, or one above the current selection
            // when the selection already contains the hit.
            const auto sel = designer.getSelected();
            auto isAncestor = false;
            for (auto i = hit; i >= 0; )
            {
                if (i == sel) { isAncestor = true; break; }
                i = doc.indexOf(doc.getNodes()[static_cast<std::size_t>(i)].parentId);
            }
            const auto from = (isAncestor && sel >= 0) ? sel : hit;
            const auto parent = doc.indexOf(doc.getNodes()[static_cast<std::size_t>(from)].parentId);
            target = parent >= 0 ? parent : from;
        }
        else if (designer.getSelected() >= 0 && hit != designer.getSelected()
                 && isDescendant(hit, designer.getSelected()))
        {
            // Inside the current selection: a drag moves the selection (so a
            // container can be dragged by its body); a plain click without a
            // drag drills down to the node under the cursor on mouse-up.
            deferredSelect = hit;
            target = designer.getSelected();
        }
        if (target != designer.getSelected()) { designer.select(target); }
        pendingHandle = le::Handle::move;
        pendingId = doc.getNodes()[static_cast<std::size_t>(target)].id;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        auto& doc = designer.getDocument();
        if (! drag.active)
        {
            if (pendingHandle == le::Handle::none || e.getDistanceFromDragStart() < 3) { return; }
            deferredSelect = -1;
            if (! le::beginDrag(doc, drag, pendingId, pendingHandle, e.mouseDownPosition,
                                designer.getEditor().getLocalBounds().toFloat()))
            {
                designer.setStatus(pendingId + " cannot be dragged (locked or root)");
                pendingHandle = le::Handle::none;
                return;
            }
        }
        juce::String error;
        if (! le::dragTo(doc, drag, e.position, designer.snapEnabled ? designer.snapSize : 0.0f, error)
            && error.isNotEmpty())
        {
            designer.setStatus(error);
        }
        designer.documentChanged();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (drag.active)
        {
            le::endDrag(designer.getDocument(), drag);
            designer.documentChanged();
        }
        else if (deferredSelect >= 0)
        {
            designer.select(deferredSelect);
        }
        deferredSelect = -1;
        pendingHandle = le::Handle::none;
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        const auto cmd = key.getModifiers().isCommandDown();
        const auto shift = key.getModifiers().isShiftDown();
        if (key == juce::KeyPress::escapeKey)
        {
            if (drag.active)
            {
                le::endDrag(designer.getDocument(), drag, true);
                designer.documentChanged();
            }
            else { designer.selectParent(); }
            return true;
        }
        if (cmd && key.getKeyCode() == 'Z') { shift ? designer.redo() : designer.undo(); return true; }
        if (cmd && key.getKeyCode() == 'S') { designer.save(); return true; }
        return false;
    }

private:
    static std::array<juce::Rectangle<float>, 8> handleRects(juce::Rectangle<float> r)
    {
        const auto s = 7.0f;
        const auto at = [s](float x, float y) { return juce::Rectangle<float>(s, s).withCentre({ x, y }); };
        return { at(r.getX(), r.getY()), at(r.getCentreX(), r.getY()), at(r.getRight(), r.getY()),
                 at(r.getRight(), r.getCentreY()), at(r.getRight(), r.getBottom()), at(r.getCentreX(), r.getBottom()),
                 at(r.getX(), r.getBottom()), at(r.getX(), r.getCentreY()) };
    }

    le::Handle handleUnder(juce::Point<float> p) const
    {
        auto& doc = designer.getDocument();
        const auto sel = designer.getSelected();
        if (sel < 0 || ! doc.isShown(sel) || doc.getNodes()[static_cast<std::size_t>(sel)].locked) { return le::Handle::none; }
        return le::handleAt(doc.rectOf(sel), p, 5.0f);
    }

    bool isDescendant(int node, int ancestor) const
    {
        auto& doc = designer.getDocument();
        for (auto i = node; i >= 0; i = doc.indexOf(doc.getNodes()[static_cast<std::size_t>(i)].parentId))
        {
            if (i == ancestor) { return true; }
        }
        return false;
    }

    static juce::MouseCursor cursorFor(le::Handle h)
    {
        switch (h)
        {
            case le::Handle::left:
            case le::Handle::right: return juce::MouseCursor::LeftRightResizeCursor;
            case le::Handle::top:
            case le::Handle::bottom: return juce::MouseCursor::UpDownResizeCursor;
            case le::Handle::topLeft:
            case le::Handle::bottomRight: return juce::MouseCursor::TopLeftCornerResizeCursor;
            case le::Handle::topRight:
            case le::Handle::bottomLeft: return juce::MouseCursor::TopRightCornerResizeCursor;
            case le::Handle::move: return juce::MouseCursor::DraggingHandCursor;
            case le::Handle::none: break;
        }
        return juce::MouseCursor::CrosshairCursor;
    }

    LayoutDesigner& designer;
    int hover { -1 };
    le::Drag drag;
    le::Handle pendingHandle { le::Handle::none };
    juce::String pendingId;
    int deferredSelect { -1 };
};

// ============================================================================
// The inspector: a separate dev window.

class LayoutInspectorContent final : public juce::Component
{
public:
    explicit LayoutInspectorContent(LayoutDesigner& d) : designer(d)
    {
        const auto button = [this](juce::TextButton& b, const juce::String& text, std::function<void()> f,
                                   const juce::String& tip)
        {
            b.setButtonText(text);
            b.setTooltip(tip);
            b.onClick = std::move(f);
            addAndMakeVisible(b);
        };
        button(editButton, "Edit", [this] { designer.setEditMode(! designer.editMode); refreshToolbar(); },
               "Edit: the overlay takes the mouse. Off: play the synth, selection stays drawn.");
        button(snapButton, "Snap", [this] { designer.snapEnabled = ! designer.snapEnabled; refreshToolbar(); },
               "Snap dragged edges to a 4 px grid");
        button(undoButton, "Undo", [this] { designer.undo(); }, "Cmd-Z");
        button(redoButton, "Redo", [this] { designer.redo(); }, "Shift-Cmd-Z");
        button(parentButton, "Parent", [this] { designer.selectParent(); }, "Select the containing node (Esc)");
        button(upButton, "Order -", [this] { designer.moveInOrder(-1); }, "Move earlier among siblings");
        button(downButton, "Order +", [this] { designer.moveInOrder(1); }, "Move later among siblings");
        button(visibleButton, "Hide", [this] { designer.toggleVisible(); }, "Toggle visible");
        button(lockButton, "Lock", [this] { designer.toggleLocked(); }, "Toggle locked (no drag)");
        button(saveButton, "Save", [this] { designer.save(); }, "Save to the user layout file (Cmd-S)");
        button(saveDefaultButton, "Save default", [this] { designer.saveAsDefault(); },
               "Write shared/UI/Style/InstrumentScene.json in the source tree");
        button(loadButton, "Load", [this] { designer.load(); }, "Load a layout file");
        button(resetButton, "Reset", [this] { designer.reset(); }, "Reset to the shipped default layout");

        addAndMakeVisible(path);
        addAndMakeVisible(status);
        path.setColour(juce::Label::textColourId, kSelect);
        status.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        viewport.setViewedComponent(&rows, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
        refreshToolbar();
    }

    void paint(juce::Graphics& g) override { g.fillAll(kPanel); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(6);
        auto row1 = area.removeFromTop(24);
        for (auto* b : { &editButton, &snapButton, &undoButton, &redoButton, &parentButton, &upButton, &downButton })
        {
            b->setBounds(row1.removeFromLeft(row1.getWidth() / juce::jmax(1, remaining(b, 1)) ).reduced(1));
        }
        area.removeFromTop(2);
        auto row2 = area.removeFromTop(24);
        for (auto* b : { &visibleButton, &lockButton, &saveButton, &saveDefaultButton, &loadButton, &resetButton })
        {
            b->setBounds(row2.removeFromLeft(row2.getWidth() / juce::jmax(1, remaining(b, 2))).reduced(1));
        }
        path.setBounds(area.removeFromTop(20));
        status.setBounds(area.removeFromBottom(20));
        viewport.setBounds(area);
        layoutRows();
    }

    void refreshToolbar()
    {
        editButton.setToggleState(designer.editMode, juce::dontSendNotification);
        editButton.setButtonText(designer.editMode ? "Edit: on" : "Edit: off");
        snapButton.setButtonText(designer.snapEnabled ? "Snap: 4" : "Snap: off");
        const auto& doc = designer.getDocument();
        undoButton.setEnabled(doc.canUndo());
        redoButton.setEnabled(doc.canRedo());
        const auto sel = designer.getSelected();
        if (sel >= 0)
        {
            const auto& n = doc.getNodes()[static_cast<std::size_t>(sel)];
            visibleButton.setButtonText(n.visible ? "Hide" : "Show");
            lockButton.setButtonText(n.locked ? "Unlock" : "Lock");
        }
    }

    void setStatus(const juce::String& text) { status.setText(text, juce::dontSendNotification); }

    // Rebuilds the rows when the selection or its role changed, otherwise
    // only refreshes the values (so a drag streams into the fields).
    void sync()
    {
        refreshToolbar();
        const auto& doc = designer.getDocument();
        const auto sel = designer.getSelected();
        juce::String crumbs;
        for (auto i = sel; i >= 0; i = doc.indexOf(doc.getNodes()[static_cast<std::size_t>(i)].parentId))
        {
            crumbs = doc.getNodes()[static_cast<std::size_t>(i)].id + (crumbs.isEmpty() ? "" : "  >  ") + crumbs;
        }
        path.setText(crumbs.isEmpty() ? "Click anything in the editor" : crumbs, juce::dontSendNotification);

        const auto signature = signatureOf(doc, sel);
        if (signature != builtFor)
        {
            build();
            builtFor = signature;
        }
        if (sel < 0) { return; }
        const auto& node = doc.getNodes()[static_cast<std::size_t>(sel)];
        for (auto& row : rowItems)
        {
            const auto value = row->property.get(node);
            if (row->editor != nullptr && ! row->editor->hasKeyboardFocus(true)) { row->editor->setText(value, false); }
            if (row->value != nullptr) { row->value->setText(value, juce::dontSendNotification); }
            if (row->combo != nullptr) { row->combo->setText(value, juce::dontSendNotification); }
            if (row->toggle != nullptr) { row->toggle->setToggleState(value == "true", juce::dontSendNotification); }
        }
    }

private:
    struct Row
    {
        Property property;
        std::unique_ptr<juce::Label> name, value;
        std::unique_ptr<juce::TextEditor> editor;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
    };

    int remaining(juce::Component* b, int row) const
    {
        const std::vector<const juce::Component*> r1 { &editButton, &snapButton, &undoButton, &redoButton, &parentButton, &upButton, &downButton };
        const std::vector<const juce::Component*> r2 { &visibleButton, &lockButton, &saveButton, &saveDefaultButton, &loadButton, &resetButton };
        const auto& list = row == 1 ? r1 : r2;
        const auto it = std::find(list.begin(), list.end(), b);
        return static_cast<int>(list.end() - it);
    }

    void build()
    {
        rowItems.clear();
        rows.removeAllChildren();
        const auto& doc = designer.getDocument();
        const auto sel = designer.getSelected();
        if (sel >= 0)
        {
            for (auto& p : propertiesFor(doc, sel))
            {
                auto row = std::make_unique<Row>();
                row->property = p;
                row->name = std::make_unique<juce::Label>(juce::String(), p.key);
                row->name->setColour(juce::Label::textColourId, juce::Colours::grey);
                rows.addAndMakeVisible(*row->name);
                const auto key = p.key;
                switch (p.type)
                {
                    case Property::Type::readOnly:
                        row->value = std::make_unique<juce::Label>();
                        rows.addAndMakeVisible(*row->value);
                        break;
                    case Property::Type::text:
                        row->editor = std::make_unique<juce::TextEditor>();
                        row->editor->onReturnKey = [this, key] { commit(key); };
                        row->editor->onFocusLost = [this, key] { commit(key); };
                        rows.addAndMakeVisible(*row->editor);
                        break;
                    case Property::Type::choice:
                        row->combo = std::make_unique<juce::ComboBox>();
                        row->combo->addItemList(p.choices, 1);
                        row->combo->onChange = [this, key] { commit(key); };
                        rows.addAndMakeVisible(*row->combo);
                        break;
                    case Property::Type::toggle:
                        row->toggle = std::make_unique<juce::ToggleButton>();
                        row->toggle->onClick = [this, key] { commit(key); };
                        rows.addAndMakeVisible(*row->toggle);
                        break;
                }
                rowItems.push_back(std::move(row));
            }
        }
        layoutRows();
    }

    void layoutRows()
    {
        const auto width = juce::jmax(100, viewport.getWidth() - 12);
        auto y = 0;
        for (auto& row : rowItems)
        {
            auto line = juce::Rectangle<int>(0, y, width, 24);
            row->name->setBounds(line.removeFromLeft(90));
            for (juce::Component* c : { static_cast<juce::Component*>(row->value.get()),
                                        static_cast<juce::Component*>(row->editor.get()),
                                        static_cast<juce::Component*>(row->combo.get()),
                                        static_cast<juce::Component*>(row->toggle.get()) })
            {
                if (c != nullptr) { c->setBounds(line.reduced(1)); }
            }
            y += 26;
        }
        rows.setSize(width, y);
    }

    void commit(const juce::String& key)
    {
        auto& doc = designer.getDocument();
        const auto sel = designer.getSelected();
        if (sel < 0) { return; }
        for (auto& row : rowItems)
        {
            if (row->property.key != key || row->property.set == nullptr) { continue; }
            const auto text = row->editor != nullptr ? row->editor->getText()
                            : row->combo != nullptr ? row->combo->getText()
                            : row->toggle != nullptr ? juce::String(row->toggle->getToggleState() ? "true" : "false")
                                                     : juce::String();
            auto node = doc.getNodes()[static_cast<std::size_t>(sel)];
            if (row->property.get(node) == text) { return; }
            juce::String error;
            if (! row->property.set(node, text))
            {
                designer.setStatus("Invalid value for " + key + ": " + text);
            }
            else if (! doc.updateNode(node, error))
            {
                designer.setStatus(error);
            }
            else
            {
                designer.setStatus(key + " = " + text);
            }
            // Deferred: committing can rebuild the rows, which owns this editor.
            juce::Component::SafePointer<LayoutInspectorContent> self(this);
            juce::MessageManager::callAsync([self] { if (self != nullptr) { self->designer.documentChanged(); } });
            return;
        }
    }

    LayoutDesigner& designer;
    juce::TextButton editButton, snapButton, undoButton, redoButton, parentButton, upButton, downButton;
    juce::TextButton visibleButton, lockButton, saveButton, saveDefaultButton, loadButton, resetButton;
    juce::Label path, status;
    juce::Viewport viewport;
    juce::Component rows;
    std::vector<std::unique_ptr<Row>> rowItems;
    juce::String builtFor { "-" };
};

class LayoutInspectorWindow final : public juce::DocumentWindow
{
public:
    explicit LayoutInspectorWindow(LayoutDesigner& d)
        : juce::DocumentWindow("PX3 Layout Inspector", kPanel, juce::DocumentWindow::closeButton),
          designer(d)
    {
        setUsingNativeTitleBar(true);
        content = new LayoutInspectorContent(d);
        setContentOwned(content, false);
        setResizable(true, false);
        centreWithSize(420, 640);
        setAlwaysOnTop(true);
    }

    void closeButtonPressed() override { designer.setActive(false); }

    LayoutInspectorContent* content { nullptr };

private:
    LayoutDesigner& designer;
};

// ============================================================================

LayoutDesigner::LayoutDesigner(juce::Component& editorIn, InstrumentSceneDocument& documentIn,
                               SceneBinding& bindingIn, Host hostIn)
    : editor(editorIn), document(documentIn), binding(bindingIn), host(std::move(hostIn))
{
    overlay = std::make_unique<LayoutDesignerOverlay>(*this);
    editor.addChildComponent(*overlay);
    inspector = std::make_unique<LayoutInspectorWindow>(*this);
}

LayoutDesigner::~LayoutDesigner()
{
    if (overlay != nullptr) { editor.removeChildComponent(overlay.get()); }
}

juce::File LayoutDesigner::userLayoutFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("P(X3)/Layouts/InstrumentScene.json");
}

void LayoutDesigner::setActive(bool shouldBeActive)
{
    active = shouldBeActive;
    overlay->setVisible(active);
    inspector->setVisible(active);
    if (active)
    {
        editorLaidOut();
        if (selected < 0) { select(document.rootIndex()); }
        inspector->toFront(false);
        inspector->content->sync();
    }
}

void LayoutDesigner::editorLaidOut()
{
    if (overlay == nullptr) { return; }
    overlay->setBounds(editor.getLocalBounds());
    overlay->setInterceptsMouseClicks(active && editMode, false);
    overlay->toFront(false);
    overlay->repaint();
}

void LayoutDesigner::setEditMode(bool shouldEdit)
{
    editMode = shouldEdit;
    editorLaidOut();
}

bool LayoutDesigner::isEligible(int index) const
{
    // A node is pickable when it is laid out AND what shows it is on screen:
    // the nearest bound component at or above it must be showing. That keeps
    // the overlapping, hidden section views out of reach.
    if (! document.isShown(index)) { return false; }
    for (auto i = index; i >= 0; i = document.indexOf(document.getNodes()[static_cast<std::size_t>(i)].parentId))
    {
        if (auto* c = binding.componentFor(document.getNodes()[static_cast<std::size_t>(i)].id))
        {
            return c->isShowing();
        }
    }
    return true;
}

void LayoutDesigner::select(int index)
{
    selected = index;
    overlay->repaint();
    inspector->content->sync();
}

void LayoutDesigner::selectParent()
{
    if (selected < 0) { return; }
    const auto parent = document.indexOf(document.getNodes()[static_cast<std::size_t>(selected)].parentId);
    if (parent >= 0) { select(parent); }
}

void LayoutDesigner::documentChanged()
{
    if (selected >= static_cast<int>(document.getNodes().size())) { selected = document.rootIndex(); }
    if (host.relayout) { host.relayout(); }
    overlay->repaint();
    inspector->content->sync();
}

void LayoutDesigner::undo()
{
    if (document.undo()) { setStatus("Undo"); }
    documentChanged();
}

void LayoutDesigner::redo()
{
    if (document.redo()) { setStatus("Redo"); }
    documentChanged();
}

void LayoutDesigner::moveInOrder(int direction)
{
    if (selected < 0) { return; }
    juce::String error;
    le::reorder(document, document.getNodes()[static_cast<std::size_t>(selected)].id, direction, error);
    if (error.isNotEmpty()) { setStatus(error); }
    documentChanged();
}

void LayoutDesigner::toggleVisible()
{
    if (selected < 0) { return; }
    auto node = document.getNodes()[static_cast<std::size_t>(selected)];
    node.visible = ! node.visible;
    juce::String error;
    if (! document.updateNode(node, error)) { setStatus(error); }
    documentChanged();
}

void LayoutDesigner::toggleLocked()
{
    if (selected < 0) { return; }
    auto node = document.getNodes()[static_cast<std::size_t>(selected)];
    node.locked = ! node.locked;
    juce::String error;
    if (! document.updateNode(node, error)) { setStatus(error); }
    documentChanged();
}

void LayoutDesigner::save()
{
    const auto file = userLayoutFile();
    file.getParentDirectory().createDirectory();
    setStatus(file.replaceWithText(document.toJson()) ? "Saved " + file.getFullPathName()
                                                      : "Could not write " + file.getFullPathName());
}

void LayoutDesigner::saveAsDefault()
{
    if (host.sourceFile == juce::File() || ! host.sourceFile.getParentDirectory().isDirectory())
    {
        setStatus("No source-tree InstrumentScene.json found (run from the repo)");
        return;
    }
    setStatus(host.sourceFile.replaceWithText(document.toJson()) ? "Saved default " + host.sourceFile.getFullPathName()
                                                                 : "Could not write " + host.sourceFile.getFullPathName());
}

void LayoutDesigner::load()
{
    chooser = std::make_unique<juce::FileChooser>("Load layout", userLayoutFile().getParentDirectory(), "*.json");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc)
                         {
                             const auto file = fc.getResult();
                             if (! file.existsAsFile()) { return; }
                             juce::String error;
                             if (document.replaceFromJson(file.loadFileAsString(), error))
                             {
                                 setStatus("Loaded " + file.getFileName());
                             }
                             else
                             {
                                 setStatus(error);
                             }
                             documentChanged();
                         });
}

void LayoutDesigner::reset()
{
    juce::String error;
    setStatus(document.replaceFromJson(host.defaultJson, error) ? "Reset to the shipped default (undoable)" : error);
    documentChanged();
}

void LayoutDesigner::setStatus(const juce::String& text)
{
    if (inspector != nullptr && inspector->content != nullptr) { inspector->content->setStatus(text); }
}
}

#endif
