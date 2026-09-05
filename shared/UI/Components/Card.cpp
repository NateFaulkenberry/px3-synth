#include "Card.h"

#include "UIConfigManager.h"

#include "UIConfig.h"

#include <cmath>

namespace px3::ui
{
namespace
{

// Named in config rather than numbered, because "contain" says what it does
// and "1" does not. An unrecognised name keeps the fallback rather than
// silently picking one, so a typo shows up as "my change did nothing" rather
// than as artwork mysteriously cropped.
ArtworkFit parseArtworkFit(const juce::String& name, ArtworkFit fallback)
{
    const auto text = name.trim().toLowerCase();
    if (text == "cover") { return ArtworkFit::cover; }
    if (text == "contain" || text == "fit") { return ArtworkFit::contain; }
    if (text == "stretch") { return ArtworkFit::stretch; }
    return fallback;
}

ArtworkAlign parseArtworkAlign(const juce::String& name, ArtworkAlign fallback)
{
    const auto text = name.trim().toLowerCase().removeCharacters(" -_");
    if (text == "centre" || text == "center") { return ArtworkAlign::centre; }
    if (text == "topleft") { return ArtworkAlign::topLeft; }
    if (text == "topright") { return ArtworkAlign::topRight; }
    if (text == "bottomleft") { return ArtworkAlign::bottomLeft; }
    if (text == "bottomright") { return ArtworkAlign::bottomRight; }
    if (text == "top") { return ArtworkAlign::top; }
    if (text == "bottom") { return ArtworkAlign::bottom; }
    if (text == "left") { return ArtworkAlign::left; }
    if (text == "right") { return ArtworkAlign::right; }
    return fallback;
}

// The x and y halves of JUCE's placement flags. Split into two so an alignment
// names one of each rather than nine separate constants.
int alignmentFlags(ArtworkAlign align)
{
    switch (align)
    {
        case ArtworkAlign::topLeft:     return juce::RectanglePlacement::xLeft  | juce::RectanglePlacement::yTop;
        case ArtworkAlign::topRight:    return juce::RectanglePlacement::xRight | juce::RectanglePlacement::yTop;
        case ArtworkAlign::bottomLeft:  return juce::RectanglePlacement::xLeft  | juce::RectanglePlacement::yBottom;
        case ArtworkAlign::bottomRight: return juce::RectanglePlacement::xRight | juce::RectanglePlacement::yBottom;
        case ArtworkAlign::top:         return juce::RectanglePlacement::xMid   | juce::RectanglePlacement::yTop;
        case ArtworkAlign::bottom:      return juce::RectanglePlacement::xMid   | juce::RectanglePlacement::yBottom;
        case ArtworkAlign::left:        return juce::RectanglePlacement::xLeft  | juce::RectanglePlacement::yMid;
        case ArtworkAlign::right:       return juce::RectanglePlacement::xRight | juce::RectanglePlacement::yMid;
        case ArtworkAlign::centre:
        default:                        return juce::RectanglePlacement::centred;
    }
}

// Reads a property from the defaults object, then lets the per-card object
// override it. Every getter below follows this shape, so a card's JSON only has
// to declare what differs.
struct StyleReader
{
    const UIConfig* config;
    juce::String defaultsPath;
    juce::String stylePath;

    juce::var raw(const juce::String& key) const
    {
        if (config == nullptr)
        {
            return {};
        }
        auto value = config->getValue(stylePath + "." + key);
        if (! value.isVoid())
        {
            return value;
        }
        return config->getValue(defaultsPath + "." + key);
    }

    float number(const juce::String& key, float fallback) const
    {
        const auto value = raw(key);
        return value.isVoid() ? fallback : static_cast<float>(value);
    }

    bool boolean(const juce::String& key, bool fallback) const
    {
        const auto value = raw(key);
        return value.isVoid() ? fallback : static_cast<bool>(value);
    }

    juce::Colour colour(const juce::String& key, juce::Colour fallback) const
    {
        if (config == nullptr)
        {
            return fallback;
        }
        if (! config->getValue(stylePath + "." + key).isVoid())
        {
            return config->getColour(stylePath + "." + key, fallback);
        }
        return config->getColour(defaultsPath + "." + key, fallback);
    }

