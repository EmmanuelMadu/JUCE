// The instrument's interface: a name rail with the preset selector, two rows
// of controls and a playable keyboard.  Everything is drawn in code (no image
// files), at a base size of 1040 x 600 that scales with the window.
#pragma once

#include "PluginProcessor.h"

class HearthLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    HearthLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float startAngle,
                          float endAngle, juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW,
                      int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getPopupMenuFont() override;
};

class Knob final : public juce::Component {
public:
    Knob(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label, bool bipolar = false);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Slider slider;
    int maxSize = 82;

private:
    juce::String label_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
};

class AmpSwitch final : public juce::Component {
public:
    explicit AmpSwitch(juce::AudioProcessorValueTreeState& state);
    void resized() override;

private:
    juce::TextButton buttons_[3];
    juce::ParameterAttachment attachment_;
};

class LevelMeter final : public juce::Component {
public:
    void setLevels(float l, float r);
    void paint(juce::Graphics&) override;

private:
    float l_ = 0.0f, r_ = 0.0f;
};

class Keyboard final : public juce::MidiKeyboardComponent {
public:
    explicit Keyboard(juce::MidiKeyboardState& state);
    void drawWhiteNote(int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour lineColour,
                       juce::Colour textColour) override;
    void drawBlackNote(int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour fill) override;
    juce::String getWhiteNoteText(int) override { return {}; }
};

class HearthPanel final : public juce::Component, private juce::Timer {
public:
    static constexpr int kWidth = 1040, kHeight = 600;
    explicit HearthPanel(HearthProcessor&);
    ~HearthPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void section(juce::Graphics&, juce::Rectangle<int> area, const juce::String& title) const;
    void stepPreset(int delta);

    HearthProcessor& processor_;
    HearthLookAndFeel look_;
    juce::OwnedArray<Knob> knobs_;
    AmpSwitch amp_;
    juce::ComboBox presetBox_;
    juce::TextButton prev_{"<"}, next_{">"};
    LevelMeter meter_;
    Keyboard keyboard_;
    juce::Rectangle<int> secTines_, secMaster_, secAmp_, secTrem_, secChorus_, secPhaser_, secReverb_;
};

class HearthEditor final : public juce::AudioProcessorEditor {
public:
    explicit HearthEditor(HearthProcessor&);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    HearthPanel panel_;
};
