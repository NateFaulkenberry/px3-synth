#include "Wavetable3DRenderer.h"

namespace
{
// One point per two pixels of a typical view is plenty; the ribbon's own
// antialiasing hides the rest. More points cost buffer memory and buy nothing
// once each segment is under a pixel long.
constexpr int kPointsPerFrame = 128;

// How many frames are drawn, regardless of how many the table holds. A 256-frame
// table drawn in full is a solid wall; the picture is about the SHAPE of the
// table, and 48 curves reads as a stack where 256 reads as a surface.
constexpr int kDrawnFrames = 48;

// How wide the waveform runs in model space.
//
// Widening it to use a graph that is twice as wide as it is tall was tried and
// measured: at 1.3 the framing is unchanged and past that it gets worse, because
// the auto-fit simply pulls back to accommodate the wider silhouette when the
// camera swings round. The width is free to look at; it is not free to frame.
constexpr float kWaveformHalfWidth = 1.0f;

// How far the stack runs front to back in model space, half-extent, so the
// frames sit between -kStackHalfDepth and +kStackHalfDepth.
//
// It was 1.0, written as a bare -1..1 in three places. Spreading the frames
// apart is the only way to put air between them: the COUNT is what makes the
// picture read as a stack rather than a surface, so thinning it out is not an
// option, and once the ribbons are drawn thicker the old spacing left the far
// half of the table looking solid.
//
// The camera does not need a matching constant. autoFrame measures the
// geometry it is actually given, so a deeper stack simply fits at a greater
// distance.
constexpr float kStackHalfDepth = 1.8f;

// The environment.
//
// A procedural background rather than a texture or a skybox: it is four
// vertices and one screen-space evaluation per fragment, it costs nothing to
// resize, and it can follow the subject around the frame - which a texture
// cannot. The research note in docs/WAVETABLE_3D_RENDERER.md has the reasoning
// behind each term; what they are is:
//
//   pool      a soft light behind the subject, the way a product render puts a
//             large diffuse source behind what it is lighting. Aspect-corrected
//             so it stays round in a wide panel.
//   vertical  a gentle floor-to-ceiling gradient, lighter low, which is what
//             makes the floor read as standing ON something.
//   vignette  edges a few percent down, so the eye settles in the middle.
//
// All three are additions to the configured background colour, never
// replacements for it, so turning the environment off returns exactly the flat
// colour that was there before.
const char* kEnvironmentVertexShader = R"(
attribute vec2 aCorner;
varying vec2 vNdc;

void main()
{
    vNdc = aCorner;
    gl_Position = vec4(aCorner, 0.0, 1.0);
}
)";

const char* kEnvironmentFragmentShader = R"(
varying vec2 vNdc;

uniform vec2 uSubject;
uniform float uAspect;
uniform vec3 uBase;
uniform vec3 uGlow;
uniform float uAmount;

void main()
{
    vec2 toSubject = vec2((vNdc.x - uSubject.x) * uAspect, vNdc.y - uSubject.y);
    float pool = exp(-dot(toSubject, toSubject) * 1.15);

    // Lighter at the bottom of the frame. The stack sits above its floor, so a
    // gradient the other way would light the scene from under the floor.
    float vertical = 0.5 - 0.5 * vNdc.y;

    // Ambient: a flat lift applied everywhere, which is the term that stops the
    // scene having a true void in it. Without it the vignette is free to take
    // the extreme corners BELOW the background colour, and measured, it did -
    // the darkest pixel in the frame fell from 0.0480 to 0.0401 when the
    // environment was switched on, which is the opposite of what an
    // environment is for.
    vec3 colour = uBase + uGlow * (0.30 + 0.62 * pool + 0.38 * vertical);

    // Restrained on purpose. At 0.30 the panel reads as a photograph with a
    // filter on it; at 0.14, against the ambient lift above, the corners
    // settle back without falling below where they started.
    vec2 fromCentre = vec2(vNdc.x * uAspect, vNdc.y);
    colour *= 1.0 - 0.14 * smoothstep(0.55, 1.45, length(fromCentre));

    gl_FragColor = vec4(mix(uBase, colour, uAmount), 1.0);
}
)";

const char* kVertexShader = R"(
attribute vec3 aPosition;
attribute vec3 aNeighbour;
attribute float aSide;
attribute float aFrame;
attribute float aStored;
attribute float aFloor;

uniform mat4 uViewProjection;
uniform float uHalfWidth;
uniform float uAspect;

varying float vFrame;
varying float vSide;
varying float vStored;
varying float vFloor;
varying float vDepth;
varying float vKey;

