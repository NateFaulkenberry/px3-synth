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
    "disabled": { "saturation": 0, "dim": 0.75 }   // how a bypassed card looks
  },

  "subOsc": { "width": 300 },         // instances declare only what differs
  "osc1":   { "width": 300 },
  "osc2":   { "width": 300 },
  "osc3":   { "width": 300 },
  "mixerChannel": { "margin": 2, "padding": 6, ... }
}
```

28 properties. That is the whole styling model.

## Semantics worth being precise about

**margin is outside, padding is inside.** Margin reduces the slot before the
card is sized; padding reduces the card before its contents are laid out. Both
accept a single number or per-side values. Over-large insets collapse the box to
zero rather than inverting it.

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

**Cards own their titles.** The title is drawn by the card, not by the parent
panel. `OscPanel` used to paint titles into its children's bounds, which meant a
title was not tied to the lifetime of the component it named — the same
ownership mistake that left stale FX outlines behind when panels were swapped.

## What lives where

| | |
| --- | --- |
| `Source/UI/Card.h` / `Card.cpp` | The model, parsing, geometry resolution, rendering |
| `UIConfig.json` → `panels`, `cards` | Style and layout only. No UI text. |
| Component code | Its own semantic content — the title string, its controls |

`UIConfig.json` owns *where, how big, how spaced, what colour, how opaque, how
rounded, how bordered, how the gloss looks, how the title looks.* It does not own
what the title says.

## Adding a card

1. Give the component `setUIConfig`, `setPanelContentBounds`, and a style key.
2. In `resized()`: resolve the style and the card box, lay controls out inside
   `cardStyle.contentBounds(cardBounds)`.
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
`Source/UI/UIConfig.json` and confirm each of these visibly changes:

**Sub Osc** (`cards.subOsc`)

- [ ] `border.enabled: false` — outline disappears
- [ ] `border.width: 4` — outline thickens
- [ ] `border.color: "#FF0000"` — outline turns red
- [ ] `border.opacity: 1.0` — outline becomes solid
- [ ] `border.radius: 20` — corners round noticeably
- [ ] `margin: 20` — card shrinks away from its column edges
- [ ] `padding: 30` — controls move inward, card unchanged
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

`Source/UI/ComponentCardDrawing.*` was the previous card primitive and has been
removed — nothing referenced it once the last component migrated.

## UIConfig.json contains no UI text

Zero text values remain. Every title (`"OSC 1"`, `"SUB OSC"`, `"AMP ENV"`,
`"ON"`) lives in the component that owns it. The config owns where, how big, how
spaced, what colour, how opaque, how rounded, how bordered, how the gloss looks
and how the title looks — and nothing else.

## Still to come

Card *internals* — knobs, graphs, dropdowns, faders — are deliberately not
modelled yet. The split between "the frame" and "what is inside the frame" is
what will let that be added without disturbing any of this.
