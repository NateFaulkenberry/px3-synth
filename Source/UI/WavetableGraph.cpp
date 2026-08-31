#include "WavetableGraph.h"

namespace
{
// Rendered at the display's scale so the cached surface is not a 1x bitmap
// stretched into a Retina context - the same trap the compressor's cached face
// fell into.
float displayScale()
{
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        return static_cast<float>(d->scale);
    }
    return 1.0f;
}
} // namespace

WavetableGraph::WavetableGraph()
{
    setInterceptsMouseClicks(true, true);
    setOpaque(false);

    // The overlay is a child of the GL view, not a sibling of it. An attached
    // OpenGL context composites above sibling components on macOS whatever the
    // z-order says, so a sibling overlay is simply invisible; a child is drawn
    // over the GL output by JUCE's own component painting.
    addAndMakeVisible(glView);
    glView.addAndMakeVisible(overlay);
}

void WavetableGraph::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    config = std::move(configIn);
    glView.setBackgroundColour(configColour("osc.wavetable.graph.background",
                                            juce::Colour::fromRGB(12, 12, 14)));
    surface = juce::Image();
    repaint();
}

void WavetableGraph::setAccentColour(juce::Colour accentIn)
{
    if (accent == accentIn)
    {
        return;
    }
    accent = accentIn;
    glView.setAccentColour(accentIn);
    surface = juce::Image();
    repaint();
}

void WavetableGraph::setDisplay(px3::WavetableDisplay displayIn)
{
    display = displayIn;
    glView.setDisplay(std::move(displayIn));
    surface = juce::Image();
    repaint();
}

void WavetableGraph::setPosition(float base, float modulated)
{
    const auto clampedBase = juce::jlimit(0.0f, 1.0f, base);
    const auto clampedModulated = juce::jlimit(0.0f, 1.0f, modulated);

    // A repaint per frame for a value that has not moved is the usual way a
    // display ends up costing more than the synth.
    if (std::abs(clampedBase - basePosition) < 0.0005f
        && std::abs(clampedModulated - modulatedPosition) < 0.0005f)
    {
        return;
    }

    basePosition = clampedBase;
    modulatedPosition = clampedModulated;
    glView.setPosition(clampedModulated);
    overlay.repaint();
}

void WavetableGraph::setMissingTableName(const juce::String& name)
{
    if (missingTableName == name)
    {
        return;
    }
    missingTableName = name;
    overlay.repaint();
}

juce::Colour WavetableGraph::configColour(const juce::String& path, juce::Colour fallback) const
{
    return config != nullptr ? config->getColour(path, fallback) : fallback;
}

float WavetableGraph::configFloat(const juce::String& path, float fallback) const
{
    return config != nullptr ? config->getFloat(path, fallback) : fallback;
}

bool WavetableGraph::isSupportedFile(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".aif" || extension == ".aiff"
        || extension == ".flac" || extension == ".ogg"
        || extension == ".png" || extension == ".jpg" || extension == ".jpeg"
        || extension == ".gif";
}

bool WavetableGraph::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        if (isSupportedFile(juce::File(path)))
        {
            return true;
        }
    }
    return false;
}

void WavetableGraph::fileDragEnter(const juce::StringArray&, int, int)
{
    dragging = true;
    overlay.repaint();
}

void WavetableGraph::fileDragExit(const juce::StringArray&)
{
    dragging = false;
    overlay.repaint();
}

void WavetableGraph::filesDropped(const juce::StringArray& files, int, int)
{
    dragging = false;
    overlay.repaint();

    for (const auto& path : files)
    {
        const juce::File file(path);
        if (isSupportedFile(file) && onFileDropped != nullptr)
        {
            // The first supported file only. Dropping a folder of samples and
            // getting the last one silently is worse than getting the first one
            // predictably.
            onFileDropped(file);
            return;
        }
    }
}

void WavetableGraph::resized()
{
    surface = juce::Image();
    glView.setBounds(getLocalBounds());
    overlay.setBounds(glView.getLocalBounds());
}

void WavetableGraph::rebuildSurface()
{
    const auto bounds = getLocalBounds();
    if (bounds.isEmpty() || display.isEmpty())
    {
        return;
    }

    const auto scale = juce::jlimit(1.0f, 4.0f, displayScale());
    surface = juce::Image(juce::Image::ARGB,
                          juce::roundToInt(static_cast<float>(bounds.getWidth()) * scale),
                          juce::roundToInt(static_cast<float>(bounds.getHeight()) * scale),
                          true);

    juce::Graphics g(surface);
    g.addTransform(juce::AffineTransform::scale(scale));

    const auto area = bounds.toFloat().reduced(configFloat("osc.wavetable.graph.inset", 6.0f));
    const auto frameCount = static_cast<int>(display.frames.size());

    // Stacked and offset: the frames recede up and to the right, which is the
    // visual language every wavetable synth uses for this and the one a user
    // will already read as "a table of waveforms" rather than "a graph".
    const auto depthX = configFloat("osc.wavetable.graph.depthX", 0.28f) * area.getWidth();
    const auto depthY = configFloat("osc.wavetable.graph.depthY", 0.42f) * area.getHeight();
    const auto amplitude = configFloat("osc.wavetable.graph.amplitude", 0.30f) * area.getHeight();

    const auto nearColour = configColour("osc.wavetable.graph.nearColor", accent);
    const auto farColour = configColour("osc.wavetable.graph.farColor",
                                        accent.withMultipliedSaturation(0.35f)
                                              .withMultipliedBrightness(0.55f));

    // Back to front, so nearer frames overlap the ones behind them.
    for (int f = frameCount - 1; f >= 0; --f)
    {
        const auto depth = frameCount > 1
                             ? static_cast<float>(f) / static_cast<float>(frameCount - 1)
                             : 0.0f;
        const auto& points = display.frames[static_cast<std::size_t>(f)];
        if (points.size() < 2)
        {
            continue;
        }

        const auto originX = area.getX() + depthX * depth;
        const auto originY = area.getBottom() - depthY * depth - amplitude;
        const auto width = area.getWidth() - depthX;

        juce::Path path;
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const auto x = originX + width * static_cast<float>(i)
                                       / static_cast<float>(points.size() - 1);
            const auto y = originY - points[i] * amplitude;
            if (i == 0) { path.startNewSubPath(x, y); }
            else { path.lineTo(x, y); }
        }

        // Nearer frames are brighter and heavier; the far ones fade back rather
        // than crowding the picture.
        const auto colour = farColour.interpolatedWith(nearColour, 1.0f - depth);
        g.setColour(colour.withMultipliedAlpha(0.35f + 0.65f * (1.0f - depth)));
        g.strokePath(path, juce::PathStrokeType(0.9f + 0.6f * (1.0f - depth)));
    }
}

