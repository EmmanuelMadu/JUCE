#include "PluginEditor.h"

namespace {
// Palette: warm charcoal panel, cream lettering, amber for anything that shows a value.
const juce::Colour kBackTop{0xff231c18}, kBackBottom{0xff17120f};
const juce::Colour kSection{0xff1b1512}, kSectionEdge{0xff3a2f28};
const juce::Colour kCream{0xfff2e8d9}, kMuted{0xffb3a391}, kDim{0xff7c6e60};
const juce::Colour kAmber{0xffe2a35a}, kEmber{0xffd9633a};
const juce::Colour kRailTop{0xffd3ccbf}, kRailBottom{0xffa59d90}, kRailInk{0xff2a221e};
const juce::Colour kTrack{0xff3b302a};

juce::Font font(float height, bool bold = false, float tracking = 0.0f) {
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor(tracking);
}
}  // namespace

// ------------------------------------------------------------ look and feel ----
HearthLookAndFeel::HearthLookAndFeel() {
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff241d19));
    setColour(juce::PopupMenu::textColourId, kCream);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kAmber);
    setColour(juce::PopupMenu::highlightedTextColourId, kRailInk);
    setColour(juce::ComboBox::textColourId, kRailInk);
    setColour(juce::Label::textColourId, kRailInk);
}

void HearthLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float pos, float startAngle,
                                         float endAngle, juce::Slider& slider) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float arcR = size * 0.5f - 3.0f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool active = slider.isMouseOverOrDragging();

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour(kTrack);
    g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = static_cast<bool>(slider.getProperties().getWithDefault("bipolar", false));
    const float from = bipolar ? 0.5f * (startAngle + endAngle) : startAngle;
    if (std::abs(angle - from) > 0.01f) {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
        g.setColour(active ? kAmber.brighter(0.15f) : kAmber);
        g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float bodyR = arcR - 7.0f;
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillEllipse(centre.x - bodyR, centre.y - bodyR + 2.0f, bodyR * 2.0f, bodyR * 2.0f);
    juce::ColourGradient body(juce::Colour(active ? 0xff4a3e37 : 0xff41362f), centre.x, centre.y - bodyR,
                              juce::Colour(0xff1f1916), centre.x, centre.y + bodyR, false);
    g.setGradientFill(body);
    g.fillEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour(juce::Colour(0xff5a4b41));
    g.drawEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle(-1.4f, -bodyR + 3.5f, 2.8f, bodyR * 0.48f, 1.4f);
    g.setColour(kCream);
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
}

void HearthLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) {
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    g.setColour(juce::Colour(0xfff6f0e4).withAlpha(box.isMouseOver(true) ? 0.95f : 0.8f));
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(kRailInk.withAlpha(0.35f));
    g.drawRoundedRectangle(r, 5.0f, 1.0f);
    juce::Path arrow;
    const float ax = static_cast<float>(width) - 18.0f, ay = static_cast<float>(height) * 0.5f;
    arrow.startNewSubPath(ax - 4.5f, ay - 2.0f);
    arrow.lineTo(ax, ay + 2.5f);
    arrow.lineTo(ax + 4.5f, ay - 2.0f);
    g.setColour(kRailInk);
    g.strokePath(arrow, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font HearthLookAndFeel::getComboBoxFont(juce::ComboBox&) { return font(15.0f, true); }
juce::Font HearthLookAndFeel::getPopupMenuFont() { return font(14.5f); }

void HearthLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(10, 0, box.getWidth() - 36, box.getHeight());
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

void HearthLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down) {
    const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool onRail = static_cast<bool>(b.getProperties().getWithDefault("rail", false));
    if (onRail) {
        g.setColour(juce::Colour(0xfff6f0e4).withAlpha(down ? 1.0f : (highlighted ? 0.95f : 0.8f)));
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(kRailInk.withAlpha(0.35f));
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
        return;
    }
    const bool on = b.getToggleState();
    g.setColour(on ? kAmber : (highlighted ? juce::Colour(0xff342a25) : juce::Colour(0xff2a221e)));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(on ? kAmber.darker(0.3f) : kSectionEdge);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
}

void HearthLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    const bool onRail = static_cast<bool>(b.getProperties().getWithDefault("rail", false));
    g.setFont(onRail ? font(16.0f, true) : font(10.5f, true, 0.08f));
    g.setColour(onRail || b.getToggleState() ? kRailInk : kMuted);
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

// --------------------------------------------------------------------- knob ----
Knob::Knob(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label, bool bipolar)
    : label_(label) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.getProperties().set("bipolar", bipolar);
    slider.setMouseDragSensitivity(180);
    addAndMakeVisible(slider);
    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, paramId, slider);
    if (auto* p = state.getParameter(paramId))
        slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    slider.setTitle(label);
}

