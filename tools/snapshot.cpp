// Renders the plugin's interface to a PNG file, without a screen.
// Usage: hk_snapshot <out.png> [preset index] [scale] [notes held, e.g. 60,64,67 or -] [page: tines | grand]
#include "../src/plugin/PluginEditor.h"

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    HearthProcessor processor;
    processor.prepareToPlay(48000.0, 512);
    if (argc > 2) processor.loadPreset(juce::String(argv[2]).getIntValue());
    const float scale = argc > 3 ? juce::String(argv[3]).getFloatValue() : 2.0f;
    for (int i = 0; i < 200 && processor.grandState.load() == HearthProcessor::grandSearching; ++i)
        juce::Thread::sleep(50);                 // let the search for the recordings finish
    if (argc > 4 && juce::String(argv[4]) != "-") {
        juce::StringArray notes;
        notes.addTokens(argv[4], ",", "");
        for (const auto& n : notes) processor.keyboardState.noteOn(1, n.getIntValue(), 0.8f);
    }
    HearthPanel panel(processor);
    if (argc > 5) panel.showPage(juce::String(argv[5]) == "grand" ? 1 : 0);
    const auto image = panel.createComponentSnapshot(panel.getLocalBounds(), true, scale);
    juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(argc > 1 ? argv[1] : "panel.png"));
    out.deleteFile();
    juce::FileOutputStream stream(out);
    juce::PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream(image, stream);
    std::cout << (ok ? "wrote " : "FAILED ") << out.getFullPathName() << " " << image.getWidth() << "x" << image.getHeight() << std::endl;
    return ok ? 0 : 1;
}
