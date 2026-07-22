#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include "../third_party/miniaudio.h"

#include "audio_engine.h"

#include <bcrypt.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>

#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#endif

namespace uar {
namespace {

std::string PathText(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::string WinError(const char* prefix, NTSTATUS status = 0) {
    std::ostringstream out;
    out << prefix;
    if (status != 0) out << " (0x" << std::hex << static_cast<unsigned long>(status) << ")";
    return out.str();
}

bool AesEcbDecrypt(const std::vector<unsigned char>& encrypted,
                   const unsigned char* keyBytes,
                   std::vector<unsigned char>& plain,
                   std::string& error) {
    if (encrypted.empty() || encrypted.size() % 16 != 0) {
        error = "NCM 密钥块长度无效";
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    std::vector<unsigned char> object;
    DWORD objectLength = 0;
    DWORD resultLength = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (status < 0) { error = WinError("无法初始化 AES", status); return false; }
    const wchar_t ecb[] = BCRYPT_CHAIN_MODE_ECB;
    status = BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE,
                               reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(ecb)),
                               sizeof(ecb), 0);
    if (status < 0) { error = WinError("无法设置 AES-ECB", status); BCryptCloseAlgorithmProvider(algorithm, 0); return false; }
    DWORD cb = 0;
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                               reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &cb, 0);
    if (status < 0) { error = WinError("无法获取 AES 参数", status); BCryptCloseAlgorithmProvider(algorithm, 0); return false; }
    object.resize(objectLength);
    status = BCryptGenerateSymmetricKey(algorithm, &key, object.data(), objectLength,
                                        const_cast<PUCHAR>(keyBytes), 16, 0);
    if (status < 0) { error = WinError("无法创建 AES 密钥", status); BCryptCloseAlgorithmProvider(algorithm, 0); return false; }
    plain.resize(encrypted.size());
    status = BCryptDecrypt(key, const_cast<PUCHAR>(encrypted.data()),
                           static_cast<ULONG>(encrypted.size()), nullptr, nullptr, 0,
                           plain.data(), static_cast<ULONG>(plain.size()), &resultLength,
                           BCRYPT_BLOCK_PADDING);
    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0 || resultLength == 0) {
        error = WinError("NCM AES 解密失败", status);
        return false;
    }
    plain.resize(resultLength);
    return true;
}

bool ReadExact(std::ifstream& file, void* destination, size_t size) {
    file.read(static_cast<char*>(destination), static_cast<std::streamsize>(size));
    return file.good() && static_cast<size_t>(file.gcount()) == size;
}

bool Skip(std::ifstream& file, std::uint64_t count) {
    file.seekg(static_cast<std::streamoff>(count), std::ios::cur);
    return file.good();
}

bool IsExtension(const std::filesystem::path& path, std::initializer_list<const wchar_t*> extensions) {
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), towlower);
    for (const auto* item : extensions) {
        if (ext == item) return true;
    }
    return false;
}

} // namespace

bool LoadAudio(const std::filesystem::path& path, AudioBuffer& audio, std::string& error) {
    audio = {};
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 44100);
    ma_decoder decoder{};
    const ma_result initResult = ma_decoder_init_file_w(path.c_str(), &config, &decoder);
    if (initResult != MA_SUCCESS) {
        error = "无法解码音频文件: " + PathText(path);
        return false;
    }
    ma_uint64 frameCount = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount) != MA_SUCCESS || frameCount == 0) {
        ma_decoder_uninit(&decoder);
        error = "音频没有可读采样: " + PathText(path);
        return false;
    }
    if (frameCount > kMaxDecodedFrames ||
        frameCount > (std::numeric_limits<size_t>::max() / 2)) {
        ma_decoder_uninit(&decoder);
        error = "音频文件过大";
        return false;
    }
    audio.sampleRate = 44100;
    audio.channels = 2;
    audio.samples.resize(static_cast<size_t>(frameCount) * 2);
    ma_uint64 readFrames = 0;
    const ma_result readResult = ma_decoder_read_pcm_frames(&decoder, audio.samples.data(), frameCount, &readFrames);
    ma_decoder_uninit(&decoder);
    if (readResult != MA_SUCCESS && readResult != MA_AT_END) {
        audio = {};
        error = "读取音频采样失败: " + PathText(path);
        return false;
    }
    audio.samples.resize(static_cast<size_t>(readFrames) * 2);
    return !audio.samples.empty();
}

bool WriteWav(const std::filesystem::path& path, const AudioBuffer& audio, std::string& error) {
    if (audio.empty() || audio.channels != 2) {
        error = "输出音频数据为空或声道数不支持";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32,
                                                      static_cast<ma_uint32>(audio.channels),
                                                      static_cast<ma_uint32>(audio.sampleRate));
    ma_encoder encoder{};
    if (ma_encoder_init_file_w(path.c_str(), &config, &encoder) != MA_SUCCESS) {
        error = "无法创建 WAV 输出: " + PathText(path);
        return false;
    }
    ma_uint64 written = 0;
    const ma_result result = ma_encoder_write_pcm_frames(&encoder, audio.samples.data(),
                                                         static_cast<ma_uint64>(audio.frames()), &written);
    ma_encoder_uninit(&encoder);
    if (result != MA_SUCCESS || written != audio.frames()) {
        error = "写入 WAV 失败: " + PathText(path);
        return false;
    }
    return true;
}

