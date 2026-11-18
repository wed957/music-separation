#include "signal_processing.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace uar::signal {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float Hann(int index, int size) noexcept {
    return 0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(index) /
                                   static_cast<float>(size));
}

} // namespace

std::complex<float>& ComplexSpectrogram::at(int channel, int frequency, int frame) {
    return values[(static_cast<size_t>(channel) * static_cast<size_t>(freqs) +
                   static_cast<size_t>(frequency)) * static_cast<size_t>(frames) +
                  static_cast<size_t>(frame)];
}

const std::complex<float>& ComplexSpectrogram::at(
    int channel, int frequency, int frame) const {
    return values[(static_cast<size_t>(channel) * static_cast<size_t>(freqs) +
                   static_cast<size_t>(frequency)) * static_cast<size_t>(frames) +
                  static_cast<size_t>(frame)];
}

bool ComplexSpectrogram::valid() const noexcept {
    if (channels <= 0 || freqs <= 0 || frames <= 0) return false;
    const size_t expected = static_cast<size_t>(channels) * static_cast<size_t>(freqs) *
                            static_cast<size_t>(frames);
    return values.size() == expected;
}

FftPlan::FftPlan(int size) : size_(size) {
    if (size < 2 || (size & (size - 1)) != 0)
        throw std::invalid_argument("FFT size must be a power of two");
    roots_.resize(static_cast<size_t>(size / 2));
    for (int i = 0; i < size / 2; ++i) {
        const float angle = -2.0f * kPi * static_cast<float>(i) /
                            static_cast<float>(size);
        roots_[static_cast<size_t>(i)] = {std::cos(angle), std::sin(angle)};
    }
    bitReverse_.resize(static_cast<size_t>(size));
    int bits = 0;
    while ((1 << bits) < size) ++bits;
    for (int i = 0; i < size; ++i) {
        int value = i;
        int reversed = 0;
        for (int bit = 0; bit < bits; ++bit) {
            reversed = (reversed << 1) | (value & 1);
            value >>= 1;
        }
        bitReverse_[static_cast<size_t>(i)] = reversed;
    }
}

void FftPlan::Forward(std::vector<std::complex<float>>& data) const {
    if (data.size() != static_cast<size_t>(size_))
        throw std::invalid_argument("FFT input size does not match the plan");
    for (int i = 0; i < size_; ++i) {
        const int j = bitReverse_[static_cast<size_t>(i)];
        if (i < j) std::swap(data[static_cast<size_t>(i)], data[static_cast<size_t>(j)]);
    }
    for (int length = 2; length <= size_; length <<= 1) {
        const int half = length / 2;
        const int stride = size_ / length;
        for (int start = 0; start < size_; start += length) {
            for (int i = 0; i < half; ++i) {
                const auto weight = roots_[static_cast<size_t>(i * stride)];
                const auto even = data[static_cast<size_t>(start + i)];
                const auto odd = data[static_cast<size_t>(start + i + half)] * weight;
                data[static_cast<size_t>(start + i)] = even + odd;
                data[static_cast<size_t>(start + i + half)] = even - odd;
            }
        }
    }
}

void FftPlan::Inverse(std::vector<std::complex<float>>& data) const {
    if (data.size() != static_cast<size_t>(size_))
        throw std::invalid_argument("FFT input size does not match the plan");
    for (auto& item : data) item = std::conj(item);
    Forward(data);
    const float scale = 1.0f / static_cast<float>(size_);
    for (auto& item : data) item = std::conj(item) * scale;
}

int ReflectIndex(int index, int samples) noexcept {
    if (samples <= 1) return 0;
    const int period = 2 * (samples - 1);
    int reflected = index % period;
    if (reflected < 0) reflected += period;
    return reflected < samples ? reflected : period - reflected;
}

