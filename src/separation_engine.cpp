#include "separation_engine.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>
#include <numeric>
#include <thread>
#include <vector>
#include <string_view>

namespace uar {
namespace {

std::string PathText(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

constexpr int kSampleRate = 44100;
constexpr int kNfft = 2048;
// 内置文件是 131584 采样、512 hop 的 Mel-Band 导出版本，正好得到 258 帧。
constexpr int kHop = 512;
constexpr int kChunkSamples = 131584;
constexpr int kFreqs = 1025;
constexpr int kMelPackedFreqs = 3958;
constexpr int kMelFrames = 258;

struct ComplexSpectrogram {
    int channels = 2;
    int frames = 0;
    std::vector<std::complex<float>> values; // channel-major, freq-major, frame-major
    std::complex<float>& at(int channel, int freq, int frame) {
        return values[(static_cast<size_t>(channel) * kFreqs + static_cast<size_t>(freq)) *
                      static_cast<size_t>(frames) + static_cast<size_t>(frame)];
    }
    const std::complex<float>& at(int channel, int freq, int frame) const {
        return values[(static_cast<size_t>(channel) * kFreqs + static_cast<size_t>(freq)) *
                      static_cast<size_t>(frames) + static_cast<size_t>(frame)];
    }
};

inline float Hann(int index, int size) {
    return 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * index / size);
}

int ReflectIndex(int index, int samples) noexcept {
    if (samples <= 1) return 0;
    const int period = 2 * (samples - 1);
    int reflected = index % period;
    if (reflected < 0) reflected += period;
    return reflected < samples ? reflected : period - reflected;
}

class FftPlan {
public:
    explicit FftPlan(int size) : size_(size), roots_(static_cast<size_t>(size / 2)) {
        for (int i = 0; i < size / 2; ++i) {
            const float angle = -2.0f * static_cast<float>(M_PI) * i / size;
            roots_[static_cast<size_t>(i)] = {std::cos(angle), std::sin(angle)};
        }
        bitReverse_.resize(static_cast<size_t>(size));
        int bits = 0;
        while ((1 << bits) < size) ++bits;
        for (int i = 0; i < size; ++i) {
            int x = i;
            int r = 0;
            for (int b = 0; b < bits; ++b) { r = (r << 1) | (x & 1); x >>= 1; }
            bitReverse_[static_cast<size_t>(i)] = r;
        }
    }

    void Forward(std::vector<std::complex<float>>& data) const {
        for (int i = 0; i < size_; ++i) {
            const int j = bitReverse_[static_cast<size_t>(i)];
            if (i < j) std::swap(data[static_cast<size_t>(i)], data[static_cast<size_t>(j)]);
        }
        for (int length = 2; length <= size_; length <<= 1) {
            const int half = length / 2;
            const int stride = size_ / length;
            for (int start = 0; start < size_; start += length) {
                for (int i = 0; i < half; ++i) {
                    const auto w = roots_[static_cast<size_t>(i * stride)];
                    const auto even = data[static_cast<size_t>(start + i)];
                    const auto odd = data[static_cast<size_t>(start + i + half)] * w;
                    data[static_cast<size_t>(start + i)] = even + odd;
                    data[static_cast<size_t>(start + i + half)] = even - odd;
                }
            }
        }
    }

