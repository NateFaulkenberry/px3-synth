#pragma once

#include <JuceHeader.h>

#include "../DSP/Wavetable.h"

#include <mutex>

// The wavetable stack, rendered on the GPU.
//
// Replaces a JUCE-primitive display that sheared each frame up and to the right
// by a fixed offset. That is not a projection: nothing converges, so the picture
// reads as stripes rather than as depth, and no amount of extra colour in the
// same approach fixes it.
//
// Every waveform is a triangle-strip ribbon rather than a line primitive,
// because glLineWidth above 1 is not supported in a macOS core profile - and
// because a ribbon is better than the line would have been: constant width in
// PIXELS regardless of depth, and a cross-ribbon coordinate that gives both
// antialiasing and the glow falloff out of one smoothstep.
class Wavetable3DRenderer final : public juce::Component,
                                  private juce::OpenGLRenderer
{
public:
    Wavetable3DRenderer();
    ~Wavetable3DRenderer() override;

    // Message thread. Parked behind a lock and picked up by the GL thread at
    // the start of its next frame - the only cross-thread hand-off here.
    void setDisplay(px3::WavetableDisplay display);

    // 0..1 through the table. Continuous, not a frame index: the selected
    // waveform moves through the stack rather than stepping between frames.
    void setPosition(float position);

    void setAccentColour(juce::Colour accent);

    // Cleared by the GL pass. Anything painted underneath this component is
    // hidden by the native layer, so the background has to come from here.
    void setBackgroundColour(juce::Colour colour);

    // True once the GL context has produced a frame, so a caller can fall back
    // to something else if the context never came up.
    bool isRendering() const noexcept { return framesRendered.load() > 0; }

    // Empty unless the shaders failed. Surfaced so a failure reads as a reason
    // rather than as a renderer that quietly does nothing.
    juce::String getShaderError() const;

    // Diagnostics. Counting render CALLS proves only that the thread is alive -
    // it says nothing about whether anything was drawn, which is exactly the
    // failure this had. With the audit on, the framebuffer is read back and the
    // lit pixels counted.
    void setPixelAudit(bool shouldAudit) { pixelAudit.store(shouldAudit); }
    int getLitPixelCount() const noexcept { return litPixels.load(); }
    int getAuditedPixelCount() const noexcept { return auditedPixels.load(); }

    void resized() override;
    void visibilityChanged() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

    // The camera, exposed for the tests: orbiting into an unusable orientation
    // is the failure this has to be held to.
    struct Camera
    {
        float azimuth { 0.62f };     // radians, around the stack
        float elevation { 0.42f };   // radians, above it
        float distance { 3.4f };
    };
    Camera getCamera() const noexcept { return camera; }
    void resetCamera();

    static constexpr float kMinElevation = -0.15f;
    static constexpr float kMaxElevation = 1.25f;
    static constexpr float kMinDistance = 1.8f;
    static constexpr float kMaxDistance = 7.0f;

private:
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    void rebuildVertices();
    void uploadGeometry();
    juce::Matrix3D<float> buildViewProjection(float aspect) const;

    juce::OpenGLContext context;

    std::unique_ptr<juce::OpenGLShaderProgram> program;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uniformViewProjection;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uniformSelected;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uniformHalfWidth;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uniformAspect;
    std::unique_ptr<juce::OpenGLShaderProgram::Uniform> uniformAccent;

    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> attribPosition;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> attribNeighbour;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> attribSide;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> attribFrame;
    std::unique_ptr<juce::OpenGLShaderProgram::Attribute> attribStored;

    GLuint vertexBuffer { 0 };

    struct Vertex
    {
        float position[3];
        float neighbour[3];
        float side;
        float frame;
        float stored;
    };

    // Rebuilt only when the TABLE changes. Scanning, orbiting and resizing are
    // all uniforms.
    std::vector<Vertex> vertices;
    int verticesPerFrame { 0 };
    int frameCount { 0 };

    // Whether the CPU vertices have been uploaded to the CURRENT context's
    // buffer.
    //
    // This has to be separate from displayDirty, and the two ways it goes wrong
    // are both real. A context can be recreated - hiding and re-showing the tab
    // does exactly that - which hands back a new empty buffer while the
    // vertices still look valid. And a display can arrive BEFORE the context
    // exists, which used to consume displayDirty with nowhere to upload to.
    // Either way the draw calls were issued against an empty buffer, and a
    // vertex fetch past the end of one is a segfault inside the driver.
    bool geometryUploaded { false };

    mutable std::mutex displayMutex;
    juce::String shaderError;
    px3::WavetableDisplay pendingDisplay;
    bool displayDirty { false };

    std::atomic<float> selectedPosition { 0.0f };
    std::atomic<int> framesRendered { 0 };
    std::atomic<bool> pixelAudit { false };
    std::atomic<int> litPixels { -1 };
    std::atomic<int> auditedPixels { 0 };
    juce::Colour accentColour { juce::Colour::fromRGB(96, 168, 255) };
    juce::Colour backgroundColour { juce::Colour::fromRGB(12, 12, 14) };

    Camera camera;
    Camera dragStartCamera;
    juce::Point<float> dragStart;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Wavetable3DRenderer)
};