void WavetableGraph::paint(juce::Graphics& g)
{
    // Only reachable before the GL view has any size. Once it covers this
    // component the background comes from the GL clear, because the native
    // layer hides whatever is painted underneath it.
    if (glView.getBounds().isEmpty())
    {
        g.setColour(configColour("osc.wavetable.graph.background",
                                 juce::Colour::fromRGBA(12, 12, 14, 190)));
        g.fillRoundedRectangle(getLocalBounds().toFloat(),
                               configFloat("osc.wavetable.graph.cornerRadius", 6.0f));
    }
}

void WavetableGraph::paintOverlay(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto radius = configFloat("osc.wavetable.graph.cornerRadius", 6.0f);

    if (display.isEmpty())
    {
        g.setColour(configColour("osc.wavetable.graph.emptyTextColor",
                                 juce::Colour::fromRGBA(200, 200, 200, 120)));
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText("DROP AUDIO OR IMAGE HERE", getLocalBounds(),
                         juce::Justification::centred, 2);
    }
    else
    {
        // The CPU stack is now a FALLBACK, drawn only if the GL context never
        // came up - a machine with no usable context still gets a picture rather
        // than an empty panel.
        if (! glView.isRendering())
        {
            const auto expectedWidth = juce::roundToInt(static_cast<float>(getWidth())
                                                        * juce::jlimit(1.0f, 4.0f, displayScale()));
            if (surface.isNull() || surface.getWidth() != expectedWidth)
            {
                rebuildSurface();
            }
            if (surface.isValid())
            {
                g.drawImage(surface, bounds);
            }
        }

        // The scan marker. Base and modulated are drawn separately so an LFO
        // sweeping the scan shows its range as well as its position.
        const auto area = bounds.reduced(configFloat("osc.wavetable.graph.inset", 6.0f));
        const auto markerFor = [&area](float position)
        {
            const auto x = area.getX() + area.getWidth() * juce::jlimit(0.0f, 1.0f, position);
            return juce::Line<float>(x, area.getY(), x, area.getBottom());
        };

        if (std::abs(modulatedPosition - basePosition) > 0.002f)
        {
            g.setColour(configColour("osc.wavetable.graph.baseMarkerColor",
                                     accent.withAlpha(0.35f)));
            g.drawLine(markerFor(basePosition),
                       configFloat("osc.wavetable.graph.baseMarkerWidth", 1.0f));
        }

        g.setColour(configColour("osc.wavetable.graph.markerColor", accent.brighter(0.4f)));
        g.drawLine(markerFor(modulatedPosition),
                   configFloat("osc.wavetable.graph.markerWidth", 1.6f));

        const auto shaderError = glView.getShaderError();
        if (shaderError.isNotEmpty())
        {
            g.setColour(configColour("osc.wavetable.graph.missingTextColor",
                                     juce::Colour::fromRGB(240, 170, 90)));
            g.setFont(juce::FontOptions(9.5f));
            g.drawFittedText("GPU: " + shaderError.upToFirstOccurrenceOf("\n", false, false),
                             getLocalBounds().reduced(4), juce::Justification::bottomLeft, 2);
        }

        if (missingTableName.isNotEmpty())
        {
            // A preset asked for a table this machine does not have. Saying so
            // is the whole point - falling back silently leaves a preset that
            // sounds wrong with nothing to explain it.
            g.setColour(configColour("osc.wavetable.graph.missingTextColor",
                                     juce::Colour::fromRGB(240, 170, 90)));
            g.setFont(juce::FontOptions(10.0f));
            g.drawFittedText("MISSING: " + missingTableName,
                             getLocalBounds().reduced(4), juce::Justification::topLeft, 2);
        }
    }

    if (dragging)
    {
        g.setColour(configColour("osc.wavetable.graph.dropHighlightColor",
                                 accent.withAlpha(0.85f)));
        g.drawRoundedRectangle(bounds.reduced(1.0f), radius,
                               configFloat("osc.wavetable.graph.dropHighlightWidth", 2.0f));
        g.setColour(configColour("osc.wavetable.graph.dropTextColor",
                                 juce::Colour::fromRGB(240, 240, 240)));
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText("DROP TO IMPORT", getLocalBounds(), juce::Justification::centred, 1);
    }
    else
    {
        g.setColour(configColour("osc.wavetable.graph.borderColor",
                                 juce::Colour::fromRGBA(255, 255, 255, 40)));
        g.drawRoundedRectangle(bounds.reduced(0.5f), radius,
                               configFloat("osc.wavetable.graph.borderWidth", 1.0f));
    }
}
