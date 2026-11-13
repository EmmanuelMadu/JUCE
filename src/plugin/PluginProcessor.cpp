#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace {
juce::String percentText(float v) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; }
}  // namespace

juce::AudioProcessorValueTreeState::ParameterLayout HearthProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < hk::kNumParams; ++i) {
        const hk::ParamInfo& p = hk::paramInfo(i);
        const juce::ParameterID pid{p.id, 1};
        if (p.steps > 0) {
            juce::StringArray choices;
            choices.addTokens(p.choices, "|", "");
            layout.add(std::make_unique<juce::AudioParameterChoice>(pid, p.name, choices, juce::roundToInt(p.def)));
            continue;
        }
        const juce::String unit(p.unit);
        juce::NormalisableRange<float> range(p.min, p.max);
        auto attr = juce::AudioParameterFloatAttributes().withLabel(unit);
        if (unit == "dB") {
            attr = attr.withStringFromValueFunction([](float v, int) {
                return (v > 0.05f ? "+" : "") + juce::String(std::abs(v) < 0.05f ? 0.0f : v, 1) + " dB";
            });
        } else if (unit == "Hz") {
            const bool tune = (i == hk::kTune);
            attr = attr.withStringFromValueFunction([tune](float v, int) { return juce::String(v, tune ? 1 : 2) + " Hz"; });
        } else {
            attr = attr.withStringFromValueFunction([](float v, int) { return percentText(v); })
                       .withValueFromStringFunction([](const juce::String& s) { return s.retainCharacters("0123456789.-").getFloatValue() / 100.0f; });
        }
        layout.add(std::make_unique<juce::AudioParameterFloat>(pid, p.name, range, p.def, attr));
    }
    return layout;
}

HearthProcessor::HearthProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "HearthKeys", createLayout()) {
    for (int i = 0; i < hk::kNumParams; ++i) raw_[i] = apvts.getRawParameterValue(hk::paramInfo(i).id);
}

void HearthProcessor::prepareToPlay(double sampleRate, int) {
    engine_.prepare(sampleRate);
    setLatencySamples(engine_.latency());
    keyboardState.reset();
}

bool HearthProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && layouts.getMainInputChannelSet().isDisabled();
}

void HearthProcessor::handleMidi(const juce::MidiMessage& m) {
    if (m.isNoteOn()) engine_.noteOn(m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff()) engine_.noteOff(m.getNoteNumber());
    else if (m.isSustainPedalOn() || m.isSustainPedalOff() || m.isControllerOfType(64)) engine_.sustain(static_cast<float>(m.getControllerValue()) / 127.0f);
    else if (m.isAllNotesOff()) engine_.allNotesOff();
    else if (m.isAllSoundOff()) engine_.allSoundOff();
}

void HearthProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0) {
        buffer.clear();
        return;
    }
    keyboardState.processNextMidiBuffer(midi, 0, n, true);       // adds notes played on the on-screen keyboard
    for (int i = 0; i < hk::kNumParams; ++i) engine_.setParam(i, raw_[i]->load(std::memory_order_relaxed));

    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);
    int pos = 0;
    for (const auto meta : midi) {
        const int at = juce::jlimit(0, n, meta.samplePosition);
        if (at > pos) {
            engine_.process(left + pos, right + pos, at - pos);
            pos = at;
        }
        handleMidi(meta.getMessage());
    }
    if (pos < n) engine_.process(left + pos, right + pos, n - pos);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, n);

    const float pl = buffer.getMagnitude(0, 0, n), pr = buffer.getMagnitude(1, 0, n);
    const float decay = std::pow(0.5f, static_cast<float>(n) / static_cast<float>(0.25 * getSampleRate()));
    meterL.store(juce::jmax(pl, meterL.load() * decay));
    meterR.store(juce::jmax(pr, meterR.load() * decay));
    activeKeys.store(engine_.activeKeys());
}

juce::AudioProcessorEditor* HearthProcessor::createEditor() { return new HearthEditor(*this); }

int HearthProcessor::getNumPrograms() { return hk::numPresets(); }
const juce::String HearthProcessor::getProgramName(int index) { return hk::preset(index).name; }
void HearthProcessor::setCurrentProgram(int index) { loadPreset(index); }

void HearthProcessor::loadPreset(int index) {
    program_ = juce::jlimit(0, hk::numPresets() - 1, index);
    const hk::Preset& p = hk::preset(program_);
    for (int i = 0; i < hk::kNumParams; ++i) {
        if (auto* param = apvts.getParameter(hk::paramInfo(i).id)) {
            const float norm = param->convertTo0to1(p.v[i]);
            param->beginChangeGesture();
            param->setValueNotifyingHost(norm);
            param->endChangeGesture();
        }
    }
}

void HearthProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    state.setProperty("program", program_, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void HearthProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        if (xml->hasTagName(apvts.state.getType())) {
            auto state = juce::ValueTree::fromXml(*xml);
            program_ = juce::jlimit(0, hk::numPresets() - 1, static_cast<int>(state.getProperty("program", 0)));
            apvts.replaceState(state);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HearthProcessor(); }