void main()
{
    // The key light, per vertex. Ribbons have no surface and so no normal to
    // shade against, but they do have a POSITION, and a light that brightens
    // one side of the scene and lets the other fall away gives the same
    // directional information a shaded surface would - at the cost of one dot
    // product on 12288 vertices rather than on every fragment.
    //
    // Up, left and toward the viewer: the direction a key light sits in for
    // almost every product render, because it is where the sun is not.
    vec3 lightDirection = normalize(vec3(-0.55, 0.72, 0.42));
    vKey = 0.5 + 0.5 * dot(lightDirection, normalize(aPosition + vec3(0.0, 0.35, 0.0)));

    vec4 here = uViewProjection * vec4(aPosition, 1.0);
    vec4 next = uViewProjection * vec4(aNeighbour, 1.0);

    // The perpendicular is taken AFTER projection, in normalised device space,
    // so the ribbon is the same width on screen wherever it is. Offsetting in
    // model space instead would make a curve steep in Z draw wider than one
    // that is not, and would make distant frames thin away to nothing.
    vec2 hereNdc = here.xy / max(here.w, 0.0001);
    vec2 nextNdc = next.xy / max(next.w, 0.0001);

    vec2 direction = nextNdc - hereNdc;
    direction.x *= uAspect;
    float length2 = dot(direction, direction);
    vec2 tangent = length2 > 1.0e-12 ? normalize(direction) : vec2(1.0, 0.0);
    vec2 normal = vec2(-tangent.y, tangent.x);
    normal.x /= uAspect;

    gl_Position = here;
    gl_Position.xy += normal * aSide * uHalfWidth * here.w;

    vFrame = aFrame;
    vSide = aSide;
    vStored = aStored;
    vFloor = aFloor;
    vDepth = clamp(here.z / max(here.w, 0.0001) * 0.5 + 0.5, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(
varying float vFrame;
varying float vSide;
varying float vStored;
varying float vFloor;
varying float vDepth;
varying float vKey;

uniform float uSelected;
uniform vec4 uAccent;
uniform float uGrayscale;

// How much of the environment to apply: the key light, the haze and the
// floor's near-to-far illumination. 0 restores exactly the flat-lit scene.
uniform float uEnvironment;

// What distant geometry fades TOWARDS. Atmospheric perspective is not things
// going dark, it is things taking on the colour of the air between - fading to
// black is a fade-out, fading to the environment is distance.
uniform vec3 uHaze;

// The ribbon's half-width in PIXELS. Antialiasing has to be done in pixels or
// it is not antialiasing - see the coverage term below.
uniform float uHalfWidthPixels;

void main()
{
    // Distance from this frame to where the scan actually is. Everything below
    // is a function of it, which is what keeps the hierarchy consistent instead
    // of being three unrelated effects.
    float toSelected = abs(vFrame - uSelected);

    // The neighbourhood of the scan reads as a group; past about a fifth of the
    // table a frame is background.
    //
    // Called closeness rather than `near` as a precaution only. `near` and
    // `far` are reserved in some GLSL profiles, and this was suspected of being
    // why the renderer showed nothing - it was not. Tested both ways on this
    // driver: the shader compiles either way, and the real cause was the buffer
    // never being uploaded (see uploadGeometry). Kept because the name is still
    // a portability risk on drivers this has not run on, not because it fixed
    // anything.
    float closeness = 1.0 - smoothstep(0.0, 0.22, toSelected);
    float selected = 1.0 - smoothstep(0.0, 0.035, toSelected);

    // Across the ribbon: 1 at the centre, 0 at the edges.
    float across = 1.0 - abs(vSide);

    // Coverage, in PIXELS: solid across the ribbon and ramping to nothing over
    // the last pixel at each edge. This is what a crisp antialiased line is.
    //
    // It replaces a smoothstep over the ribbon's own width, which is not
    // antialiasing but a gradient - and a gradient that got softer as the line
    // got thicker, because the ramp was a FRACTION of the half-width rather
    // than a fixed distance. At 2.2 px wide that read as slightly soft; when
    // the lines were doubled to 4.4 px it read as blurry, and would have gone
    // on blurring at any further thickness.
    float across01 = clamp(across * uHalfWidthPixels, 0.0, 1.0);
    float core = across01 * across01 * (3.0 - 2.0 * across01);

    // The halo, still measured across the whole ribbon - because a glow SHOULD
    // be soft. Kept off the base alpha of every ribbon, where it was the other
    // half of the blur, and spent only where it means something: the emissive
    // lift on the selected frame and the light spilling off the floor's rails.
    float glow = smoothstep(0.0, 1.0, across);

    // Frames the table does not actually contain are drawn weaker, so the
    // picture says what is in the wavetable rather than implying that every
    // curve is a stored waveform.
    float storedWeight = mix(0.45, 1.0, vStored);

    // Depth: further back is dimmer and less saturated, which is what makes the
    // stack recede without any fog geometry.
    float depthFade = mix(1.0, 0.22, vFrame);

    vec3 base = uAccent.rgb;
    vec3 hot = mix(base, vec3(1.0), 0.65);
    vec3 colour = mix(base, hot, selected);

    float alpha = storedWeight * depthFade * (0.16 + 0.84 * closeness);
    alpha *= core;
    alpha += selected * glow * 0.5;

    // Emissive lift on the selected frame, added rather than mixed so it reads
    // as light rather than as a lighter colour.
    colour += hot * selected * glow * 0.55;

    // The floor is a spatial reference, not part of the instrument. It takes
    // the same depth fade as the stack - so the far edge sits back and the near
    // edge comes forward, which is what makes it read as a plane rather than as
    // a rectangle drawn on the glass - and none of the selection treatment.
    if (vFloor > 0.5)
    {
        // The same pixel coverage the stack uses. A separate smoothstep over
        // 70% of the ribbon was the floor's share of the same blur.
        float edge = core;

        // vStored carries the floor's emphasis. Equal across the box today, so
        // it reads as a wireframe; the attribute stays because varying it is how
        // any future emphasis would be expressed.
        // More translucent than it was. At 0.16 the box held its own against
        // the stack, which was fine while both were drawn as hairlines; once
        // the ribbons doubled in thickness the same alpha made the floor read
        // as a structure rather than as a reference.
        float floorAlpha = 0.115 * depthFade * edge * vStored;

        // The near edge is lit and the far edge falls away, which is what makes
        // the box read as a SURFACE occupying space rather than as an outline
        // drawn around one. Depth attenuation does the whole job; there is no
        // light to sample and no shadow to cast.
        //
        // Deliberately not compensated for the environment: the brief is
        // explicit that a lit scene should let the floor be MORE subtle, not
        // less, so this only ever scales the floor down.
        float floorLit = mix(1.0, mix(1.15, 0.55, vFrame), uEnvironment);
        floorAlpha *= floorLit;

        // The wavetable itself as a light source.
        //
        // The floor's vFrame is its position ALONG the box, normalised exactly
        // the way the stack's frames are - so vFrame == uSelected is the point
        // directly beneath the selected waveform. A pool centred there travels
        // along the side rails as the scan moves and lifts each end rail as the
        // scan reaches it, which is what a light moving over a surface does.
        //
        // This is the one place the floor is allowed to get brighter, and it is
        // not the floor asserting itself: it is the floor reporting where the
        // instrument is.
        float pool = exp(-toSelected * toSelected * 30.0) * uEnvironment;
        floorAlpha += pool * (0.26 * core + 0.16 * glow);

        // The rim. Broader than the core and much weaker, so an edge reads as
        // having a little light spilling off it rather than as a thicker line.
        floorAlpha += uEnvironment * 0.05 * glow * depthFade * vStored;

        // Light adds colour as well as brightness: a white rail under a blue
        // source is not white. Away from the pool the box stays neutral, which
        // is what keeps it scenery - it only takes the instrument's hue where
        // the instrument is actually lighting it.
        vec3 floorColour = mix(vec3(1.0), hot, clamp(pool * 0.7, 0.0, 1.0));

        // Which means it needs the grayscale treatment after all. It did not
        // when it was white everywhere - white has no colour to drain - but a
        // lit rail does, and a bypassed oscillator whose floor still glowed
        // blue would be the one coloured thing left on a grey card.
        float floorLuma = dot(floorColour, vec3(0.299, 0.587, 0.114));
        floorColour = mix(floorColour, vec3(floorLuma), uGrayscale);

        gl_FragColor = vec4(floorColour, clamp(floorAlpha, 0.0, 1.0) * uAccent.a);
        return;
    }

    // The key light. A narrow range on purpose - the scene should read as
    // illuminated from somewhere, which needs far less contrast than it sounds
    // like. Wider than this and the stack starts to look like a chrome object.
    colour *= mix(1.0, mix(0.86, 1.14, vKey), uEnvironment);

    // Atmospheric perspective, applied to the BACK of the stack only and never
    // to the selected frame. Fading everything uniformly would flatten the
    // hierarchy the frame weighting just established; this deepens it.
    float haze = uEnvironment * 0.38 * vFrame * (1.0 - selected);
    colour = mix(colour, uHaze, haze);

    // Drained of colour when the oscillator is bypassed, the same way the knobs
    // grey out. Luminance-weighted rather than an average, so a bypassed stack
    // keeps the same apparent brightness instead of going muddy.
    float luma = dot(colour, vec3(0.299, 0.587, 0.114));
    colour = mix(colour, vec3(luma), uGrayscale);

    gl_FragColor = vec4(colour, clamp(alpha, 0.0, 1.0) * uAccent.a);
}
)";
} // namespace

Wavetable3DRenderer::Wavetable3DRenderer()
{
    setOpaque(false);

    context.setRenderer(this);
    context.setContinuousRepainting(false);

    // Component painting stays ON, and this is not a detail. On macOS an
    // attached context creates a native layer that composites ABOVE sibling
    // JUCE components regardless of z-order, so anything drawn beside this
    // would simply disappear behind the stack. With component painting enabled
    // JUCE draws this component and its children over the GL output instead,
    // which is why the overlay is a child of this and not a sibling.
    context.setComponentPaintingEnabled(true);

    // NOT attached here. There is one of these per oscillator card, so
    // attaching on construction means three GL contexts and three render
    // threads for a picture that at most one card is showing. The context
    // follows visibility instead - see visibilityChanged.
}

Wavetable3DRenderer::~Wavetable3DRenderer()
{
    // Blocks until the render thread has stopped, which is what makes it safe
    // for the members below to be destroyed afterwards.
    context.detach();
}

void Wavetable3DRenderer::visibilityChanged()
{
    if (isVisible())
    {
        if (! context.isAttached())
        {
            context.attachTo(*this);
        }
    }
    else if (context.isAttached())
    {
        context.detach();
    }
}

void Wavetable3DRenderer::setDisplay(px3::WavetableDisplay display)
{
    {
        const std::scoped_lock lock(displayMutex);
        pendingDisplay = std::move(display);
        displayDirty = true;
    }
    context.triggerRepaint();
}

void Wavetable3DRenderer::setPosition(float position)
{
    const auto clamped = juce::jlimit(0.0f, 1.0f, position);
    if (std::abs(clamped - selectedPosition.load()) < 0.0005f)
    {
        return;
    }
    selectedPosition.store(clamped);
    context.triggerRepaint();
}

juce::String Wavetable3DRenderer::getShaderError() const
{
    const std::scoped_lock lock(displayMutex);
    return shaderError;
}

void Wavetable3DRenderer::setBypassed(bool shouldBeBypassed)
{
    if (bypassed.exchange(shouldBeBypassed) == shouldBeBypassed) { return; }
    context.triggerRepaint();
}

void Wavetable3DRenderer::setBackgroundColour(juce::Colour colour)
{
    if (backgroundColour == colour) { return; }
    backgroundColour = colour;
    context.triggerRepaint();
}

void Wavetable3DRenderer::setAccentColour(juce::Colour accent)
{
    if (accentColour == accent) { return; }
    accentColour = accent;
    context.triggerRepaint();
}

void Wavetable3DRenderer::resized()
{
    context.triggerRepaint();
}

void Wavetable3DRenderer::resetCamera()
{
    // Back to the FRAMED view, not to the struct's defaults - those do not know
    // how big the loaded table is.
    camera = framedCamera;
    context.triggerRepaint();
}

void Wavetable3DRenderer::mouseDown(const juce::MouseEvent& event)
{
    dragStart = event.position;
    dragStartCamera = camera;
}

void Wavetable3DRenderer::mouseDrag(const juce::MouseEvent& event)
{
    const auto delta = event.position - dragStart;

    // Clamped like the elevation. The stack is meant to be read from roughly
    // the front: swinging behind it shows the same curves back to front and is
    // not a view worth the framing it would cost, since the camera has to pull
    // back far enough to fit whatever orientation is reachable.
    camera.azimuth = juce::jlimit(kMinAzimuth, kMaxAzimuth,
                                  dragStartCamera.azimuth + delta.x * 0.008f);

    // Clamped rather than wrapped. Orbiting under the stack and back out the
    // other side is not a view anyone means to arrive at, and getting there by
    // accident is the usual way a 3D control becomes something people avoid.
    // Dragging UP raises the camera, so the stack tilts to show more of its
    // top. The other sign is what a screen-space delta gives you literally, and
    // it feels inverted against every other 3D view a user has ever used.
    camera.elevation = juce::jlimit(kMinElevation, kMaxElevation,
                                    dragStartCamera.elevation - delta.y * 0.006f);

    // Pulled back only as far as THIS orientation needs. Framing the default
    // view against the worst reachable one would shrink the picture by a fifth
    // for an angle the user is not looking from.
    if (lastAspect > 0.0f && ! vertices.empty())
    {
        camera.distance = juce::jmax(dragStartCamera.distance,
                                     distanceToFit(camera, lastAspect, 0.06f));
    }

    context.triggerRepaint();
}

void Wavetable3DRenderer::mouseDoubleClick(const juce::MouseEvent&)
{
    resetCamera();
}

void Wavetable3DRenderer::mouseWheelMove(const juce::MouseEvent&,
                                         const juce::MouseWheelDetails& wheel)
{
    camera.distance = juce::jlimit(kMinDistance, kMaxDistance,
                                   camera.distance - wheel.deltaY * 1.4f);
    context.triggerRepaint();
}

void Wavetable3DRenderer::newOpenGLContextCreated()
{
    program = std::make_unique<juce::OpenGLShaderProgram>(context);

    if (! program->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(kVertexShader))
        || ! program->addFragmentShader(juce::OpenGLHelpers::translateFragmentShaderToV3(kFragmentShader))
        || ! program->link())
    {
        // Recorded rather than swallowed. A shader that fails to compile leaves
        // the CPU fallback drawing, which looks exactly like "the GPU renderer
        // is not working" and says nothing about why - and the driver's message
        // is the only thing that does.
        {
            const std::scoped_lock lock(displayMutex);
            shaderError = program->getLastError();
        }
        DBG("Wavetable3DRenderer shader failed: " << program->getLastError());

        program.reset();
        return;
    }

    {
        const std::scoped_lock lock(displayMutex);
        shaderError.clear();
    }

    const auto uniform = [this](const char* name)
    {
        return std::make_unique<juce::OpenGLShaderProgram::Uniform>(*program, name);
    };
    const auto attribute = [this](const char* name)
    {
        return std::make_unique<juce::OpenGLShaderProgram::Attribute>(*program, name);
    };

    uniformViewProjection = uniform("uViewProjection");
    uniformSelected = uniform("uSelected");
    uniformHalfWidth = uniform("uHalfWidth");
    uniformAspect = uniform("uAspect");
    uniformAccent = uniform("uAccent");
    uniformGrayscale = uniform("uGrayscale");
    uniformEnvironment = uniform("uEnvironment");
    uniformHaze = uniform("uHaze");
    uniformHalfWidthPixels = uniform("uHalfWidthPixels");

    attribPosition = attribute("aPosition");
    attribNeighbour = attribute("aNeighbour");
    attribSide = attribute("aSide");
    attribFrame = attribute("aFrame");
    attribStored = attribute("aStored");
    attribFloor = attribute("aFloor");

    context.extensions.glGenBuffers(1, &vertexBuffer);

    // A brand new buffer holds nothing, whatever the CPU vertices still say.
    geometryUploaded = false;

    // The environment. A failure here is not fatal the way the stack's is: the
    // scene simply keeps the flat background it had, which is why this does not
    // record into shaderError and does not return early.
    environmentProgram = std::make_unique<juce::OpenGLShaderProgram>(context);
    if (environmentProgram->addVertexShader(
            juce::OpenGLHelpers::translateVertexShaderToV3(kEnvironmentVertexShader))
        && environmentProgram->addFragmentShader(
            juce::OpenGLHelpers::translateFragmentShaderToV3(kEnvironmentFragmentShader))
        && environmentProgram->link())
    {
        uniformEnvSubject = std::make_unique<juce::OpenGLShaderProgram::Uniform>(
            *environmentProgram, "uSubject");
        uniformEnvAspect = std::make_unique<juce::OpenGLShaderProgram::Uniform>(
            *environmentProgram, "uAspect");
        uniformEnvBase = std::make_unique<juce::OpenGLShaderProgram::Uniform>(
            *environmentProgram, "uBase");
        uniformEnvGlow = std::make_unique<juce::OpenGLShaderProgram::Uniform>(
            *environmentProgram, "uGlow");
        uniformEnvAmount = std::make_unique<juce::OpenGLShaderProgram::Uniform>(
            *environmentProgram, "uAmount");
        attribEnvCorner = std::make_unique<juce::OpenGLShaderProgram::Attribute>(
            *environmentProgram, "aCorner");

        // Four corners of clip space, uploaded once. The environment never
        // changes shape, so there is nothing here to rebuild on a resize, on a
        // camera move or on a new table.
        constexpr float corners[] = { -1.0f, -1.0f,  1.0f, -1.0f,
                                      -1.0f,  1.0f,  1.0f,  1.0f };
        context.extensions.glGenBuffers(1, &environmentBuffer);
        context.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, environmentBuffer);
        context.extensions.glBufferData(juce::gl::GL_ARRAY_BUFFER,
                                        static_cast<GLsizeiptr>(sizeof(corners)),
                                        corners, juce::gl::GL_STATIC_DRAW);
        context.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, 0);
    }
    else
    {
        DBG("Wavetable3DRenderer environment shader failed: "
            << environmentProgram->getLastError());
        environmentProgram.reset();
    }
}

