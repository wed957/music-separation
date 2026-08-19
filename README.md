# Music Separation (UARMusic)

Music Separation 是一款面向 Windows 的离线人声与伴奏分离工具。选择歌曲目录和输出目录后，程序会递归扫描歌曲，并为每首歌曲生成一份人声轨和一份伴奏轨。

- 全程本地处理，不上传歌曲
- 默认亮色界面，可在右上角切换暗色界面
- 使用两套 Roformer ONNX 模型串行推理并融合结果
- 原生 Win32 界面，程序本体约 `2 MB`
- 输入文件不会被修改

> 当前版本只提供 Windows x64 构建。音频分离需要较多计算时间和内存，实际速度取决于处理器、歌曲长度及可用内存。

![Music Separation 亮色界面](docs/images/app-light.png)

<details>
<summary>查看暗色界面</summary>

![Music Separation 暗色界面](docs/images/app.png)

</details>

## 下载与使用

从 [GitHub Releases](https://github.com/wed957/music-separation/releases) 下载 `UARMusic-win-x64.zip`，完整解压后运行 `UARMusic.exe`。不要只复制可执行文件，程序还需要同目录中的 ONNX Runtime 动态库和 `models` 目录。

1. 点击“选择歌曲目录”，选择包含歌曲的文件夹。
2. 点击“选择输出目录”，选择用于保存分轨结果的文件夹。
3. 点击“开始分轨”。程序会扫描输入目录及其所有子目录。
4. 在处理记录中查看进度。处理期间可点击同一按钮停止任务。

建议让输入目录和输出目录彼此独立。若输出目录位于输入目录内，后续再次扫描时可能把之前生成的 WAV 文件当作新输入。

## 支持的音频格式

程序按扩展名扫描以下文件：

| 类型 | 扩展名 | 解码方式 |
| --- | --- | --- |
| NCM | `.ncm` | 本地解包后按 MP3 或 FLAC 解码 |
| 常用音频 | `.wav`、`.wave`、`.mp3`、`.flac` | 内置解码器，失败时尝试系统解码器 |
| MPEG-4 / AAC | `.aac`、`.m4a`、`.m4b`、`.mp4` | Windows Media Foundation |
| Windows Media | `.wma` | Windows Media Foundation |
| AIFF | `.aif`、`.aiff` | Windows Media Foundation |
| Ogg / Opus | `.ogg`、`.oga`、`.opus` | Windows Media Foundation |
| 其他已扫描格式 | `.alac`、`.ape`、`.ac3`、`.amr`、`.mka` | Windows Media Foundation |

“已扫描”不代表任意 Windows 环境都一定能够解码。除 WAV、MP3、FLAC 和 NCM 内嵌的 MP3/FLAC 外，其余格式取决于当前 Windows 版本、系统组件、文件容器和实际编码。以下文件不保证可用：

- 受 DRM 保护或需要在线授权的文件
- 内容损坏、扩展名与真实编码不一致的文件
- 系统没有对应解码器的扩展格式
- 使用非标准封装或特殊编码参数的文件

兼容性优先时，建议先将歌曲转换为 WAV、MP3 或 FLAC。NCM 功能仅用于用户本人有权访问的本地文件。

## 分离方法与模型参数

程序采用双模型集成。两条推理路径分别估计人声的复数频谱掩码，在时域得到两份人声波形后等权融合。伴奏不由第三个模型单独预测，而是用原始混合信号减去融合后的人声：

```text
vocals       = 0.5 * BS-Roformer(mixture) + 0.5 * Mel-Band-Roformer(mixture)
instrumental = clamp(mixture - vocals, -1.0, 1.0)
```

### 架构背景

[BS-RoFormer](https://arxiv.org/abs/2309.02612) 由 Wei-Tsung Lu、Ju-Chiang Wang、Qiuqiang Kong 和 Yun-Ning Hung 提出。它沿用 Band-Split RNN 的频带拆分思路，将复数频谱划分为不重叠子带，再交替沿时间轴和频带轴执行 Transformer。RoPE 旋转位置编码用于表达帧序和频带位置。论文中的完整系统使用 MUSDB18-HQ 与额外 500 首歌曲训练，并在 Sound Demixing Challenge 2023 的音乐分离赛道取得第一名；这是论文系统的背景结果，不等同于本项目双模型融合后的实测指标。

[Mel-Band RoFormer](https://arxiv.org/abs/2310.01809) 是 Ju-Chiang Wang、Wei-Tsung Lu 和 Minz Won 的后续工作。它保留时间轴/频带轴两级 Transformer 和 RoPE，将经验设定的不重叠频带改为相互重叠的 Mel 频带。频带密度随听觉尺度分配，多个频带覆盖同一 FFT 单元时再聚合预测。论文在 MUSDB18-HQ 上报告了人声、鼓和 other 等分轨相对 BS-RoFormer 的改进。

两种架构的公开 PyTorch 实现来自 [lucidrains/BS-RoFormer](https://github.com/lucidrains/BS-RoFormer)，本项目所用训练配置和检查点体系来自 [ZFTurbo/Music-Source-Separation-Training](https://github.com/ZFTurbo/Music-Source-Separation-Training)。

### 权重与转换来源

| 本地文件 | 已确认的上游资产 | 检查点背景 | 当前 ONNX 形式 |
| --- | --- | --- | --- |
| `bs_roformer_quantized.onnx` | [`xycld/BS-RoFormer-ONNX`](https://huggingface.co/xycld/BS-RoFormer-ONNX) 的 `bs_roformer_ep317_sdr12.9755_quantized_uint8.onnx` | viperx 训练的 `model_bs_roformer_ep_317_sdr_12.9755.ckpt`；上游模型卡记录训练数据为 MUSDB18-HQ 加 500 首额外歌曲 | ONNX opset 18，uint8 动态量化，约 `159.8M` 参数 |
| `mel_band_roformer_vocals.onnx` | [ZFTurbo v1.0.0](https://github.com/ZFTurbo/Music-Source-Separation-Training/releases/tag/v1.0.0) 的 `model_vocals_mel_band_roformer_sdr_8.42.ckpt` | 检查点内部标识为 `model_ep_122_sdr_8.4262`；发布页未记录具体训练数据构成 | ONNX opset 17，float32，`33,666,916` 个权重及常量值 |

BS 文件与上游量化文件的大小及 SHA-256 完全一致，只在项目中缩短了文件名。上游模型卡把原始权重归于 viperx，并指向 TRvlvr 的公开模型仓库；`12.9755` 是检查点发布名称中的 SDR 标记，不是本项目重新测得的结果。

Mel 文件与 ZFTurbo 检查点的模型结构完全对齐，且抽取出的 `337` 个同名张量均逐字节一致，没有发现不匹配项。对应的公开配置是 [`config_vocals_mel_band_roformer.yaml`](https://github.com/ZFTurbo/Music-Source-Separation-Training/blob/main/configs/config_vocals_mel_band_roformer.yaml)：隐藏维度 `192`、`8` 层、`8` 个注意力头、head 维度 `64`、`60` 个 Mel 频带、单人声目标。当前文件是移除 STFT/iSTFT 的网络核心导出，ONNX 元数据只记录 `PyTorch 2.10.0`，没有保留转换脚本作者信息。

上述论文与代码实现采用公开许可，不代表训练素材中的全部音频自动取得相同许可。发布或商用前，应分别核对代码、模型权重和训练数据的授权条件。两个发布名称中的 SDR 数值来自各自上游，不宜直接横向比较，也不代表任意歌曲上的固定分离质量。

### 处理流程

1. 输入音频统一解码为 `44.1 kHz`、双声道、32 位浮点采样。
2. 每个声道使用 `2048` 点 Hann 窗进行短时傅里叶变换，得到 `1025` 个非负频率单元。窗口以帧中心对齐，边界采用反射填充。
3. BS-Roformer 接收按时间排列的双声道复数频谱，输出人声复数掩码。
4. Mel-Band Roformer 按 60 个 Slaney Mel 三角频带的覆盖范围重排频率单元，输出第二份人声复数掩码。重叠频带还原到线性频率轴时取平均值。
5. 两条路径分别执行逆短时傅里叶变换，并对相邻分块做加权重叠合成。
6. 两份人声估计以 `0.5 : 0.5` 融合；伴奏由混合信号相减得到。

### 有效推理参数

| 参数 | BS-Roformer | Mel-Band Roformer |
| --- | ---: | ---: |
| 模型文件 | `bs_roformer_quantized.onnx` | `mel_band_roformer_vocals.onnx` |
| 隐藏维度 / 主干层数 | `512` / `12` | `192` / `8` |
| 注意力头数 / head 维度 | `8` / `64` | `8` / `64` |
| 时间 / 频带 Transformer 深度 | `1` / `1` | `1` / `1` |
| 频带划分 | `62` 个不重叠频带 | `60` 个重叠 Mel 频带 |
| 掩码估计器深度 | `2` | `2` |
| 目标 stem 数 | `1`，人声 | `1`，人声 |
| 权重规模 / 精度 | 约 `159.8M`，矩阵权重动态 uint8 量化 | `33,666,916` 个 float32 权重及常量值 |
| 采样率 / 声道 | `44,100 Hz` / `2` | `44,100 Hz` / `2` |
| FFT 点数 / 频率单元 | `2048` / `1025` | `2048` / `1025` |
| STFT 步长 | `441` 采样，`10 ms` | `512` 采样，约 `11.61 ms` |
| 单块长度 | `352,800` 采样，`8 s` | `131,584` 采样，约 `2.984 s` |
| 单块帧数 | `801` | `258` |
| 分块策略 | 步长 `176,400` 采样，固定 `50%` 重叠 | 默认 `8%` 重叠，步长 `121,057` 采样 |
| 边缘合成 | `35,280` 采样线性淡入淡出 | 按相邻块的实际交叠长度线性加权 |
| ONNX 输入形状 | `[1, 801, 4100]` | `[1, 3958, 258, 2]` |
| ONNX 输出形状 | `[1, 1, 2050, 801, 2]` | `[1, 1, 3958, 258, 2]` |
| 输出含义 | 双声道人声复数掩码 | 按 Mel 频带展开的人声复数掩码 |

BS 输入的 `4100` 由 `1025` 个频率单元、`2` 个声道及实部/虚部组成。Mel 输入的 `3958` 是 60 个 Mel 频带覆盖的有效频率单元按双声道展开后的数量，末维 `2` 表示实部和虚部。

两套模型串行运行：BS 会话完成后立即释放，再创建 Mel 会话，避免两套模型同时常驻内存。ONNX Runtime 启用全部图优化，执行模式为顺序执行；算子内线程数按处理器逻辑线程数设置，范围为 `1` 至 `4`，算子间线程数固定为 `1`。当前发布包使用 CPU 推理，不包含 CUDA 执行提供程序。

源码中的 `segmentSize=256` 和 `batchSize=1` 是保留的兼容字段，当前固定形状的 ONNX 推理没有使用它们，因此不列为可调参数。实际参与分块调度的是 Mel 路径的 `overlap`，默认值为 `8`，内部限制范围为 `0` 至 `90`。

## 输出与限制

每首歌曲默认生成两个文件：

```text
歌曲名_vocals.wav
歌曲名_instrumental.wav
```

输出规格固定为：

| 项目 | 规格 |
| --- | --- |
| 采样率 | `44.1 kHz` |
| 声道 | 双声道 |
| 格式 | `32` 位浮点 WAV |
| 单首最大长度 | `15` 分钟 |

需要注意：

- 程序不会覆盖输入歌曲。
- 再次处理同名歌曲时，同名输出 WAV 可能被替换。
- 输入目录中的子目录结构不会复制到输出目录。不同子目录里具有相同文件名的歌曲会产生同名输出，请分批处理或使用不同输出目录。
- NCM 解包产生的中间文件保存在输出目录的 `.ncm_decoded` 子目录。
- 两份浮点 WAV 的体积通常明显大于原始压缩音频，请预留足够磁盘空间。

## 发布包

完整运行目录约 `315 MB`，其中 `UARMusic.exe` 约 `2 MB`。主要体积来自两套内置分离模型，而不是界面或解码组件。

```text
UARMusic-win-x64/
|-- UARMusic.exe
|-- onnxruntime.dll
|-- onnxruntime_providers_shared.dll
|-- THIRD_PARTY_NOTICES.md
|-- licenses/
|   |-- lucide/
|   `-- onnxruntime/
`-- models/
    |-- bs_roformer_quantized.onnx
    `-- mel_band_roformer_vocals.onnx
```

模型文件校验信息：

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `bs_roformer_quantized.onnx` | `165,858,657` 字节 | `B86B6224FF34D6D34C2530E0D41495B6A2C12DB46ACC1A7A5C7F64D2352807E9` |
| `mel_band_roformer_vocals.onnx` | `135,441,533` 字节 | `BD42C4B3339F43B41BACEB2A0204C941744F43911A47F006DF5B62E560F91CC1` |

## 从源码构建

### 环境要求

- Windows 10 或 Windows 11 x64
- CMake `3.20` 或更高版本
- Ninja
- 支持 C++17 的 MinGW-w64 工具链
- Git LFS

仓库包含 miniaudio、Lucide 图标数据和裁剪后的 ONNX Runtime `1.20.1` Windows x64 SDK。两套模型通过 Git LFS 管理。

### 获取模型并编译

在仓库根目录运行：

```powershell
git lfs install
git lfs pull

cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

构建完成后，可直接运行：

```text
build/bin/UARMusic.exe
```

生成独立发布目录：

```powershell
cmake --install build --prefix dist/UARMusic-win-x64
```

### 自定义依赖路径

CMake 支持以下缓存参数：

| 参数 | 用途 |
| --- | --- |
| `UAR_ORT_ROOT` | ONNX Runtime Windows x64 SDK 根目录 |
| `UAR_BS_MODEL_FILE` | BS Roformer 模型文件路径 |
| `UAR_MEL_MODEL_FILE` | Mel-band Roformer 模型文件路径 |

示例：

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DUAR_ORT_ROOT=C:/sdk/onnxruntime `
  -DUAR_BS_MODEL_FILE=D:/models/bs_roformer_quantized.onnx `
  -DUAR_MEL_MODEL_FILE=D:/models/mel_band_roformer_vocals.onnx
```

模型参数用于改变文件位置，不表示支持任意 ONNX 模型。配置阶段会严格检查上表中的文件大小和 SHA-256；文件不匹配时构建会停止。自定义 ONNX Runtime 目录必须包含：

```text
include/onnxruntime_cxx_api.h
lib/onnxruntime.lib
bin/onnxruntime.dll
bin/onnxruntime_providers_shared.dll
```

官方 SDK 若把 DLL 放在 `lib` 目录，构建脚本也会自动识别。

## 项目结构

```text
music-separation/
|-- src/                     # Win32 界面、音频解码和分离引擎
|-- resources/               # Windows 资源与应用清单
|-- models/                  # Git LFS 管理的 ONNX 模型
|-- third_party/             # miniaudio、Lucide 许可和 ONNX Runtime SDK
|-- docs/images/             # README 界面截图
|-- testdata/                # 本地冒烟测试音频
|-- CMakeLists.txt
|-- THIRD_PARTY_NOTICES.md
`-- README.md
```

核心模块：

- `src/main.cpp`：窗口、主题、目录选择、任务调度和进度界面
- `src/audio_engine.cpp`：目录扫描、音频解码、重采样、NCM 解包和 WAV 写入
- `src/separation_engine.cpp`：双模型推理、分块、融合和双轨生成
- `src/lucide_icons.h`：界面统一使用的 Lucide 矢量图标

## 隐私与使用边界

- 分离过程在本机完成，程序不会上传歌曲。
- 程序不会修改原始输入文件。
- 项目不提供受保护内容的分发能力。
- 使用 NCM、本地歌曲和分离结果时，请确保拥有相应访问权和使用权。
- 模型属于第三方训练资产，使用、再分发或商业发布前请确认其来源与许可条件。

第三方组件及许可信息见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 故障排查

### 程序无法启动

确认已完整解压发布包，且 `UARMusic.exe`、两个 ONNX Runtime DLL 和 `models` 目录保持原有相对位置。当前构建仅支持 Windows x64。

### 找到了歌曲但无法处理

先用 WAV、MP3 或 FLAC 文件验证。对于 AAC、M4A、WMA、Ogg、Opus 等扩展格式，失败通常表示 Windows Media Foundation 无法解码该文件；可先用可信的音频工具转为 WAV 或 FLAC。受 DRM 保护的文件无法处理。

### 提示歌曲太长

单首歌曲解码后的长度上限为 15 分钟。请先裁剪或分段后再处理。

### CMake 提示模型不完整

确认 Git LFS 已安装，并重新拉取模型：

```powershell
git lfs install
git lfs pull
git lfs ls-files
```

若问题仍存在，请用 `Get-FileHash -Algorithm SHA256 <模型路径>` 与本文的校验值比较。

### 输出文件互相覆盖

程序按输入文件的基础名称生成输出，不保留输入目录层级。把同名歌曲分批放入不同输入目录，或为每批任务选择不同输出目录。
