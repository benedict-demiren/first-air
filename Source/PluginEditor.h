#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "BinaryData.h"

// ============================================================================
// Custom LookAndFeel — black background, Instrument Sans font, dark theme
// ============================================================================
class FirstAirLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FirstAirLookAndFeel()
    {
        // Load Instrument Sans from binary data (variable font, weight 400-700)
        customTypeface = juce::Typeface::createSystemTypefaceFor(
            BinaryData::InstrumentSansVariable_ttf,
            BinaryData::InstrumentSansVariable_ttfSize);

        // Dark theme colours
        setColour(juce::Slider::backgroundColourId,     juce::Colour(0xff1a1a1a));
        setColour(juce::Slider::trackColourId,           juce::Colour(0xff5588bb));
        setColour(juce::Slider::thumbColourId,           juce::Colour(0xff88bbdd));
        setColour(juce::Slider::textBoxTextColourId,     juce::Colour(0xffcccccc));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff111111));
        setColour(juce::Slider::textBoxOutlineColourId,  juce::Colour(0xff333333));

        setColour(juce::ComboBox::backgroundColourId,    juce::Colour(0xff111111));
        setColour(juce::ComboBox::textColourId,          juce::Colour(0xffcccccc));
        setColour(juce::ComboBox::outlineColourId,       juce::Colour(0xff333333));
        setColour(juce::ComboBox::arrowColourId,         juce::Colour(0xff888888));

        setColour(juce::PopupMenu::backgroundColourId,   juce::Colour(0xff111111));
        setColour(juce::PopupMenu::textColourId,         juce::Colour(0xffcccccc));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff335577));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour(juce::PopupMenu::headerTextColourId,   juce::Colour(0xff668899));

        setColour(juce::TextButton::buttonColourId,      juce::Colour(0xff222222));
        setColour(juce::TextButton::textColourOffId,     juce::Colour(0xffaaccdd));

        setColour(juce::ToggleButton::textColourId,      juce::Colour(0xffaaaaaa));
        setColour(juce::ToggleButton::tickColourId,      juce::Colour(0xff88bbdd));

        setColour(juce::Label::textColourId,             juce::Colour(0xffaaaaaa));

        setColour(juce::ScrollBar::thumbColourId,        juce::Colour(0xff444444));
        setColour(juce::ScrollBar::trackColourId,        juce::Colour(0xff0a0a0a));

        // Set default sans-serif to our custom font name
        if (customTypeface != nullptr)
            setDefaultSansSerifTypefaceName(customTypeface->getName());
    }

    // Override typeface resolution: ALL font requests get Instrument Sans
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override
    {
        if (customTypeface != nullptr)
            return customTypeface;
        return LookAndFeel_V4::getTypefaceForFont(font);
    }

    // Helper: create a Font directly from the custom typeface at given size
    juce::Font makeFont(float height, int styleFlags = 0) const
    {
        if (customTypeface != nullptr)
            return juce::Font(juce::FontOptions(customTypeface)).withHeight(height);
        return juce::Font(juce::FontOptions(height, styleFlags));
    }

    // Override component-specific font methods to ensure our typeface is used
    juce::Font getComboBoxFont(juce::ComboBox&) override { return makeFont(14.0f); }
    juce::Font getPopupMenuFont() override { return makeFont(14.0f); }
    juce::Font getLabelFont(juce::Label& label) override { return makeFont(label.getFont().getHeight()); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return makeFont(13.0f); }

    juce::Typeface::Ptr customTypeface;
};

// Clickable group header — click to collapse/expand the group
class ClickableGroupHeader : public juce::Label
{
public:
    ClickableGroupHeader(const juce::String& text, bool startCollapsed = false)
        : collapsed(startCollapsed)
    {
        updateText(text);
        setFont(juce::FontOptions(14.0f, juce::Font::bold));
        setColour(juce::Label::textColourId, juce::Colour(0xff6699bb));
        setJustificationType(juce::Justification::centredLeft);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        collapsed = !collapsed;
        updateText(baseName);
        if (onToggle) onToggle();
    }

    bool isCollapsed() const { return collapsed; }

    std::function<void()> onToggle;

private:
    void updateText(const juce::String& name)
    {
        baseName = name;
        setText((collapsed ? "> " : "v ") + name, juce::dontSendNotification);
    }

    juce::String baseName;
    bool collapsed = false;
};

// Inner component that owns all controls and sits inside the Viewport
class ContentComponent : public juce::Component
{
public:
    void recalculateHeight();
};

// ==========================================================================
// Room Visualization Component — isometric 3D wireframe
// ==========================================================================
class RoomVisualizationComponent : public juce::Component
{
public:
    // Zone pointers set by the editor from Faust param metadata
    float* roomLengthZone = nullptr;
    float* roomWidthZone  = nullptr;
    float* roomHeightZone = nullptr;
    float* roomSkewZone   = nullptr;
    float* roomCurveZone  = nullptr;
    float* srcXZone  = nullptr;
    float* srcYZone  = nullptr;
    float* srcZZone  = nullptr;
    float* lisXZone  = nullptr;
    float* lisYZone  = nullptr;
    float* lisZZone  = nullptr;
    float* energyZone = nullptr;

    void paint(juce::Graphics& g) override;

private:
    // Isometric projection: 3D (x,y,z) → 2D screen (sx, sy)
    // x = left-right, y = front-back (depth), z = up
    juce::Point<float> project(float x, float y, float z, float scale,
                               float cx, float cy) const
    {
        float sx = (x - y) * 0.866f * scale + cx;
        float sy = (x + y) * 0.5f * scale - z * scale + cy;
        return {sx, sy};
    }
};

class FirstAirEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit FirstAirEditor(FirstAirProcessor&);
    ~FirstAirEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void layoutControls();
    void presetChanged();

    FirstAirProcessor& processor;
    FirstAirLookAndFeel firstAirLnF;

    // Fixed header area
    juce::ComboBox presetSelector;
    juce::TextButton savePresetButton{"Save"};
    juce::TextButton deletePresetButton{"Del"};
    int numFactoryPresets = 0;

    // Material macro (sets all 6 walls at once)
    juce::ComboBox materialMacro;
    bool materialMacroChanging = false;  // prevent feedback loops

    // Pitch snap note display
    juce::Label pitchNoteLabel;
    float* snappedNoteZone = nullptr;   // points into Faust DSP memory
    bool hasPitchEnable = false;
    bool hasSnapEnable = false;
    juce::String pitchEnableId;
    juce::String snapEnableId;

    // Scrollable content area
    juce::Viewport viewport;
    ContentComponent contentComponent;

    // Room visualization
    RoomVisualizationComponent roomViz;

    // Auto-generated parameter controls
    struct ParamSlider {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
        std::string group;
    };

    struct ParamComboBox {
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::ComboBoxParameterAttachment> attachment;
        std::string group;
    };

    struct ParamToggle {
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<juce::ButtonParameterAttachment> attachment;
        std::string group;
    };

    // Group headers (clickable for collapse)
    std::vector<std::unique_ptr<ClickableGroupHeader>> groupHeaders;

    // Collapsed state per group name
    std::map<std::string, bool> groupCollapsed;

    // Controls in display order
    struct ControlEntry {
        enum Type { Slider, ComboBox, Toggle, GroupHeader };
        Type type;
        int index;         // index into the respective vector
        std::string group;
    };
    std::vector<ControlEntry> controlOrder;

    std::vector<ParamSlider> sliders;
    std::vector<ParamComboBox> comboBoxes;
    std::vector<ParamToggle> toggles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FirstAirEditor)
};