void Wavetable3DRenderer::setEnvironmentEnabled(bool shouldBeEnabled)
{
    if (environmentEnabled.load() == shouldBeEnabled) { return; }

    environmentEnabled.store(shouldBeEnabled);
    if (context.isAttached()) { context.triggerRepaint(); }
}

Wavetable3DRenderer::LuminanceProbe Wavetable3DRenderer::getLuminanceProbe() const
{
    const std::scoped_lock lock(probeMutex);
    return luminanceProbe;
}

void Wavetable3DRenderer::drawEnvironment(float aspect)
{
    if (environmentProgram == nullptr || environmentBuffer == 0)
    {
        return;
    }

    environmentProgram->use();

    if (uniformEnvAspect != nullptr) { uniformEnvAspect->set(aspect); }

    // Where the light sits, in normalised device coordinates. Behind the
    // SUBJECT, not behind the middle of the panel: the stack does not project
    // symmetrically about the centre, and a pool that ignores where it actually
    // landed lights the empty half of the frame.
    //
    // Lifted a little above it, which is where a key light goes.
    auto subjectX = 0.0f;
    auto subjectY = 0.12f;
    if (! vertices.empty() && aspect > 0.0f)
    {
        const auto bounds = projectedBounds(camera, aspect);
        if (bounds.getWidth() > 0.0f && bounds.getWidth() < 1.0e5f)
        {
            subjectX = bounds.getCentreX();
            subjectY = bounds.getCentreY() + 0.12f;
        }
    }
    if (uniformEnvSubject != nullptr) { uniformEnvSubject->set(subjectX, subjectY); }

    if (uniformEnvBase != nullptr)
    {
        uniformEnvBase->set(backgroundColour.getFloatRed(),
                            backgroundColour.getFloatGreen(),
                            backgroundColour.getFloatBlue());
    }
    if (uniformEnvGlow != nullptr)
    {
        // Built from the accent, but mostly drained of it. A background that
        // carries the accent at strength competes with the waveform for the
        // same hue; at a fifth saturation it reads as "the air in here is
        // faintly the colour of the instrument", which is what a studio
        // backdrop does.
        const auto glow = accentColour.withMultipliedSaturation(0.22f);
        constexpr auto lift = 0.085f;
        uniformEnvGlow->set(glow.getFloatRed() * lift,
                            glow.getFloatGreen() * lift,
                            glow.getFloatBlue() * lift);
    }
    if (uniformEnvAmount != nullptr)
    {
        uniformEnvAmount->set(environmentEnabled.load() ? 1.0f : 0.0f);
    }

    // No depth, no blend: this is the deepest thing in the scene and it covers
    // every pixel, so there is nothing to test against and nothing to blend
    // with.
    juce::gl::glDisable(juce::gl::GL_DEPTH_TEST);
    juce::gl::glDisable(juce::gl::GL_BLEND);

    context.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, environmentBuffer);
    if (attribEnvCorner != nullptr)
    {
        context.extensions.glVertexAttribPointer(
            static_cast<GLuint>(attribEnvCorner->attributeID), 2, juce::gl::GL_FLOAT,
            juce::gl::GL_FALSE, 2 * static_cast<GLsizei>(sizeof(float)), nullptr);
        context.extensions.glEnableVertexAttribArray(
            static_cast<GLuint>(attribEnvCorner->attributeID));
    }

    juce::gl::glDrawArrays(juce::gl::GL_TRIANGLE_STRIP, 0, 4);

    if (attribEnvCorner != nullptr)
    {
        context.extensions.glDisableVertexAttribArray(
            static_cast<GLuint>(attribEnvCorner->attributeID));
    }
    context.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, 0);
}

