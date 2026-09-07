# 模型与推理参考

[返回项目首页](../README.md)

从原首页整理，来源提交 `77c2966`。以下命令均从仓库根目录执行。历史实验、语料与权重核对记录不代表本次文档更新重新执行了这些验证；当前使用入口与限制见首页。

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
