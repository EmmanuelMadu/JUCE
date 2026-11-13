// Renders the plugin's interface to a PNG file, without a screen.
// Usage: hk_snapshot <out.png> [preset index] [scale] [notes held, e.g. 60,64,67]
#include "../src/plugin/PluginEditor.h"

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    HearthProcessor processor;
    processor.prepareToPlay(48000.0, 512);
    if (argc > 2) processor.loadPreset(juce::String(argv[2]).getIntValue());
    const float scale = argc > 3 ? juce::String(argv[3]).getFloatValue() : 2.0f;
    if (argc > 4) {
        juce::StringArray notes;
        notes.addTokens(argv[4], ",", "");
        for (const auto& n : notes) processor.keyboardState.noteOn(1, n.getIntValue(), 0.8f);
    }
    HearthPanel panel(processor);
    const auto image = panel.createComponentSnapshot(panel.getLocalBounds(), true, scale);
    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "panel.png"));
    out.deleteFile();
    juce::FileOutputStream stream(out);
    juce::PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream(image, stream);
    std::cout << (ok ? "wrote " : "FAILED ") << out.getFullPathName() << " " << image.getWidth() << "x" << image.getHeight() << std::endl;
    return ok ? 0 : 1;
}
