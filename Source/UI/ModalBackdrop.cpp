#include "ModalBackdrop.h"

namespace px3::ui
{

void paintModalBackdrop(juce::Graphics& g,
                        juce::Rectangle<int> fullBounds,
                        juce::Rectangle<float> panelBounds,
                        const juce::Image& snapshot,
                        float panelCornerRadius,
                        juce::Colour dimColour)
{
    // A non-zero-winding path with the panel punched out of it, so everything
    // below is masked to the region OUTSIDE the sheet in one clip rather than
    // being drawn and then painted over.
    juce::Path outsidePanelMask;
    outsidePanelMask.setUsingNonZeroWinding(false);
    outsidePanelMask.addRectangle(fullBounds.toFloat());
    outsidePanelMask.addRoundedRectangle(panelBounds.expanded(1.0f), panelCornerRadius);

    if (snapshot.isValid())
    {
        g.saveState();
        g.reduceClipRegion(outsidePanelMask);

        // The base pass, at full opacity, BEFORE the blur.
        //
        // Without it the offset copies are the only coverage, and an offset
        // copy does not reach the edge it is shifted away from - so a strip
        // around the window gets fewer copies than the middle, and a corner,
        // being short on two axes at once, gets fewest of all. Measured on a
        // uniform source: 0.1765 at the corner against 0.1961 in the middle,
        // which shows up as a darker box in the corner of the dimmed backdrop.
        // The base pass guarantees every pixel the same starting coverage.
        g.setOpacity(1.0f);
        g.drawImageAt(snapshot, 0, 0, false);

        // A box blur as a stack of offset draws. Cheaper than a real
        // convolution and, under the dim below, indistinguishable from one.
        g.setOpacity(0.075f);
        for (int dy = -6; dy <= 6; dy += 2)
        {
            for (int dx = -6; dx <= 6; dx += 2)
            {
                if (dx == 0 && dy == 0)
                {
                    continue;
                }
                g.drawImageAt(snapshot, dx, dy, false);
            }
        }

        g.setOpacity(1.0f);
        g.restoreState();
    }

    g.setColour(dimColour);
    g.fillPath(outsidePanelMask);
}

} // namespace px3::ui
