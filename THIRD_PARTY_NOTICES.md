# 第三方文件说明

本项目源码直接提交了 `third_party/miniaudio.h` 和裁剪后的 ONNX Runtime Windows x64 SDK，私有仓库还通过 Git LFS 保存音频分离模型。大型调试符号、下载缓存和构建目录不写入 Git 历史。

## miniaudio

- 版本：`0.11.25`
- 项目地址：<https://github.com/mackron/miniaudio>
- 许可：Public Domain 或 MIT-0，具体条款见头文件末尾的许可声明

## ONNX Runtime

- 版本：`1.20.1`
- 项目地址：<https://github.com/microsoft/onnxruntime>
- 许可：MIT
- 官方 `LICENSE` 和 `ThirdPartyNotices.txt` 保存在 `third_party/onnxruntime/`

## 音频分离模型

发布包使用 `mel_band_roformer_vocals.onnx`。模型属于第三方训练资产，使用、再分发和商业发布前必须遵守模型提供者的许可及来源要求。当前私有仓库使用 Git LFS 保存模型二进制，正式发布时也会把模型放入完整运行包，并在发布说明中记录 SHA-256。

## 本地 NCM 文件

NCM 解包功能只面向用户本人有权访问的本地文件。项目不联网、不上传歌曲，也不提供受保护内容的分发服务。