void Knob::resized() {
    auto area = getLocalBounds();
    area.removeFromBottom(30);
    const int size = juce::jmin(area.getWidth(), area.getHeight(), maxSize);
    slider.setBounds(area.withSizeKeepingCentre(size, size).withBottomY(area.getBottom() - 2));
}

void Knob::paint(juce::Graphics& g) {
    auto area = getLocalBounds();
    auto text = area.removeFromBottom(30);
    g.setColour(kCream);
    g.setFont(font(12.0f, true, 0.06f));
    g.drawFittedText(label_.toUpperCase(), text.removeFromTop(16), juce::Justification::centred, 1, 0.8f);
    g.setColour(slider.isMouseOverOrDragging() ? kAmber : kDim);
    g.setFont(font(11.0f));
    g.drawText(slider.getTextFromValue(slider.getValue()), text, juce::Justification::centredTop, false);
}

// --------------------------------------------------------------- amp switch ----
AmpSwitch::AmpSwitch(juce::AudioProcessorValueTreeState& state)
    : attachment_(*state.getParameter("amp"), [this](float v) {
          const int index = juce::roundToInt(v);
          for (int i = 0; i < 3; ++i) buttons_[i].setToggleState(i == index, juce::dontSendNotification);
      }) {
    static const char* names[3] = {"DIRECT", "SUITCASE", "COMBO"};
    for (int i = 0; i < 3; ++i) {
        buttons_[i].setButtonText(names[i]);
        buttons_[i].setClickingTogglesState(false);
        buttons_[i].onClick = [this, i] { attachment_.setValueAsCompleteGesture(static_cast<float>(i)); };
        addAndMakeVisible(buttons_[i]);
    }
    attachment_.sendInitialUpdate();
}

void AmpSwitch::resized() {
    auto area = getLocalBounds();
    const int h = (area.getHeight() - 8) / 3;
    for (int i = 0; i < 3; ++i) {
        buttons_[i].setBounds(area.removeFromTop(h));
        area.removeFromTop(4);
    }
}

// ------------------------------------------------------- page tabs and lights ----
PowerButton::PowerButton(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title)
    : attachment_(*state.getParameter(paramId), [this](float v) {
          on_ = v > 0.5f;
          repaint();
      }) {
    setTitle(title);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    attachment_.sendInitialUpdate();
}

void PowerButton::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat().withSizeKeepingCentre(11.0f, 11.0f);
    if (on_) {
        g.setColour(kAmber.withAlpha(0.25f));
        g.fillEllipse(r.expanded(3.0f));
    }
    g.setColour(on_ ? kAmber : juce::Colour(0xff2a221e));
    g.fillEllipse(r);
    g.setColour(on_ ? kAmber.brighter(0.4f) : kDim);
    g.drawEllipse(r, 1.2f);
}

void PowerButton::mouseUp(const juce::MouseEvent& e) {
    if (getLocalBounds().contains(e.getPosition())) attachment_.setValueAsCompleteGesture(on_ ? 0.0f : 1.0f);
}

