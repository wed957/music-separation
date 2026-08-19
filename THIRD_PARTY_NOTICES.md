# 第三方文件说明

本项目源码直接提交了 `third_party/miniaudio.h`、Lucide 图标几何和裁剪后的 ONNX Runtime Windows x64 SDK，私有仓库还通过 Git LFS 保存音频分离模型。大型调试符号、下载缓存和构建目录不写入 Git 历史。

## miniaudio

- 版本：`0.11.25`
- 项目地址：<https://github.com/mackron/miniaudio>
- 许可：Public Domain 或 MIT-0，具体条款见头文件末尾的许可声明

## ONNX Runtime

- 版本：`1.20.1`
- 项目地址：<https://github.com/microsoft/onnxruntime>
- 许可：MIT
- 官方 `LICENSE` 和 `ThirdPartyNotices.txt` 保存在 `third_party/onnxruntime/`

## Lucide

- 项目地址：<https://github.com/lucide-icons/lucide>
- 许可：ISC
- 界面图标基于 Lucide 的矢量几何，由应用在运行时统一绘制
- 完整许可保存在 `third_party/lucide/LICENSE`

## 音频分离模型

发布包内置以下两个模型文件：

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `bs_roformer_quantized.onnx` | `165,858,657` 字节 | `B86B6224FF34D6D34C2530E0D41495B6A2C12DB46ACC1A7A5C7F64D2352807E9` |
| `mel_band_roformer_vocals.onnx` | `135,441,533` 字节 | `BD42C4B3339F43B41BACEB2A0204C941744F43911A47F006DF5B62E560F91CC1` |

程序串行执行两个模型，再在波形层融合两次分离结果，避免两个模型同时常驻内存。模型属于第三方训练资产，使用、再分发和商业发布前必须遵守模型提供者的许可及来源要求。当前仓库使用 Git LFS 保存两个模型二进制，正式发布时也会把它们放入完整运行包，并在发布说明中记录上述 SHA-256。

### 架构与实现

- BS-RoFormer 论文：<https://arxiv.org/abs/2309.02612>
- Mel-Band RoFormer 论文：<https://arxiv.org/abs/2310.01809>
- PyTorch 架构实现：<https://github.com/lucidrains/BS-RoFormer>（MIT）
- 训练与推理框架：<https://github.com/ZFTurbo/Music-Source-Separation-Training>（MIT）

### 权重来源

- `bs_roformer_quantized.onnx` 与 `xycld/BS-RoFormer-ONNX` 发布的 `bs_roformer_ep317_sdr12.9755_quantized_uint8.onnx` 在大小和 SHA-256 上一致。该模型卡将原始 `model_bs_roformer_ep_317_sdr_12.9755.ckpt` 归于 viperx，并指向 TRvlvr 的公开模型仓库：<https://huggingface.co/xycld/BS-RoFormer-ONNX>
- `mel_band_roformer_vocals.onnx` 来自 ZFTurbo v1.0.0 发布的 `model_vocals_mel_band_roformer_sdr_8.42.ckpt`，检查点内部标识为 `model_ep_122_sdr_8.4262`：<https://github.com/ZFTurbo/Music-Source-Separation-Training/releases/tag/v1.0.0>
- Mel ONNX 与该检查点有 `337` 个同名张量逐字节一致且无不匹配项；模型结构对应 `configs/config_vocals_mel_band_roformer.yaml`。ONNX 元数据未记录转换脚本作者。

代码仓库的 MIT 许可不自动覆盖训练音频及其相关权利。两个检查点发布名称中的 SDR 数值属于上游标记，不是本项目独立复现的评测结果；再分发或商业使用前应单独核对模型权重和训练数据的许可条件。

## 本地 NCM 文件

NCM 解包功能只面向用户本人有权访问的本地文件。项目不联网、不上传歌曲，也不提供受保护内容的分发服务。