void Wavetable3DRenderer::openGLContextClosing()
{
    if (vertexBuffer != 0)
    {
        context.extensions.glDeleteBuffers(1, &vertexBuffer);
        vertexBuffer = 0;
    }
    geometryUploaded = false;

    if (environmentBuffer != 0)
    {
        context.extensions.glDeleteBuffers(1, &environmentBuffer);
        environmentBuffer = 0;
    }

    uniformEnvSubject.reset();
    uniformEnvAspect.reset();
    uniformEnvBase.reset();
    uniformEnvGlow.reset();
    uniformEnvAmount.reset();
    attribEnvCorner.reset();
    environmentProgram.reset();

    uniformViewProjection.reset();
    uniformSelected.reset();
    uniformHalfWidth.reset();
    uniformAspect.reset();
    uniformAccent.reset();
    uniformGrayscale.reset();
    uniformEnvironment.reset();
    uniformHaze.reset();
    uniformHalfWidthPixels.reset();
    attribPosition.reset();
    attribNeighbour.reset();
    attribSide.reset();
    attribFrame.reset();
    attribStored.reset();
    attribFloor.reset();
    program.reset();
}

void Wavetable3DRenderer::rebuildVertices()
{
    px3::WavetableDisplay display;
    {
        const std::scoped_lock lock(displayMutex);
        if (! displayDirty) { return; }
        display = pendingDisplay;
        displayDirty = false;
    }

    vertices.clear();
    frameCount = 0;
    verticesPerFrame = 0;

    const auto sourceFrames = static_cast<int>(display.frames.size());
    if (sourceFrames < 2) { return; }

    frameCount = juce::jmin(kDrawnFrames, sourceFrames);
    verticesPerFrame = kPointsPerFrame * 2;
    vertices.reserve(static_cast<std::size_t>(frameCount * verticesPerFrame));

    for (int f = 0; f < frameCount; ++f)
    {
        const auto frameNorm = frameCount > 1
                                 ? static_cast<float>(f) / static_cast<float>(frameCount - 1)
                                 : 0.0f;

        // Which source frame this is nearest, and whether it IS one - a drawn
        // curve that falls between two stored frames is an interpolation and is
        // marked as such.
        const auto exact = frameNorm * static_cast<float>(sourceFrames - 1);
        const auto nearest = juce::roundToInt(exact);
        const auto stored = std::abs(exact - static_cast<float>(nearest)) < 0.02f ? 1.0f : 0.0f;
        const auto& samples = display.frames[static_cast<std::size_t>(
            juce::jlimit(0, sourceFrames - 1, nearest))];

        if (samples.size() < 2) { continue; }

        const auto sampleAt = [&samples](float t)
        {
            const auto scaled = juce::jlimit(0.0f, 1.0f, t)
                                * static_cast<float>(samples.size() - 1);
            const auto index = static_cast<int>(scaled);
            const auto frac = scaled - static_cast<float>(index);
            const auto a = samples[static_cast<std::size_t>(index)];
            const auto b = samples[static_cast<std::size_t>(
                juce::jmin(index + 1, static_cast<int>(samples.size()) - 1))];
            return a + (b - a) * frac;
        };

        for (int p = 0; p < kPointsPerFrame; ++p)
        {
            const auto t = static_cast<float>(p) / (kPointsPerFrame - 1);
            const auto tNext = static_cast<float>(juce::jmin(p + 1, kPointsPerFrame - 1))
                               / (kPointsPerFrame - 1);

            // x across the waveform, y its amplitude, z the frame. A genuine
            // 3D position, not a 2D point with an offset added.
            const auto x = (t * 2.0f - 1.0f) * kWaveformHalfWidth;
            const auto y = sampleAt(t) * 0.55f;
            const auto z = (frameNorm * 2.0f - 1.0f) * kStackHalfDepth;

            const auto xNext = (tNext * 2.0f - 1.0f) * kWaveformHalfWidth;
            const auto yNext = sampleAt(tNext) * 0.55f;

            for (const auto side : { -1.0f, 1.0f })
            {
                Vertex vertex {};
                vertex.position[0] = x;
                vertex.position[1] = y;
                vertex.position[2] = z;
                vertex.neighbour[0] = xNext;
                vertex.neighbour[1] = yNext;
                vertex.neighbour[2] = z;
                vertex.side = side;
                vertex.frame = frameNorm;
                vertex.stored = stored;
                vertices.push_back(vertex);
            }
        }
    }

    buildFloor();
    geometryUploaded = false;
}

