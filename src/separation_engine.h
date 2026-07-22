#pragma once

#include "audio_engine.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>

namespace uar {

struct SeparationConfig {
    std::filesystem::path bsModel;
    std::filesystem::path melModel;
    std::wstring outputSuffix;
    int segmentSize = 256; // 保留与参考界面一致的参数；Roformer 导出使用固定 8 秒块。
    int overlap = 8;
    int batchSize = 1;
};

struct SeparationCallbacks {
    std::function<void(const std::string&)> log;
    std::function<void(double)> progress;
};

class SeparationEngine {
public:
    SeparationEngine();
    ~SeparationEngine();
    SeparationEngine(const SeparationEngine&) = delete;
    SeparationEngine& operator=(const SeparationEngine&) = delete;

    bool ValidateModels(const SeparationConfig& config, std::string& error) const;
    bool ProcessFile(const std::filesystem::path& input,
                     const std::filesystem::path& outputDirectory,
                     const SeparationConfig& config,
                     const SeparationCallbacks& callbacks,
                     std::atomic_bool& cancel,
                     std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace uar
