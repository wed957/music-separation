#pragma once

#include "audio_engine.h"

#include <complex>
#include <cstddef>
#include <vector>

namespace uar::signal {

// A channel-major, frequency-major, frame-major complex spectrogram.
struct ComplexSpectrogram {
    int channels = 0;
    int freqs = 0;
    int frames = 0;
    std::vector<std::complex<float>> values;

    std::complex<float>& at(int channel, int frequency, int frame);
    const std::complex<float>& at(int channel, int frequency, int frame) const;
    bool valid() const noexcept;
};

class FftPlan {
public:
    explicit FftPlan(int size);

    FftPlan(const FftPlan&) = delete;
    FftPlan& operator=(const FftPlan&) = delete;

    int size() const noexcept { return size_; }
    void Forward(std::vector<std::complex<float>>& data) const;
    void Inverse(std::vector<std::complex<float>>& data) const;

private:
    int size_;
    std::vector<std::complex<float>> roots_;
    std::vector<int> bitReverse_;
};

int ReflectIndex(int index, int samples) noexcept;

void Stft(const std::vector<float>& planar, int channels, int samples, int hop,
          int nfft, ComplexSpectrogram& result, const FftPlan& fft);

void Istft(const ComplexSpectrogram& spectrogram, int outputSamples, int hop,
           int nfft, std::vector<float>& planar, const FftPlan& fft);

std::vector<float> InterleavedToPlanar(const AudioBuffer& audio);
AudioBuffer PlanarToAudio(const std::vector<float>& planar, int channels, int sampleRate);

} // namespace uar::signal
