# PX3 v0.7.4

The seven effects became products in v0.7.0, but they left the Synth as bare
controls on a flat background: no card, no palette, a bypass switch that did not
bypass, and a window sized to nothing in particular. This release makes a
standalone effect the same card the Synth draws — asserted, not assumed — gives
every card its own artwork and colour scheme, and finishes the Reverb, whose
Synth card had been showing two of its eleven controls.

It also closes three gaps that let a regression through: nothing checked that an
effect was still connected to audio, that a knob was connected to a parameter,
or that a card was on screen at all.

---

## The FX cards

**Each card has a background image.** Eight of them, in `shared/UI/Artwork`,
layered under the gloss and clipped to the card's rounded corners. Opacity, fit
and alignment are per card in `UIConfig.json`:

```json
"artwork": { "image": "Lucy-artwork.png", "opacity": 0.7,
             "fit": "stretch", "align": "topLeft" }
```

`fit` takes `cover` (fill and crop), `contain` (the whole picture, letterboxed)
or `stretch` (the whole picture, filling the card); `align` decides which edges
`cover` crops and which side `contain`'s bands fall on. All eight ship as
`stretch` from `topLeft`.

**Each card carries its own colour scheme.** Chip buttons and captions read
their background, opacity, text colour, outline and font size from
`cards.<key>.controls`, so Doom's chips are its red and Mood's are its green
rather than every card wearing the same translucent white.

**Cards cast a shadow.** `cards.defaults.shadow` reaches every card style at
once — colour, opacity, radius, offset — drawn from the card's own rounded
rectangle so it follows the corner radius. The macro depth popover casts the
same one, under `macroDepth.shadow`.

**A bypassed card goes fully grey.** Its artwork desaturates and dims through
the same `disabled.saturation` and `disabled.dim` numbers every other layer
uses, and its captions grey with the knobs they name. Previously a switched-off
card kept a row of coloured chips, which were then the brightest thing on it.

**The rainbow ring is gone.** Every FX amount knob wore an animated rainbow
border; with artwork behind them the two competed, so `psychedelicFx` and its
drawing code are removed. Bypass greyscale is unaffected — despite sharing a
prefix in the source it was always a different thing.

## The standalone effects

- **They are the Synth's cards now.** Card frame, artwork, palette, chip and
  caption styling, all read from the same `UIConfig.json`, which ships inside
  every bundle. `FxProducts_AStandaloneCardMatchesTheSynthsCardExactly`
  compares the two as a text signature — every control's id and bounds, the
  palette, the artwork and its fit — and fails if they diverge.
- **Bypass bypasses.** The switch moved its parameter but nothing was wired to
  the audio path, so a bypassed standalone kept processing. The card also greys
  out when it is off, which it did not do at all before.
- **They open at the size they are in the Synth**, 318 × 500 plus a margin,
  instead of at a default window size.
- **Mood and Delay were empty.** Their panels laid out correctly and had no
  controls in them.
- `scripts/build-product.sh fx-standalone [--run]` builds all seven in one tree
  and opens them.

## Reverb

The Synth's Reverb card showed AMOUNT and MODE. The other nine parameters
existed, were saved in presets and were reachable from a host — they simply had
no control in the Synth. All eleven are on the card now, in the same layout the
standalone uses: SIZE, DECAY, DAMPING, PRE, DEPTH, RATE, WIDTH, REGEN, SMEAR,
plus MODE and AMOUNT.

The card was also relaid out — its second row of knobs ran past the edge of the
inner card — and its knobs now use the PX3 rotary rather than stock JUCE ones.

There is **no backwards-compatibility shim**: the parameters are unchanged, so
existing presets load exactly as they did.

## Macros and presets

- Macro captions moved above their knobs, and each knob has a **DEPTH** button
  beneath it styled like the FX chips.
- **Cmd-click is assignment again.** The depth panel had taken the gesture;
  now that depth has its own button, the modifier went back to the thing with no
  other affordance.
- **Double-clicking a preset loads it.**
- The preset overlay's `CLOSE` button now reads `CANCEL`.

## Fixed

- **Vibe, Delay and Mood vanished from the Synth's FX panel.** Removing the old
  Reverb component took three neighbours' `addAndMakeVisible` calls with it.
  Nothing caught this: `componentForSection` still answered, the signal-flow
  strip still listed all eight stages, and the whole suite stayed green.
  `FxPanel_EveryStageIsOnScreen` now checks each of the eight is in the grid and
  visible.
- **Replacing an artwork PNG did nothing.** A `POST_BUILD` copy only runs when
  the target is built, and editing an image does not make a target out of date.
  The images are `LINK_DEPENDS` now, so touching one relinks; the copy moved to
  `PRE_LINK` so it lands before JUCE's own install step; and the runtime cache
  keys on the file's modification time and size rather than on its path alone.
- **Bundles are less than half the size.** Every bundle carried every card
  background — PX3 Mood shipped Doom's and Lucy's, 27 MB of images to draw one
  of them. Each product now ships only what it draws: about 432 MB down to
  about 186 MB across the bundles.

## Testing

**1399 assertions pass**, up from 1366. Three of the new ones exist because
this release found what was missing:

- `FxBus_<stage>IsAudibleInTheChain` — all eight stages are dispatched from one
  switch in `processBlock` and nothing asserted a stage was still wired to
  audio. Each is now rendered twice, once engaged and once with the chain off,
  and must differ by more than 1% of the dry level. They measure 85% (Chorus) to
  176% (Vibe), so the bar catches a stage that does nothing at all rather than
  pinning how strong any effect is.
- `FxCards_EveryKnobOnTheSynthsCardsIsAttached` and its standalone counterpart —
  a knob whose attach call is missing or misspelt still lays out, still draws,
  still turns, and does nothing. Neither the parity comparison nor the
  look-and-feel check catches that. All 116 knobs move a parameter.
- `FxPanel_EveryStageIsOnScreen` — see above.

Also new: the preset browser has a test harness; a bypassed card's captions are
greyed and un-greyed; and each card's artwork fit is compared against what its
config declares rather than against a written-down expectation, so retuning one
stays a one-line edit.

## Known limitations

- **An update install still has not been watched end to end.** Nothing in this
  release touches the updater, so this carries forward unchanged from v0.7.3:
  the refusal bug is fixed and verified against a local reproduction, but no
  full prepare → quit DAW → relaunch cycle has been run.
- **A second updater helper can still be started across sessions** if the
  settings panel is opened in a fresh session while an update is already staged.
  Within one session the state machine prevents it.
- **The card artwork is large.** The eight PNGs are 27 MB together, and the
  Synth needs all of them because it draws all eight cards. They are 1176 × 1904
  for a card drawn at 318 × 500, which is generous but not the cause — the
  weight is in the images themselves.
- The preset browser keeps its `CANCEL` button alongside the corner glyph.
- Windows is still unpackaged.
