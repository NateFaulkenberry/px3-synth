# PX3 Card System

A small CSS-style layout and styling model for PX3's UI. Three levels:

```
Panel      full-width section        height, vertical scrolling
  Card     the standard frame        box, border, background, gloss, title
    ...    component internals       NOT modelled here (a later phase)
```

The rule the whole thing is built around: **every property in `UIConfig.json`
is read by the layout or rendering code.** There are no placeholders and no
properties that exist because they sound CSS-like. `PX3Tests cardstyle` proves
this — one test changes each property in isolation and requires the resolved
style or geometry to differ.

## Schema

```jsonc
"panels": {
  "osc": { "height": 0, "overflowY": "hidden" }   // height 0 = use the editor's allocation
},

"cards": {
  "defaults": {                       // every card inherits this
    "width":  "auto",                 // "auto" | 300 | "300px" | "33%"
    "height": "auto",
    "margin":  6,                     // OUTSIDE the card; number or {top,right,bottom,left}
    "padding": 10,                    // INSIDE the border, before content
    "border":     { "enabled": true, "width": 1.2, "color": "#DCE8FC", "opacity": 0.35, "radius": 8 },
    "background": { "color": "#68C2FF", "opacity": 0.10 },
    "gloss": {
      "margin": 6,                    // gap between the border and the gloss
      "split": 0.5,                   // where topFill ends and bottomFill begins
      "topFill":    { "color": "#68C2FF", "opacity": 0.10 },
      "bottomFill": { "color": "#000000", "opacity": 0.06 }
    },
    "title": { "fontSize": 11, "color": "#DCE8FC", "align": "center", "y": 0, "height": 14 },
    "shadow": { "color": "#000000", "opacity": 0.45, "radius": 11, "offsetX": 0, "offsetY": 3 },
    "disabled": { "saturation": 0, "dim": 0.75 }   // how a bypassed card looks
  },

  "lucy": {                           // an FX card, which may also carry artwork
    "artwork": {
      "image":   "Lucy-artwork.png",  // found in shared/UI/Artwork
      "opacity": 0.7,
      "fit":     "stretch",           // cover | contain | stretch
      "align":   "topLeft"            // centre | topLeft | top | right | ...
    }
  },

  "subOsc": { "width": 300 },         // instances declare only what differs
  "osc1":   { "width": 300 },
  "osc2":   { "width": 300 },
  "osc3":   { "width": 300 },
  "mixerChannel": { "margin": 2, "padding": 6, ... }
}
```

37 properties. That is the whole styling model.

## Semantics worth being precise about

**margin is outside, padding is inside.** Margin reduces the slot before the
card is sized; padding reduces the card before its contents are laid out. Both
accept a single number or per-side values. Over-large insets collapse the box to
zero rather than inverting it.

**Insets have three spellings.** A single number sets all four sides; an object
`{ "top": 4, "right": 8, "bottom": 4, "left": 8 }` sets them individually; and
flat siblings — `paddingTop`, `paddingRight`, `paddingBottom`, `paddingLeft`,
and the same four for `margin` — override one edge at a time. The sibling form
is applied *after* the generic one, so `"padding": 4, "paddingTop": 10` means
10 on top and 4 elsewhere. All three work on `cardInner` and on any row inside
it, and a row may override a single edge of what `rows.default` set.

**Percentages reference the parent Panel's content box.** Not the slot, not a
sibling, not the plugin window, not the card's own bounds. The reference is an
explicit argument — `resolveBounds(slot, panelContent)` — so it cannot be
confused at a call site. `Panel height 400, card height "50%"` is always 200px.

**A card never exceeds its slot** — the equivalent of `max-width: 100%`. A
percentage larger than the slot is capped rather than overflowing into the next
column. This is deliberate: overflow between columns reads as a bug, and the cap
is what keeps fixed pixel widths sensible as the window narrows.