Wavetable3DRenderer::FloorInfo Wavetable3DRenderer::getFloorInfo() const
{
    FloorInfo info;
    info.edgeCount = floorEdgeCount;
    if (floorEdgeCount <= 0 || floorFirstVertex <= 0)
    {
        return info;
    }

    info.lowestWaveformY = 1.0e9f;
    auto highestWaveformY = -1.0e9f;
    for (int i = 0; i < floorFirstVertex; ++i)
    {
        const auto y = vertices[static_cast<std::size_t>(i)].position[1];
        info.lowestWaveformY = juce::jmin(info.lowestWaveformY, y);
        highestWaveformY = juce::jmax(highestWaveformY, y);
    }
    info.waveformHeight = highestWaveformY - info.lowestWaveformY;

    info.topY = -1.0e9f;
    info.bottomY = 1.0e9f;
    for (std::size_t i = static_cast<std::size_t>(floorFirstVertex); i < vertices.size(); ++i)
    {
        info.halfWidth = juce::jmax(info.halfWidth, std::abs(vertices[i].position[0]));
        info.halfDepth = juce::jmax(info.halfDepth, std::abs(vertices[i].position[2]));
        info.topY = juce::jmax(info.topY, vertices[i].position[1]);
        info.bottomY = juce::jmin(info.bottomY, vertices[i].position[1]);
    }
    return info;
}

