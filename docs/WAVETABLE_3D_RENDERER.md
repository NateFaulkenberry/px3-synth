# Wavetable 3D visualisation — GPU renderer

Design and implementation notes for replacing the JUCE-primitive wavetable
display with an OpenGL renderer.

---

## A. What the current renderer does, and why it is limited

`WavetableGraph::rebuildSurface()` draws each frame as a `juce::Path` polyline,
offset up and to the right by a fixed fraction of the view:

```cpp
const auto originX = area.getX() + depthX * depth;
const auto originY = area.getBottom() - depthY * depth - amplitude;
```

That is a **shear, not a projection**. Its limitations are structural rather
than cosmetic:

| | Current | Why it matters |
|---|---|---|
| Depth | A constant 2D offset per frame | No perspective, so nothing converges; the stack reads as stripes rather than as depth |
| Camera | None | Cannot be orbited, and the viewing angle is baked into two constants |
| Line quality | `strokePath` at 0.9-1.5 px | Thin and soft; the software rasteriser is doing coverage AA on a hairline |
| Hierarchy | Alpha ramp by frame index | The selected frame is not distinguished at all - only depth is |
| Frame identity | None | Stored frames and interpolated positions look identical |
| Cost | 40 polylines of 192 points, CPU, into a cached image | Fine at this size, but the cache does not pay for itself (measured: 0.898 ms with, 0.819 ms without) and the ceiling is low |

The picture is legible. It is not dimensional, and no amount of extra colour or
geometry in the same approach makes it so — which is what §15 of the brief is
getting at.

---

## B. The constraint that shapes the implementation

**`glLineWidth` greater than 1 is not supported in a macOS core profile.** Line
width came out of the fixed-function pipeline, and macOS offers no 3.2+
compatibility context at all. So a GPU renderer here cannot draw thick lines
with line primitives, and a 1-pixel line is exactly the problem being solved.

Every waveform is therefore drawn as a **triangle-strip ribbon**: two vertices
per waveform sample, offset perpendicular to the curve in *screen space* by the
vertex shader. This is not a workaround for a missing feature — it is better
than `glLineWidth` would have been:

- Width is constant in **pixels** regardless of depth, so a distant frame stays
  visible instead of thinning to nothing.
- The cross-ribbon coordinate is interpolated for free, which gives both
  **antialiasing** and the **glow** falloff from one `smoothstep` in the
  fragment shader.
- High-DPI is handled by scaling the half-width by the context's render scale.

---

## C. Architecture

```
px3::WavetableDisplay          (already exists - a drawing-sized copy)
        |
Wavetable3DRenderer            juce::Component + juce::OpenGLRenderer
        |
        |-- static  : vertex buffer, rebuilt only when the TABLE changes
        |-- dynamic : uniforms - MVP, selected position, size, time
        |
   GLSL vertex + fragment shaders
```

**Static vs dynamic is the whole performance story.** The vertex buffer holds
positions, tangents, the frame index and whether the frame is a stored one. None
of that changes when the scan moves or the camera turns — those are uniforms. So
scanning the wavetable, orbiting the camera and resizing the view all cost one
uniform upload and one draw pass; only loading a different table touches the
buffer.

### Vertex format

| attribute | meaning |
|---|---|
| `position` | the waveform point: x = sample position, y = amplitude, z = frame |
| `neighbour` | the next point along the same curve, so the shader can find the tangent |
| `side` | +1 / -1, which edge of the ribbon this vertex is |
| `frame` | 0..1 through the table, for colour and fade |
| `stored` | 1 for a real wavetable frame, 0 for an interpolated one |

The tangent is computed in the shader from `position` and `neighbour` *after*
projection, because the perpendicular has to be perpendicular on screen, not in
model space — otherwise a curve steep in Z gets a wider ribbon than one that is
not.

### Camera

Perspective, orbiting a target at the centre of the stack. Elevation is
constrained to keep the stack the right way up; §12 of the brief asks for that
explicitly, and an envelope of a wavetable seen from underneath is not a view
anyone wants to arrive at by accident.

---

## D. Visual hierarchy

Three treatments, all driven by one value in the fragment shader — the distance
from this frame to the selected position:

- **The selected frame** is at full brightness, gets an emissive lift and a wider
  glow shoulder.
- **Nearby frames** fall off over about a fifth of the table, so the neighbourhood
  of the scan reads as a group.
- **Distant frames** fade toward the background and desaturate, which is what
  produces depth without fog geometry.

