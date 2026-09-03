#include "UIConfig.h"

#include <cmath>

namespace
{
juce::var ensureObject(const juce::var& maybeObject)
{
    if (auto* dyn = maybeObject.getDynamicObject())
    {
        juce::ignoreUnused(dyn);
        return maybeObject;
    }

    return juce::var(new juce::DynamicObject());
}

bool isHexDigit(juce::juce_wchar c)
{
    return (c >= '0' && c <= '9')
           || (c >= 'a' && c <= 'f')
           || (c >= 'A' && c <= 'F');
}

int parseHexByte(const juce::String& text, int start)
{
    const auto hi = juce::CharacterFunctions::getHexDigitValue(text[start]);
    const auto lo = juce::CharacterFunctions::getHexDigitValue(text[start + 1]);
    return (hi << 4) | lo;
}
}

std::shared_ptr<UIConfig> UIConfig::fromJsonText(const juce::String& jsonText, juce::String& error)
{
    error.clear();

    juce::var parsed;
    const auto parseResult = juce::JSON::parse(jsonText, parsed);
    if (parseResult.failed())
    {
        error = "Invalid UIConfig JSON: " + parseResult.getErrorMessage();
        return nullptr;
    }

    if (parsed.isVoid() || parsed.isUndefined())
    {
        error = "UIConfig JSON parsed to empty value.";
        return nullptr;
    }

    if (parsed.getDynamicObject() == nullptr)
    {
        error = "UIConfig root must be a JSON object.";
        return nullptr;
    }

    auto warning = juce::String();
    if (lookupPath(parsed, "editor").getDynamicObject() == nullptr)
    {
        warning = "UIConfig missing 'editor' section; using runtime fallbacks.";
    }

    return std::shared_ptr<UIConfig>(new UIConfig(parsed, warning));
}

UIConfig::UIConfig(juce::var rootIn, juce::String warningIn)
    : root(std::move(rootIn)),
      palette(lookupPath(root, "palette")),
      validationWarning(std::move(warningIn))
{
}

bool UIConfig::isValid() const
{
    return root.getDynamicObject() != nullptr;
}

juce::String UIConfig::getValidationWarning() const
{
    return validationWarning;
}

juce::var UIConfig::lookupPath(const juce::var& source, const juce::String& path)
{
    if (path.trim().isEmpty())
    {
        return source;
    }

    auto current = source;
    juce::StringArray segments;
    segments.addTokens(path, ".", "");
    segments.removeEmptyStrings();

    for (const auto& segment : segments)
    {
        auto* obj = current.getDynamicObject();
        if (obj == nullptr)
        {
            return {};
        }

        if (!obj->hasProperty(segment))
        {
            return {};
        }

        current = obj->getProperty(segment);
    }

    return current;
}

juce::var UIConfig::getValue(const juce::String& path) const
{
    return lookupPath(root, path);
}

juce::String UIConfig::getString(const juce::String& path, const juce::String& fallback) const
{
    const auto value = getValue(path);
    if (value.isString())
    {
        return value.toString();
    }
    return fallback;
}

bool UIConfig::getBool(const juce::String& path, bool fallback) const
{
    const auto value = getValue(path);
    if (value.isBool() || value.isInt() || value.isInt64() || value.isDouble())
    {
        return static_cast<bool>(value);
    }

    if (value.isString())
    {
        const auto normalized = value.toString().trim().toLowerCase();
        if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on")
        {
            return true;
        }
        if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off")
        {
            return false;
        }
    }

    return fallback;
}

int UIConfig::getInt(const juce::String& path, int fallback) const
{
    const auto value = getValue(path);
    if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
    {
        return static_cast<int>(value);
    }

    if (value.isString())
    {
        return value.toString().getIntValue();
    }

    return fallback;
}

float UIConfig::getFloat(const juce::String& path, float fallback) const
{
    const auto value = getValue(path);
    if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
    {
        return static_cast<float>(static_cast<double>(value));
    }

    if (value.isString())
    {
        return value.toString().getFloatValue();
    }

    return fallback;
}

juce::Colour UIConfig::parseHexColour(const juce::String& text, juce::Colour fallback)
{
    const auto trimmed = text.trim();
    if (!trimmed.startsWithChar('#'))
    {
        return fallback;
    }

    const auto hex = trimmed.substring(1);
    const auto len = hex.length();
    if (!(len == 6 || len == 8))
    {
        return fallback;
    }

    for (int i = 0; i < len; ++i)
    {
        if (!isHexDigit(hex[i]))
        {
            return fallback;
        }
    }

    const auto r = parseHexByte(hex, 0);
    const auto g = parseHexByte(hex, 2);
    const auto b = parseHexByte(hex, 4);
    const auto a = len == 8 ? parseHexByte(hex, 6) : 255;
    return juce::Colour::fromRGBA(static_cast<juce::uint8>(r),
                                  static_cast<juce::uint8>(g),
                                  static_cast<juce::uint8>(b),
                                  static_cast<juce::uint8>(a));
}

