#include "ModalBackdrop.h"

namespace px3::ui
{
namespace
{
// One box pass of `radius` pixels along rows (horizontal) or columns, edges
// clamped. Running sums, so the cost does not depend on the radius; raw
// strides, so a column pass costs about what a row pass does.
void boxPass(const juce::Image::BitmapData& src, juce::Image::BitmapData& dst, int radius, bool horizontal)
{
    const auto length = horizontal ? src.width : src.height;
    const auto lines = horizontal ? src.height : src.width;
    const auto srcStep = horizontal ? src.pixelStride : src.lineStride;
    const auto dstStep = horizontal ? dst.pixelStride : dst.lineStride;
    const auto srcLine = horizontal ? src.lineStride : src.pixelStride;
    const auto dstLine = horizontal ? dst.lineStride : dst.pixelStride;
    const auto window = static_cast<juce::uint32>(2 * radius + 1);
    const auto half = window / 2;

    for (int line = 0; line < lines; ++line)
    {
        const auto* in = src.data + line * srcLine;
        auto* out = dst.data + line * dstLine;
        const auto pixel = [&](int i) { return in + juce::jlimit(0, length - 1, i) * srcStep; };

        juce::uint32 sum[4] {};
        for (int i = -radius; i <= radius; ++i)
        {
            const auto* p = pixel(i);
            for (int c = 0; c < 4; ++c) { sum[c] += p[c]; }
        }
        for (int i = 0; i < length; ++i, out += dstStep)
        {
            for (int c = 0; c < 4; ++c) { out[c] = static_cast<juce::uint8>((sum[c] + half) / window); }
            const auto* entering = pixel(i + radius + 1);
            const auto* leaving = pixel(i - radius);
            for (int c = 0; c < 4; ++c) { sum[c] += static_cast<juce::uint32>(entering[c]) - leaving[c]; }
        }
    }
}
} // namespace

juce::Image blurredCopy(const juce::Image& source, float radius)
{
    // Every pass reads one image and writes the other, so a pass never reads
    // a pixel it has already blurred.
    auto a = source.convertedToFormat(juce::Image::ARGB).createCopy();
    if (radius <= 0.0f || a.getWidth() < 2 || a.getHeight() < 2) { return a; }
    juce::Image b(juce::Image::ARGB, a.getWidth(), a.getHeight(), false);

    // Two box passes per axis: a smooth, nearly Gaussian falloff. A box of
    // half-width r has variance r^2/3; two of half-width k match it at
    // k = r / sqrt(2) - the spread the stack of offset draws had.
    const auto k = juce::jmax(1, juce::roundToInt(radius / std::sqrt(2.0f)));
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const auto horizontal : { true, false })
        {
            {
                const juce::Image::BitmapData src(a, juce::Image::BitmapData::readOnly);
                juce::Image::BitmapData dst(b, juce::Image::BitmapData::writeOnly);
                boxPass(src, dst, k, horizontal);
            }
            std::swap(a, b);
        }
    }
    return a;
}

juce::Image prepareBackdrop(const juce::Image& snapshot, juce::Rectangle<int> fullBounds, float blurRadius)
{
    if (! snapshot.isValid()) { return {}; }
    // Blurred once, on the pixels. This used to be 48 fractional-offset draws
    // of the whole window, each one resampled, on every paint: on a Retina
    // display that measured over 2 s before a sheet appeared.
    //
    // The radius is in component pixels; a snapshot taken at 2x needs twice as
    // many of its own. And it is blurred at half resolution: a blur has no
    // detail left to lose, and it quarters the work.
    const auto pixelScale = fullBounds.getWidth() > 0
                                ? static_cast<float>(snapshot.getWidth()) / static_cast<float>(fullBounds.getWidth())
                                : 1.0f;
    const auto reduced = blurRadius * pixelScale >= 2.0f && snapshot.getWidth() > 2 && snapshot.getHeight() > 2;
    const auto source = reduced ? snapshot.rescaled(snapshot.getWidth() / 2, snapshot.getHeight() / 2,
                                                    juce::Graphics::mediumResamplingQuality)
                                : snapshot;
    return blurredCopy(source, blurRadius * pixelScale * (reduced ? 0.5f : 1.0f));
}

void paintModalBackdrop(juce::Graphics& g,
                        juce::Rectangle<int> fullBounds,
                        juce::Rectangle<float> panelBounds,
                        const juce::Image& snapshot,
                        float panelCornerRadius,
                        juce::Colour dimColour,
                        float blurRadius)
{
    // A non-zero-winding path with the panel punched out of it, so everything
    // below is masked to the region OUTSIDE the sheet in one clip rather than
    // being drawn and then painted over.
    //
    // An empty panelBounds punches nothing: the caller wants the treatment
    // across the whole area, because it is painting BEHIND the sheet.
    juce::Path outsidePanelMask;
    outsidePanelMask.setUsingNonZeroWinding(false);
    outsidePanelMask.addRectangle(fullBounds.toFloat());
    if (! panelBounds.isEmpty())
    {
        // A square sheet is cut out exactly: grown by a pixel or rounded, the
        // hole left the sheet's corners and a rim around it unblurred, so the
        // sharp editor showed through around a panel with square corners.
        if (panelCornerRadius <= 0.0f)
        {
            outsidePanelMask.addRectangle(panelBounds);
        }
        else
        {
            outsidePanelMask.addRoundedRectangle(panelBounds.expanded(1.0f), panelCornerRadius);
        }
    }

    if (snapshot.isValid())
    {
        g.saveState();
        g.reduceClipRegion(outsidePanelMask);

        // A radius of 0 means the image is already a prepared backdrop.
        const auto image = blurRadius > 0.0f ? prepareBackdrop(snapshot, fullBounds, blurRadius) : snapshot;
        g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
        g.drawImage(image, fullBounds.toFloat().withPosition(0.0f, 0.0f));
        g.restoreState();
    }

    g.setColour(dimColour);
    g.fillPath(outsidePanelMask);
}

} // namespace px3::ui
