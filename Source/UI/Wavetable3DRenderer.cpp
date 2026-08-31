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

const char* kVertexShader = R"(
attribute vec3 aPosition;
attribute vec3 aNeighbour;
attribute float aSide;
attribute float aFrame;
attribute float aStored;

uniform mat4 uViewProjection;
uniform float uHalfWidth;
uniform float uAspect;

varying float vFrame;
varying float vSide;
varying float vStored;
varying float vDepth;

void main()
{
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
    vDepth = clamp(here.z / max(here.w, 0.0001) * 0.5 + 0.5, 0.0, 1.0);
}
)";

const char* kFragmentShader = R"(
varying float vFrame;
varying float vSide;
varying float vStored;
varying float vDepth;

uniform float uSelected;
uniform vec4 uAccent;

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

    // Across the ribbon: 1 at the centre, 0 at the edges. This is the
    // antialiasing and the glow, out of the same value - a hard edge here would
    // undo the whole point of drawing ribbons instead of lines.
    float across = 1.0 - abs(vSide);
    float core = smoothstep(0.0, 0.55, across);
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
    alpha *= core * 0.85 + glow * 0.35;
    alpha += selected * glow * 0.5;

    // Emissive lift on the selected frame, added rather than mixed so it reads
    // as light rather than as a lighter colour.
    colour += hot * selected * glow * 0.55;

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
    camera = Camera {};
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

    camera.azimuth = dragStartCamera.azimuth + delta.x * 0.008f;

    // Clamped rather than wrapped. Orbiting under the stack and back out the
    // other side is not a view anyone means to arrive at, and getting there by
    // accident is the usual way a 3D control becomes something people avoid.
    // Dragging UP raises the camera, so the stack tilts to show more of its
    // top. The other sign is what a screen-space delta gives you literally, and
    // it feels inverted against every other 3D view a user has ever used.
    camera.elevation = juce::jlimit(kMinElevation, kMaxElevation,
                                    dragStartCamera.elevation - delta.y * 0.006f);

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

    attribPosition = attribute("aPosition");
    attribNeighbour = attribute("aNeighbour");
    attribSide = attribute("aSide");
    attribFrame = attribute("aFrame");
    attribStored = attribute("aStored");

    context.extensions.glGenBuffers(1, &vertexBuffer);

    // A brand new buffer holds nothing, whatever the CPU vertices still say.
    geometryUploaded = false;
}

void Wavetable3DRenderer::openGLContextClosing()
{
    if (vertexBuffer != 0)
    {
        context.extensions.glDeleteBuffers(1, &vertexBuffer);
        vertexBuffer = 0;
    }
    geometryUploaded = false;

    uniformViewProjection.reset();
    uniformSelected.reset();
    uniformHalfWidth.reset();
    uniformAspect.reset();
    uniformAccent.reset();
    attribPosition.reset();
    attribNeighbour.reset();
    attribSide.reset();
    attribFrame.reset();
    attribStored.reset();
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
            const auto x = t * 2.0f - 1.0f;
            const auto y = sampleAt(t) * 0.55f;
            const auto z = frameNorm * 2.0f - 1.0f;

            const auto xNext = tNext * 2.0f - 1.0f;
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

    geometryUploaded = false;
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

juce::Matrix3D<float> Wavetable3DRenderer::buildViewProjection(float aspect) const
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

    const auto eyeX = std::sin(camera.azimuth) * std::cos(camera.elevation) * camera.distance;
    const auto eyeY = std::sin(camera.elevation) * camera.distance;
    const auto eyeZ = std::cos(camera.azimuth) * std::cos(camera.elevation) * camera.distance;

    const juce::Vector3D<float> eye(eyeX, eyeY, eyeZ);
    const juce::Vector3D<float> target(0.0f, 0.0f, 0.0f);
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

    return view * projection;
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

    rebuildVertices();
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
        const auto pixels = 2.2f * scale;
        uniformHalfWidth->set(pixels / static_cast<float>(height));
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

    context.extensions.glBindBuffer(juce::gl::GL_ARRAY_BUFFER, 0);

    framesRendered.fetch_add(1);
}
