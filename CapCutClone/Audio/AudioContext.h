#pragma once

#include "miniaudio.h"
#include <atomic>
#include <mutex>
#include <vector>

class AudioContext {
public:
  AudioContext();
  ~AudioContext();

  bool Init(int sampleRate, int channels);
  void Close();

  // Push decoupled float audio data to the internal ring buffer
  // Expects planar or interleaved data depending on implementation,
  // but miniaudio's ring buffer handles bytes.
  // We will standardize on Interleaved Float32 for simplicity in this context
  // if possible, or just copy raw bytes if we match the device format.
  void PushAudio(const float *data, int frameCount);

  // Check if we have enough data (to avoid underruns)
  size_t GetAvailableWriteFrames();

  // Clear the audio buffer (useful for seeking)
  void Clear();

private:
  static void DataCallback(ma_device *pDevice, void *pOutput,
                           const void *pInput, ma_uint32 frameCount);

  ma_device m_Device;
  ma_pcm_rb m_RingBuffer;
  bool m_IsInitialized;

  // Buffer for the ring buffer
  std::vector<uint8_t> m_RBData;
};