Interpolated frames are drawn dimmer than stored ones (§10), so the picture says
what the table actually contains rather than implying 48 equally real waveforms.

---

## E. Lifecycle

JUCE's contract, followed exactly: shaders and buffers are created in
`newOpenGLContextCreated()`, used in `renderOpenGL()`, and destroyed in
`openGLContextClosing()` — all of which run on the GL thread. Nothing touches a
GL object from the message thread.

New display data arrives on the message thread and is parked behind a lock with
a dirty flag; the GL thread picks it up at the start of the next frame and
rebuilds the buffer there. That is the only cross-thread hand-off, and it is a
lock the audio thread never touches.

### Component painting stays ON, and the overlay is a child

On macOS an attached OpenGL context creates a native layer that composites
**above sibling JUCE components regardless of z-order**. A scan marker drawn
beside the GL view is therefore not merely behind it - it is invisible.

So `setComponentPaintingEnabled(true)`, and the overlay carrying the markers,
the drop feedback and the missing-table warning is a **child** of the GL
component, which JUCE draws over the rendered output. The background is cleared
by the GL pass for the same reason: anything painted underneath the layer is
hidden by it.

---

## F. Why this is better than what it replaced

| | Before | After |
|---|---|---|
| Depth | A constant 2D shear per frame | Perspective projection; the stack converges |
| Camera | Two baked constants | Orbit, elevate and zoom, all constrained |
| Line quality | 0.9-1.5 px `strokePath` hairlines | Ribbons with shader antialiasing, constant width in pixels at any depth |
| Selected frame | Not distinguished | Emissive lift and a wider glow shoulder |
| Hierarchy | Alpha by frame index only | One value - distance to the scan - drives grouping, selection and fade together |
| Frame identity | None | Interpolated frames drawn weaker than stored ones |
| Scan response | Redrawn image | A uniform; the buffer is untouched |
| Cost per frame | 40 CPU polylines into a cached image | One uniform upload and 48 draw calls |

The single most consequential difference is the projection. Everything else is
treatment; the shear was the reason the old picture could not read as depth no
matter how it was coloured.

---

## G. The bug that made it look broken

The first build showed nothing and then crashed on a tab switch. Both were one
fault, in `glDrawArrays`, confirmed from the crash report rather than guessed:

```
Thread 14 "OpenGL Renderer"  EXC_BAD_ACCESS (SIGSEGV)
  GLEngine   gleRunVertexSubmitImmediate
  GLEngine   glDrawArrays_Exec
  PX3 Synth  Wavetable3DRenderer::renderOpenGL()
```

Geometry upload was gated on the same `displayDirty` flag that guards building
the CPU vertices, and that flag is consumed whether or not the upload can
actually happen. Two ways that goes wrong, and both occurred:

- **A display arrives before the context exists.** The vertices are built, the
  flag is cleared, and there is no buffer to upload to. The context comes up
  later, generates an empty buffer, and the draw calls run against it.
- **The context is recreated.** Hiding and re-showing the oscillator tab does
  exactly that. `glGenBuffers` hands back a new empty buffer while the vertices
  still look perfectly valid.

Either way the GPU was told to read 6144 vertices out of a buffer holding none.
That is a fetch past the end of an allocation inside the driver — not an
exception anything can catch.

Fixed by tracking upload state separately from the display state, forcing a
re-upload whenever a context is created, and clamping every draw range against
what was actually uploaded.

**A note on a wrong guess.** `near` was suspected of being a reserved GLSL word
and the cause of a silent compile failure. Tested both ways on this driver: the
shader compiles either way. The rename is kept as portability insurance and is
commented as such, not as a fix.

### Verifying it, rather than assuming

`PX3Tests glcheck` opens a real off-screen window, attaches the context,
compiles the shaders, renders, and moves the scan for twenty more frames — the
draw-range fault would take the process down there. It is a separate mode rather
than part of the suite because it needs a window and a message loop, which a
console test does not otherwise have.

```
  rendering:    YES
  shader error: (none)
  survived 20 more frames with the scan moving
```

## H. Framing

The camera framed nothing correctly to begin with, and the numbers say so
plainly. Projecting the geometry and reading its normalised device bounds - past
±1 is off screen - every factory table was clipped:

| table | minY before | needed distance |
|---|---|---|
| Saw Fold | -1.285 | 4.88 |
| Vowel Morph | **-1.847** | **6.03** |
| Comb Digital | -1.511 | 5.24 |

