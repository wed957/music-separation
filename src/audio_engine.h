#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace uar {

// 限制单首歌曲的解码长度，避免损坏文件头触发超大内存分配。
constexpr size_t kMaxDecodedFrames = static_cast<size_t>(44100) * 60 * 15;

struct AudioBuffer {
    int sampleRate = 44100;
    int channels = 2;
    std::vector<float> samples; // 交错布局：L/R/L/R…

    size_t frames() const { return channels > 0 ? samples.size() / static_cast<size_t>(channels) : 0; }
    bool empty() const { return samples.empty(); }
};

bool LoadAudio(const std::filesystem::path& path, AudioBuffer& audio, std::string& error,
               std::atomic_bool* cancel = nullptr);
bool WriteWav(const std::filesystem::path& path, const AudioBuffer& audio, std::string& error);

// 将用户有权访问的本地 NCM 文件解包为临时 MP3/FLAC 文件。不会联网，也不会修改源文件。
bool DecryptNcm(const std::filesystem::path& input,
               const std::filesystem::path& outputDirectory,
               std::filesystem::path& outputFile,
               std::string& error,
               std::atomic_bool* cancel = nullptr);

std::vector<std::filesystem::path> ScanAudioFiles(const std::filesystem::path& directory);

} // namespace uar