**`auto` fills the space the layout gave it.** That is the default, so a card
that declares nothing behaves exactly as it did before this system existed.

**A bypassed card goes greyscale.** `disabled.saturation` (0 = fully grey) and
`disabled.dim` (multiplies every layer's opacity) are applied to *every* layer —
border, background, both gloss fills and the title. Desaturating only the border
and title left a bypassed card still reading as "the blue one", which defeats the
point. Bypass is runtime state, so it is applied to the parsed style via
`CardStyle::disabledVariant()` rather than being a second style block that could
drift out of sync with the active one. The variant is returned rather than drawn
so the transform is unit-testable.

**Artwork sits between the background and the gloss.** `artwork.image` names a
file in `shared/UI/Artwork`, copied into every product's `Contents/Resources` at
build time and found at runtime by `UIConfigManager::findArtworkFile`. It is
always clipped to the card's rounded rectangle, so none of it escapes the card's
shape whichever fit is chosen:

| `fit` | |
| --- | --- |
| `cover` | Scales by whichever axis needs *more* and crops the rest. The default. |
| `contain` | Scales by whichever axis needs *less*. The whole picture is inside the card, letterboxed. |
| `stretch` | Scales the two axes independently. Fills the card with the whole picture, distorted. |

`align` decides which edges `cover` crops away and which side `contain`'s bands
fall on; `stretch` fills the card exactly and leaves it nothing to decide. Both
parse by name and keep their fallback on a name they do not recognise, so a
typo draws the default rather than failing — which is why
`FxCards_ArtworkFitAndAlignmentComeFromConfig` compares what each card resolved
against what its config says.

Artwork greys with everything else on bypass, through the same
`disabled.saturation` and `disabled.dim` numbers, rather than having a second
opinion about what bypassed looks like.

**A card's shadow is cast, not drawn on.** `shadow` is rendered from the card's
own rounded rectangle *before* the background, so it follows the corner radius
instead of being a soft rectangle behind a rounded card. It is off by default at
`radius: 0`, and it spills *outside* the card — radius and offset want to stay
inside the gap the grid already leaves. Note that the ground behind a card is
near-black, so a black shadow has little room to work in; a shadow that needs to
read strongly is a sign the panel behind it should be lighter.

**A knob can carry two functions.** `KnobSpec` takes an optional `altId`,
`altLabel` and `altTooltip`. A knob that names one gets a SECOND slider at
exactly the same bounds and a second caption chip beneath the first, drawn
smaller and dimmed. `setAltMode(bool)` swaps which slider is visible and which
caption is emphasised.

It is visibility, not attachment: both sliders keep their parameters at all
times, so automating an alternate never depends on which one the panel is
showing, and the mode itself is not a parameter. `knob(id)` and `knobLabel(id)`
answer for an alternate's id as well as a primary's, so attaching one is the
same call either way, and `allKnobs()` / `allKnobLabels()` include them - which
is what keeps the styling, bypass-greyscale and attachment passes reaching a
control nobody is currently looking at.

DOOM and LUCY use this for the six pairs their pedals print; see
`shared/UI/Fx/DoomCardLayout.h` and `LucyCardLayout.h`, which are also the one
place each card's rows are declared, shared by the Synth's card and the
standalone.

**Cards own their titles.** The title is drawn by the card, not by the parent
panel. `OscPanel` used to paint titles into its children's bounds, which meant a
title was not tied to the lifetime of the component it named — the same
ownership mistake that left stale FX outlines behind when panels were swapped.

## What lives where

| | |
| --- | --- |
| `shared/UI/Components/Card.h` / `Card.cpp` | The model, parsing, geometry resolution, rendering |
| `shared/UI/Components/CardInner.h` / `CardInner.cpp` | The layout *inside* a card — rows, flex, control placement |
| `UIConfig.json` → `panels`, `cards` | Style and layout only. No UI text. |
| Component code | Its own semantic content — the title string, its controls |

`UIConfig.json` owns *where, how big, how spaced, what colour, how opaque, how
rounded, how bordered, how the gloss looks, how the title looks.* It does not own
what the title says.

## Adding a card

1. Give the component `setUIConfig`, `setPanelContentBounds`, and a style key.
2. In `resized()`: lay out the card, then `cardInner` inside
   `card.contentBelowTitle()`, then place the controls into its rows.
3. In `paint()`: call `px3::ui::drawCard(g, cardBounds, style, title)`.
4. Add a block under `cards` declaring only what differs from `defaults`.

Runtime state (enabled/disabled, selection) modulates the parsed style in the
component — it is not configuration and does not belong in the JSON.

## Live reload

Editing `UIConfig.json` in a debug build reloads it through
`UIConfigManager::reloadIfChanged()`, which propagates to `OscPanel::setUIConfig`
and from there to every card. Card styles are re-parsed in `resized()`, so a
change to any property takes effect on the next reload.

## Manual verification checklist

Automated tests prove each property changes the resolved style; they cannot
prove it looks right. With the standalone running and a debug build, edit
`shared/UI/Style/UIConfig.json` and confirm each of these visibly changes:

**Sub Osc** (`cards.subOsc`)

- [ ] `border.enabled: false` — outline disappears
- [ ] `border.width: 4` — outline thickens
- [ ] `border.color: "#FF0000"` — outline turns red
- [ ] `border.opacity: 1.0` — outline becomes solid
- [ ] `border.radius: 20` — corners round noticeably
- [ ] `margin: 20` — card shrinks away from its column edges
- [ ] `padding: 30` — controls move inward, card unchanged
- [ ] `paddingTop: 0` alongside it — only the top edge opens back up
- [ ] `width: 200` — card narrows, stays centred in its column
- [ ] `width: "50%"` — card takes half the panel width (capped by the column)
- [ ] `height: 200` — card shortens
- [ ] `height: "50%"` — card takes half the panel height
- [ ] `background.color` / `background.opacity` — fill changes
- [ ] `gloss.margin: 20` — visible gap opens between border and gloss
- [ ] `gloss.split: 0.2` — the top fill occupies the top fifth
- [ ] `gloss.topFill.color` / `.opacity` — upper fill changes
- [ ] `gloss.bottomFill.color` / `.opacity` — lower fill changes
- [ ] `title.fontSize: 20` — title grows
- [ ] `title.color: "#FF0000"` — title turns red
- [ ] `title.align: "left"` — title moves to the left edge
- [ ] `title.y: -6` / `6` — title moves up / down
- [ ] `disabled.saturation: 1` — a bypassed card keeps its colour
- [ ] `disabled.dim: 0.2` — a bypassed card fades much further

**Osc 1, 2, 3** (`cards.osc1`, `cards.osc2`, `cards.osc3`)

- [ ] The geometry checks above behave identically on each
- [ ] Styling `osc2` alone leaves `osc1` and `osc3` untouched

**Panel** (`panels.osc`)

- [ ] `height: 200` — the OSC panel shortens
- [ ] `overflowY: "auto"` with a height smaller than the content — a vertical
      scrollbar appears and scrolls
- [ ] `overflowY: "hidden"` — no scrollbar

**Regression, by hand**

- [ ] Bypassing any component greys its whole card, and re-enabling restores it
- [ ] Oscillator mode, macro knobs, pitch and enable all still work
- [ ] Sub osc octave, waveform, pitch and enable all still work
- [ ] FX drag-and-drop reordering still works
- [ ] Plugin resizes without cards overlapping

## Migrated components

Every major component now renders through the same Card:

| Card key | Component | Accent |
| --- | --- | --- |
| `subOsc` | Sub Osc | teal `#3FBFC9` |
| `osc1` `osc2` `osc3` | Oscillators | blue `#4A99FF` |
| `filter1` `filter2` | Filters | red `#FF5858` |
| `lfo1` `lfo2` `lfo3` | LFOs | purple `#BA70FF` |
| `env1` `env2` `env3` | Mod envelopes | green `#49DE79` |
| `ampEnv` | Amp envelope | green `#49DE79` |
| `vibe` | Vibe | blue `#68C2FF` |
| `delay` | Delay | orange `#FFC66E` |
| `mood` | Mood | amber `#EEB678` |
| `reverb` | Reverb | cyan `#80D0FF` |
| `mixerChannel` | Mixer channels | neutral `#8FA8C8` |

All of them lay their interiors out through `cardInner` as well — see below —
except `mixerChannel`, whose faders are not a three-row structure.

Each block carries a complete colour identity — border, background, both gloss
fills and title — so a component's palette is one local edit. The FX accents are
carried over unchanged from the colours the editor used to paint, so the FX rack
keeps the identity it already had.

**No panel paints into its children any more.** `OscPanel`, `FltPanel`,
`ModPanel`, `AmpPanel` and `FxPanel` all drew card titles using their children's
bounds; every one of those now belongs to the component it names. `FxPanel` in
particular no longer tracks section geometry for drawing at all, which is why
drag-and-drop reordering needs no bookkeeping: a card follows its own
component's bounds.

`Source/UI/ComponentCardDrawing.*` (in the pre-restructure tree) was the
previous card primitive and has been removed — nothing referenced it once the last component migrated.

## UIConfig.json contains no UI text

Zero text values remain. Every title (`"OSC 1"`, `"SUB OSC"`, `"AMP ENV"`,
`"ON"`) lives in the component that owns it. The config owns where, how big, how
spaced, what colour, how opaque, how rounded, how bordered, how the gloss looks
and how the title looks — and nothing else.

## cardInner — the layout inside a card

The card owns the frame; `cardInner` owns what is inside it. The hierarchy is
explicit, and every percentage is resolved against exactly one level of it:

```
Panel → Card → cardInner → Row → controls
```

A standard card has three rows. `AMP ENV` is the deliberate exception: one row
that fills `cardInner`, because it contains a single full-size ADSR graph and
three rows would be meaningless there.

### Schema

```jsonc
"cardInner": {
  "margin": 0, "padding": 4,          // independent of the Card's own
  "paddingTop": 10,                   // per-side override of the line above.
                                      // Also paddingRight/Bottom/Left, and
                                      // marginTop/Right/Bottom/Left. Valid on
                                      // rows too.
  "display": "flex",                  // or "none"
  "direction": "column",              // row | rowReverse | column | columnReverse
  "wrap": "nowrap",
  "justifyContent": "center",         // start | end | center | spaceBetween | spaceAround
  "alignItems": "center",             // start | end | center | stretch
  "alignContent": "center",
  "gap": 2,
  "rows": {
    "default": { /* every row property; the base every row starts from */ },
    "row1": { "height": "26%", "gap": 8 },
    "row2": { "height": "30%" },
    "row3": { "height": "44%", "padding": 2, "paddingBottom": 0 }
  }
}
```

### cardInner is defined per component TYPE

There is **no `cards.defaults.cardInner`**. A layout block belongs to a component
type, and every instance of that type shares it:

| Block | Used by |
| --- | --- |
| `cards.subOsc.cardInner` | Sub Osc |
| `cards.osc.cardInner` | Osc 1, 2 and 3 |
| `cards.lfo.cardInner` | LFO 1, 2 and 3 |
| `cards.env.cardInner` | ENV 1, 2 and 3 |
| `cards.ampEnv.cardInner` | AMP ENV |
| `cards.filter.cardInner` | Filter 1 and 2 |
| `cards.vibe` `cards.delay` `cards.reverb` `cards.mood` | one each |

The type is the style key with any trailing instance number stripped —
`px3::ui::cardTypeKey()`, so `"osc2"` → `"osc"`. *Colours* stay per instance
(`cards.osc2.border`); only the layout is shared. The three oscillators cannot
drift apart from one another, and no card inherits another card's layout.

Each block is self-contained. Lookup within one is two layers, most specific
first:

```
cards.<type>.cardInner.rows.rowN
cards.<type>.cardInner.rows.default
```

Anything a block omits falls back to the C++ defaults in `CardInnerStyle`, not
to another card — so a missing block still lays out, as three equal rows.

### Accepted values

| Property | Values |
| --- | --- |
| `display` | `flex`, `none` |
| `direction` | `row`, `row-reverse`, `column`, `column-reverse` |
| `wrap` | `nowrap`, `wrap` |
| `justifyContent` | `flex-start`, `flex-end`, `center`, `space-between`, `space-around` |
| `alignItems` / `alignContent` | `flex-start`, `flex-end`, `center`, `stretch` |

Spellings are normalised before matching — lowercased, with hyphens and
underscores removed — so `flex-start`, `flexStart` and `flex_start` are the same
value, as are `center` and `centre`. Prefer the CSS spelling in the table.

An unrecognised value falls back to the default rather than failing, which is
silent by design in a release build; a debug build logs
`UIConfig: unrecognised justifyContent value "..." - ignored` so a typo is
visible while you are editing.

### Rules worth being precise about

**Row height is a percentage of the cardInner content height** — after
`cardInner`'s own margin and padding, and of nothing else. Not the card, not the
panel, not the previous row.

**Row width is not configurable.** A row spans `cardInner` by definition.

**Rows that total more than 100% are scaled down, not overflowed.** The heights
are resolved here rather than left to FlexBox's shrinking, which does not apply
to explicitly sized items. If the total — including the space the gaps consume —
exceeds what is available, every row is scaled by the same factor. Rows totalling
less than 100% simply leave space, positioned by `justifyContent`.

**Gloss corners are configurable per fill.** `gloss.topRadius` rounds the top
fill's *top* two corners and `gloss.bottomRadius` the bottom fill's *bottom*
two; the edges where the two meet at the split stay square, because they abut.
Both default to `"auto"`, which follows the card's border radius less the gloss
margin so the gloss stays concentric with the border. A pixel or percentage
value overrides that — a percentage is of that fill's shorter side, as in CSS —
and each is capped at half the shorter side so a corner can never fold back on
itself.

**`display: "none"` removes a row from the layout.** It gets no height and no
gap, and the rows around it close up. They do not grow to absorb the space,
exactly as in CSS.

**A wrapping row's items must be sized for the number of lines they will take.**
FlexBox derives line height from the items, so giving each the full row height
makes two lines twice as tall as the row. `px3::ui::wrappedLineCount()` works out
the line count from the natural widths and the gap; divide the row height by it.
Delay's row 3 (five controls) and Mood's row 3 (nine knobs) both rely on this.

### The power slot — outside the flex flow

Every card's power toggle is pinned to the top-left of `cardInner`, deliberately
**not** part of any row:

```jsonc
"cardInner": {
  "power": { "x": 0, "y": 0, "size": 25 },
  "rows": { ... }
}
```

`x` and `y` offset it from the cardInner content corner and may be negative, so
it can be pulled back over the padding. `size` is the square side; `0` removes it
(AMP ENV, which is declared `alwaysEnabled` and has no off state).

It sits outside the flow because it is not one control among a row's controls —
it is the card's own switch. Keeping it there means it stays put when a row gains
or loses items, and a row never has to reserve space for it. Vibe, Reverb and
Delay are two-row cards for exactly that reason: the row that used to hold only
the toggle no longer exists.

**Clicking the card background toggles the same parameter.** One shared rule,
`px3::ui::isCardBackgroundToggleClick`, excludes drags — so dragging a knob past
its cell edge, or dragging an FX card to reorder, cannot flip a bypass. ENV
additionally excludes its graph, which is draggable.

### The control block — inside one cell

The row decides where each **cell** goes; `control` decides what happens **inside**
one. It is declared per row:

```jsonc
"row1": {
  "height": "26%",
  "gap": 6,                       // between CELLS - ON | OCT | WAVE
  "control": {
    "direction": "column",        // column: label above. row: label beside.
    "justifyContent": "center",
    "alignItems": "center",
    "gap": 3,                     // between LABEL, CONTROL and READOUT
    "labelHeight": "auto",
    "readoutHeight": "auto",
    "size": "auto"
  }
}
```

The two `gap`s are different things, and confusing them is the easy mistake: the
row's gap separates one labelled control from the next, the control's gap
separates a label from the control it names.

**Why this exists.** The stack used to be hand-rolled — label pinned to the top of
the cell, readout pinned to the bottom, control centred in whatever was left. So
the distance between a label and its control was not a value anywhere; it was
leftover space, and it grew with the row height. On a 195px row with a 14px label
and a 22px checkbox, that left ~80px of air above and below the control.

**`justifyContent: "space-between"` reproduces that old spread exactly** — label
at the top, readout at the bottom, control between — so defaulting to `center`
loses nothing. Both are pinned by tests.

**`"auto"` means "whatever the component asked for".** The component still says
its knob should never exceed 56px, or that this label wants 14px; config values of
`auto` defer to that. A pixel or percentage overrides it, with percentages
measured against the cell's shorter side. That default is what let this be
introduced without every control in the plugin changing size.

`ControlShape` stays in code, not config: a knob is round and a dropdown is not,
and that follows from what the control *is*, not from how it is styled.

### Placing controls

`cardInner` decides where the rows are. The component fills them, because the
component owns its controls — that split is the whole point.

```cpp
inner.setStylePath("cards.subOsc.cardInner");
inner.setConfig(uiConfig);
inner.setRowCount(3);
inner.layout(card.contentBelowTitle());

auto flex = inner.rowFlex(0);            // pre-configured from the row's style
const auto gap = inner.rowGap(0);
flex.items.add(juce::FlexItem(46.0f, cellHeight).withMargin(gap));
flex.performLayout(inner.rowContent(0).toFloat());
```

`px3::ui::layoutLabelledControl()` then places one cell's label, control and
readout, given the row's control style. It takes a `ControlShape`, and getting
that wrong is not subtle:

| | |
| --- | --- |
| `ControlShape::square` | Knobs and tick boxes — the largest centred square that fits |
| `ControlShape::stretch` | Dropdowns and buttons — full width, capped height, centred |

Pass only the parts a control actually has. A knob with no label today must not
acquire one here: this phase changes layout, not what a control displays.

### Live reload

`cardInner` parses its rows in `resized()`, so `setUIConfig` must call `resized()`
and not merely `repaint()`. LFO, ENV, Vibe, Reverb, Delay and Mood each needed
that added — without it a reload draws new colours into the old geometry. This is
the same class of bug the `CardStyleCache` fixed for card styles.

### What this replaced

Every migrated component previously derived its interior layout twice: once in
`resized()` to place controls, and again in `paint()` — by replaying the same
sequence of `removeFromTop` calls — to find the graph. Keeping the two copies in
step was manual. Three components went further and derived their own card
rectangle independently of the one `CardHost` had already resolved, and `Filter`'s
interior was laid out by `FltPanel` rather than by the component at all.

`FilterComponent` is still the one component that does not own its controls —
they are `FltPanel`'s children, not its own. It exposes `rowBounds()`, `rowFlex()`
and `rowGap()`, and the panel translates them into its own coordinates. That is
the smallest change that let the geometry become declarative without moving
control ownership, which is a separate concern.

## Still to come

Control *styling* — `KnobStyle`, `GraphStyle`, `DropdownStyle`, `ButtonStyle` —
is deliberately not modelled yet. `cardInner` places controls; it does not draw
them. That split is what will let styling be added without disturbing any of the
layout above.
