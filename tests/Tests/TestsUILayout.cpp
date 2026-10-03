#include "TestSupport.h"
#include "UILayout.h"

namespace px3tests
{
namespace
{
px3::ui::InstrumentSceneNode makeSceneNode(const juce::String& id,
                                           const juce::String& parentId,
                                           juce::Rectangle<float> bounds,
                                           int order,
                                           const juce::String& styleToken,
                                           px3::ui::InstrumentSceneNodeKind kind
                                               = px3::ui::InstrumentSceneNodeKind::container,
                                           const juce::String& bindingId = {})
{
    px3::ui::InstrumentSceneNode node;
    node.id = id;
    node.parentId = parentId;
    node.kind = kind;
    node.layout = px3::ui::InstrumentSceneLayoutMode::absolute;
    node.bindingId = bindingId;
    node.styleToken = styleToken;
    node.bounds = bounds;
    node.order = order;
    return node;
}
}

void testUILayout()
{
    suite("INSTRUMENT SCENE");

    using px3::ui::InstrumentSceneDocument;
    using px3::ui::InstrumentSceneNodeKind;

    {
        InstrumentSceneDocument shipped;
        juce::String error;
        const auto file = juce::File::getCurrentWorkingDirectory()
                              .getChildFile("shared/UI/Style/InstrumentScene.json");
        const auto loaded = file.existsAsFile() && shipped.loadJson(file.loadFileAsString(), error);
        const auto* surface = shipped.findStyleToken("surface");
        const auto* oscStyle = shipped.findStyleToken("oscillator.panel");
        const auto* filterStyle = shipped.findStyleToken("filter.panel");
        const auto* ampStyle = shipped.findStyleToken("amp.panel");
          const auto* primary = shipped.findNode("primary");
          const auto* oscNode = shipped.findNode("primary.osc");
          const auto* filterNode = shipped.findNode("primary.filter");
          const auto* ampNode = shipped.findNode("primary.amp");
        check("InstrumentScene_LoadsShippedPrimaryAndDedicatedViews",
              loaded && shipped.findNode("primary.osc") != nullptr
                  && shipped.findNode("primary.filter") != nullptr
                  && shipped.findNode("primary.amp") != nullptr
                  && shipped.findNode("view.filter") != nullptr
                  && shipped.findNode("view.amp") != nullptr,
              error);
        const auto oscBounds = shipped.resolveBounds("primary.osc", { 0, 0, 1200, 480 });
        const auto filterBounds = shipped.resolveBounds("primary.filter", { 0, 0, 1200, 480 });
        const auto ampBounds = shipped.resolveBounds("primary.amp", { 0, 0, 1200, 480 });
        check("InstrumentScene_PrimaryModulesFollowHorizontalSignalFlow",
              primary != nullptr && primary->layout == px3::ui::InstrumentSceneLayoutMode::row
                  && oscNode != nullptr && oscNode->parentId == "primary"
                  && filterNode != nullptr && filterNode->parentId == "primary"
                  && ampNode != nullptr && ampNode->parentId == "primary"
                  && oscBounds.getX() == 0.0f && filterBounds.getX() >= oscBounds.getRight()
                  && ampBounds.getX() >= filterBounds.getRight()
                  && juce::approximatelyEqual(oscBounds.getY(), 0.0f)
                  && juce::approximatelyEqual(filterBounds.getY(), 0.0f)
                  && juce::approximatelyEqual(ampBounds.getY(), 0.0f)
                  && juce::approximatelyEqual(oscBounds.getHeight(), 480.0f)
                  && juce::approximatelyEqual(filterBounds.getHeight(), 480.0f)
                  && juce::approximatelyEqual(ampBounds.getHeight(), 480.0f)
                  && ampBounds.getWidth() >= 280.0f
                  && juce::approximatelyEqual(ampBounds.getRight(), 1200.0f),
              "OSC, filter, AMP share a row without overlap; AMP has room for its controls");
        check("InstrumentScene_PrimaryModulesShareTheFaceplatePalette",
              surface != nullptr && oscStyle != nullptr && filterStyle != nullptr && ampStyle != nullptr
                  && surface->background == "#101214"
                  && oscStyle->background == filterStyle->background
                  && filterStyle->background == ampStyle->background
                  && surface->foreground == "#F2F3F1"
                  && oscStyle->borderRadius == 0.0f && oscStyle->borderWidth == 0.0f
                  && filterStyle->borderRadius == 0.0f && filterStyle->borderWidth == 0.0f
                  && ampStyle->borderRadius == 0.0f && ampStyle->borderWidth == 0.0f,
              "near-black shared surface, white labels, and no floating panel borders");
    }

    InstrumentSceneDocument document;
    juce::String error;
    const auto surfaceStyle = document.addStyleToken(
        { "surface", "#171A1C", "#E6E9E7", "#68A9C8", "none" }, error);
    const auto panelStyle = document.addStyleToken(
        { "panel.osc", "#15181B", "#E6E9E7", "#68A9C8", "none" }, error);
    const auto filterStyle = document.addStyleToken(
        { "panel.filter", "#202629", "#E6E9E7", "#E36C54", "painted-metal-fine" }, error);
    const auto knobStyle = document.addStyleToken(
        { "control.knob", "#25292B", "#E6E9E7", "#68A9C8", "none" }, error);

    const auto rootAdded = document.addNode(
        makeSceneNode("surface", {}, { 0, 0, 1, 1 }, 0, "surface"), error);
    const auto panelAdded = document.addNode(
        makeSceneNode("surface.osc", "surface", { 0.1f, 0.2f, 0.6f, 0.5f }, 0, "panel.osc"), error);
    const auto controlAdded = document.addNode(
        makeSceneNode("osc1.octave", "surface.osc", { 0.1f, 0.1f, 0.2f, 0.2f }, 0,
                      "control.knob", InstrumentSceneNodeKind::parameterControl,
                      "voice.osc1.tuning.octave"), error);
    const auto siblingAdded = document.addNode(
        makeSceneNode("surface.mod", "surface", { 0, 0, 1, 1 }, 1, "panel.osc"), error);

    check("InstrumentScene_AddsTypedNodesAndBindings",
          rootAdded && panelAdded && controlAdded && siblingAdded,
          "container, panel and parameter-control node accepted");
    check("InstrumentScene_DeclaresReusableStyleTokens",
          surfaceStyle && panelStyle && filterStyle && knobStyle,
          "surface, panel and control tokens accepted");

    const auto serialized = document.toJson();
    InstrumentSceneDocument roundTripped;
    const auto parsed = roundTripped.loadJson(serialized, error);
    const auto* panel = roundTripped.findNode("surface.osc");
    const auto* control = roundTripped.findNode("osc1.octave");
    check("InstrumentScene_RoundTripsBindingsStylesAndConstraints",
          parsed && panel != nullptr && panel->parentId == "surface"
              && juce::approximatelyEqual(panel->bounds.getX(), 0.1f)
              && panel->kind == InstrumentSceneNodeKind::container
              && panel->styleToken == "panel.osc"
              && control != nullptr
              && control->kind == InstrumentSceneNodeKind::parameterControl
              && control->bindingId == "voice.osc1.tuning.octave"
              && control->styleToken == "control.knob"
              && roundTripped.getStyleTokens().size() == 4,
          error);

    {
        auto token = *roundTripped.findStyleToken("panel.osc");
        const auto original = roundTripped.toJson();
        roundTripped.beginTransaction();
        token.background = "#283034";
        token.knobDiameter = 62.0f;
        const auto edited = roundTripped.updateStyleToken(token, error);
        const auto committed = roundTripped.commitTransaction();
        InstrumentSceneDocument styleRoundTrip;
        const auto loaded = styleRoundTrip.loadJson(roundTripped.toJson(), error);
        const auto* restoredToken = styleRoundTrip.findStyleToken("panel.osc");
        check("InstrumentScene_StyleTokenEditsRoundTripAndUndo",
              edited && committed && loaded && restoredToken != nullptr
                  && restoredToken->background == "#283034" && restoredToken->knobDiameter == 62.0f
                  && roundTripped.undo() && roundTripped.toJson() == original, error);
        token.background = "invalid";
        check("InstrumentScene_InvalidStyleTokenEditsLeaveDocumentIntact",
              ! roundTripped.updateStyleToken(token, error) && roundTripped.toJson() == original);
    }

    roundTripped.beginTransaction();
    const auto presentationChanged = roundTripped.setNodePresentation(
        "osc1.octave", px3::ui::InstrumentSceneNodeKind::parameterControl,
        "OSC 1 OCTAVE", "voice.osc1.tuning.octave", error);
    const auto presentationCommitted = roundTripped.commitTransaction();
    InstrumentSceneDocument presentationRoundTrip;
    const auto presentationLoaded = presentationRoundTrip.loadJson(roundTripped.toJson(), error);
    control = presentationRoundTrip.findNode("osc1.octave");
    check("InstrumentScene_EditsAndSerializesControlPresentation",
          presentationChanged && presentationCommitted && presentationLoaded
              && control != nullptr && control->label == "OSC 1 OCTAVE"
              && control->bindingId == "voice.osc1.tuning.octave",
          error);
    const auto presentationBeforeReject = roundTripped.toJson();
    const auto invalidPresentation = ! roundTripped.setNodePresentation(
        "osc1.octave", InstrumentSceneNodeKind::parameterControl, "BROKEN", {}, error);
    check("InstrumentScene_RejectsUnboundControlPresentationWithoutMutation",
          invalidPresentation && roundTripped.toJson() == presentationBeforeReject,
          error);

    const auto previous = panel != nullptr ? panel->bounds : juce::Rectangle<float>();
    const auto rejectedBounds
        = ! roundTripped.setBounds("surface.osc", { 0.8f, 0.2f, 0.6f, 0.5f }, error);
    panel = roundTripped.findNode("surface.osc");
    check("InstrumentScene_RejectsOutOfParentBoundsWithoutMutation",
          rejectedBounds && panel != nullptr && panel->bounds == previous,
          error);

    const auto duplicateRejected = ! document.addNode(
        makeSceneNode("surface.osc", "surface", { 0, 0, 0.5f, 0.5f }, 2, "panel.osc"), error);
    check("InstrumentScene_RejectsDuplicateIds", duplicateRejected, error);

    const auto unknownParentRejected = ! document.addNode(
        makeSceneNode("orphan", "missing", { 0, 0, 0.5f, 0.5f }, 2, "panel.osc"), error);
    check("InstrumentScene_RejectsUnknownParents", unknownParentRejected, error);

    const auto unboundControlRejected = ! document.addNode(
        makeSceneNode("unbound", "surface.osc", { 0, 0, 0.5f, 0.5f }, 1,
                      "control.knob", InstrumentSceneNodeKind::parameterControl), error);
    check("InstrumentScene_RejectsUnboundParameterControls", unboundControlRejected, error);

    const auto unsupportedVersionRejected = ! roundTripped.loadJson(
        R"({"schemaVersion":99,"styles":[],"nodes":[]})", error);
    check("InstrumentScene_RejectsUnsupportedSchemaVersion", unsupportedVersionRejected, error);

    const auto cycleRejected = ! roundTripped.loadJson(
        R"({"schemaVersion":1,"styles":[{"id":"surface","background":"#171A1C","foreground":"#E6E9E7","accent":"#68A9C8","texture":"none","knobDiameter":54,"labelSize":11,"borderRadius":4,"borderWidth":1}],"nodes":[{"id":"root","parentId":"","kind":"container","layout":"absolute","bindingId":"","styleToken":"surface","label":"","order":0,"visible":true,"bounds":{"x":0,"y":0,"width":1,"height":1},"minimumSize":{"width":0,"height":0},"maximumSize":{"width":0,"height":0}},{"id":"a","parentId":"b","kind":"container","layout":"absolute","bindingId":"","styleToken":"surface","label":"","order":0,"visible":true,"bounds":{"x":0,"y":0,"width":0.5,"height":0.5},"minimumSize":{"width":0,"height":0},"maximumSize":{"width":0,"height":0}},{"id":"b","parentId":"a","kind":"container","layout":"absolute","bindingId":"","styleToken":"surface","label":"","order":0,"visible":true,"bounds":{"x":0,"y":0,"width":0.5,"height":0.5},"minimumSize":{"width":0,"height":0},"maximumSize":{"width":0,"height":0}}]})",
        error);
    check("InstrumentScene_RejectsHierarchyCycles", cycleRejected, error);

    roundTripped.beginTransaction();
    const auto edited = roundTripped.setBounds("surface.osc", { 0.2f, 0.2f, 0.6f, 0.5f }, error);
    const auto committed = roundTripped.commitTransaction();
    const auto undone = roundTripped.undo();
    const auto* restored = roundTripped.findNode("surface.osc");
    const auto undoRestored = restored != nullptr
                           && juce::approximatelyEqual(restored->bounds.getX(), 0.1f);
    const auto redone = roundTripped.redo();
    restored = roundTripped.findNode("surface.osc");
    check("InstrumentScene_TransactionsUndoAndRedoBounds",
          edited && committed && undone && undoRestored && redone
              && restored != nullptr && juce::approximatelyEqual(restored->bounds.getX(), 0.2f),
          error);

    roundTripped.beginTransaction();
    const auto styleChanged = roundTripped.setNodeStyleToken("surface.osc", "panel.filter", error);
    const auto styleCommitted = roundTripped.commitTransaction();
    const auto styleUndone = roundTripped.undo();
    auto* styledNode = roundTripped.findNode("surface.osc");
    const auto originalStyleRestored = styledNode != nullptr && styledNode->styleToken == "panel.osc";
    const auto styleRedone = roundTripped.redo();
    styledNode = roundTripped.findNode("surface.osc");
    check("InstrumentScene_StyleTokenChangeIsUndoable",
          styleChanged && styleCommitted && styleUndone && originalStyleRestored && styleRedone
              && styledNode != nullptr && styledNode->styleToken == "panel.filter",
          error);

    const auto reordered = roundTripped.setOrder("surface.osc", 1, error);
    const auto* osc = roundTripped.findNode("surface.osc");
    const auto* mod = roundTripped.findNode("surface.mod");
    check("InstrumentScene_ReordersSiblingsWithoutDuplicateOrders",
          reordered && osc != nullptr && mod != nullptr && osc->order == 1 && mod->order == 0,
          error);

    InstrumentSceneDocument flowDocument;
    flowDocument.addStyleToken({ "surface", "#171A1C", "#E6E9E7", "#68A9C8", "none" }, error);
    auto rowRoot = makeSceneNode("root", {}, { 0, 0, 1, 1 }, 0, "surface");
    rowRoot.layout = px3::ui::InstrumentSceneLayoutMode::row;
    rowRoot.spacing = 100.0f;
    flowDocument.addNode(rowRoot, error);
    auto firstCell = makeSceneNode("first", "root", { 0, 0, 0.5f, 1 }, 0, "surface");
    firstCell.flexGrow = 1.0f;
    flowDocument.addNode(firstCell, error);
    auto secondCell = makeSceneNode("second", "root", { 0, 0, 0.5f, 1 }, 1, "surface");
    secondCell.flexGrow = 2.0f;
    flowDocument.addNode(secondCell, error);
    const auto firstBounds = flowDocument.resolveBounds("first", { 10, 20, 1000, 400 });
    const auto secondBounds = flowDocument.resolveBounds("second", { 10, 20, 1000, 400 });
    check("InstrumentScene_RowLayoutResolvesWeightedChildren",
          juce::approximatelyEqual(firstBounds.getWidth(), 300.0f)
              && juce::approximatelyEqual(secondBounds.getWidth(), 600.0f)
              && juce::approximatelyEqual(secondBounds.getX(), 410.0f),
          "1:2 grow weights with 100px gap resolve to 300px and 600px");

    const auto flowUpdated = flowDocument.setFlowProperties("root", 1.0f, 100.0f, 2, error);
    const auto modeUpdated = flowDocument.setLayoutMode("root", px3::ui::InstrumentSceneLayoutMode::column,
                                                        error);
        const auto constraintsUpdated = flowDocument.setSizeConstraints("first", { 120.0f, 80.0f },
                                                   { 480.0f, 320.0f }, error);
    const auto parentUpdated = flowDocument.setParentNode("second", "first", error);
    const auto* reparented = flowDocument.findNode("second");
    check("InstrumentScene_EditsLayoutModeFlowAndParent",
            flowUpdated && modeUpdated && parentUpdated && constraintsUpdated && reparented != nullptr
              && reparented->parentId == "first",
          error);

        const auto* constrained = flowDocument.findNode("first");
        check("InstrumentScene_EditsAndSerializesSizeConstraints",
            constrained != nullptr && constrained->minimumSize == juce::Point<float>(120.0f, 80.0f)
              && constrained->maximumSize == juce::Point<float>(480.0f, 320.0f));
        const auto validConstraints = flowDocument.toJson();
        const auto invalidConstraints = ! flowDocument.setSizeConstraints("first", { 500.0f, 80.0f },
                                                    { 480.0f, 320.0f }, error);
        check("InstrumentScene_RejectsInvalidSizeConstraintsWithoutMutation",
            invalidConstraints && flowDocument.toJson() == validConstraints,
            error);

    const auto cycleParentRejected = ! flowDocument.setParentNode("root", "second", error);
    check("InstrumentScene_RejectsReparentCycle", cycleParentRejected, error);

    for (const auto layout : { px3::ui::InstrumentSceneLayoutMode::absolute,
                               px3::ui::InstrumentSceneLayoutMode::overlay,
                               px3::ui::InstrumentSceneLayoutMode::row,
                               px3::ui::InstrumentSceneLayoutMode::column,
                               px3::ui::InstrumentSceneLayoutMode::grid })
    {
        for (const auto& hiddenId : { "root", "container", "leaf" })
        {
            InstrumentSceneDocument visibilityDocument;
            visibilityDocument.addStyleToken({ "surface" }, error);
            auto visibilityRoot = makeSceneNode("root", {}, { 0, 0, 1, 1 }, 0, "surface");
            visibilityRoot.layout = layout;
            visibilityRoot.visible = juce::String(hiddenId) != "root";
            auto container = makeSceneNode("container", "root", { 0, 0, 1, 1 }, 0, "surface");
            container.layout = layout;
            container.visible = juce::String(hiddenId) != "container";
            auto leaf = makeSceneNode("leaf", "container", { 0, 0, 1, 1 }, 0, "surface");
            leaf.visible = juce::String(hiddenId) != "leaf";
            const auto loaded = visibilityDocument.addNode(visibilityRoot, error)
                && visibilityDocument.addNode(container, error)
                && visibilityDocument.addNode(leaf, error);
            const auto testName = "InstrumentScene_HiddenAncestorOrLeaf_"
                + juce::String(static_cast<int>(layout)) + "_" + hiddenId;
            check(testName.toRawUTF8(),
                  loaded && ! visibilityDocument.isNodeVisible("leaf")
                      && visibilityDocument.resolveBounds("leaf", { 0, 0, 1000, 400 }).isEmpty(),
                  error);
        }
    }
    check("InstrumentScene_VisibleNodeAndMissingNode",
          document.isNodeVisible("osc1.octave") && ! document.isNodeVisible("missing"));

        document.beginTransaction();
        const auto hidden = document.setVisible("surface.osc", false, error);
        const auto visibilityCommitted = document.commitTransaction();
        InstrumentSceneDocument hiddenRoundTrip;
        const auto hiddenLoaded = hiddenRoundTrip.loadJson(document.toJson(), error);
        check("InstrumentScene_VisibilityPersistsAndHidesDescendants",
            hidden && visibilityCommitted && hiddenLoaded
              && ! hiddenRoundTrip.isNodeVisible("osc1.octave"));
        const auto visibilityUndone = document.undo();
        const auto visibleRestored = document.isNodeVisible("osc1.octave");
        const auto visibilityRedone = document.redo();
        check("InstrumentScene_VisibilityIsUndoable",
            visibilityUndone && visibleRestored && visibilityRedone
              && ! document.isNodeVisible("osc1.octave"));
        const auto unchanged = document.toJson();
        check("InstrumentScene_UnknownVisibilityEditDoesNotMutate",
            ! document.setVisible("missing", false, error) && document.toJson() == unchanged,
            error);
}
}