    void Inverse(std::vector<std::complex<float>>& data) const {
        for (auto& item : data) item = std::conj(item);
        Forward(data);
        const float scale = 1.0f / size_;
        for (auto& item : data) item = std::conj(item) * scale;
    }

private:
    int size_;
    std::vector<std::complex<float>> roots_;
    std::vector<int> bitReverse_;
};

void Stft(const std::vector<float>& planar, int channels, int samples,
          ComplexSpectrogram& result, const FftPlan& fft) {
    const int frames = 1 + samples / kHop;
    result.channels = channels;
    result.frames = frames;
    result.values.assign(static_cast<size_t>(channels) * kFreqs * frames, {});
    std::vector<std::complex<float>> work(static_cast<size_t>(kNfft));
    for (int channel = 0; channel < channels; ++channel) {
        const float* input = samples > 0
            ? planar.data() + static_cast<size_t>(channel) * samples
            : nullptr;
        for (int frame = 0; frame < frames; ++frame) {
            const int center = frame * kHop;
            for (int n = 0; n < kNfft; ++n) {
                const int source = center + n - kNfft / 2;
                const float value = samples > 0 ? input[ReflectIndex(source, samples)] : 0.0f;
                work[static_cast<size_t>(n)] = value * Hann(n, kNfft);
            }
            fft.Forward(work);
            for (int freq = 0; freq < kFreqs; ++freq)
                result.at(channel, freq, frame) = work[static_cast<size_t>(freq)];
        }
    }
}

void Istft(const ComplexSpectrogram& spectrogram, int outputSamples,
           std::vector<float>& planar, const FftPlan& fft) {
    planar.assign(static_cast<size_t>(spectrogram.channels) * outputSamples, 0.0f);
    std::vector<float> weights(static_cast<size_t>(outputSamples), 0.0f);
    std::vector<std::complex<float>> work(static_cast<size_t>(kNfft));
    for (int channel = 0; channel < spectrogram.channels; ++channel) {
        float* output = planar.data() + static_cast<size_t>(channel) * outputSamples;
        std::fill(work.begin(), work.end(), std::complex<float>{});
        std::fill(weights.begin(), weights.end(), 0.0f);
        for (int frame = 0; frame < spectrogram.frames; ++frame) {
            std::fill(work.begin(), work.end(), std::complex<float>{});
            for (int freq = 0; freq < kFreqs; ++freq) {
                work[static_cast<size_t>(freq)] = spectrogram.at(channel, freq, frame);
                if (freq > 0 && freq < kFreqs - 1)
                    work[static_cast<size_t>(kNfft - freq)] = std::conj(work[static_cast<size_t>(freq)]);
            }
            fft.Inverse(work);
            const int offset = frame * kHop - kNfft / 2;
            for (int n = 0; n < kNfft; ++n) {
                const int destination = offset + n;
                if (destination < 0 || destination >= outputSamples) continue;
                const float window = Hann(n, kNfft);
                output[destination] += work[static_cast<size_t>(n)].real() * window;
                weights[static_cast<size_t>(destination)] += window * window;
            }
        }
        for (int i = 0; i < outputSamples; ++i)
            output[i] /= std::max(weights[static_cast<size_t>(i)], 1.0e-5f);
    }
}

std::vector<float> InterleavedToPlanar(const AudioBuffer& audio) {
    std::vector<float> planar(static_cast<size_t>(audio.channels) * audio.frames());
    for (size_t i = 0; i < audio.frames(); ++i)
        for (int c = 0; c < audio.channels; ++c)
            planar[static_cast<size_t>(c) * audio.frames() + i] = audio.samples[i * audio.channels + c];
    return planar;
}

AudioBuffer PlanarToAudio(const std::vector<float>& planar, int channels, int sampleRate) {
    AudioBuffer audio;
    audio.channels = channels;
    audio.sampleRate = sampleRate;
    const size_t frames = planar.size() / static_cast<size_t>(channels);
    audio.samples.resize(frames * static_cast<size_t>(channels));
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            audio.samples[i * channels + c] = planar[static_cast<size_t>(c) * frames + i];
    return audio;
}

std::vector<float> MelBank() {
    // librosa.filters.mel(sr=44100,n_fft=2048,n_mels=60,norm='slaney') 的等价实现。
    constexpr int bands = 60;
    std::vector<float> bank(static_cast<size_t>(bands) * kFreqs, 0.0f);
    constexpr float linearStep = 200.0f / 3.0f;
    constexpr float minLogHz = 1000.0f;
    const float minLogMel = minLogHz / linearStep;
    const float logStep = std::log(6.4f) / 27.0f;
    auto mel = [=](float hz) {
        return hz < minLogHz ? hz / linearStep
                             : minLogMel + std::log(hz / minLogHz) / logStep;
    };
    auto hz = [=](float value) {
        return value < minLogMel ? value * linearStep
                                 : minLogHz * std::exp(logStep * (value - minLogMel));
    };
    const float low = mel(0.0f);
    const float high = mel(22050.0f);
    std::array<float, bands + 2> edges{};
    for (int i = 0; i < bands + 2; ++i) edges[static_cast<size_t>(i)] = hz(low + (high - low) * i / (bands + 1));
    for (int band = 0; band < bands; ++band) {
        const float left = edges[static_cast<size_t>(band)];
        const float center = edges[static_cast<size_t>(band + 1)];
        const float right = edges[static_cast<size_t>(band + 2)];
        const float norm = 2.0f / std::max(right - left, 1.0e-6f);
        for (int freq = 0; freq < kFreqs; ++freq) {
            const float frequency = 22050.0f * freq / (kFreqs - 1);
            float value = 0.0f;
            if (frequency >= left && frequency <= center) value = (frequency - left) / std::max(center - left, 1.0e-6f);
            else if (frequency > center && frequency <= right) value = (right - frequency) / std::max(right - center, 1.0e-6f);
            bank[static_cast<size_t>(band) * kFreqs + freq] = std::max(0.0f, value) * norm;
        }
    }
    bank[0] = bank[1] * 0.25f;
    const size_t last = static_cast<size_t>(bands) * kFreqs - 1;
    bank[last] = bank[last - 1] * 0.25f;
    return bank;
}

} // namespace

struct SeparationEngine::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "UARMusic"};
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> mel;
    FftPlan fft{kNfft};
    std::vector<float> melBank = MelBank();

    Impl() {
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        options.SetIntraOpNumThreads(static_cast<int>(
            std::clamp(std::thread::hardware_concurrency(), 1u, 4u)));
        options.SetInterOpNumThreads(1);
        options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        options.DisableMemPattern();
        options.DisableCpuMemArena();
    }

    bool LoadSessions(const SeparationConfig& config, std::string& error) {
        try {
            if (!mel)
                mel = std::make_unique<Ort::Session>(env, config.melModel.wstring().c_str(), options);
            return true;
        } catch (const Ort::Exception& exception) {
            error = "加载 ONNX 模型失败: " + std::string(exception.what());
            mel.reset();
            return false;
        }
    }
};