juce::Colour UIConfig::parseColourValue(const juce::var& value,
                                        const juce::var& paletteObject,
                                        juce::Colour fallback)
{
    if (value.isVoid() || value.isUndefined())
    {
        return fallback;
    }

    if (value.isString())
    {
        const auto text = value.toString().trim();
        if (text.startsWithChar('#'))
        {
            return parseHexColour(text, fallback);
        }

        if (text.startsWithChar('$'))
        {
            const auto key = text.substring(1);
            if (auto* obj = paletteObject.getDynamicObject(); obj != nullptr && obj->hasProperty(key))
            {
                return parseColourValue(obj->getProperty(key), paletteObject, fallback);
            }
            return fallback;
        }
    }

    if (auto* obj = value.getDynamicObject())
    {
        const auto rr = static_cast<int>(obj->getProperty("r"));
        const auto gg = static_cast<int>(obj->getProperty("g"));
        const auto bb = static_cast<int>(obj->getProperty("b"));
        const auto aa = obj->hasProperty("a") ? static_cast<int>(obj->getProperty("a")) : 255;

        return juce::Colour::fromRGBA(static_cast<juce::uint8>(juce::jlimit(0, 255, rr)),
                                      static_cast<juce::uint8>(juce::jlimit(0, 255, gg)),
                                      static_cast<juce::uint8>(juce::jlimit(0, 255, bb)),
                                      static_cast<juce::uint8>(juce::jlimit(0, 255, aa)));
    }

    return fallback;
}

juce::Colour UIConfig::getColour(const juce::String& path, juce::Colour fallback) const
{
    return parseColourValue(getValue(path), palette, fallback);
}

juce::var UIConfig::getObject(const juce::String& path) const
{
    return ensureObject(getValue(path));
}

juce::var UIConfig::mergeObjects(const juce::var& baseObject, const juce::var& overrideObject)
{
    auto result = ensureObject(baseObject);
    auto* resultObj = result.getDynamicObject();
    auto* overrideDyn = overrideObject.getDynamicObject();
    if (resultObj == nullptr || overrideDyn == nullptr)
    {
        return result;
    }

    for (const auto& property : overrideDyn->getProperties())
    {
        const auto key = property.name;
        const auto value = property.value;
        if (value.getDynamicObject() != nullptr)
        {
            const auto mergedChild = mergeObjects(resultObj->hasProperty(key) ? resultObj->getProperty(key)
                                                                               : juce::var(new juce::DynamicObject()),
                                                  value);
            resultObj->setProperty(key, mergedChild);
        }
        else
        {
            resultObj->setProperty(key, value);
        }
    }

    return result;
}

juce::var UIConfig::mergedObject(const juce::String& defaultsPath, const juce::String& overridePath) const
{
    return mergeObjects(getObject(defaultsPath), getObject(overridePath));
}

juce::Rectangle<int> UIConfig::getRect(const juce::String& path,
                                       const juce::Rectangle<int>& parent,
                                       const juce::Rectangle<int>& fallback) const
{
    const auto object = getObject(path);
    auto* obj = object.getDynamicObject();
    if (obj == nullptr)
    {
        return fallback;
    }

    auto out = fallback;
    const auto width = obj->hasProperty("width") ? static_cast<int>(obj->getProperty("width")) : out.getWidth();
    const auto height = obj->hasProperty("height") ? static_cast<int>(obj->getProperty("height")) : out.getHeight();
    const auto x = obj->hasProperty("x") ? static_cast<int>(obj->getProperty("x")) : out.getX();
    const auto y = obj->hasProperty("y") ? static_cast<int>(obj->getProperty("y")) : out.getY();
    out = { x, y, juce::jmax(1, width), juce::jmax(1, height) };

    if (obj->hasProperty("relativeX"))
    {
        out.setX(parent.getX() + static_cast<int>(std::round(static_cast<double>(obj->getProperty("relativeX")) * parent.getWidth())));
    }
    if (obj->hasProperty("relativeY"))
    {
        out.setY(parent.getY() + static_cast<int>(std::round(static_cast<double>(obj->getProperty("relativeY")) * parent.getHeight())));
    }
    if (obj->hasProperty("relativeWidth"))
    {
        out.setWidth(juce::jmax(1, static_cast<int>(std::round(static_cast<double>(obj->getProperty("relativeWidth")) * parent.getWidth()))));
    }
    if (obj->hasProperty("relativeHeight"))
    {
        out.setHeight(juce::jmax(1, static_cast<int>(std::round(static_cast<double>(obj->getProperty("relativeHeight")) * parent.getHeight()))));
    }

    return out;
}