TabLabel::TabLabel(const juce::String& text) : text_(text) {
    setTitle(text);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void TabLabel::paint(juce::Graphics& g) {
    g.setColour(selected_ ? kAmber : (isMouseOver() ? kMuted : kDim));
    g.setFont(font(11.0f, true, 0.22f));
    g.drawText(text_, getLocalBounds(), juce::Justification::centredLeft, false);
    if (selected_) g.fillRect(0, getHeight() - 3, juce::jmax(8, getWidth() - 8), 2);
}

void TabLabel::mouseUp(const juce::MouseEvent& e) {
    if (getLocalBounds().contains(e.getPosition()) && onClick) onClick();
}

void TabLabel::setSelected(bool selected) {
    if (selected != selected_) {
        selected_ = selected;
        repaint();
    }
}

// -------------------------------------------------------------------- meter ----
void LevelMeter::setLevels(float l, float r) {
    if (std::abs(l - l_) > 1e-4f || std::abs(r - r_) > 1e-4f) {
        l_ = l;
        r_ = r;
        repaint();
    }
}

void LevelMeter::paint(juce::Graphics& g) {
    auto area = getLocalBounds().toFloat();
    const float h = (area.getHeight() - 3.0f) * 0.5f;
    for (int ch = 0; ch < 2; ++ch) {
        auto bar = area.removeFromTop(h);
        area.removeFromTop(3.0f);
        g.setColour(kRailInk.withAlpha(0.25f));
        g.fillRoundedRectangle(bar, 2.0f);
        const float db = juce::Decibels::gainToDecibels(ch == 0 ? l_ : r_, -60.0f);
        const float frac = juce::jlimit(0.0f, 1.0f, (db + 48.0f) / 48.0f);
        if (frac > 0.0f) {
            g.setColour(db > -1.0f ? kEmber : kRailInk.withAlpha(0.8f));
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * frac), 2.0f);
        }
    }
}

// ----------------------------------------------------------------- keyboard ----
Keyboard::Keyboard(juce::MidiKeyboardState& state) : juce::MidiKeyboardComponent(state, juce::MidiKeyboardComponent::horizontalKeyboard) {
    setAvailableRange(21, 108);
    setScrollButtonsVisible(false);
    setBlackNoteLengthProportion(0.62f);
    setBlackNoteWidthProportion(0.64f);
    setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour(0xff6b5f52));
}

void Keyboard::drawWhiteNote(int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour, juce::Colour) {
    juce::ColourGradient fill(isDown ? kAmber.brighter(0.25f) : juce::Colour(0xfff5efe3), 0.0f, area.getY(),
                              isDown ? kAmber : juce::Colour(isOver ? 0xffefe3cd : 0xffe4dac8), 0.0f, area.getBottom(), false);
    g.setGradientFill(fill);
    g.fillRoundedRectangle(area.reduced(0.5f, 0.0f).withTrimmedBottom(1.0f), 2.0f);
    g.setColour(juce::Colour(0xff8f8272).withAlpha(0.7f));
    g.fillRect(area.getRight() - 0.5f, area.getY(), 1.0f, area.getHeight() - 2.0f);
}

void Keyboard::drawBlackNote(int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour) {
    g.setColour(juce::Colour(0xff0e0b09));
    g.fillRoundedRectangle(area, 1.5f);
    const auto top = area.reduced(1.5f, 0.0f).withTrimmedBottom(isDown ? 2.0f : 5.0f);
    juce::ColourGradient fill(isDown ? kAmber : juce::Colour(isOver ? 0xff4a3f38 : 0xff39302b), 0.0f, top.getY(),
                              isDown ? kAmber.darker(0.4f) : juce::Colour(0xff1c1613), 0.0f, top.getBottom(), false);
    g.setGradientFill(fill);
    g.fillRoundedRectangle(top, 1.5f);
}