The camera sat at 3.4. Vowel Morph hung 85% of its height off the bottom of the
view.

Rather than pick a bigger constant, the camera **frames itself**: on every
geometry change it centres on the stack and bisects for the closest distance
that still fits, with a 6% margin. Tables differ enough for that to matter -
across the library the required distance ranges from 4.96 to 6.30.

Two things were measured and rejected on the way:

- **Widening the waveform** to use a graph twice as wide as it is tall. At 1.3x
  the framing is unchanged and past that it gets worse, because the fit simply
  pulls back to accommodate the wider silhouette. Width is free to look at; it
  is not free to frame.
- **Fitting the worst reachable orientation.** It never clips, and it costs a
  fifth of the picture at the angle the stack is actually read from: 0.727 of
  the frame against 0.939. The default view is fitted snugly instead, and
  orbiting pulls back only as far as that particular orientation needs.

Azimuth is clamped for the same reason elevation is. Allowing a full turn makes
the framing obey a silhouette the user has to swing behind the stack to see -
where the curves read back to front anyway.

Result: every table fills **92.5-93.9%** of the frame, centred, at every
orientation the camera can reach, at both a wide and a square aspect. Held by
`Wavetable3D_EveryTableIsFullyInFrame` and `Wavetable3D_TheStackFillsTheView`.

## I. The floor

A shallow box beneath the stack, in the same coordinate system, so it turns with
the camera and carries the perspective rather than being drawn on the glass.

**A box rather than a plane**, because a single rectangle seen at a shallow angle
is ambiguous - it could be lying flat or standing up. A little thickness resolves
that in one glance, and the four short corner posts are what say which way is
down. Twelve edges in total: a top face, a bottom face, and the posts between
them, every one at the same weight so it reads as a wireframe rather than a
solid with a lit top. The underside and posts were drawn at 45% at first, on the
theory that playing them down would make the box read as a slab - it does, and a
slab is a surface, which is the opposite of what is wanted. Depth still separates
near from far, but that is perspective rather than shading.

**Only the perimeter of it, and deliberately so.** The rectangle alone already says how
wide the waveform is, how deep the table is and which way the camera is looking.
Subdividing it into cells would turn the picture into a graph, which is the one
thing this is not meant to look like.

Each edge is its own ribbon rather than one closed loop: the ribbon takes its
perpendicular from the direction to the next point, so a strip running through a
corner would pinch there. Straight runs leave a notch a couple of pixels across
that nothing can see.

It rides in the same vertex buffer and the same shader as the stack, flagged by
one attribute. That flag gets it a fixed low alpha and none of the selection
treatment, while it keeps the stack's depth fade - so the far edge sits back and
the near edge comes forward, which is what makes it read as a plane rather than
as an outline. It greys out with the oscillator like everything else.

Drawn first, before the frames, so the stack blends over it: it is the
environment, not part of the instrument.

Its height is `lowest waveform point - kFloorDrop`, computed from the geometry
rather than assumed, so it stays beneath the waveform whatever the table
contains. Held by a test across all eight factory tables: the box runs from
y -0.466 to -0.666 under a waveform bottoming out at -0.411, spanning the full
2.0 x 2.0 of the table.

Its depth is checked against the WAVEFORM's height, not against the gap beneath
it. Measuring against the gap - which the first version of that test did - made
the two settings fight: moving the floor closer to the waveform shrank the gap,
and so shrank the depth the box was allowed to have.

Including it in the auto-fit costs a little of the stack's size - the framing has
to hold the floor too - which is the honest trade for having it in frame at all.

## J. Status

Implemented and building for AU, VST3 and Standalone, with the camera under test
(elevation clamped both ways, azimuth free, zoom stopped, reset, and data
accepted with no context attached).

The CPU renderer is kept as a fallback for a machine whose context never comes
up, selected on `isRendering()`. Two things are deliberately not done: no
geometry shader (not available on the macOS core profile this targets), and no
multisampling request - the ribbon's own edge falloff is doing that work, and
asking for MSAA as well would cost fill rate for an effect already present.

---

# The environment

Research and design for the scene treatment around the stack. Written against
the brief's section numbers.

## A. What creates depth without a scene (§1)

Product renders, studio photography and the better plugin UIs all reach for the
same small set of tricks, and almost none of them involve modelling anything:

