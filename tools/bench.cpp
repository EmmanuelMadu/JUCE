// Times the grand on this machine.  Usage: hk_bench <folder with the installed recordings>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../src/dsp/Engine.h"

namespace {
double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Renders `secs` seconds with `notes` keys of the grand held on the pedal; returns how many
// times faster than real time, and the longest single block in milliseconds.
void run(const hk::GrandPack* pack, double fs, int notes, float resonance, double secs, const char* label) {
    auto engine = std::make_unique<hk::Engine>();
    engine->prepare(fs);
    engine->setGrandPack(pack);
    engine->setParam(hk::kTinesOn, 0.0f);
    engine->setParam(hk::kGrandOn, 1.0f);
    engine->setParam(hk::kGrandRes, resonance);
    engine->sustain(1.0f);
    std::vector<float> l(256), r(256);
    unsigned seed = 12345;
    for (int i = 0; i < notes; ++i) {
        seed = seed * 1664525u + 1013904223u;
        engine->noteOn(28 + int((seed >> 8) % 72u), 0.7f);
    }
    const int blocks = int(secs * fs / 256);
    double worst = 0;
    const double t0 = seconds();
    for (int b = 0; b < blocks; ++b) {
        const double a = seconds();
        engine->process(l.data(), r.data(), 256);
        const double d = seconds() - a;
        if (d > worst) worst = d;
    }
    const double dt = seconds() - t0;
    std::printf("  %-44s %6.1fx real time, slowest block %.2f ms (a block lasts %.2f ms), %d voices, %d strings\n", label,
                blocks * 256 / fs / dt, worst * 1000, 256 / fs * 1000, engine->grandVoices(), engine->ringingStrings());
}
}  // namespace

// A passage as it is played: a new note every 100 ms for `secs` seconds, pedal changes every
// two seconds.  Reports the slowest block and how many blocks took longer than they last.
void passage(const hk::GrandPack* pack, double fs, double secs, const char* label) {
    auto engine = std::make_unique<hk::Engine>();
    engine->prepare(fs);
    engine->setGrandPack(pack);
    engine->setParam(hk::kTinesOn, 0.0f);
    engine->setParam(hk::kGrandOn, 1.0f);
    std::vector<float> l(256), r(256);
    unsigned seed = 777;
    const int blocks = int(secs * fs / 256);
    const double blockTime = 256 / fs;
    double worst = 0, total = 0;
    int late = 0, voices = 0;
    long sample = 0, nextNote = 0, nextPedal = 0;
    bool pedal = false;
    for (int b = 0; b < blocks; ++b) {
        if (sample >= nextNote) {
            seed = seed * 1664525u + 1013904223u;
            engine->noteOn(28 + int((seed >> 8) % 72u), 0.3f + 0.6f * float((seed >> 20) % 100u) / 100.0f);
            nextNote += long(0.1 * fs);
        }
        if (sample >= nextPedal) {
            pedal = !pedal;
            engine->sustain(pedal ? 1.0f : 0.0f);
            if (!pedal) engine->allNotesOff();
            nextPedal += long(2.0 * fs);
        }
        const double a = seconds();
        engine->process(l.data(), r.data(), 256);
        const double d = seconds() - a;
        total += d;
        if (d > worst) worst = d;
        if (d > blockTime) ++late;
        if (engine->grandVoices() > voices) voices = engine->grandVoices();
        sample += 256;
    }
    std::printf("  %-44s %6.1fx real time, slowest block %.2f ms, %d of %d blocks late, up to %d voices\n", label,
                blocks * blockTime / total, worst * 1000, late, blocks, voices);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: hk_bench <recordings folder>\n"); return 2; }
    std::string error;
    double t = seconds();
    auto pack = hk::GrandPack::open(argv[1], error);
    if (!pack) { std::printf("cannot open the recordings: %s\n", error.c_str()); return 1; }
    std::printf("opened %d recordings in %.2f s\n", hk::kGrandFiles, seconds() - t);
    passage(pack.get(), 48000.0, 10.0, "a played passage straight after opening");
    run(pack.get(), 48000.0, 64, 0.5f, 4.0, "64 notes at once, straight after opening");
    t = seconds();
    const int ok = pack->verify();
    std::printf("read and checked every sample in %.2f s: %s\n", seconds() - t, ok == 1 ? "intact" : "DAMAGED");
    passage(pack.get(), 48000.0, 10.0, "a played passage");
    run(pack.get(), 48000.0, 64, 0.5f, 4.0, "64 notes, strings ringing");
    run(pack.get(), 48000.0, 64, 0.5f, 4.0, "the same again");
    run(pack.get(), 48000.0, 64, 0.0f, 4.0, "64 notes, no string resonance");
    run(pack.get(), 48000.0, 24, 0.5f, 4.0, "24 notes, strings ringing");
    run(pack.get(), 44100.0, 64, 0.5f, 4.0, "64 notes at 44.1 kHz");
    run(pack.get(), 48000.0, 0, 0.5f, 4.0, "no notes (pedal noise only)");
    return ok == 1 ? 0 : 1;
}
