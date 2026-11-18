#include "../src/signal_processing.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

namespace {

void test_fft_round_trip() {
    uar::signal::FftPlan plan(256);
    std::vector<std::complex<float>> values(256);
    for (size_t index = 0; index < values.size(); ++index) {
        const float phase = static_cast<float>(index) * 0.037f;
        values[index] = {std::sin(phase) + 0.2f * std::cos(phase * 2.0f),
                         0.1f * std::sin(phase * 3.0f)};
    }
    const auto original = values;
    plan.Forward(values);
    plan.Inverse(values);
    float maximumError = 0.0f;
    for (size_t index = 0; index < values.size(); ++index)
        maximumError = std::max(maximumError, std::abs(values[index] - original[index]));
    assert(maximumError < 1.0e-4f);
}

void test_stft_istft_round_trip() {
    constexpr int frames = 3000;
    uar::AudioBuffer input;
    input.channels = 2;
    input.sampleRate = 44100;
    input.samples.resize(static_cast<size_t>(frames) * 2);
    for (int frame = 0; frame < frames; ++frame) {
        input.samples[static_cast<size_t>(frame) * 2] =
            0.6f * std::sin(frame * 0.019f);
        input.samples[static_cast<size_t>(frame) * 2 + 1] =
            0.4f * std::cos(frame * 0.013f);
    }
    const auto planar = uar::signal::InterleavedToPlanar(input);
    uar::signal::FftPlan plan(256);
    uar::signal::ComplexSpectrogram spectrogram;
    uar::signal::Stft(planar, 2, frames, 128, 256, spectrogram, plan);
    assert(spectrogram.valid());
    assert(spectrogram.channels == 2 && spectrogram.freqs == 129);

    std::vector<float> restored;
    uar::signal::Istft(spectrogram, frames, 128, 256, restored, plan);
    assert(restored.size() == planar.size());
    float maximumError = 0.0f;
    for (size_t index = 0; index < restored.size(); ++index)
        maximumError = std::max(maximumError, std::abs(restored[index] - planar[index]));
    assert(maximumError < 2.0e-4f);
}

void test_invalid_shape_fails_closed() {
    bool rejected = false;
    try {
        uar::signal::FftPlan invalid(-2);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);

    uar::signal::FftPlan plan(256);
    uar::signal::ComplexSpectrogram spectrogram;
    uar::signal::Stft({}, 2, 10, 128, 256, spectrogram, plan);
    assert(!spectrogram.valid());
    std::vector<float> restored{1.0f};
    uar::signal::Istft(spectrogram, 10, 128, 256, restored, plan);
    assert(restored.empty());
}

} // namespace

int main() {
    test_fft_round_trip();
    test_stft_istft_round_trip();
    test_invalid_shape_fails_closed();
    return 0;
}
