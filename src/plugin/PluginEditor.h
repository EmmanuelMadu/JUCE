// The instrument's interface: a name rail with the preset selector, two rows
// of controls and a playable keyboard.  The first row holds the two
// instruments on two pages (TINES, GRAND), each with its own on/off light,
// the controls of touch that both share, and the master section.  Everything
// is drawn in code (no image files), at a base size of 1040 x 600 that
// scales with the window.
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

// Small light that switches an instrument on and off.
class PowerButton final : public juce::Component {
public:
    PowerButton(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title);
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool isOn() const { return on_; }

private:
    bool on_ = false;
    juce::ParameterAttachment attachment_;
};

// Name of a page in a section header.
class TabLabel final : public juce::Component {
public:
    explicit TabLabel(const juce::String& text);
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void setSelected(bool selected);
    std::function<void()> onClick;

private:
    juce::String text_;
    bool selected_ = false;
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
    void showPage(int page);            // 0 = tines, 1 = grand

private:
    using Group = std::vector<Knob*>;
    void timerCallback() override;
    void section(juce::Graphics&, juce::Rectangle<int> area, const juce::String& title) const;
    void stepPreset(int delta);
    void pageForProgram();
    void refresh();                     // what is visible and what is dimmed
    void place(juce::Rectangle<int> section, const Group& group, int leftInset = 0);

    HearthProcessor& processor_;
    HearthLookAndFeel look_;
    juce::OwnedArray<Knob> knobs_;
    Group tineKnobs_, grandKnobs_, touchKnobs_, masterKnobs_, ampKnobs_, tremKnobs_, chorusKnobs_, phaserKnobs_, reverbKnobs_;
    AmpSwitch amp_;
    PowerButton tinesPower_, grandPower_;
    TabLabel tinesTab_{"TINES"}, grandTab_{"GRAND"};
    juce::ComboBox presetBox_;
    juce::TextButton prev_{"<"}, next_{">"};
    LevelMeter meter_;
    Keyboard keyboard_;
    juce::Rectangle<int> secInst_, secTouch_, secMaster_, secAmp_, secTrem_, secChorus_, secPhaser_, secReverb_;
    int page_ = 0, grandState_ = -1, lastProgram_ = -1, retry_ = 0;
    bool tinesOn_ = true, grandOn_ = false;
};

class HearthEditor final : public juce::AudioProcessorEditor {
public:
    explicit HearthEditor(HearthProcessor&);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    HearthPanel panel_;
};
