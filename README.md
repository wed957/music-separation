# UAR Music

UAR Music 是一个面向普通用户的 Windows 歌曲处理工具。它把一首歌保存成两份：

- **唱歌声**：主要保留人声
- **音乐声**：主要保留乐器和背景音乐

程序只在本机处理文件，不上传歌曲，也不会修改原文件。界面只保留“选择歌曲、选择保存位置、开始制作”三个步骤。

![歌曲小助手界面](docs/images/app.png)

## 功能

- 支持 `.ncm`、`.wav`、`.mp3`、`.flac`
- 自动扫描所选文件夹及其子文件夹
- 每首歌生成 `_唱歌声.wav` 和 `_音乐声.wav`
- 内置离线处理文件，正式发布包无需额外下载
- 支持 Windows 高分辨率屏幕和 4K / 200% 显示缩放
- 可在处理过程中停止任务

## 下载和使用

完整 Windows 发布包放在 [GitHub Releases](https://github.com/wed957/UARMusic/releases)。下载 `UARMusic-win-x64.zip` 并完整解压，不要只单独复制 `UARMusic.exe`。

1. 双击 `UARMusic.exe`。
2. 点击“选择歌曲文件夹”。
3. 点击“选择保存位置”。
4. 点击“开始制作”。

输出文件示例：

```text
歌曲名_唱歌声.wav
歌曲名_音乐声.wav
```

## 发布包结构

```text
UARMusic-win-x64/
├── UARMusic.exe
├── onnxruntime.dll
├── onnxruntime_providers_shared.dll
└── models/
    └── mel_band_roformer_vocals.onnx
```

完整运行目录约 `149 MB`（约 `142 MiB`）。模型文件的预期 SHA-256 为：

```text
BD42C4B3339F43B41BACEB2A0204C941744F43911A47F006DF5B62E560F91CC1
```

## 源码目录

```text
UARMusic/
├── src/                     # Win32 界面、音频读写和歌曲处理
├── resources/               # 应用清单和 Windows 资源
├── third_party/             # miniaudio 和裁剪后的 ONNX Runtime SDK
├── models/                  # 通过 Git LFS 保存内置模型
├── testdata/                # 简短的本地冒烟测试音频
├── CMakeLists.txt
├── THIRD_PARTY_NOTICES.md
└── README.md
```

构建目录、下载缓存、完整 ONNX Runtime 调试包和发布包均由 `.gitignore` 排除。仓库只保留约 12 MB 的最小 ONNX Runtime 构建文件；当前使用的模型通过 Git LFS 跟踪，旧模型和实验文件不会提交。

## 开发者构建

### 环境要求

- Windows 10 或 Windows 11 x64
- CMake `3.20+`
- Ninja
- 支持 C++17 的 MinGW-w64 工具链
- Git LFS `3.x`

仓库已经包含 `third_party/miniaudio.h` 和 ONNX Runtime `1.20.1` 的最小 Windows x64 SDK，模型保存在 Git LFS 中。克隆后先确认大文件已经拉取：

```powershell
git lfs install
git lfs pull
```

### 编译

默认使用仓库内的 ONNX Runtime 和 `models/mel_band_roformer_vocals.onnx`，无需填写本机依赖路径：

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --parallel
```

程序和运行所需文件会自动复制到：

```text
build/bin/
```

也可以生成独立发布目录：

```powershell
cmake --install build --prefix dist/UARMusic-win-x64
```

需要替换依赖时，可以额外传入 `-DUAR_ORT_ROOT=<SDK目录>` 或 `-DUAR_MODEL_FILE=<模型文件>`。如果缺少头文件、DLL、导入库或模型，CMake 会在配置阶段给出明确错误。

## 验证情况

- Release 构建通过，编译器警告已启用：`-Wall -Wextra -Wpedantic`
- 已在 `1000x720` 和 `2520x1640` 窗口尺寸下检查界面
- 已用 `testdata/in/sample.wav` 完成真实处理
- 输出为 `44.1 kHz`、双声道、32 位浮点 WAV，时长与输入一致

## 隐私和使用边界

- 程序不联网，不上传歌曲
- 原歌曲不会被覆盖
- NCM 解包只用于用户本人有权访问的本地文件
- 项目不提供受保护内容的分发能力
- 模型属于第三方训练资产，使用和再分发前应确认其来源与许可

第三方组件和许可说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