| Technique | What it actually does | Cost here |
|---|---|---|
| Gradient background | Stands in for a large diffuse source behind the subject | 1 exp() per fragment |
| Vignette | Lowers edge luminance so fixation moves inward | 1 smoothstep |
| Ambient | Fills the black, so nothing reads as a hole | 1 add |
| Key light | Gives the scene a direction | 1 dot per **vertex** |
| Atmospheric perspective | Distance = loss of contrast, not loss of light | 1 mix |
| Depth haze | The same thing, applied to the air | folded into the above |
| Bloom | Needs a second framebuffer pass | **rejected**, see below |
| Soft shadows | Needs a shadow map and surfaces to receive them | **rejected**, no surfaces |
| Ambient occlusion | Needs depth/normal buffers | **rejected**, no normals |

The three rejections are not cost-cutting; they are category errors here. The
stack is **line geometry**: ribbons with no surface, no normals and no volume.
Shadows, occlusion and physically based materials all describe how light meets
a surface, and there isn't one. Anything claiming to do them would be decorating
rather than lighting.

**Bloom is rejected on architecture.** It needs the scene rendered to a texture,
a bright-pass, a separable blur and a composite - four passes and two
framebuffers, in a UI panel that repaints continuously. §12 asks whether it
would improve the selected frame; the selected frame already gets an additive
emissive lift in the fragment shader (`colour += hot * selected * glow`), which
is what bloom would be approximating, at one multiply-add.

## B. What was implemented

Ordered as §18 asks, though with one pass rather than several:

1. **Environmental background** - its own program, four static clip-space
   vertices, no camera and no depth. Three terms: an ambient lift everywhere, a
   soft pool of light behind where the stack actually projected, and a vertical
   gradient lighter at the bottom. The pool follows `projectedBounds`, not the
   centre of the panel, because the stack does not project symmetrically about
   the centre and a pool that ignores that lights the empty half of the frame.
2. **Vignette** - applied to the composed background.
3. **Key light** - one dot product per vertex against a fixed direction (up,
   left, toward the viewer). Ribbons have no normal, but they have a position,
   and a light that brightens one side of the scene reads as direction just as
   well. Range 0.86..1.14: wider and the stack starts to look like chrome.
4. **Atmospheric haze** - the back of the stack fades **toward the environment
   colour**, not toward black. Excluded from the selected frame, so the effect
   deepens the existing hierarchy instead of flattening it.
5. **Floor illumination** - near edge up, far edge down, from the depth
   attenuation already there. Scaled so the environment can only make the floor
   more subtle, never brighter, per §16.

## C. The tuning, measured (§21)

`PX3Tests envcheck` renders the real scene twice on a real context, with the
environment off and on, and reports mean luminance by region. That is the §23
final quality test, run as a command rather than by eye.

The first version applied the vignette to the whole background:

```
  darkest pixel           0.0480    0.0401   -0.0078
```

The darkest pixel in the frame got **darker** when the environment was switched
on - the vignette was taking light out of the corners rather than the
environment putting light in, which is the opposite of §5. An ambient term was
added and the vignette reduced from 0.18 to 0.14:

```
                             OFF        ON     delta
  centre luminance        0.1572    0.2076   +0.0503
  corner luminance        0.0480    0.0767   +0.0287
  lower luminance         0.0490    0.0981   +0.0491
  upper luminance         0.0819    0.1108   +0.0290
  darkest pixel           0.0480    0.0625   +0.0145

  centre over corners:   +0.1093 off, +0.1309 on
```

Which is the shape the brief describes: nothing anywhere is darker than it was,
the centre lifts most, the bottom lifts more than the top, and the middle now
stands further above the edges than it did on flat black.

Three of those are checked as pass/fail by `envcheck`, and the check was
confirmed to fail against the version without the ambient term.

## D. Consequences worth knowing

**`glcheck`'s lit-pixel count is taken with the environment off, deliberately.**
That figure means "how much of the frame differs from the cleared background",
which is a measurement of whether the STACK drew. The environment lifts almost
every pixel off the clear colour, which took the number from 31% to 82% without
one extra ribbon being drawn. With the environment off it reads 95688 pixels,
identical to before the environment existed - which is also the evidence that
none of this changed how the stack itself draws.

**No new per-frame allocation and no new geometry.** The environment's four
vertices are uploaded once when the context is created. A resize, a camera move
and a new table all cost nothing extra.

## E. The floor as a lit surface

The floor was an outline. Three changes make it a surface in a lit scene:

**More translucent** - 0.16 to 0.115. The old alpha was chosen while both the
stack and the floor were hairlines; once the ribbons doubled in thickness the
same number made the floor read as a structure rather than as a reference.