juce::Justification UIConfig::parseJustification(const juce::String& text, juce::Justification fallback)
{
    const auto key = text.trim().toLowerCase();
    if (key == "centred" || key == "center") return juce::Justification::centred;
    if (key == "left") return juce::Justification::centredLeft;
    if (key == "right") return juce::Justification::centredRight;
    if (key == "top") return juce::Justification::centredTop;
    if (key == "bottom") return juce::Justification::centredBottom;
    if (key == "topleft") return juce::Justification::topLeft;
    if (key == "topright") return juce::Justification::topRight;
    if (key == "bottomleft") return juce::Justification::bottomLeft;
    if (key == "bottomright") return juce::Justification::bottomRight;
    return fallback;
}

juce::Font UIConfig::parseFont(const juce::var& style,
                               const juce::String& familyFallback,
                               float sizeFallback,
                               bool boldFallback,
                               bool italicFallback)
{
    auto family = familyFallback;
    auto size = sizeFallback;
    auto bold = boldFallback;
    auto italic = italicFallback;

    if (auto* obj = style.getDynamicObject())
    {
        if (obj->hasProperty("fontFamily")) family = obj->getProperty("fontFamily").toString();
        if (obj->hasProperty("fontSize")) size = static_cast<float>(static_cast<double>(obj->getProperty("fontSize")));
        if (obj->hasProperty("bold")) bold = static_cast<bool>(obj->getProperty("bold"));
        if (obj->hasProperty("italic")) italic = static_cast<bool>(obj->getProperty("italic"));
    }

    auto font = juce::Font(juce::FontOptions(size));
    if (family.isNotEmpty())
    {
        font.setTypefaceName(family);
    }
    if (bold)
    {
        font = font.boldened();
    }
    if (italic)
    {
        font = font.italicised();
    }
    return font;
}

void UIConfig::applyTextButtonStyle(const juce::var& style, juce::TextButton& button) const
{
    if (auto* obj = style.getDynamicObject())
    {
        if (obj->hasProperty("text"))
        {
            button.setButtonText(obj->getProperty("text").toString());
        }

        if (obj->hasProperty("buttonColour"))
        {
            button.setColour(juce::TextButton::buttonColourId,
                             parseColourValue(obj->getProperty("buttonColour"), palette, button.findColour(juce::TextButton::buttonColourId)));
        }

        if (obj->hasProperty("buttonOnColour"))
        {
            button.setColour(juce::TextButton::buttonOnColourId,
                             parseColourValue(obj->getProperty("buttonOnColour"), palette, button.findColour(juce::TextButton::buttonOnColourId)));
        }

        if (obj->hasProperty("textColour"))
        {
            button.setColour(juce::TextButton::textColourOffId,
                             parseColourValue(obj->getProperty("textColour"), palette, button.findColour(juce::TextButton::textColourOffId)));
        }

        if (obj->hasProperty("textOnColour"))
        {
            button.setColour(juce::TextButton::textColourOnId,
                             parseColourValue(obj->getProperty("textOnColour"), palette, button.findColour(juce::TextButton::textColourOnId)));
        }
    }
}

void UIConfig::applyComboStyle(const juce::var& style, juce::ComboBox& comboBox) const
{
    if (auto* obj = style.getDynamicObject())
    {
        if (obj->hasProperty("backgroundColour"))
        {
            comboBox.setColour(juce::ComboBox::backgroundColourId,
                               parseColourValue(obj->getProperty("backgroundColour"), palette, comboBox.findColour(juce::ComboBox::backgroundColourId)));
        }

        if (obj->hasProperty("textColour"))
        {
            comboBox.setColour(juce::ComboBox::textColourId,
                               parseColourValue(obj->getProperty("textColour"), palette, comboBox.findColour(juce::ComboBox::textColourId)));
        }

        if (obj->hasProperty("outlineColour"))
        {
            comboBox.setColour(juce::ComboBox::outlineColourId,
                               parseColourValue(obj->getProperty("outlineColour"), palette, comboBox.findColour(juce::ComboBox::outlineColourId)));
        }
    }
}