void Wavetable3DRenderer::buildFloor()
{
    // A shallow box beneath the stack, in the same coordinate system, so it
    // turns with the camera and carries the perspective rather than being drawn
    // on the glass.
    //
    // A box rather than a plane because a single rectangle seen at a shallow
    // angle is ambiguous - it could be lying flat or standing up. Giving it a
    // little thickness resolves that in one glance: the four short corner edges
    // are what say which way is down.
    //
    // Still only the perimeter of it. Subdividing either face into cells would
    // turn the picture into a graph, which is the one thing it is not meant to
    // look like.
    //
    // Each edge is its own ribbon rather than one loop, because the ribbon takes
    // its perpendicular from the direction to the NEXT point and a strip running
    // through a corner would pinch there.
    floorFirstVertex = static_cast<int>(vertices.size());
    floorEdgeCount = 0;

    if (vertices.empty())
    {
        return;
    }

    auto lowest = 0.0f;
    for (const auto& vertex : vertices)
    {
        lowest = juce::jmin(lowest, vertex.position[1]);
    }

    const auto topY = lowest - kFloorDrop;
    const auto bottomY = topY - kFloorThickness;

    const auto addEdge = [this](float x0, float y0, float z0,
                                float x1, float y1, float z1,
                                float emphasis)
    {
        for (int end = 0; end < 2; ++end)
        {
            const auto hx = end == 0 ? x0 : x1;
            const auto hy = end == 0 ? y0 : y1;
            const auto hz = end == 0 ? z0 : z1;
            const auto nx = end == 0 ? x1 : x0;
            const auto ny = end == 0 ? y1 : y0;
            const auto nz = end == 0 ? z1 : z0;

            for (const auto side : { -1.0f, 1.0f })
            {
                Vertex vertex {};
                vertex.position[0] = hx;
                vertex.position[1] = hy;
                vertex.position[2] = hz;
                vertex.neighbour[0] = nx;
                vertex.neighbour[1] = ny;
                vertex.neighbour[2] = nz;
                vertex.side = side;
                // Its own depth drives the same fade the stack uses, so the far
                // edge sits back and the near edge comes forward.
                vertex.frame = (hz / kStackHalfDepth + 1.0f) * 0.5f;
                // `stored` carries the emphasis for floor vertices. The
                // waveform's meaning for it - whether the frame is a real one -
                // does not apply here, and the fragment shader takes a different
                // branch, so the slot is free.
                vertex.stored = emphasis;
                vertex.floorFlag = 1.0f;
                vertices.push_back(vertex);
            }
        }
        ++floorEdgeCount;
    };

    const auto w = kWaveformHalfWidth;
    const auto d = kStackHalfDepth;

    // Every edge at the same weight, so the box reads as a wireframe rather
    // than as a solid with a lit top.
    //
    // The underside and the posts were drawn at 45% at first, on the theory that
    // playing them down would make the box read as a slab. It does - and a slab
    // is a surface, which is the opposite of what is wanted here. Depth still
    // separates near from far; that is perspective, not shading.
    constexpr float kTopEmphasis = 1.0f;
    constexpr float kUnderEmphasis = 1.0f;

    for (const auto& face : { std::make_pair(topY, kTopEmphasis),
                              std::make_pair(bottomY, kUnderEmphasis) })
    {
        addEdge(-w, face.first, -d,  w, face.first, -d, face.second);
        addEdge( w, face.first, -d,  w, face.first,  d, face.second);
        addEdge( w, face.first,  d, -w, face.first,  d, face.second);
        addEdge(-w, face.first,  d, -w, face.first, -d, face.second);
    }

    // The four corner posts, which are what make it a box rather than two
    // rectangles that happen to be near each other.
    for (const auto cornerX : { -w, w })
    {
        for (const auto cornerZ : { -d, d })
        {
            addEdge(cornerX, topY, cornerZ, cornerX, bottomY, cornerZ, kUnderEmphasis);
        }
    }
}

void Wavetable3DRenderer::uploadGeometry()
{
    if (geometryUploaded || vertexBuffer == 0 || vertices.empty())
    {
        return;
    }

    context.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, vertexBuffer);
    context.extensions.glBufferData(
        juce::gl::GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
        vertices.data(),
        juce::gl::GL_STATIC_DRAW);

    geometryUploaded = true;
}

juce::Rectangle<float> Wavetable3DRenderer::projectedBounds(const Camera& forCamera,
                                                            float aspect) const
{
    if (vertices.empty())
    {
        return {};
    }

    const auto matrix = buildViewProjection(aspect, forCamera);

    auto minX = 1.0e9f, maxX = -1.0e9f, minY = 1.0e9f, maxY = -1.0e9f;
    for (const auto& vertex : vertices)
    {
        const auto* m = matrix.mat;
        const auto x = vertex.position[0], y = vertex.position[1], z = vertex.position[2];

        // Column-major, the same convention the shader multiplies in.
        const auto cx = m[0] * x + m[4] * y + m[8] * z + m[12];
        const auto cy = m[1] * x + m[5] * y + m[9] * z + m[13];
        const auto cw = m[3] * x + m[7] * y + m[11] * z + m[15];

        if (cw <= 1.0e-6f)
        {
            // Behind the camera: treat as maximally out of frame rather than
            // letting a divide by a tiny w report it as comfortably inside.
            return { -1.0e6f, -1.0e6f, 2.0e6f, 2.0e6f };
        }

        minX = juce::jmin(minX, cx / cw);
        maxX = juce::jmax(maxX, cx / cw);
        minY = juce::jmin(minY, cy / cw);
        maxY = juce::jmax(maxY, cy / cw);
    }

    return { minX, minY, maxX - minX, maxY - minY };
}

void Wavetable3DRenderer::autoFrame(float aspect)
{
    if (vertices.empty() || aspect <= 0.0f)
    {
        return;
    }

    // Centre, then fit, then centre again. The two interact - pulling back
    // changes where the stack sits in frame - so a couple of passes settles it
    // where one would leave the picture slightly low.
    framedCamera = Camera {};
    for (int pass = 0; pass < 3; ++pass)
    {
        framedCamera.distance = distanceToFit(framedCamera, aspect, 0.06f);

        const auto bounds = projectedBounds(framedCamera, aspect);
        if (bounds.getHeight() <= 0.0f || bounds.getHeight() > 1.0e5f)
        {
            break;
        }

        // Shift what the camera aims at until the stack sits in the middle of
        // the frame rather than low in it.
        const auto centreNdc = bounds.getY() + bounds.getHeight() * 0.5f;
        framedCamera.targetY += centreNdc * framedCamera.distance * 0.35f;
    }

    // Only the framing moves; whatever the user has orbited to is left alone.
    camera.distance = framedCamera.distance;
    camera.targetY = framedCamera.targetY;
}

float Wavetable3DRenderer::distanceToFit(const Camera& orientation, float aspect,
                                         float margin) const
{
    if (vertices.empty())
    {
        return Camera {}.distance;
    }

    // Extent shrinks monotonically as the camera pulls back, so bisection finds
    // the closest distance that still fits rather than a hand-picked constant
    // that happens to work for one table.
    const auto extentAt = [this, aspect, &orientation](float distance)
    {
        auto probe = orientation;
        probe.distance = distance;

        const auto bounds = projectedBounds(probe, aspect);
        return juce::jmax(juce::jmax(std::abs(bounds.getX()), std::abs(bounds.getRight())),
                          juce::jmax(std::abs(bounds.getY()), std::abs(bounds.getBottom())));
    };

    const auto target = juce::jlimit(0.1f, 1.0f, 1.0f - margin);
    auto low = 0.5f;
    auto high = 40.0f;
    for (int i = 0; i < 40; ++i)
    {
        const auto mid = 0.5f * (low + high);
        if (extentAt(mid) > target) { low = mid; } else { high = mid; }
    }
    return high;
}

float Wavetable3DRenderer::distanceThatFits(float aspect, float margin) const
{
    // The worst orientation the camera can reach. This is what the framing is
    // CHECKED against, not what sets the default view: fitting the default to
    // the worst case costs a fifth of the picture at the angle it is actually
    // seen from.
    auto worst = 0.0f;
    for (int a = 0; a <= 8; ++a)
    {
        for (const auto elevation : { kMinElevation, 0.5f * (kMinElevation + kMaxElevation),
                                      kMaxElevation })
        {
            Camera probe = framedCamera;
            probe.azimuth = kMinAzimuth
                            + (kMaxAzimuth - kMinAzimuth) * static_cast<float>(a) / 8.0f;
            probe.elevation = elevation;
            worst = juce::jmax(worst, distanceToFit(probe, aspect, margin));
        }
    }
    return worst;
}

