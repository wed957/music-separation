# Algorithm Refactor (2026-09-10)

The separation path now has a shared signal-processing module and explicit
model contracts. The model weights and tensor layouts are unchanged.

## Signal path

- `signal_processing` owns the radix-2 FFT, center-aligned reflect-padded
  STFT/iSTFT, and planar/interleaved conversion. Spectrogram indexing carries
  its frequency count instead of relying on a hidden global constant.
- Mel inference mirrors the upstream generic path: long tracks are extended by
  the overlap border, substantial tail chunks use reflect padding, and only
  samples that belong to the original track are overlap-added.
- The Mel input continues to use the model's binary `freqs_per_band` layout;
  the triangle amplitudes are not applied because the upstream network also
  thresholds the filter bank before indexing.
- Model sessions reject wrong input/output ranks or dimensions before any
  expensive inference starts.

## Decode and output behavior

- miniaudio reads bounded frame blocks and checks cancellation between blocks.
- Media Foundation validates the next decoded buffer against the 15-minute
  limit before appending it and sanitizes non-finite float samples.
- Media Foundation resampling uses a 32-tap windowed-sinc kernel instead of
  linear interpolation, with a low-pass cutoff for downsampling.
- Each generated WAV is written to a process-specific temporary file and then
  replaced with `MoveFileExW(..., MOVEFILE_WRITE_THROUGH)`, so an interrupted
  encode cannot leave a truncated final file.

## Verification

The following checks pass in the repository environment:

```text
g++ -std=c++17 -Wall -Wextra -Wpedantic tests/signal_processing_test.cpp src/signal_processing.cpp
g++ -std=c++17 -Wall -Wextra -Wpedantic tests/audio_engine_resampling_test.cpp src/audio_engine.cpp
MinGW C++ model probes: short sample and 30,000-frame boundary sample, both models, exit code 0
```

The short probe output is byte-identical to the previous implementation. The
long probe exercises Mel edge padding and produces finite stereo float WAVs at
44.1 kHz with the original frame count.