// -------------------------------------------------------------------- panel ----
HearthPanel::HearthPanel(HearthProcessor& p)
    : processor_(p), amp_(p.apvts), tinesPower_(p.apvts, "tineson", "Tines on"), grandPower_(p.apvts, "grandon", "Grand on"),
      keyboard_(p.keyboardState) {
    setLookAndFeel(&look_);
    auto make = [this, &p](Group& group, const char* id, const char* label, int size, bool bipolar = false) {
        auto* k = knobs_.add(new Knob(p.apvts, id, label, bipolar));
        k->maxSize = size;
        addAndMakeVisible(k);
        group.push_back(k);
    };
    for (auto d : {std::pair{"bark", "Bark"}, {"timbre", "Timbre"}, {"hammer", "Hammer"}, {"bell", "Bell"}, {"thump", "Thump"}, {"sustain", "Sustain"}})
        make(tineKnobs_, d.first, d.second, 70);
    for (auto d : {std::pair{"ghammer", "Hammer"}, {"gtone", "Tone"}, {"gres", "Resonance"}, {"tinering", "Tine Ring"}, {"gnoise", "Noises"},
                   {"gwidth", "Width"}, {"gcomp", "Comp"}})
        make(grandKnobs_, d.first, d.second, 70, juce::String(d.first) == "gtone" || juce::String(d.first) == "ghammer");
    make(touchKnobs_, "dynamics", "Dynamics", 70);
    make(touchKnobs_, "damper", "Damper", 70);
    make(masterKnobs_, "tune", "Tune", 70, true);
    make(masterKnobs_, "detune", "Detune", 70);
    make(masterKnobs_, "balance", "Balance", 70, true);
    make(masterKnobs_, "volume", "Volume", 70);
    make(ampKnobs_, "bass", "Bass", 62, true);
    make(ampKnobs_, "treble", "Treble", 62, true);
    make(ampKnobs_, "drive", "Drive", 62);
    make(tremKnobs_, "tremrate", "Rate", 62);
    make(tremKnobs_, "tremdepth", "Depth", 62);
    make(tremKnobs_, "tremstereo", "Stereo", 62);
    make(chorusKnobs_, "chorus", "Mix", 62);
    make(chorusKnobs_, "chorusrate", "Rate", 62);
    make(phaserKnobs_, "phaser", "Mix", 62);
    make(phaserKnobs_, "phaserrate", "Rate", 62);
    make(reverbKnobs_, "reverb", "Mix", 62);
    make(reverbKnobs_, "reverbsize", "Size", 62);
    addAndMakeVisible(amp_);
    for (auto* c : {static_cast<juce::Component*>(&tinesPower_), static_cast<juce::Component*>(&grandPower_),
                    static_cast<juce::Component*>(&tinesTab_), static_cast<juce::Component*>(&grandTab_)})
        addAndMakeVisible(c);
    tinesTab_.onClick = [this] { showPage(0); };
    grandTab_.onClick = [this] { showPage(1); };

    int lastGroup = -1;
    for (int i = 0; i < hk::numPresets(); ++i) {
        const hk::Preset& pr = hk::preset(i);
        const bool tines = pr.v[hk::kTinesOn] > 0.5f, grand = pr.v[hk::kGrandOn] > 0.5f;
        const int group = grand ? (tines ? 2 : 1) : (i < hk::numTinePresets() ? 0 : 2);
        static const char* headings[3] = {"TINES", "GRAND", "TOGETHER"};
        if (i == 0 || group != lastGroup) presetBox_.addSectionHeading(headings[group]);
        lastGroup = group;
        presetBox_.addItem(pr.name, i + 1);
    }
    presetBox_.setSelectedItemIndex(p.getCurrentProgram(), juce::dontSendNotification);
    presetBox_.onChange = [this] { processor_.loadPreset(presetBox_.getSelectedItemIndex()); };
    presetBox_.setTitle("Preset");
    addAndMakeVisible(presetBox_);
    for (auto* b : {&prev_, &next_}) {
        b->getProperties().set("rail", true);
        addAndMakeVisible(*b);
    }
    prev_.onClick = [this] { stepPreset(-1); };
    next_.onClick = [this] { stepPreset(1); };
    prev_.setTitle("Previous preset");
    next_.setTitle("Next preset");
    addAndMakeVisible(meter_);
    addAndMakeVisible(keyboard_);
    setSize(kWidth, kHeight);
    lastProgram_ = processor_.getCurrentProgram();
    grandState_ = processor_.grandState.load();
    tinesOn_ = tinesPower_.isOn();
    grandOn_ = grandPower_.isOn();
    showPage(grandOn_ && !tinesOn_ ? 1 : 0);
    startTimerHz(30);
}