juce::Matrix3D<float> Wavetable3DRenderer::buildViewProjection(float aspect) const
{
    return buildViewProjection(aspect, camera);
}

juce::Matrix3D<float> Wavetable3DRenderer::buildViewProjection(float aspect,
                                                               const Camera& forCamera) const
{
    // A restrained field of view. Wide angles make a wavetable look like a
    // fish-eye photograph of one; this is enough to say "depth" and no more.
    const auto fov = 0.62f;
    const auto nearPlane = 0.4f;
    const auto farPlane = 24.0f;

    const auto f = 1.0f / std::tan(fov * 0.5f);
    const juce::Matrix3D<float> projection(
        f / aspect, 0.0f, 0.0f, 0.0f,
        0.0f, f, 0.0f, 0.0f,
        0.0f, 0.0f, (farPlane + nearPlane) / (nearPlane - farPlane), -1.0f,
        0.0f, 0.0f, (2.0f * farPlane * nearPlane) / (nearPlane - farPlane), 0.0f);

    const auto eyeY0 = forCamera.targetY;
    const auto eyeX = std::sin(forCamera.azimuth) * std::cos(forCamera.elevation) * forCamera.distance;
    const auto eyeY = eyeY0 + std::sin(forCamera.elevation) * forCamera.distance;
    const auto eyeZ = std::cos(forCamera.azimuth) * std::cos(forCamera.elevation) * forCamera.distance;

    const juce::Vector3D<float> eye(eyeX, eyeY, eyeZ);
    const juce::Vector3D<float> target(0.0f, forCamera.targetY, 0.0f);
    const juce::Vector3D<float> up(0.0f, 1.0f, 0.0f);

    // Written out rather than using a cross-product helper: juce::Vector3D has
    // no crossProduct, and an orthonormal basis is three lines.
    const auto cross = [](juce::Vector3D<float> a, juce::Vector3D<float> b)
    {
        return juce::Vector3D<float>(a.y * b.z - a.z * b.y,
                                     a.z * b.x - a.x * b.z,
                                     a.x * b.y - a.y * b.x);
    };

    const auto forward = (target - eye).normalised();
    const auto right = cross(forward, up).normalised();
    const auto trueUp = cross(right, forward);

    const juce::Matrix3D<float> view(
        right.x, trueUp.x, -forward.x, 0.0f,
        right.y, trueUp.y, -forward.y, 0.0f,
        right.z, trueUp.z, -forward.z, 0.0f,
        -(right.x * eye.x + right.y * eye.y + right.z * eye.z),
        -(trueUp.x * eye.x + trueUp.y * eye.y + trueUp.z * eye.z),
        (forward.x * eye.x + forward.y * eye.y + forward.z * eye.z),
        1.0f);

    // projection * view, not the other way round. juce::Matrix3D multiplies in
    // the column-major convention, so a * b applies b FIRST - and the reversed
    // order projected the stack somewhere off screen entirely, which looked
    // exactly like a renderer that was running and drawing nothing.
    return projection * view;
}

