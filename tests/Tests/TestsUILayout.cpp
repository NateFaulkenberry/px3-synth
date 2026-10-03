#include "TestSupport.h"
#include "UILayout.h"

#include <cmath>

// The layout engine and the scene document. Every case builds a small scene
// from JSON, resolves it against a known root, and checks rects exactly - the
// engine is deterministic, so there is nothing to approximate beyond float
// rounding.

namespace px3tests
{
namespace
{
using px3::ui::InstrumentSceneDocument;
using px3::ui::InstrumentSceneNode;
using px3::ui::Length;

juce::String sceneJson(const juce::String& nodes, int version = 2)
{
    return "{ \"schemaVersion\": " + juce::String(version)
         + ", \"styles\": [ { \"id\": \"s\", \"background\": \"#101214\", \"foreground\": \"#F2F3F1\", "
           "\"accent\": \"#E5E8E6\", \"texture\": \"none\", \"knobDiameter\": 54, \"labelSize\": 11, "
           "\"borderRadius\": 0, \"borderWidth\": 0 } ], \"nodes\": [ " + nodes + " ] }";
}

bool load(InstrumentSceneDocument& doc, const juce::String& nodes, juce::String& error)
{
    return doc.loadJson(sceneJson(nodes), error);
}

bool near(float a, float b) { return std::abs(a - b) < 0.01f; }

bool rectIs(const InstrumentSceneDocument& doc, const char* id, float x, float y, float w, float h)
{
    const auto r = doc.rectOf(id);
    return near(r.getX(), x) && near(r.getY(), y) && near(r.getWidth(), w) && near(r.getHeight(), h);
}

juce::String describe(const InstrumentSceneDocument& doc, std::initializer_list<const char*> ids)
{
    juce::String s;
    for (auto* id : ids)
    {
        const auto r = doc.rectOf(id);
        s << id << "=" << r.getX() << "," << r.getY() << " " << r.getWidth() << "x" << r.getHeight() << "  ";
    }
    return s;
}

const juce::Rectangle<float> kRoot { 0, 0, 400, 200 };
}

void testUILayout()
{
    suite("INSTRUMENT SCENE / LAYOUT ENGINE");
    juce::String error;

    // ---- Length -----------------------------------------------------------
    {
        Length a, b, c, d;
        const auto ok = Length::fromString("40%", a, false) && Length::fromString("12", b, false)
                     && Length::fromString("2fr", c, true) && Length::fromString("auto", d, false);
        Length bad;
        const auto frRejected = ! Length::fromString("1fr", bad, false) && ! Length::fromString("abc", bad, false);
        check("Layout_LengthParsesPxPercentFrAuto",
              ok && a == Length::percent(40) && b == Length::px(12) && c == Length::fr(2) && d.isAuto()
                  && frRejected && a.toString() == "40%" && c.toString() == "2fr",
              a.toString() + " " + b.toString() + " " + c.toString());
    }

    // ---- flex row: fixed, grow, gaps, padding, margins ----------------------
    {
        InstrumentSceneDocument doc;
        const auto ok = load(doc,
            "{ \"id\": \"r\", \"layout\": \"flex\", \"padding\": [10, 20, 10, 20], \"gap\": 10 },"
            "{ \"id\": \"a\", \"parent\": \"r\", \"width\": 100 },"
            "{ \"id\": \"b\", \"parent\": \"r\", \"grow\": 1 },"
            "{ \"id\": \"c\", \"parent\": \"r\", \"grow\": 3, \"margin\": [0, 0, 0, 10] }", error);
        doc.resolve(kRoot);
        // content 360 wide: 100 + 10 + b + 10 + 10 + c = 360 -> free 230 split 1:3
        check("Layout_FlexRowFixedGrowGapPaddingMargin",
              ok && rectIs(doc, "a", 20, 10, 100, 180) && rectIs(doc, "b", 130, 10, 57.5f, 180)
                 && rectIs(doc, "c", 207.5f, 10, 172.5f, 180),
              error + describe(doc, { "a", "b", "c" }));
    }

    // ---- min/max clamping redistributes -------------------------------------
    {
        InstrumentSceneDocument doc;
        const auto ok = load(doc,
            "{ \"id\": \"r\", \"layout\": \"flex\" },"
            "{ \"id\": \"a\", \"parent\": \"r\", \"grow\": 1, \"maxWidth\": 50 },"
            "{ \"id\": \"b\", \"parent\": \"r\", \"grow\": 1 },"
            "{ \"id\": \"c\", \"parent\": \"r\", \"grow\": 1, \"minWidth\": \"50%\" }", error);
        doc.resolve(kRoot);
        // a capped at 50, c floored at 200; b takes the remaining 150.
        check("Layout_MinMaxClampingRedistributesTheRest",
              ok && rectIs(doc, "a", 0, 0, 50, 200) && rectIs(doc, "b", 50, 0, 150, 200)
                 && rectIs(doc, "c", 200, 0, 200, 200),
              error + describe(doc, { "a", "b", "c" }));
    }

    // ---- shrink is weighted by basis; percent basis ---------------------------
    {
        InstrumentSceneDocument doc;
        const auto ok = load(doc,
            "{ \"id\": \"r\", \"layout\": \"flex\" },"
            "{ \"id\": \"a\", \"parent\": \"r\", \"basis\": \"75%\" },"
            "{ \"id\": \"b\", \"parent\": \"r\", \"basis\": 300 },"
            "{ \"id\": \"c\", \"parent\": \"r\", \"basis\": 100, \"shrink\": 0 }", error);
        doc.resolve(kRoot);
        // 300 + 300 + 100 = 700 in 400: c is rigid, a and b lose 150 each.
        check("Layout_ShrinkWeightedByBasisAndPercentBasis",
              ok && rectIs(doc, "a", 0, 0, 150, 200) && rectIs(doc, "b", 150, 0, 150, 200)
                 && rectIs(doc, "c", 300, 0, 100, 200),
              error + describe(doc, { "a", "b", "c" }));
    }

    // ---- justify -------------------------------------------------------------
    {
        const auto justified = [&](const char* mode)
        {
            InstrumentSceneDocument doc;
            load(doc, juce::String("{ \"id\": \"r\", \"layout\": \"flex\", \"justify\": \"") + mode + "\" },"
                      "{ \"id\": \"a\", \"parent\": \"r\", \"width\": 100 },"
                      "{ \"id\": \"b\", \"parent\": \"r\", \"width\": 100 }", error);
            doc.resolve(kRoot);
            return std::pair<float, float> { doc.rectOf("a").getX(), doc.rectOf("b").getX() };
        };
        const auto start = justified("start");
        const auto center = justified("center");
        const auto end = justified("end");
        const auto between = justified("space-between");
        const auto around = justified("space-around");
        const auto evenly = justified("space-evenly");
        check("Layout_JustifyContentModes",
              near(start.first, 0) && near(start.second, 100) && near(center.first, 100) && near(center.second, 200)
                  && near(end.first, 200) && near(end.second, 300) && near(between.first, 0) && near(between.second, 300)
                  && near(around.first, 50) && near(around.second, 250)
                  && near(evenly.first, 200.0f / 3.0f) && near(evenly.second, 100.0f + 400.0f / 3.0f),
              juce::String(start.first) + "," + juce::String(start.second) + " c" + juce::String(center.first) + "," + juce::String(center.second)
                  + " e" + juce::String(end.first) + "," + juce::String(end.second) + " b" + juce::String(between.first) + "," + juce::String(between.second)
                  + " a" + juce::String(around.first) + "," + juce::String(around.second) + " v" + juce::String(evenly.first) + "," + juce::String(evenly.second));
    }

    // ---- align items / self, column direction ---------------------------------
    {
        InstrumentSceneDocument doc;
        const auto ok = load(doc,
            "{ \"id\": \"r\", \"layout\": \"flex\", \"direction\": \"column\", \"alignItems\": \"center\" },"
            "{ \"id\": \"a\", \"parent\": \"r\", \"width\": 100, \"height\": 50 },"
            "{ \"id\": \"b\", \"parent\": \"r\", \"height\": 50, \"alignSelf\": \"stretch\" },"
            "{ \"id\": \"c\", \"parent\": \"r\", \"width\": 40, \"height\": 50, \"alignSelf\": \"end\" },"
            "{ \"id\": \"d\", \"parent\": \"r\", \"width\": 40, \"grow\": 1, \"alignSelf\": \"start\" }", error);
        doc.resolve(kRoot);
        check("Layout_AlignItemsSelfAndColumn",
              ok && rectIs(doc, "a", 150, 0, 100, 50) && rectIs(doc, "b", 0, 50, 400, 50)
                 && rectIs(doc, "c", 360, 100, 40, 50) && rectIs(doc, "d", 0, 150, 40, 50),
              error + describe(doc, { "a", "b", "c", "d" }));
    }

    // ---- wrap -----------------------------------------------------------------
    {
        InstrumentSceneDocument doc;
        const auto ok = load(doc,
            "{ \"id\": \"r\", \"layout\": \"flex\", \"wrap\": true, \"gapX\": 10, \"gapY\": 20 },"
            "{ \"id\": \"a\", \"parent\": \"r\", \"width\": 150, \"height\": 40 },"
            "{ \"id\": \"b\", \"parent\": \"r\", \"width\": 150, \"height\": 60 },"
            "{ \"id\": \"c\", \"parent\": \"r\", \"width\": 150, \"height\": 40 }", error);
        doc.resolve(kRoot);
        // Lines 60 and 40 tall + 20 gap = 120; the spare 80 is shared (align-content stretch).
        check("Layout_WrapBreaksLinesWithCrossGap",
              ok && rectIs(doc, "a", 0, 0, 150, 40) && rectIs(doc, "b", 160, 0, 150, 60)
                 && rectIs(doc, "c", 0, 120, 150, 40),
              error + describe(doc, { "a", "b", "c" }));
    }

    // ---- order ------------------------------------------------------------------
    {
        InstrumentSceneDocument doc;
        load(doc,
             "{ \"id\": \"r\", \"layout\": \"flex\" },"
             "{ \"id\": \"a\", \"parent\": \"r\", \"width\": 100, \"order\": 2 },"
             "{ \"id\": \"b\", \"parent\": \"r\", \"width\": 100, \"order\": 1 }", error);
        doc.resolve(kRoot);
        check("Layout_OrderControlsSequence",
              rectIs(doc, "b", 0, 0, 100, 200) && rectIs(doc, "a", 100, 0, 100, 200),
              describe(doc, { "a", "b" }));
    }

    // ---- absolute ------------------------------------------------------------------
    {
        InstrumentSceneDocument doc;
        const auto ok = load(doc,
            "{ \"id\": \"r\", \"layout\": \"flex\", \"padding\": 20 },"
            "{ \"id\": \"f\", \"parent\": \"r\", \"grow\": 1 },"
            "{ \"id\": \"a\", \"parent\": \"r\", \"position\": \"absolute\", \"x\": 10, \"y\": \"50%\", \"width\": \"25%\", \"height\": 30 },"
            "{ \"id\": \"b\", \"parent\": \"r\", \"position\": \"absolute\", \"x\": 300, \"y\": 0 }", error);
        doc.resolve(kRoot);
        // Absolute children ignore the flow and the padding; auto size fills to the far edge.
        check("Layout_AbsoluteChildrenPxPercentAndFill",
              ok && rectIs(doc, "f", 20, 20, 360, 160) && rectIs(doc, "a", 10, 100, 100, 30)
                 && rectIs(doc, "b", 300, 0, 100, 200),
              error + describe(doc, { "f", "a", "b" }));
    }

    // ---- grid ----------------------------------------------------------------------
    {
        InstrumentSceneDocument doc;
        const auto ok = load(doc,
            "{ \"id\": \"g\", \"layout\": \"grid\", \"columns\": [100, \"1fr\", \"2fr\"], \"rows\": [50, \"1fr\"], \"gap\": 10 },"
            "{ \"id\": \"a\", \"parent\": \"g\" },"
            "{ \"id\": \"b\", \"parent\": \"g\", \"columnSpan\": 2 },"
            "{ \"id\": \"c\", \"parent\": \"g\", \"column\": 3, \"row\": 2 },"
            "{ \"id\": \"d\", \"parent\": \"g\" }", error);
        doc.resolve(kRoot);
        // cols: 100, (400-20-100)/3=93.33, 186.67. rows: 50, 140.
        check("Layout_GridTracksSpansAndPlacement",
              ok && rectIs(doc, "a", 0, 0, 100, 50) && rectIs(doc, "b", 110, 0, 290, 50)
                 && rectIs(doc, "c", 213.333f, 60, 186.667f, 140) && rectIs(doc, "d", 0, 60, 100, 140),
              error + describe(doc, { "a", "b", "c", "d" }));
    }

    // ---- aspect, hidden, runtime hidden, empty container collapse ---------------------
    {
        InstrumentSceneDocument doc;
        const auto ok = load(doc,
            "{ \"id\": \"r\", \"layout\": \"flex\" },"
            "{ \"id\": \"k\", \"parent\": \"r\", \"width\": 100, \"aspect\": 1 },"
            "{ \"id\": \"h\", \"parent\": \"r\", \"width\": 100, \"visible\": false },"
            "{ \"id\": \"cell\", \"parent\": \"r\", \"layout\": \"flex\", \"width\": 100 },"
            "{ \"id\": \"x\", \"parent\": \"cell\", \"grow\": 1 },"
            "{ \"id\": \"z\", \"parent\": \"r\", \"width\": 100 }", error);
        doc.resolve(kRoot);
        const auto before = ok && rectIs(doc, "k", 0, 50, 100, 100) && doc.rectOf("h").isEmpty()
                         && rectIs(doc, "cell", 100, 0, 100, 200) && rectIs(doc, "z", 200, 0, 100, 200);
        doc.setRuntimeHidden("x", true);
        doc.resolve(kRoot);
        const auto collapsed = doc.rectOf("cell").isEmpty() && rectIs(doc, "z", 100, 0, 100, 200);
        check("Layout_AspectHiddenAndCollapsingCells", before && collapsed,
              error + describe(doc, { "k", "cell", "z" }));
    }

    // ---- caching -----------------------------------------------------------------------
    {
        InstrumentSceneDocument doc;
        load(doc, "{ \"id\": \"r\", \"layout\": \"flex\" }, { \"id\": \"a\", \"parent\": \"r\", \"grow\": 1 }", error);
        const auto start = doc.getResolveCount();
        doc.resolve(kRoot);
        doc.resolve(kRoot);
        const auto afterSame = doc.getResolveCount() - start;
        doc.resolve({ 0, 0, 500, 200 });
        const auto afterResize = doc.getResolveCount() - start;
        auto node = *doc.findNode("a");
        node.grow = 2;
        doc.updateNode(node, error);
        doc.resolve({ 0, 0, 500, 200 });
        check("Layout_ResolveIsCachedUntilSizeOrDocumentChange",
              afterSame == 1 && afterResize == 2 && doc.getResolveCount() - start == 3,
              juce::String(afterSame) + "/" + juce::String(afterResize));
    }

    // ---- JSON round trip and strict validation ---------------------------------------
    {
        InstrumentSceneDocument doc;
        const auto nodes =
            "{ \"id\": \"r\", \"layout\": \"grid\", \"columns\": [\"1fr\", 80, \"auto\"], \"gapX\": 4, \"gapY\": 6, \"padding\": [1, 2, 3, 4] },"
            "{ \"id\": \"a\", \"parent\": \"r\", \"kind\": \"control\", \"binding\": \"voice.osc1.mode\", \"style\": \"s\", "
            "  \"basis\": \"30%\", \"grow\": 0.5, \"shrink\": 0, \"minWidth\": 10, \"maxHeight\": \"80%\", "
            "  \"margin\": [1, 2, 1, 2], \"alignSelf\": \"center\", \"columnSpan\": 2, \"aspect\": 1, \"locked\": true }";
        const auto ok = load(doc, nodes, error);
        InstrumentSceneDocument again;
        const auto reparsed = again.loadJson(doc.toJson(), error);
        doc.resolve(kRoot);
        again.resolve(kRoot);
        check("Layout_JsonRoundTripPreservesEverything",
              ok && reparsed && again.toJson() == doc.toJson() && again.rectOf("a") == doc.rectOf("a")
                  && again.findNode("a")->locked && again.findNode("a")->bindingId == "voice.osc1.mode",
              error);

        const auto rejects = [&](const juce::String& n, int version = 2)
        {
            InstrumentSceneDocument d;
            juce::String e;
            return ! d.loadJson(sceneJson(n, version), e) && e.isNotEmpty();
        };
        check("Layout_StrictValidationRejectsBadScenes",
              rejects("{ \"id\": \"r\" }", 1)
                  && rejects("{ \"id\": \"r\", \"widht\": 3 }")
                  && rejects("{ \"id\": \"r\" }, { \"id\": \"r\", \"parent\": \"r\" }")
                  && rejects("{ \"id\": \"r\" }, { \"id\": \"q\" }")
                  && rejects("{ \"id\": \"r\" }, { \"id\": \"a\", \"parent\": \"b\" }, { \"id\": \"b\", \"parent\": \"a\" }")
                  && rejects("{ \"id\": \"r\" }, { \"id\": \"a\", \"parent\": \"nope\" }")
                  && rejects("{ \"id\": \"r\", \"width\": \"1fr\" }")
                  && rejects("{ \"id\": \"r\", \"layout\": \"grid\" }")
                  && rejects("{ \"id\": \"r\", \"grow\": -1 }")
                  && rejects("{ \"id\": \"r\", \"style\": \"missing\" }")
                  && rejects("{ \"id\": \"r\", \"minWidth\": 50, \"maxWidth\": 10 }"),
              "version, unknown key, duplicate, two roots, cycle, orphan, fr width, grid w/o columns, negative grow, style, min>max");
    }

    // ---- edits, transactions, undo/redo ------------------------------------------------
    {
        InstrumentSceneDocument doc;
        load(doc, "{ \"id\": \"r\", \"layout\": \"flex\" }, { \"id\": \"a\", \"parent\": \"r\", \"width\": 100 }", error);
        doc.beginTransaction();
        for (int w = 110; w <= 150; w += 10)
        {
            auto n = *doc.findNode("a");
            n.width = Length::px(static_cast<float>(w));
            doc.updateNode(n, error);
        }
        doc.commitTransaction();
        const auto coalesced = doc.findNode("a")->width == Length::px(150);
        const auto undone = doc.undo() && doc.findNode("a")->width == Length::px(100) && ! doc.canUndo();
        const auto redone = doc.redo() && doc.findNode("a")->width == Length::px(150);

        auto bad = *doc.findNode("a");
        bad.parentId = "missing";
        const auto rejected = ! doc.updateNode(bad, error) && doc.findNode("a")->parentId == "r";

        auto n = *doc.findNode("a");
        n.visible = false;
        doc.updateNode(n, error);
        doc.resolve(kRoot);
        const auto hidden = doc.rectOf("a").isEmpty();
        doc.undo();
        doc.resolve(kRoot);
        check("Layout_EditsCoalesceUndoRedoAndRollBackInvalid",
              coalesced && undone && redone && rejected && hidden && rectIs(doc, "a", 0, 0, 150, 200),
              describe(doc, { "a" }));
    }

    // ---- the shipped scene -------------------------------------------------------------
    {
        InstrumentSceneDocument shipped;
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("shared/UI/Style/InstrumentScene.json");
        const auto loaded = file.existsAsFile() && shipped.loadJson(file.loadFileAsString(), error);
        shipped.resolve({ 0, 0, 1518, 918 });
        auto required = juce::StringArray { "instrument", "header", "header.logo", "header.menu", "header.gain.knob",
                                            "macros", "views", "view.osc", "view.mod",
                                            "view.fx", "view.mix", "view.settings", "primary.osc", "primary.filter",
                                            "primary.amp", "keys", "keys.performance", "keys.keyboard" };
        juce::StringArray missing;
        for (const auto& id : required)
        {
            if (shipped.findNode(id) == nullptr) { missing.add(id); }
        }
        check("InstrumentScene_ShippedSceneCoversTheEditor",
              loaded && missing.isEmpty(), error + " missing: " + missing.joinIntoString(","));

        const auto osc = shipped.rectOf("primary.osc");
        const auto flt = shipped.rectOf("primary.filter");
        const auto amp = shipped.rectOf("primary.amp");
        check("InstrumentScene_PrimaryModulesFollowHorizontalSignalFlow",
              ! osc.isEmpty() && flt.getX() >= osc.getRight() && amp.getX() >= flt.getRight()
                  && near(osc.getY(), flt.getY()) && near(flt.getHeight(), amp.getHeight()),
              describe(shipped, { "primary.osc", "primary.filter", "primary.amp" }));
    }
}
}