    juce::String text(const juce::String& key, const juce::String& fallback) const
    {
        const auto value = raw(key);
        return value.isVoid() ? fallback : value.toString();
    }

    Fill fill(const juce::String& key, Fill fallback) const
    {
        Fill result;
        result.colour = colour(key + ".color", fallback.colour);
        result.opacity = juce::jlimit(0.0f, 1.0f, number(key + ".opacity", fallback.opacity));
        return result;
    }
};

juce::Justification parseAlign(const juce::String& text, juce::Justification fallback)
{
    const auto lower = text.trim().toLowerCase();
    if (lower == "left")   return juce::Justification::topLeft;
    if (lower == "right")  return juce::Justification::topRight;
    if (lower == "center" || lower == "centre" || lower == "centred" || lower == "centered")
        return juce::Justification::centredTop;
    return fallback;
}
} // namespace

// ---------------------------------------------------------------------------
// Dimension
// ---------------------------------------------------------------------------

Dimension Dimension::parse(const juce::var& value, Dimension fallback)
{
    if (value.isVoid())
    {
        return fallback;
    }

    if (value.isDouble() || value.isInt() || value.isInt64())
    {
        // A bare number is pixels. Negative sizes are meaningless, so they fall
        // back rather than producing an inverted rectangle.
        const auto number = static_cast<float>(value);
        if (number < 0.0f)
        {
            return fallback;
        }
        return { Unit::pixels, number };
    }

    const auto text = value.toString().trim().toLowerCase();
    if (text.isEmpty() || text == "auto")
    {
        return { Unit::automatic, 0.0f };
    }

    if (text.endsWithChar('%'))
    {
        const auto number = text.dropLastCharacters(1).getFloatValue();
        if (number < 0.0f)
        {
            return fallback;
        }
        return { Unit::percent, number };
    }

    if (text.endsWith("px"))
    {
        const auto number = text.dropLastCharacters(2).getFloatValue();
        if (number < 0.0f)
        {
            return fallback;
        }
        return { Unit::pixels, number };
    }

    // A string that is a plain number, e.g. "300".
    const auto number = text.getFloatValue();
    if (number > 0.0f || text == "0")
    {
        return { Unit::pixels, juce::jmax(0.0f, number) };
    }

    // Anything unrecognised keeps the fallback rather than collapsing the card.
    return fallback;
}

float Dimension::resolve(float reference, float available) const
{
    switch (unit)
    {
        case Unit::pixels:  return juce::jmax(0.0f, value);
        case Unit::percent: return juce::jmax(0.0f, reference * value * 0.01f);
        case Unit::automatic:
        default:            return juce::jmax(0.0f, available);
    }
}

// ---------------------------------------------------------------------------
// Insets
// ---------------------------------------------------------------------------

Insets Insets::parse(const juce::var& value, Insets fallback)
{
    if (value.isVoid())
    {
        return fallback;
    }

    if (value.isDouble() || value.isInt() || value.isInt64())
    {
        const auto all = static_cast<float>(value);
        return { all, all, all, all };
    }

    if (auto* object = value.getDynamicObject())
    {
        Insets result = fallback;
        const auto read = [object](const char* key, float current)
        {
            const auto found = object->getProperty(key);
            return found.isVoid() ? current : static_cast<float>(found);
        };
        result.top = read("top", fallback.top);
        result.right = read("right", fallback.right);
        result.bottom = read("bottom", fallback.bottom);
        result.left = read("left", fallback.left);
        return result;
    }

    return fallback;
}

juce::Rectangle<float> Insets::shrink(juce::Rectangle<float> r) const
{
    // Never let insets invert the rectangle: an over-large padding should
    // collapse the content box to nothing, not produce negative dimensions.
    const auto w = juce::jmax(0.0f, r.getWidth() - horizontal());
    const auto h = juce::jmax(0.0f, r.getHeight() - vertical());
    return { r.getX() + left, r.getY() + top, w, h };
}

// ---------------------------------------------------------------------------
// CardStyle
// ---------------------------------------------------------------------------

CardStyle CardStyle::fromConfig(const UIConfig* config,
                                const juce::String& defaultsPath,
                                const juce::String& stylePath,
                                const CardStyle& fallback)
{
    CardStyle style = fallback;
    if (config == nullptr)
    {
        return style;
    }

    const StyleReader reader { config, defaultsPath, stylePath };

    style.width = Dimension::parse(reader.raw("width"), fallback.width);
    style.height = Dimension::parse(reader.raw("height"), fallback.height);
    style.margin = Insets::parse(reader.raw("margin"), fallback.margin);
    style.padding = Insets::parse(reader.raw("padding"), fallback.padding);

    style.border.enabled = reader.boolean("border.enabled", fallback.border.enabled);
    style.border.width = juce::jmax(0.0f, reader.number("border.width", fallback.border.width));
    style.border.colour = reader.colour("border.color", fallback.border.colour);
    style.border.opacity = juce::jlimit(0.0f, 1.0f, reader.number("border.opacity", fallback.border.opacity));
    style.border.radius = juce::jmax(0.0f, reader.number("border.radius", fallback.border.radius));

    style.background = reader.fill("background", fallback.background);

    style.artwork.image = reader.text("artwork.image", fallback.artwork.image);
    style.artwork.opacity = juce::jlimit(0.0f, 1.0f,
                                         reader.number("artwork.opacity", fallback.artwork.opacity));
    style.artwork.fit = parseArtworkFit(reader.text("artwork.fit", {}), fallback.artwork.fit);
    style.artwork.align = parseArtworkAlign(reader.text("artwork.align", {}), fallback.artwork.align);

    style.shadow.colour = reader.colour("shadow.color", fallback.shadow.colour);
    style.shadow.opacity = juce::jlimit(0.0f, 1.0f, reader.number("shadow.opacity", fallback.shadow.opacity));
    style.shadow.radius = juce::jmax(0.0f, reader.number("shadow.radius", fallback.shadow.radius));
    style.shadow.offsetX = reader.number("shadow.offsetX", fallback.shadow.offsetX);
    style.shadow.offsetY = reader.number("shadow.offsetY", fallback.shadow.offsetY);

    style.gloss.margin = juce::jmax(0.0f, reader.number("gloss.margin", fallback.gloss.margin));
    style.gloss.split = juce::jlimit(0.0f, 1.0f, reader.number("gloss.split", fallback.gloss.split));
    style.gloss.topRadius = Dimension::parse(reader.raw("gloss.topRadius"), fallback.gloss.topRadius);
    style.gloss.bottomRadius = Dimension::parse(reader.raw("gloss.bottomRadius"), fallback.gloss.bottomRadius);
    style.gloss.topFill = reader.fill("gloss.topFill", fallback.gloss.topFill);
    style.gloss.bottomFill = reader.fill("gloss.bottomFill", fallback.gloss.bottomFill);

    style.title.fontSize = juce::jmax(1.0f, reader.number("title.fontSize", fallback.title.fontSize));
    style.title.colour = reader.colour("title.color", fallback.title.colour);
    style.title.align = parseAlign(reader.text("title.align", {}), fallback.title.align);
    style.title.y = reader.number("title.y", fallback.title.y);
    style.title.height = juce::jmax(1.0f, reader.number("title.height", fallback.title.height));

    style.disabled.saturation = juce::jlimit(0.0f, 1.0f,
                                             reader.number("disabled.saturation", fallback.disabled.saturation));
    style.disabled.dim = juce::jlimit(0.0f, 1.0f, reader.number("disabled.dim", fallback.disabled.dim));
    style.disabled.darken = juce::jlimit(0.0f, 1.0f, reader.number("disabled.darken", fallback.disabled.darken));

    return style;
}

CardStyle CardStyle::fromConfig(const UIConfig* config,
                                const juce::String& defaultsPath,
                                const juce::String& stylePath)
{
    return fromConfig(config, defaultsPath, stylePath, CardStyle {});
}

juce::Rectangle<float> CardStyle::resolveBounds(juce::Rectangle<float> slot,
                                                juce::Rectangle<float> panelContent) const
{
    // Margin is applied to the slot first: it is space OUTSIDE the card, so it
    // reduces what the card has to work with before any sizing happens.
    const auto marginBox = margin.shrink(slot);

    // Percentages reference the parent PANEL's content box - never the slot,
    // never the card's own bounds, never the plugin. "auto" fills the margin
    // box, which is what an unstyled card does today.
    const auto resolvedWidth = juce::jmin(width.resolve(panelContent.getWidth(), marginBox.getWidth()),
                                          marginBox.getWidth());
    const auto resolvedHeight = juce::jmin(height.resolve(panelContent.getHeight(), marginBox.getHeight()),
                                           marginBox.getHeight());

    return juce::Rectangle<float>(resolvedWidth, resolvedHeight)
               .withCentre(marginBox.getCentre());
}

namespace
{
// Desaturate and darken a picture in place, by the same numbers the card's
// colour layers use.
//
// A luminance blend on the raw bytes rather than per-pixel juce::Colour work:
// this runs over every pixel of a full-size image, and going through Colour's
// HSV conversion for each one turns a cache miss into a visible stall.
//
// The image is premultiplied, and scaling all three channels by the same factor
// leaves it premultiplied - which is why the blend is done on the stored values
// directly and alpha is not touched.
void applyTint(juce::Image& image, float saturation, float brightness)
{
    const auto sat = juce::jlimit(0.0f, 1.0f, saturation);
    const auto bri = juce::jlimit(0.0f, 1.0f, brightness);

    juce::Image::BitmapData data(image, juce::Image::BitmapData::readWrite);

    for (int y = 0; y < data.height; ++y)
    {
        auto* line = data.getLinePointer(y);

        for (int x = 0; x < data.width; ++x)
        {
            auto* pixel = line + x * data.pixelStride;

            // BGRA in memory on the formats JUCE decodes PNGs into; read by
            // offset rather than by name so the maths is the same either way.
            const auto b = static_cast<float>(pixel[0]);
            const auto g = static_cast<float>(pixel[1]);
            const auto r = static_cast<float>(pixel[2]);

            const auto luma = 0.299f * r + 0.587f * g + 0.114f * b;

            const auto mix = [sat, bri, luma](float channel)
            {
                const auto blended = luma + (channel - luma) * sat;
                return static_cast<juce::uint8>(juce::jlimit(0.0f, 255.0f, blended * bri));
            };

            pixel[0] = mix(b);
            pixel[1] = mix(g);
            pixel[2] = mix(r);
        }
    }
}
} // namespace

CardStyle CardStyle::disabledVariant() const
{
    CardStyle result = *this;
    const auto saturation = juce::jlimit(0.0f, 1.0f, disabled.saturation);
    const auto dim = juce::jlimit(0.0f, 1.0f, disabled.dim);
    const auto darken = juce::jlimit(0.0f, 1.0f, disabled.darken);

    // Saturation is removed from every layer, not just the border and title.
    // Leaving the background or the gloss coloured made a bypassed card still
    // read as "blue" or "orange", which defeats the point of greying it out.
    const auto grey = [saturation, darken](juce::Colour c)
    {
        return c.withSaturation(saturation)
                .withBrightness(juce::jlimit(0.0f, 1.0f, c.getBrightness() * (1.0f - darken)));
    };

    result.border.colour = grey(result.border.colour);
    result.border.opacity *= dim;
    result.background.colour = grey(result.background.colour);
    result.background.opacity *= dim;
    // Artwork greys with everything else. A bypassed card that keeps a full
    // colour picture behind grey controls does not read as bypassed - and on a
    // card whose whole face is a photograph, the picture is what the eye reads
    // first, so dimming it alone was not enough.
    result.artwork.opacity *= dim;
    result.artwork.saturation = saturation;
    result.artwork.brightness = 1.0f - darken;

    result.gloss.topFill.colour = grey(result.gloss.topFill.colour);
    result.gloss.topFill.opacity *= dim;
    result.gloss.bottomFill.colour = grey(result.gloss.bottomFill.colour);
    result.gloss.bottomFill.opacity *= dim;
    // The title carries its alpha in the colour, so it is dimmed there.
    result.title.colour = grey(result.title.colour).withMultipliedAlpha(dim);

    return result;
}

juce::Rectangle<float> CardStyle::contentBounds(juce::Rectangle<float> cardBounds) const
{
    return padding.shrink(cardBounds);
}

// ---------------------------------------------------------------------------
// PanelStyle
// ---------------------------------------------------------------------------

PanelStyle PanelStyle::fromConfig(const UIConfig* config,
                                  const juce::String& path,
                                  const PanelStyle& fallback)
{
    PanelStyle style = fallback;
    if (config == nullptr)
    {
        return style;
    }

    const auto height = config->getValue(path + ".height");
    if (! height.isVoid())
    {
        style.height = juce::jmax(0, static_cast<int>(height));
    }

    const auto overflow = config->getValue(path + ".overflowY");
    if (! overflow.isVoid())
    {
        const auto text = overflow.toString().trim().toLowerCase();
        // "auto" and "scroll" both scroll; "hidden" and anything unrecognised
        // do not, so a typo cannot silently turn scrolling on.
        style.scrollVertically = (text == "auto" || text == "scroll");
    }

    return style;
}

PanelStyle PanelStyle::fromConfig(const UIConfig* config, const juce::String& path)
{
    return fromConfig(config, path, PanelStyle {});
}

// ---------------------------------------------------------------------------
// CardStyleCache
// ---------------------------------------------------------------------------

void CardStyleCache::setKeys(juce::String defaultsPathIn, juce::String stylePathIn)
{
    if (defaultsPath != defaultsPathIn || stylePath != stylePathIn)
    {
        defaultsPath = std::move(defaultsPathIn);
        stylePath = std::move(stylePathIn);
        hasParsed = false;
    }
}

void CardStyleCache::setConfig(std::shared_ptr<const UIConfig> configIn)
{
    config = std::move(configIn);
    // Do not compare pointers here and skip: the same object could in principle
    // be handed back after a failed reload, and re-parsing is cheap.
    hasParsed = false;
}

const CardStyle& CardStyleCache::style() const
{
    if (! hasParsed || parsedFrom != config.get() || parsedStylePath != stylePath)
    {
        cached = CardStyle::fromConfig(config.get(), defaultsPath, stylePath);
        parsedFrom = config.get();
        parsedStylePath = stylePath;
        hasParsed = true;
    }
    return cached;
}

// ---------------------------------------------------------------------------
// CardHost
// ---------------------------------------------------------------------------

void CardHost::setStyleKey(const juce::String& key)
{
    cache.setKeys("cards.defaults", "cards." + key);
    layout(lastComponentBounds);
}

void CardHost::setConfig(std::shared_ptr<const UIConfig> config)
{
    cache.setConfig(std::move(config));
    // Re-resolve immediately: the box comes from the style, so a reload that
    // only refreshed colours would draw them at the previous geometry.
    layout(lastComponentBounds);
}

void CardHost::setPanelContentBounds(juce::Rectangle<int> panelContent)
{
    panelContentBounds = panelContent;
    layout(lastComponentBounds);
}

void CardHost::layout(juce::Rectangle<int> componentBounds)
{
    lastComponentBounds = componentBounds;
    const auto slot = componentBounds.toFloat();
    const auto panel = panelContentBounds.isEmpty() ? slot : panelContentBounds.toFloat();
    cardBounds = cache.style().resolveBounds(slot, panel);
}

juce::Rectangle<int> CardHost::contentBelowTitle() const
{
    const auto& s = cache.style();
    auto area = content().toNearestInt();
    area.removeFromTop(static_cast<int>(std::ceil(s.title.height + s.title.y)));
    return area;
}

void CardHost::draw(juce::Graphics& g, const juce::String& title) const
{
    drawCard(g, cardBounds, cache.style(), title);
}

void CardHost::drawInactive(juce::Graphics& g, const juce::String& title) const
{
    drawCard(g, cardBounds, cache.style().disabledVariant(), title);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void drawCard(juce::Graphics& g,
              juce::Rectangle<float> cardBounds,
              const CardStyle& style,
              const juce::String& title)
{
    if (cardBounds.getWidth() <= 0.0f || cardBounds.getHeight() <= 0.0f)
    {
        return;
    }

    const auto radius = style.border.radius;

    // 0. Shadow, behind the card entirely.
    //
    // DropShadow takes an integer radius and refuses anything below one, so a
    // configured radius that rounds to zero is treated as no shadow rather
    // than as a hard black copy of the card offset by a few pixels.
    if (style.shadow.opacity > 0.0f && style.shadow.radius >= 0.5f)
    {
        juce::Path shape;
        shape.addRoundedRectangle(cardBounds, radius);

        const juce::DropShadow shadow(style.shadow.colour.withMultipliedAlpha(style.shadow.opacity),
                                      juce::roundToInt(style.shadow.radius),
                                      { juce::roundToInt(style.shadow.offsetX),
                                        juce::roundToInt(style.shadow.offsetY) });
        shadow.drawForPath(g, shape);
    }

    // 1. Background, behind everything.
    if (style.background.opacity > 0.0f)
    {
        g.setColour(style.background.effective());
        g.fillRoundedRectangle(cardBounds, radius);
    }

    // 1b. Artwork, over the background and UNDER the gloss, so the two gloss
    //     fills tint it the way they tint the background rather than covering
    //     it. Clipped to the card's rounded rectangle so it cannot square off
    //     the corners the border is about to draw.
    if (style.artwork.image.isNotEmpty() && style.artwork.opacity > 0.0f)
    {
        // Cached, but keyed on the file's CONTENT IDENTITY rather than its path.
        //
        // ImageCache::getFromFile hashes the path alone, so a PNG replaced on
        // disk is never noticed: the old picture is served for as long as the
        // process lives, which for a plug-in means until the host unloads it.
        // Replacing artwork and seeing no change is the whole point of having
        // it in a directory, so the modification time and size go into the key
        // and a changed file misses the cache exactly once.
        const auto file = UIConfigManager::findArtworkFile(style.artwork.image);
        juce::Image image;

        if (file.existsAsFile())
        {
            // The grey version is a different picture as far as the cache is
            // concerned, so both live in it and neither is recomputed per
            // frame - which matters, because desaturating two million pixels
            // is not something to do while painting.
            const auto tint = juce::roundToInt(juce::jlimit(0.0f, 1.0f, style.artwork.saturation) * 1000.0f)
                            + juce::roundToInt(juce::jlimit(0.0f, 1.0f, style.artwork.brightness) * 1000.0f) * 1009;

            const auto key = file.getFullPathName().hashCode64()
                           ^ (file.getLastModificationTime().toMilliseconds() * 31)
                           ^ (file.getSize() * 131)
                           ^ (static_cast<juce::int64>(tint) * 1000003);

            image = juce::ImageCache::getFromHashCode(key);

            if (image.isNull())
            {
                image = juce::ImageFileFormat::loadFrom(file);

                if (image.isValid()
                        && (style.artwork.saturation < 0.999f || style.artwork.brightness < 0.999f))
                {
                    image = image.createCopy();
                    applyTint(image, style.artwork.saturation, style.artwork.brightness);
                }

                if (image.isValid()) { juce::ImageCache::addImageToCache(image, key); }
            }
        }

        if (image.isValid())
        {
            juce::Graphics::ScopedSaveState clip(g);

            juce::Path shape;
            shape.addRoundedRectangle(cardBounds, radius);
            g.reduceClipRegion(shape);

            // The rectangle overload rather than the nine-argument one: that
            // takes ints for the destination, so every float here was converted
            // implicitly - four warnings, and a policy that fails the build on
            // them.
            //
            // The alignment flags on their own are JUCE's "contain": they
            // scale by whichever axis needs LESS, so the whole picture is
            // inside the card and the background shows through wherever the
            // aspect ratios differ. Adding fillDestination flips it to scaling
            // by whichever axis needs more, cropping the rest - which the clip
            // above contains. stretchToFit scales the two axes independently,
            // filling the card with the whole picture at the cost of distorting
            // it, and leaves the alignment with nothing to decide.
            auto flags = alignmentFlags(style.artwork.align);
            if (style.artwork.fit == ArtworkFit::cover)
            {
                flags |= juce::RectanglePlacement::fillDestination;
            }
            else if (style.artwork.fit == ArtworkFit::stretch)
            {
                flags |= juce::RectanglePlacement::stretchToFit;
            }

            g.setOpacity(style.artwork.opacity);
            g.drawImage(image, cardBounds, juce::RectanglePlacement(flags));
            g.setOpacity(1.0f);
        }
    }

    // 2. Gloss, inset by its own margin so a gap shows between it and the
    //    border. Two fills split at gloss.split.
    const auto glossBox = cardBounds.reduced(style.gloss.margin);
    if (glossBox.getWidth() > 0.0f && glossBox.getHeight() > 0.0f)
    {
        // "auto" keeps the gloss concentric with the border: the card's radius
        // less the gloss margin. Either fill can override it.
        const auto autoRadius = juce::jmax(0.0f, radius - style.gloss.margin);
        const auto splitY = glossBox.getY() + glossBox.getHeight() * style.gloss.split;

        // A radius larger than half the shorter side would fold the corner
        // back on itself, so each is capped at what the fill can actually take.
        const auto resolveRadius = [autoRadius](const Dimension& dim, juce::Rectangle<float> fill)
        {
            const auto shorter = juce::jmin(fill.getWidth(), fill.getHeight());
            const auto value = dim.isAuto() ? autoRadius : dim.resolve(shorter, autoRadius);
            return juce::jlimit(0.0f, juce::jmax(0.0f, shorter * 0.5f), value);
        };

        if (style.gloss.topFill.opacity > 0.0f && splitY > glossBox.getY())
        {
            const auto top = glossBox.withBottom(splitY);
            const auto r = resolveRadius(style.gloss.topRadius, top);
            juce::Path path;
            path.addRoundedRectangle(top.getX(), top.getY(), top.getWidth(), top.getHeight(),
                                     r, r, true, true, false, false);
            g.setColour(style.gloss.topFill.effective());
            g.fillPath(path);
        }

        if (style.gloss.bottomFill.opacity > 0.0f && splitY < glossBox.getBottom())
        {
            const auto bottom = glossBox.withTop(splitY);
            const auto r = resolveRadius(style.gloss.bottomRadius, bottom);
            juce::Path path;
            path.addRoundedRectangle(bottom.getX(), bottom.getY(), bottom.getWidth(), bottom.getHeight(),
                                     r, r, false, false, true, true);
            g.setColour(style.gloss.bottomFill.effective());
            g.fillPath(path);
        }
    }

    // 3. Border, on top of both layers so it stays crisp.
    if (style.border.enabled && style.border.width > 0.0f && style.border.opacity > 0.0f)
    {
        g.setColour(style.border.colour.withAlpha(style.border.opacity));
        g.drawRoundedRectangle(cardBounds, radius, style.border.width);
    }

    // 4. Title, positioned inside the padding box and offset by title.y.
    //    Drawing it here - rather than in the parent panel, which is where it
    //    used to happen - means it belongs to the card and cannot outlive it.
    if (title.isNotEmpty())
    {
        const auto contentBox = style.contentBounds(cardBounds);
        const auto titleBox = juce::Rectangle<float>(contentBox.getX(),
                                                     contentBox.getY() + style.title.y,
                                                     contentBox.getWidth(),
                                                     style.title.height);
        g.setColour(style.title.colour);
        g.setFont(juce::FontOptions(style.title.fontSize, juce::Font::bold));
        g.drawText(title, titleBox, style.title.align, true);
    }
}

} // namespace px3::ui