void Wavetable3DRenderer::renderOpenGL()
{
    const auto scale = static_cast<float>(context.getRenderingScale());
    const auto width = juce::jmax(1, juce::roundToInt(static_cast<float>(getWidth()) * scale));
    const auto height = juce::jmax(1, juce::roundToInt(static_cast<float>(getHeight()) * scale));

    // The background is cleared here rather than painted by a component
    // underneath, because the GL layer covers whatever is under it.
    juce::OpenGLHelpers::clear(backgroundColour);
    juce::gl::glViewport(0, 0, width, height);

    // The environment is drawn before anything else and covers every pixel, so
    // the clear above is what shows only when the environment shader failed to
    // build.
    drawEnvironment(static_cast<float>(width) / static_cast<float>(height));

    const auto hadGeometry = ! vertices.empty();
    rebuildVertices();
    if (! vertices.empty() && (! hadGeometry || ! geometryUploaded))
    {
        autoFrame(static_cast<float>(width) / static_cast<float>(height));
    }
    uploadGeometry();

    // Nothing is drawn until the vertices are actually IN this context's buffer.
    //
    // Without this the renderer crashed on a tab switch: the context is
    // recreated, glGenBuffers hands back a new empty buffer, and the draw calls
    // below were still issued for the ranges the old one held - so the GPU
    // fetched vertices past the end of an empty buffer and took the process
    // with it.
    if (program == nullptr || vertexBuffer == 0 || vertices.empty() || ! geometryUploaded)
    {
        return;
    }

    // Blended and depth-tested, but NOT depth-writing. Writing depth from
    // translucent ribbons would let whichever frame happened to be drawn first
    // punch a hole in the ones behind it.
    juce::gl::glEnable(juce::gl::GL_BLEND);
    juce::gl::glBlendFunc(juce::gl::GL_SRC_ALPHA, juce::gl::GL_ONE_MINUS_SRC_ALPHA);
    juce::gl::glDisable(juce::gl::GL_DEPTH_TEST);
    juce::gl::glDepthMask(juce::gl::GL_FALSE);

    program->use();

    const auto aspect = static_cast<float>(width) / static_cast<float>(height);
    lastAspect = aspect;
    const auto viewProjection = buildViewProjection(aspect);

    if (uniformViewProjection != nullptr)
    {
        uniformViewProjection->setMatrix4(viewProjection.mat, 1, false);
    }
    if (uniformSelected != nullptr)
    {
        uniformSelected->set(selectedPosition.load());
    }
    if (uniformHalfWidth != nullptr)
    {
        // Half a line width, in normalised device units, from a pixel width -
        // which is what keeps the ribbon the same thickness on a Retina panel
        // as on any other.
        // Doubled from 1.1 px half-width. One uniform serves the waveform
        // ribbons and the floor edges alike, so the whole picture thickens
        // together rather than the floor turning into hairlines beside a
        // heavier stack.
        const auto pixels = 4.4f * scale;
        uniformHalfWidth->set(pixels / static_cast<float>(height));

        // The same width the fragment shader antialiases against. Half,
        // because `pixels` is the full line width: the vertex offset of
        // uHalfWidth in NDC works out to pixels/2 on each side.
        if (uniformHalfWidthPixels != nullptr)
        {
            uniformHalfWidthPixels->set(pixels * 0.5f);
        }
    }
    if (uniformAspect != nullptr)
    {
        uniformAspect->set(aspect);
    }
    if (uniformAccent != nullptr)
    {
        uniformAccent->set(accentColour.getFloatRed(), accentColour.getFloatGreen(),
                           accentColour.getFloatBlue(), 1.0f);
    }
    if (uniformGrayscale != nullptr)
    {
        uniformGrayscale->set(bypassed.load() ? 1.0f : 0.0f);
    }
    if (uniformEnvironment != nullptr)
    {
        uniformEnvironment->set(environmentEnabled.load() ? 1.0f : 0.0f);
    }
    if (uniformHaze != nullptr)
    {
        // What distance fades towards: the background, lifted slightly, so the
        // back of the stack settles INTO the environment rather than being
        // subtracted from it.
        const auto haze = backgroundColour.brighter(0.22f);
        uniformHaze->set(haze.getFloatRed(), haze.getFloatGreen(), haze.getFloatBlue());
    }

    context.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, vertexBuffer);

    constexpr auto stride = static_cast<GLsizei>(sizeof(Vertex));
    const auto bind = [this](const std::unique_ptr<juce::OpenGLShaderProgram::Attribute>& a,
                                     int size, std::size_t offset)
    {
        if (a == nullptr) { return; }
        context.extensions.glVertexAttribPointer(static_cast<GLuint>(a->attributeID), size,
                                                 juce::gl::GL_FLOAT, juce::gl::GL_FALSE,
                                                 stride,
                                                 reinterpret_cast<void*>(offset));
        context.extensions.glEnableVertexAttribArray(static_cast<GLuint>(a->attributeID));
    };

    bind(attribPosition, 3, offsetof(Vertex, position));
    bind(attribNeighbour, 3, offsetof(Vertex, neighbour));
    bind(attribSide, 1, offsetof(Vertex, side));
    bind(attribFrame, 1, offsetof(Vertex, frame));
    bind(attribStored, 1, offsetof(Vertex, stored));
    bind(attribFloor, 1, offsetof(Vertex, floorFlag));

    // The floor first, so the stack blends over it rather than the other way
    // round - it is the environment, not part of the instrument.
    for (int e = 0; e < floorEdgeCount; ++e)
    {
        const auto first = floorFirstVertex + e * 4;
        if (first >= 0 && first + 4 <= static_cast<int>(vertices.size()))
        {
            juce::gl::glDrawArrays(juce::gl::GL_TRIANGLE_STRIP, first, 4);
        }
    }

    // Back to front, so the nearer frames blend over the ones behind them.
    //
    // The range is clamped against what was actually uploaded rather than
    // trusted from frameCount. A draw call reading past the end of a buffer is
    // a segfault inside the driver, not an exception anything can catch, so the
    // bound is checked here where it is cheap.
    const auto uploadedVertices = static_cast<int>(vertices.size());
    for (int f = frameCount - 1; f >= 0; --f)
    {
        const auto first = f * verticesPerFrame;
        if (first < 0 || first + verticesPerFrame > uploadedVertices)
        {
            continue;
        }
        juce::gl::glDrawArrays(juce::gl::GL_TRIANGLE_STRIP, first, verticesPerFrame);
    }

    const auto unbind = [this](const std::unique_ptr<juce::OpenGLShaderProgram::Attribute>& a)
    {
        if (a != nullptr)
        {
            context.extensions.glDisableVertexAttribArray(static_cast<GLuint>(a->attributeID));
        }
    };
    unbind(attribPosition);
    unbind(attribNeighbour);
    unbind(attribSide);
    unbind(attribFrame);
    unbind(attribStored);
    unbind(attribFloor);

    context.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, 0);

    framesRendered.fetch_add(1);

    if (pixelAudit.load())
    {
        // Read back and count anything brighter than the cleared background.
        // Expensive, and only ever on for the diagnostic.
        const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        std::vector<juce::uint8> pixels(pixelCount * 4, 0);
        juce::gl::glReadPixels(0, 0, width, height, juce::gl::GL_RGBA,
                               juce::gl::GL_UNSIGNED_BYTE, pixels.data());

        const auto backR = backgroundColour.getRed();
        const auto backG = backgroundColour.getGreen();
        const auto backB = backgroundColour.getBlue();

        int lit = 0;
        for (std::size_t i = 0; i < pixelCount; ++i)
        {
            const auto r = pixels[i * 4];
            const auto g = pixels[i * 4 + 1];
            const auto b = pixels[i * 4 + 2];
            if (std::abs(r - backR) > 8 || std::abs(g - backG) > 8 || std::abs(b - backB) > 8)
            {
                ++lit;
            }
        }
        litPixels.store(lit);

        // Regions, so the environment can be checked as numbers. A vignette
        // that is "clearly there" and one that is not are the same sentence;
        // corner luminance against centre luminance is not.
        //
        // Every pixel, geometry included. Masking the stack out was tried and
        // is worse: the regions then measure different SAMPLE SETS as the
        // camera moves, so a number changing tells you nothing about whether
        // the picture changed. What the environment does to the whole frame is
        // the honest measurement, and the stack is a fixed contribution to it
        // while the camera is still.
        struct Accumulator { double sum { 0.0 }; int count { 0 };
                             float mean() const { return count > 0
                                                    ? static_cast<float>(sum / count) : 0.0f; } };
        Accumulator centre, corners, upper, lower;
        auto darkest = 1.0f;
        auto brightest = 0.0f;

        const auto halfW = width * 0.5;
        const auto halfH = height * 0.5;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const auto i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                               + static_cast<std::size_t>(x);
                const auto r = pixels[i * 4];
                const auto g = pixels[i * 4 + 1];
                const auto b = pixels[i * 4 + 2];

                const auto luma = static_cast<float>(0.299 * r + 0.587 * g + 0.114 * b) / 255.0f;
                darkest = juce::jmin(darkest, luma);
                brightest = juce::jmax(brightest, luma);

                // Normalised distance from the middle, 0 at the centre and 1 at
                // a corner, aspect-corrected so the regions are not skewed by a
                // wide panel.
                const auto dx = (static_cast<double>(x) - halfW) / halfW;
                const auto dy = (static_cast<double>(y) - halfH) / halfH;
                const auto radius = std::sqrt(dx * dx + dy * dy) / std::sqrt(2.0);

                if (radius < 0.25) { centre.sum += luma; ++centre.count; }
                else if (radius > 0.80) { corners.sum += luma; ++corners.count; }

                if (dy > 0.6) { upper.sum += luma; ++upper.count; }
                else if (dy < -0.6) { lower.sum += luma; ++lower.count; }
            }
        }

        {
            const std::scoped_lock lock(probeMutex);
            luminanceProbe.centre = centre.mean();
            luminanceProbe.corners = corners.mean();
            luminanceProbe.upper = upper.mean();
            luminanceProbe.lower = lower.mean();
            luminanceProbe.darkest = darkest;
            luminanceProbe.brightest = brightest;
        }
        // Recorded from the READ size, not the component size: the
        // framebuffer is at the display scale, so a Retina panel audits four
        // times as many pixels as the component has points.
        auditedPixels.store(static_cast<int>(pixelCount));
    }
}
