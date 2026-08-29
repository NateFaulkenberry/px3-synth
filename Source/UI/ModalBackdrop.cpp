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

        // A box blur done as a stack of offset draws. Cheaper than a real
        // convolution and, at this opacity, indistinguishable from one.
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

        g.setOpacity(0.14f);
        g.drawImageAt(snapshot, 0, 0, false);
        g.setOpacity(1.0f);
        g.restoreState();
    }

    g.setColour(dimColour);
    g.fillPath(outsidePanelMask);
}

} // namespace px3::ui