void HearthPanel::showPage(int page) {
    page_ = page;
    tinesTab_.setSelected(page == 0);
    grandTab_.setSelected(page == 1);
    refresh();
}

// A program that uses one instrument only brings its page to the front.
void HearthPanel::pageForProgram() {
    if (grandOn_ && !tinesOn_) showPage(1);
    else if (tinesOn_ && !grandOn_) showPage(0);
}

void HearthPanel::refresh() {
    const bool recordings = grandState_ == HearthProcessor::grandReady || grandState_ == HearthProcessor::grandDamaged;
    const float off = 0.38f;
    for (auto* k : tineKnobs_) {
        k->setVisible(page_ == 0);
        k->setAlpha(tinesOn_ ? 1.0f : off);
    }
    for (auto* k : grandKnobs_) {
        k->setVisible(page_ == 1 && recordings);
        k->setAlpha(grandOn_ ? 1.0f : off);
    }
    grandKnobs_[3]->setAlpha(tinesOn_ ? 1.0f : off);          // tine ring needs the tines, not the grand's keys
    for (auto* group : {&ampKnobs_, &tremKnobs_, &chorusKnobs_, &phaserKnobs_})
        for (auto* k : *group) k->setAlpha(tinesOn_ ? 1.0f : off);
    amp_.setAlpha(tinesOn_ ? 1.0f : off);
    masterKnobs_[1]->setAlpha(tinesOn_ ? 1.0f : off);         // detune is the tines' key-to-key spread
    masterKnobs_[2]->setAlpha(tinesOn_ && grandOn_ ? 1.0f : off);
    repaint();
}

HearthPanel::~HearthPanel() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void HearthPanel::stepPreset(int delta) {
    const int n = hk::numPresets();
    presetBox_.setSelectedItemIndex((presetBox_.getSelectedItemIndex() + delta + n) % n, juce::sendNotificationSync);
}

void HearthPanel::timerCallback() {
    meter_.setLevels(processor_.meterL.load(), processor_.meterR.load());
    if (presetBox_.getSelectedItemIndex() != processor_.getCurrentProgram() && !presetBox_.isPopupActive())
        presetBox_.setSelectedItemIndex(processor_.getCurrentProgram(), juce::dontSendNotification);

    const int state = processor_.grandState.load();
    const bool tines = tinesPower_.isOn(), grand = grandPower_.isOn();
    const bool changed = state != grandState_ || tines != tinesOn_ || grand != grandOn_;
    grandState_ = state;
    tinesOn_ = tines;
    grandOn_ = grand;
    if (processor_.getCurrentProgram() != lastProgram_) {
        lastProgram_ = processor_.getCurrentProgram();
        pageForProgram();
    } else if (changed) {
        refresh();
    }
    // recordings installed while the plugin is open are picked up without reloading it
    if (state == HearthProcessor::grandMissing && ++retry_ >= 90) {
        retry_ = 0;
        processor_.findGrand();
    }
}