SeparationEngine::SeparationEngine() : impl_(std::make_unique<Impl>()) {}
SeparationEngine::~SeparationEngine() = default;

bool SeparationEngine::ValidateModels(const SeparationConfig& config, std::string& error) const {
    std::error_code ec;
    if (config.melModel.empty() || !std::filesystem::is_regular_file(config.melModel, ec)) {
        error = "主处理文件不存在";
        return false;
    }
    return true;
}

bool SeparationEngine::ProcessFile(const std::filesystem::path& input,
                                   const std::filesystem::path& outputDirectory,
                                   const SeparationConfig& config,
                                   const SeparationCallbacks& callbacks,
                                   std::atomic_bool& cancel,
                                   std::string& error) {
    if (!impl_->LoadSessions(config, error)) return false;
    if (!impl_->mel) {
        error = "主处理文件没有打开";
        return false;
    }
    AudioBuffer inputAudio;
    if (callbacks.log) callbacks.log("正在读取歌曲：" + PathText(input));
    if (!LoadAudio(input, inputAudio, error)) {
        if (error == "音频文件过大") error = "歌曲太长";
        return false;
    }
    if (inputAudio.sampleRate != kSampleRate || inputAudio.channels != 2) {
        // miniaudio 已按配置转换到 44.1kHz/双声道。
        inputAudio.sampleRate = kSampleRate;
        inputAudio.channels = 2;
    }
    if (inputAudio.frames() > kMaxDecodedFrames) {
        error = "歌曲太长";
        return false;
    }
    const int samples = static_cast<int>(inputAudio.frames());
    auto planar = InterleavedToPlanar(inputAudio);
    std::vector<float> vocalPlanar(static_cast<size_t>(2) * samples, 0.0f);
    std::vector<float> overlapWeight(static_cast<size_t>(samples), 0.0f);
    const int step = std::max(
        kHop, kChunkSamples * (100 - std::clamp(config.overlap, 0, 90)) / 100);
    std::vector<int> starts;
    if (samples <= kChunkSamples) {
        starts.push_back(0);
    } else {
        for (int start = 0; start < samples; start += step) {
            if (start + kChunkSamples >= samples) {
                const int finalStart = samples - kChunkSamples;
                if (starts.empty() || starts.back() != finalStart) starts.push_back(finalStart);
                break;
            }
            starts.push_back(start);
        }
    }
    const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    size_t completed = 0;
    for (size_t chunkIndex = 0; chunkIndex < starts.size(); ++chunkIndex) {
        if (cancel.load()) { error = "已取消"; return false; }
        const int start = starts[chunkIndex];
        const int length = std::min(kChunkSamples, samples - start);
        std::vector<float> chunk(static_cast<size_t>(2) * kChunkSamples, 0.0f);
        for (int c = 0; c < 2; ++c)
            std::copy_n(planar.data() + static_cast<size_t>(c) * samples + start, length,
                        chunk.data() + static_cast<size_t>(c) * kChunkSamples);

        ComplexSpectrogram spec;
        Stft(chunk, 2, kChunkSamples, spec, impl_->fft);
        Ort::AllocatorWithDefaultOptions allocator;
        std::vector<float> melMask;
        if (impl_->mel) {
            // 可选融合文件使用独立的 258 帧输入契约。
            std::vector<float> melInput(
                static_cast<size_t>(kMelPackedFreqs) * kMelFrames * 2, 0.0f);
            int melIndex = 0;
            for (int band = 0; band < 60; ++band) {
                for (int freq = 0; freq < kFreqs; ++freq) {
                    const bool active =
                        impl_->melBank[static_cast<size_t>(band) * kFreqs + freq] > 0.0f;
                    if (!active) continue;
                    if (melIndex >= kMelPackedFreqs / 2) {
                        error = "歌曲处理设置不匹配";
                        return false;
                    }
                    for (int channel = 0; channel < 2; ++channel)
                        for (int frame = 0; frame < kMelFrames; ++frame) {
                            const int sourceFrame = std::min(spec.frames - 1, frame);
                            const auto value = spec.at(channel, freq, sourceFrame);
                            const size_t index =
                                (static_cast<size_t>(melIndex * 2 + channel) * kMelFrames +
                                 static_cast<size_t>(frame)) * 2;
                            melInput[index] = value.real();
                            melInput[index + 1] = value.imag();
                        }
                    ++melIndex;
                }
            }
            if (melIndex * 2 != kMelPackedFreqs) {
                error = "歌曲处理设置不匹配";
                return false;
            }
            melMask.resize(static_cast<size_t>(kMelPackedFreqs) * kMelFrames * 2);
            const std::array<int64_t, 4> melShape{1, kMelPackedFreqs, kMelFrames, 2};
            const std::array<int64_t, 5> melOutShape{1, 1, kMelPackedFreqs, kMelFrames, 2};
            auto melTensor = Ort::Value::CreateTensor<float>(
                memory, melInput.data(), melInput.size(), melShape.data(), melShape.size());
            auto melOut = Ort::Value::CreateTensor<float>(
                memory, melMask.data(), melMask.size(), melOutShape.data(), melOutShape.size());
            auto melInputNameHolder = impl_->mel->GetInputNameAllocated(0, allocator);
            auto melOutputNameHolder = impl_->mel->GetOutputNameAllocated(0, allocator);
            const char* melInputName = melInputNameHolder.get();
            const char* melOutputName = melOutputNameHolder.get();
            try {
                impl_->mel->Run(Ort::RunOptions{nullptr}, &melInputName, &melTensor, 1,
                                &melOutputName, &melOut, 1);
            } catch (const Ort::Exception& exception) {
                error = "备用处理失败: " + std::string(exception.what());
                return false;
            }
        }

        // 将 Mel 模型按频带展开的结果还原到原频率轴。
        std::vector<std::complex<float>> melByFrequency(
            static_cast<size_t>(2) * kFreqs * kMelFrames);
        std::array<int, kFreqs> melBandCount{};
        int melIndex = 0;
        for (int band = 0; band < 60; ++band) {
            for (int freq = 0; freq < kFreqs; ++freq) {
                const bool active =
                    impl_->melBank[static_cast<size_t>(band) * kFreqs + freq] > 0.0f;
                if (!active) continue;
                ++melBandCount[static_cast<size_t>(freq)];
                for (int channel = 0; channel < 2; ++channel) {
                    const int packed = melIndex * 2 + channel;
                    for (int frame = 0; frame < kMelFrames; ++frame) {
                        const size_t source =
                            (static_cast<size_t>(packed) * kMelFrames +
                             static_cast<size_t>(frame)) * 2;
                        const size_t destination =
                            (static_cast<size_t>(channel) * kFreqs +
                             static_cast<size_t>(freq)) * kMelFrames +
                            static_cast<size_t>(frame);
                        melByFrequency[destination] +=
                            std::complex<float>{melMask[source], melMask[source + 1]};
                    }
                }
                ++melIndex;
            }
        }

        // 将复数掩码应用于混合频谱，再做逆变换。
        ComplexSpectrogram estimate = spec;
        for (int channel = 0; channel < 2; ++channel)
            for (int freq = 0; freq < kFreqs; ++freq)
                for (int frame = 0; frame < spec.frames; ++frame) {
                    const auto mix = spec.at(channel, freq, frame);
                    const size_t m =
                        (static_cast<size_t>(channel) * kFreqs + static_cast<size_t>(freq)) *
                        kMelFrames + static_cast<size_t>(std::min(kMelFrames - 1, frame));
                    const float count = static_cast<float>(
                        std::max(1, melBandCount[static_cast<size_t>(freq)]));
                    const std::complex<float> mask = melByFrequency[m] / count;
                    estimate.at(channel, freq, frame) = mix * mask;
                }
        std::vector<float> estimatePlanar;
        Istft(estimate, kChunkSamples, estimatePlanar, impl_->fft);
        const int leftOverlap = chunkIndex == 0
            ? 0
            : std::clamp(starts[chunkIndex - 1] + kChunkSamples - start, 0, length);
        const int rightOverlap = chunkIndex + 1 >= starts.size()
            ? 0
            : std::clamp(start + kChunkSamples - starts[chunkIndex + 1], 0, length);
        for (int local = 0; local < length; ++local) {
            float weight = 1.0f;
            if (leftOverlap > 0 && local < leftOverlap)
                weight = std::min(weight, static_cast<float>(local + 1) / (leftOverlap + 1));
            if (rightOverlap > 0 && local >= length - rightOverlap)
                weight = std::min(weight, static_cast<float>(length - local) / (rightOverlap + 1));
            const int destination = start + local;
            overlapWeight[static_cast<size_t>(destination)] += weight;
            for (int c = 0; c < 2; ++c) {
                vocalPlanar[static_cast<size_t>(c) * samples + destination] +=
                    estimatePlanar[static_cast<size_t>(c) * kChunkSamples + local] * weight;
            }
        }
        completed++;
        if (callbacks.progress) {
            callbacks.progress(static_cast<double>(completed) /
                               static_cast<double>(starts.size()) * 0.9);
        }
    }

    for (int c = 0; c < 2; ++c)
        for (int sample = 0; sample < samples; ++sample) {
            const float weight = std::max(overlapWeight[static_cast<size_t>(sample)], 1.0e-5f);
            vocalPlanar[static_cast<size_t>(c) * samples + sample] /= weight;
        }
    AudioBuffer vocals = PlanarToAudio(vocalPlanar, 2, kSampleRate);
    AudioBuffer instrumental = inputAudio;
    for (size_t i = 0; i < instrumental.samples.size(); ++i)
        instrumental.samples[i] = std::clamp(inputAudio.samples[i] - vocals.samples[i], -1.0f, 1.0f);
    std::error_code ec;
    std::filesystem::create_directories(outputDirectory, ec);
    const auto stem = input.stem().wstring();
    std::wstring suffix = config.outputSuffix;
    for (wchar_t& character : suffix) {
        if (std::wstring_view(L"<>:\"/\\|?*").find(character) != std::wstring_view::npos)
            character = L'_';
    }
    const std::wstring version = suffix.empty() ? L"" : L"_" + suffix;
    if (callbacks.log) {
        const auto stemText = std::string(stem.begin(), stem.end());
        callbacks.log("正在保存两份歌曲：" + stemText);
    }
    if (!WriteWav(outputDirectory / (stem + version + L"_唱歌声.wav"), vocals, error)) return false;
    if (!WriteWav(outputDirectory / (stem + version + L"_音乐声.wav"), instrumental, error)) return false;
    if (callbacks.progress) callbacks.progress(1.0);
    return true;
}

} // namespace uar
