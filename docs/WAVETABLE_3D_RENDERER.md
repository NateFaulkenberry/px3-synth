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
them. The top perimeter carries the reading at full strength; the underside and
the posts are drawn at 45% of it, so the result is a slab with thickness rather
than a wireframe cage.

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
y -0.591 to -0.681 under a waveform bottoming out at -0.411, spanning the full
2.0 x 2.0 of the table, and its depth is checked against the gap below the
waveform so it can never grow from a slab into a plinth.

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
