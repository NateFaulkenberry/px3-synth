#pragma once

#include <JuceHeader.h>

#include <memory>

class UIConfig final
{
public:
    static std::shared_ptr<UIConfig> fromJsonText(const juce::String& jsonText, juce::String& error);

    bool isValid() const;
    juce::String getValidationWarning() const;

    juce::var getValue(const juce::String& path) const;
    juce::String getString(const juce::String& path, const juce::String& fallback) const;
    bool getBool(const juce::String& path, bool fallback) const;
    int getInt(const juce::String& path, int fallback) const;
    float getFloat(const juce::String& path, float fallback) const;
    juce::Colour getColour(const juce::String& path, juce::Colour fallback) const;

    juce::var getObject(const juce::String& path) const;
    juce::var mergedObject(const juce::String& defaultsPath, const juce::String& overridePath) const;

    juce::Rectangle<int> getRect(const juce::String& path,
                                 const juce::Rectangle<int>& parent,
                                 const juce::Rectangle<int>& fallback) const;

    void applyLabelStyle(const juce::var& style, juce::Label& label) const;
    void applyTextButtonStyle(const juce::var& style, juce::TextButton& button) const;
    void applyToggleStyle(const juce::var& style, juce::ToggleButton& button) const;
    void applyComboStyle(const juce::var& style, juce::ComboBox& comboBox) const;
    void applySliderStyle(const juce::var& style, juce::Slider& slider) const;

private:
    explicit UIConfig(juce::var rootIn, juce::String warningIn);

    static juce::Colour parseColourValue(const juce::var& value,
                                         const juce::var& palette,
                                         juce::Colour fallback);
    static juce::Colour parseHexColour(const juce::String& text, juce::Colour fallback);
    static juce::Justification parseJustification(const juce::String& text, juce::Justification fallback);
    static juce::Font parseFont(const juce::var& style,
                                const juce::String& familyFallback,
                                float sizeFallback,
                                bool boldFallback,
                                bool italicFallback);
    static juce::var lookupPath(const juce::var& root, const juce::String& path);
    static juce::var mergeObjects(const juce::var& baseObject, const juce::var& overrideObject);

    juce::var root;
    juce::var palette;
    juce::String validationWarning;
};