void Stft(const std::vector<float>& planar, int channels, int samples, int hop,
          int nfft, ComplexSpectrogram& result, const FftPlan& fft) {
    result = {};
    if (channels <= 0 || samples <= 0 || hop <= 0 || nfft < 2 ||
        fft.size() != nfft || planar.size() < static_cast<size_t>(channels) *
                                             static_cast<size_t>(samples)) {
        return;
    }
    const int frames = 1 + samples / hop;
    const int freqs = nfft / 2 + 1;
    result.channels = channels;
    result.freqs = freqs;
    result.frames = frames;
    result.values.assign(static_cast<size_t>(channels) * static_cast<size_t>(freqs) *
                             static_cast<size_t>(frames), {});
    std::vector<std::complex<float>> work(static_cast<size_t>(nfft));
    for (int channel = 0; channel < channels; ++channel) {
        const float* input = planar.data() + static_cast<size_t>(channel) * samples;
        for (int frame = 0; frame < frames; ++frame) {
            const int center = frame * hop;
            for (int n = 0; n < nfft; ++n) {
                const int source = center + n - nfft / 2;
                work[static_cast<size_t>(n)] =
                    input[ReflectIndex(source, samples)] * Hann(n, nfft);
            }
            fft.Forward(work);
            for (int frequency = 0; frequency < freqs; ++frequency)
                result.at(channel, frequency, frame) = work[static_cast<size_t>(frequency)];
        }
    }
}

void Istft(const ComplexSpectrogram& spectrogram, int outputSamples, int hop,
           int nfft, std::vector<float>& planar, const FftPlan& fft) {
    planar.clear();
    if (!spectrogram.valid() || outputSamples <= 0 || hop <= 0 || nfft < 2 ||
        spectrogram.freqs != nfft / 2 + 1 || fft.size() != nfft) {
        return;
    }
    planar.assign(static_cast<size_t>(spectrogram.channels) *
                      static_cast<size_t>(outputSamples), 0.0f);
    std::vector<float> weights(static_cast<size_t>(outputSamples), 0.0f);
    std::vector<std::complex<float>> work(static_cast<size_t>(nfft));
    for (int frame = 0; frame < spectrogram.frames; ++frame) {
        const int offset = frame * hop - nfft / 2;
        for (int n = 0; n < nfft; ++n) {
            const int destination = offset + n;
            if (destination < 0 || destination >= outputSamples) continue;
            const float window = Hann(n, nfft);
            weights[static_cast<size_t>(destination)] += window * window;
        }
    }

    for (int channel = 0; channel < spectrogram.channels; ++channel) {
        float* output = planar.data() + static_cast<size_t>(channel) * outputSamples;
        for (int frame = 0; frame < spectrogram.frames; ++frame) {
            std::fill(work.begin(), work.end(), std::complex<float>{});
            for (int frequency = 0; frequency < spectrogram.freqs; ++frequency) {
                work[static_cast<size_t>(frequency)] =
                    spectrogram.at(channel, frequency, frame);
                if (frequency > 0 && frequency < spectrogram.freqs - 1)
                    work[static_cast<size_t>(nfft - frequency)] =
                        std::conj(work[static_cast<size_t>(frequency)]);
            }
            fft.Inverse(work);
            const int offset = frame * hop - nfft / 2;
            for (int n = 0; n < nfft; ++n) {
                const int destination = offset + n;
                if (destination < 0 || destination >= outputSamples) continue;
                output[destination] += work[static_cast<size_t>(n)].real() *
                                       Hann(n, nfft);
            }
        }
        for (int sample = 0; sample < outputSamples; ++sample)
            output[sample] /= std::max(weights[static_cast<size_t>(sample)], 1.0e-5f);
    }
}

std::vector<float> InterleavedToPlanar(const AudioBuffer& audio) {
    if (audio.channels <= 0 || audio.samples.size() % static_cast<size_t>(audio.channels) != 0)
        return {};
    const size_t frames = audio.frames();
    std::vector<float> planar(static_cast<size_t>(audio.channels) * frames);
    for (size_t frame = 0; frame < frames; ++frame)
        for (int channel = 0; channel < audio.channels; ++channel)
            planar[static_cast<size_t>(channel) * frames + frame] =
                audio.samples[frame * static_cast<size_t>(audio.channels) +
                              static_cast<size_t>(channel)];
    return planar;
}

AudioBuffer PlanarToAudio(const std::vector<float>& planar, int channels, int sampleRate) {
    AudioBuffer audio;
    if (channels <= 0 || planar.size() % static_cast<size_t>(channels) != 0) return audio;
    audio.channels = channels;
    audio.sampleRate = sampleRate;
    const size_t frames = planar.size() / static_cast<size_t>(channels);
    audio.samples.resize(frames * static_cast<size_t>(channels));
    for (size_t frame = 0; frame < frames; ++frame)
        for (int channel = 0; channel < channels; ++channel)
            audio.samples[frame * static_cast<size_t>(channels) +
                          static_cast<size_t>(channel)] =
                planar[static_cast<size_t>(channel) * frames + frame];
    return audio;
}

} // namespace uar::signal
