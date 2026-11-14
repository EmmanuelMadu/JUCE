// Plugin wrapper: parameters, MIDI, state and presets around the hk::Engine.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "../dsp/Engine.h"

class HearthProcessor final : public juce::AudioProcessor {
public:
    HearthProcessor();
    ~HearthProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Hearth Keys"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return program_; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void loadPreset(int index);

    // The grand's recordings are looked for and checked on a background thread.
    enum GrandState { grandSearching = 0, grandReady, grandMissing, grandDamaged };
    std::atomic<int> grandState{grandSearching};
    std::atomic<float> grandProgress{0.0f};
    void findGrand();                           // look (again) for the recordings; message thread
    static juce::File grandFolder();            // where the installer puts them

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    std::atomic<float> meterL{0.0f}, meterR{0.0f};
    std::atomic<int> activeKeys{0};

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleMidi(const juce::MidiMessage& m);

    class GrandLoader;
    std::shared_ptr<hk::GrandPack> pack_;       // declared before the engine: outlives its use
    hk::Engine engine_;
    std::unique_ptr<juce::Thread> loader_;
    std::atomic<float>* raw_[hk::kNumParams] = {};
    int program_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HearthProcessor)
};