**Lit by the wavetable.** The floor's `vFrame` is its position along the box,
normalised exactly the way the stack's frames are - so `vFrame == uSelected` is
the point directly beneath the selected waveform. A pool centred there travels
along the side rails as the scan moves and lifts each end rail as the scan
reaches it. It adds colour as well as brightness, because a white rail under a
blue source is not white, and only where the light actually falls: away from the
pool the box stays neutral, which is what keeps it scenery.

That change reinstates a grayscale branch that had been removed as inert. It was
inert while the floor was white everywhere - white has no colour to drain - but
a lit rail does, and a bypassed oscillator whose floor still glowed blue would
be the one coloured thing left on a grey card.

**A rim.** Broader than the core and much weaker, so an edge reads as having a
little light spilling off it rather than as a thicker line.

Measured, sweeping the scan and watching the lower region of the frame - where
the floor lives and the stack barely reaches:

```
    scan     lower luminance
             pool off   pool on
    0.05       0.0983    0.0983
    0.35       0.0983    0.0983
    0.65       0.0983    0.0988
    0.95       0.0985    0.1080
    moves by   0.0001    0.0097
```

97x, which is the isolation: with the light removed the region is flat as the
scan sweeps, so the movement is the floor and not the stack passing through.
`envcheck` gates it at 0.003.

## F. Sharpening the lines

The ribbons were antialiased with `smoothstep(0.0, 0.55, across)`, where
`across` runs 0 at the ribbon's edge to 1 at its centre. That is a gradient
across the ribbon's own width, not antialiasing: the softness was a FRACTION of
the half-width, so it scaled with thickness. At 2.2 px it read as slightly soft;
doubling the lines to 4.4 px doubled the blur, and any further thickening would
have blurred further.

Replaced with coverage in pixels - `clamp(across * halfWidthPixels, 0, 1)`,
smoothstepped - which is solid across the ribbon and ramps to nothing over the
last pixel at each edge, at any thickness.

The other half of the blur was `alpha *= core * 0.85 + glow * 0.35`, which put a
full-ribbon-width halo under *every* line. The halo is kept, because a glow
should be soft, but it is now spent only where it means something: the emissive
lift on the selected frame, and the light spilling off the floor's rails.

## G. Framing follows the camera that is on screen

`autoFrame` used to compute the fit for the DEFAULT orientation and hand that
distance to `camera`, whose azimuth and elevation are wherever the user last
dragged them. With the view at its defaults the two are the same calculation,
which is why this survived: it is invisible until you orbit.

Orbited, and measured at the real panel's 290x149, loading a table put every
factory table off screen:

```
    table              extent, orbited      after
    Saw Fold                     1.039      0.942
    Pulse Width                  1.205      0.936
    Drawbars                     1.102      0.934
    Vowel Morph                  1.089      0.939
    Bell Partials                1.005      0.942
    Comb Digital                 1.136      0.937
    Wave Folder                  1.047      0.942
    Warm Asymmetry               1.012      0.943
```

Anything past 1.0 is outside the viewport. It stayed that way until the user
dragged back toward the default orientation - which is exactly how it was
reported: "it doesn't resize until I click and drag it".

The framing is now run twice: once from the default orientation, which is what
`resetCamera` returns to, and once from the orientation actually on screen,
which is what the camera gets. `envcheck` orbits the camera before sweeping the
factory library, because at the defaults the check cannot fail.

---

# Line sharpness: the pipeline, and what was actually wrong

Written as a diagnosis. Every claim here is a measurement from `PX3Tests
sharpcheck`, which renders on a real context and reads the framebuffer back.

## H. The pipeline, stage by stage

```
wavetable samples          px3::WavetableDisplay, 48 frames x 128 points
    |                      nearest stored frame per drawn frame; linear
    |                      interpolation BETWEEN samples only (sampleAt)
CPU geometry               rebuildVertices: 2 vertices per point, side -1/+1
    |                      12288 vertices + 48 floor vertices, rebuilt only
    |                      when the TABLE changes
GPU buffer                 one static VBO, glBufferData on change
    |
vertex shader              ribbon expansion AFTER projection, in NDC
    |
primitive                  GL_TRIANGLE_STRIP, one strip per frame
    |                      *** not GL_LINES, not glLineWidth ***
fragment shader            pixel-coverage antialiasing, depth fade, key light
    |
framebuffer                the DEFAULT framebuffer, at physical resolution
    |                      *** no offscreen FBO, no texture, no filtering ***
presentation               JUCE OpenGLContext, component painting for overlays
```

