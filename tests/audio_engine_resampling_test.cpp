#include "../src/audio_engine.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

template <typename T>
void WriteValue(std::ofstream& output, T value) {
    output.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void WritePcm16(const std::filesystem::path& path, int sampleRate, int frames,
                double frequency) {
    std::vector<std::int16_t> samples(static_cast<size_t>(frames) * 2);
    for (int frame = 0; frame < frames; ++frame) {
        const double phase = 2.0 * 3.14159265358979323846 * frequency * frame / sampleRate;
        const auto value = static_cast<std::int16_t>(
            std::lround(0.6 * 32767.0 * std::sin(phase)));
        samples[static_cast<size_t>(frame) * 2] = value;
        samples[static_cast<size_t>(frame) * 2 + 1] = value;
    }
    const std::uint32_t dataBytes =
        static_cast<std::uint32_t>(samples.size() * sizeof(samples[0]));
    const std::uint32_t riffBytes = 36 + dataBytes;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write("RIFF", 4);
    WriteValue(output, riffBytes);
    output.write("WAVEfmt ", 8);
    WriteValue(output, std::uint32_t{16});
    WriteValue(output, std::uint16_t{1});
    WriteValue(output, std::uint16_t{2});
    WriteValue(output, static_cast<std::uint32_t>(sampleRate));
    WriteValue(output, static_cast<std::uint32_t>(sampleRate * 4));
    WriteValue(output, std::uint16_t{4});
    WriteValue(output, std::uint16_t{16});
    output.write("data", 4);
    WriteValue(output, dataBytes);
    output.write(reinterpret_cast<const char*>(samples.data()),
                 static_cast<std::streamsize>(dataBytes));
    assert(output.good());
}

uar::AudioBuffer Load(const std::filesystem::path& path) {
    uar::AudioBuffer audio;
    std::string error;
    assert(uar::LoadAudio(path, audio, error));
    return audio;
}

} // namespace

int main() {
    const auto uniqueId = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("UARMusic-resampling-test-" + std::to_string(uniqueId));
    std::error_code ec;
    assert(std::filesystem::create_directory(directory, ec));
    assert(!ec);
    const auto exactPath = directory / "exact.wav";
    const auto resampledPath = directory / "resampled.wav";
    WritePcm16(exactPath, 44100, 4410, 440.0);
    WritePcm16(resampledPath, 48000, 4800, 23000.0);

    const auto exact = Load(exactPath);
    assert(exact.sampleRate == 44100 && exact.channels == 2 && exact.frames() == 4410);
    const auto resampled = Load(resampledPath);
    assert(resampled.sampleRate == 44100 && resampled.channels == 2 &&
           resampled.frames() == 4410);
    double sumSquares = 0.0;
    for (size_t index = 0; index < resampled.samples.size(); ++index) {
        const float value = resampled.samples[index];
        assert(std::isfinite(value));
        assert(value == resampled.samples[index ^ 1]);
        sumSquares += static_cast<double>(value) * value;
    }
    const double rms = std::sqrt(sumSquares / resampled.samples.size());
    assert(rms < 0.08);

    std::filesystem::remove_all(directory, ec);
    return 0;
}