bool DecryptNcm(const std::filesystem::path& input,
               const std::filesystem::path& outputDirectory,
               std::filesystem::path& outputFile,
               std::string& error,
               std::atomic_bool* cancel) {
    std::ifstream file(input, std::ios::binary);
    if (!file) { error = "无法打开 NCM 文件: " + PathText(input); return false; }
    std::array<unsigned char, 8> header{};
    if (!ReadExact(file, header.data(), header.size()) ||
        !(header[0] == 'C' && header[1] == 'T' && header[2] == 'E' && header[3] == 'N' &&
          header[4] == 'F' && header[5] == 'D' && header[6] == 'A' && header[7] == 'M')) {
        error = "不是有效的 NCM 文件: " + PathText(input); return false;
    }
    if (!Skip(file, 2)) { error = "NCM 文件头损坏"; return false; }
    std::uint32_t keyLength = 0;
    if (!ReadExact(file, &keyLength, sizeof(keyLength)) || keyLength < 16 || keyLength > 16 * 1024 * 1024) {
        error = "NCM 密钥段长度无效"; return false;
    }
    std::vector<unsigned char> encryptedKey(keyLength);
    if (!ReadExact(file, encryptedKey.data(), encryptedKey.size())) { error = "NCM 密钥段不完整"; return false; }
    for (auto& value : encryptedKey) value ^= 0x64;
    static constexpr unsigned char coreKey[16] = {
        0x68,0x7A,0x48,0x52,0x41,0x6D,0x73,0x6F,0x35,0x6B,0x49,0x6E,0x62,0x61,0x78,0x57
    };
    std::vector<unsigned char> decryptedKey;
    if (!AesEcbDecrypt(encryptedKey, coreKey, decryptedKey, error) || decryptedKey.size() <= 17) return false;
    std::array<unsigned char, 256> keyBox{};
    for (int i = 0; i < 256; ++i) keyBox[static_cast<size_t>(i)] = static_cast<unsigned char>(i);
    unsigned char last = 0;
    unsigned char offset = 0;
    for (int i = 0; i < 256; ++i) {
        const unsigned char swap = keyBox[static_cast<size_t>(i)];
        const unsigned char c = static_cast<unsigned char>((swap + last + decryptedKey[17 + offset]) & 0xFF);
        ++offset;
        if (offset >= decryptedKey.size() - 17) offset = 0;
        keyBox[static_cast<size_t>(i)] = keyBox[c];
        keyBox[c] = swap;
        last = c;
    }
    std::uint32_t metadataLength = 0;
    if (!ReadExact(file, &metadataLength, sizeof(metadataLength)) || metadataLength > 64 * 1024 * 1024 || !Skip(file, metadataLength)) {
        error = "NCM 元数据段损坏"; return false;
    }
    if (!Skip(file, 5)) { error = "NCM 封面段损坏"; return false; }
    std::uint32_t coverFrameLength = 0;
    std::uint32_t coverLength = 0;
    if (!ReadExact(file, &coverFrameLength, sizeof(coverFrameLength)) ||
        !ReadExact(file, &coverLength, sizeof(coverLength)) ||
        coverLength > coverFrameLength || !Skip(file, coverFrameLength)) {
        error = "NCM 音频段损坏"; return false;
    }
    std::vector<unsigned char> encoded((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (encoded.empty()) { error = "NCM 不包含音频数据"; return false; }
    std::vector<unsigned char> decoded(encoded.size());
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (cancel && cancel->load()) { error = "已取消"; return false; }
        const unsigned int j = static_cast<unsigned int>((i + 1) & 0xFF);
        decoded[i] = static_cast<unsigned char>(encoded[i] ^
            keyBox[(keyBox[j] + keyBox[(keyBox[j] + j) & 0xFF]) & 0xFF]);
    }
    const bool isFlac = decoded.size() >= 4 && decoded[0] == 'f' && decoded[1] == 'L' &&
                        decoded[2] == 'a' && decoded[3] == 'C';
    std::error_code ec;
    std::filesystem::create_directories(outputDirectory, ec);
    auto stem = input.stem();
    outputFile = outputDirectory / (stem.wstring() + (isFlac ? L".flac" : L".mp3"));
    std::ofstream out(outputFile, std::ios::binary);
    if (!out) { error = "无法写入 NCM 临时文件"; return false; }
    out.write(reinterpret_cast<const char*>(decoded.data()), static_cast<std::streamsize>(decoded.size()));
    if (!out.good()) { error = "写入 NCM 临时文件失败"; return false; }
    return true;
}

std::vector<std::filesystem::path> ScanAudioFiles(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) return files;
    for (std::filesystem::recursive_directory_iterator it(directory, ec), end; it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        if (IsExtension(it->path(), {L".ncm", L".wav", L".mp3", L".flac"})) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace uar