Every stage that could introduce filtering was checked and ruled out:

| Suspect | Finding |
|---|---|
| Low-resolution framebuffer | **Ruled out.** 290x149 points, 580x298 px, scale 2.00 - exactly physical |
| Offscreen FBO / texture scaling | **Ruled out.** There is no FBO and no texture in this renderer |
| Native GL lines | **Ruled out.** Ribbon triangle strips already; `glLineWidth` is never called |
| Post-processing / bloom | **Ruled out.** Single pass, no blur, no accumulation |
| CPU smoothing of samples | **Ruled out.** Linear interpolation between samples, no filtering |
| Antialiasing width | **Ruled out.** Measured at 0.50 px per side (see below) |
| Blend mode | Straight alpha, `GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA`, depth test off |

## I. The edge, measured

The measurement isolates ONE curve by subtracting two renders of the same
scene that differ only in which frame is highlighted. Whatever differs is that
curve and nothing else - not the floor, not the environment, not the other
frames. A real table cannot be used for this: moving the scan changes a whole
neighbourhood of frames, and the difference spanned 119 px of picture rather
than one curve. Two flat frames give one straight line each.

```
    edge profile of the isolated curve, physical pixel rows
     -1  0.0000
     +0  0.8401  ####################################################
     +1  0.8341  ####################################################
     ...
     +7  0.8341  ####################################################
     +8  0.1266  ########
     +9  0.0000

    core (>=90% of peak): 8 px    full extent (>=10%): 9 px
    the edge takes 0.50 px per side
```

**The edge was never the problem.** Half a pixel is antialiasing by any
definition - the core is solid and the transition is sub-pixel.

## J. The actual root cause: stacking order and depth cueing, both inverted

`vFrame` is 0 at the table's first frame and 1 at its last. The geometry puts
frame 0 at `z = -kStackHalfDepth`, and the camera sits on the POSITIVE z side.
Measured eye depths: 2.755 at z=+1.8 against 5.430 at z=-1.8. **vFrame 1 is the
end nearest the viewer.**

Against that, the renderer was doing three things backwards:

```
    depthFade = mix(1.0, 0.22, vFrame)     faded the NEAR end to nothing
    haze      = 0.38 * vFrame              hazed the NEAR frames
    for (f = frameCount - 1; f >= 0; --f)  drew NEAREST first
```

The first is measurable on its own. In a two-frame scene, with neither frame
selected, the near curve read **0.0480 against a background of 0.0480** - it was
not visible at all - while the far curve read 0.1499.

The third is the one that destroys sharpness. With the depth test off and
straight alpha, drawing nearest-first means every one of the other 47
translucent ribbons is painted **on top of** the curve you are looking at. No
antialiasing can make a line crisp when 47 semi-transparent ribbons are
composited over it afterwards.

Fixed by counting up instead of down, and by reversing the two depth cues so
they run toward the camera. Measured on the real 48-frame stack, the mean of the
top 1% of adjacent-pixel luminance jumps - the sharpest edges in the picture:

```
    before   0.1793
    after    0.2062      +15%
```

## K. What was NOT changed, and why

**The line width** - not changed as part of the diagnosis, then halved
afterwards as its own decision, which is the right order. The doubling to 4.4
logical px was asked for while the ribbon's antialiasing still scaled with its
width, so thickness was the only way to make a line read as solid. With the
edge fixed at half a pixel and the stacking order right, weight and crispness
are independent, and the thinner line is simply the sharper one:

```
    isolated curve          4.4 px      2.2 px
    core                      8 px        4 px
    sharpest edges, real stack   0.2062      0.2971     +44%
```

For reference, the same measurement was 0.1793 before the stacking order was
fixed - so order and width together account for a 66% improvement.

One caveat on the profile: at 2.2 px the isolated horizontal line reports 0.00
px of edge, which is this line's alignment with the pixel grid rather than the
absence of antialiasing. The coverage ramp is still one physical pixel wide; a
horizontal line simply lands on row boundaries where a diagonal one would not.

**Multisampling.** The edge is already sub-pixel; MSAA would only help diagonal
jaggies, at the cost of a multisample buffer for the whole panel.

**Glow.** Already a separate layer: the core alpha is coverage alone, and the
halo is added on top only for the selected frame, bounded at 2.5 px.