void HearthPanel::section(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title) const {
    const auto r = area.toFloat();
    g.setColour(kSection);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(kSectionEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
    g.setColour(kAmber);
    g.setFont(font(11.0f, true, 0.22f));
    g.drawText(title, area.withHeight(26).reduced(14, 0), juce::Justification::centredLeft, false);
}

void HearthPanel::paint(juce::Graphics& g) {
    g.setGradientFill(juce::ColourGradient(kBackTop, 0.0f, 0.0f, kBackBottom, 0.0f, static_cast<float>(kHeight), false));
    g.fillAll();

    // name rail: brushed metal, like the strip above a stage piano's keys
    const juce::Rectangle<float> rail(0.0f, 0.0f, static_cast<float>(kWidth), 62.0f);
    g.setGradientFill(juce::ColourGradient(kRailTop, 0.0f, 0.0f, kRailBottom, 0.0f, rail.getBottom(), false));
    g.fillRect(rail);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    for (int y = 2; y < 62; y += 3) g.fillRect(0.0f, static_cast<float>(y), static_cast<float>(kWidth), 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRect(0.0f, rail.getBottom(), static_cast<float>(kWidth), 2.0f);

    g.setColour(kRailInk);
    g.setFont(font(25.0f, true, 0.16f));
    g.drawText("HEARTH", 26, 10, 180, 30, juce::Justification::centredLeft, false);
    g.setFont(font(25.0f, false, 0.16f));
    g.drawText("KEYS", 158, 10, 120, 30, juce::Justification::centredLeft, false);
    g.setFont(font(10.0f, true, 0.3f));
    g.setColour(kRailInk.withAlpha(0.7f));
    g.drawText("TINES AND GRAND PIANO", 27, 38, 260, 14, juce::Justification::centredLeft, false);
    g.drawText("OUT", kWidth - 150, 24, 30, 14, juce::Justification::centredLeft, false);

    section(g, secInst_, {});
    section(g, secTouch_, "TOUCH");
    section(g, secMaster_, "MASTER");
    if (page_ == 1) {
        const bool recordings = grandState_ == HearthProcessor::grandReady || grandState_ == HearthProcessor::grandDamaged;
        const auto header = secInst_.withHeight(26).withTrimmedLeft(200).withTrimmedRight(14);
        g.setFont(font(9.5f, true, 0.14f));
        if (grandState_ == HearthProcessor::grandDamaged) {
            g.setColour(kEmber);
            g.drawText("RECORDINGS DAMAGED - RUN THE INSTALLER AGAIN", header, juce::Justification::centredRight, false);
        } else if (recordings) {
            g.setColour(kDim);
            g.drawText("RECORDINGS: SALAMANDER GRAND PIANO, A. HOLM", header, juce::Justification::centredRight, false);
        }
        if (!recordings) {
            auto body = secInst_.withTrimmedTop(40).withTrimmedBottom(20).reduced(24, 0);
            const bool looking = grandState_ == HearthProcessor::grandSearching;
            g.setColour(kCream);
            g.setFont(font(16.0f, true));
            g.drawText(looking ? "Looking for the grand's recordings..." : "The grand's recordings are not installed yet",
                       body.removeFromTop(body.getHeight() / 2), juce::Justification::centredBottom, false);
            if (!looking) {
                g.setColour(kMuted);
                g.setFont(font(12.5f));
                g.drawFittedText("Run install_mac.sh from the download once more: it fetches them (about 270 MB).\n"
                                 "The piano appears here by itself when they are in place.",
                                 body.withTrimmedTop(6), juce::Justification::centredTop, 2);
            }
        }
    }
    section(g, secAmp_, "AMP");
    section(g, secTrem_, "TREMOLO");
    section(g, secChorus_, "CHORUS");
    section(g, secPhaser_, "PHASER");
    section(g, secReverb_, "REVERB");

    // felt strip above the keys
    const auto keys = keyboard_.getBounds();
    g.setColour(juce::Colour(0xff0d0a08));
    g.fillRect(keys.getX() - 4, keys.getY() - 12, keys.getWidth() + 8, keys.getHeight() + 16);
    g.setColour(juce::Colour(0xff7a2320));
    g.fillRect(keys.getX(), keys.getY() - 8, keys.getWidth(), 6);
}

void HearthPanel::resized() {
    const int margin = 16, gap = 8;
    presetBox_.setBounds(kWidth / 2 - 150, 16, 300, 30);
    prev_.setBounds(presetBox_.getX() - 38, 16, 30, 30);
    next_.setBounds(presetBox_.getRight() + 8, 16, 30, 30);
    meter_.setBounds(kWidth - 118, 23, 94, 17);

    const int row1 = 74, rowH = 158, row2 = row1 + rowH + gap;
    secInst_ = {margin, row1, 520, rowH};
    secTouch_ = {secInst_.getRight() + gap, row1, 160, rowH};
    secMaster_ = {secTouch_.getRight() + gap, row1, kWidth - margin - secTouch_.getRight() - gap, rowH};
    // second row: every knob gets the same cell, so sections are as wide as their contents
    const int cell = 66, pad = 16, switchW = 106;
    secAmp_ = {margin, row2, pad + switchW + 3 * cell, rowH};
    secTrem_ = {secAmp_.getRight() + gap, row2, pad + 3 * cell, rowH};
    const int rest = (kWidth - margin - secTrem_.getRight() - 3 * gap) / 3;
    secChorus_ = {secTrem_.getRight() + gap, row2, rest, rowH};
    secPhaser_ = {secChorus_.getRight() + gap, row2, rest, rowH};
    secReverb_ = {secPhaser_.getRight() + gap, row2, kWidth - margin - secPhaser_.getRight() - gap, rowH};

    // page tabs in the header of the instrument section, each with its on/off light
    const int hy = row1 + 3;
    tinesPower_.setBounds(secInst_.getX() + 8, hy, 22, 22);
    tinesTab_.setBounds(tinesPower_.getRight(), hy, 62, 22);
    grandPower_.setBounds(tinesTab_.getRight() + 6, hy, 22, 22);
    grandTab_.setBounds(grandPower_.getRight(), hy, 62, 22);

    place(secInst_, tineKnobs_);
    place(secInst_, grandKnobs_);
    place(secTouch_, touchKnobs_);
    place(secMaster_, masterKnobs_);
    amp_.setBounds(secAmp_.getX() + 12, secAmp_.getY() + 44, switchW - 10, 88);
    place(secAmp_, ampKnobs_, switchW);
    place(secTrem_, tremKnobs_);
    place(secChorus_, chorusKnobs_);
    place(secPhaser_, phaserKnobs_);
    place(secReverb_, reverbKnobs_);

    const int keysTop = row2 + rowH + 26;
    keyboard_.setBounds(margin, keysTop, kWidth - 2 * margin, kHeight - keysTop - 14);
    keyboard_.setKeyWidth(static_cast<float>(keyboard_.getWidth()) / 52.0f);
}

void HearthPanel::place(juce::Rectangle<int> sec, const Group& group, int leftInset) {
    auto area = sec.withTrimmedTop(28).withTrimmedBottom(8).withTrimmedLeft(8 + leftInset).withTrimmedRight(8);
    const int w = area.getWidth() / static_cast<int>(group.size());
    for (auto* k : group) k->setBounds(area.removeFromLeft(w).reduced(1, 0));
}

// ------------------------------------------------------------------- editor ----
HearthEditor::HearthEditor(HearthProcessor& p) : juce::AudioProcessorEditor(&p), panel_(p) {
    addAndMakeVisible(panel_);
    setResizable(true, true);
    setResizeLimits(HearthPanel::kWidth * 7 / 10, HearthPanel::kHeight * 7 / 10, HearthPanel::kWidth * 2, HearthPanel::kHeight * 2);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(HearthPanel::kWidth) / HearthPanel::kHeight);
    const int w = juce::jlimit(HearthPanel::kWidth * 7 / 10, HearthPanel::kWidth * 2,
                               static_cast<int>(p.apvts.state.getProperty("uiWidth", HearthPanel::kWidth)));
    setSize(w, w * HearthPanel::kHeight / HearthPanel::kWidth);
}

void HearthEditor::paint(juce::Graphics& g) { g.fillAll(kBackBottom); }

void HearthEditor::resized() {
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(HearthPanel::kWidth);
    panel_.setTransform(juce::AffineTransform::scale(scale));
    panel_.setBounds(0, 0, HearthPanel::kWidth, HearthPanel::kHeight);
    if (auto* proc = dynamic_cast<HearthProcessor*>(getAudioProcessor()))
        proc->apvts.state.setProperty("uiWidth", getWidth(), nullptr);
}
