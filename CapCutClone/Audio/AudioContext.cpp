#define MINIAUDIO_IMPLEMENTATION
#include "AudioContext.h"
#include <iostream>

AudioContext::AudioContext() : m_IsInitialized(false) {}

AudioContext::~AudioContext() { Close(); }

bool AudioContext::Init(int sampleRate, int channels) {
  if (m_IsInitialized)
    Close();

  ma_device_config deviceConfig =
      ma_device_config_init(ma_device_type_playback);
  deviceConfig.playback.format = ma_format_f32;
  deviceConfig.playback.channels = channels;
  deviceConfig.sampleRate = sampleRate;
  deviceConfig.dataCallback = DataCallback;
  deviceConfig.pUserData = this;

  if (ma_device_init(nullptr, &deviceConfig, &m_Device) != MA_SUCCESS) {
    std::cerr << "Failed to initialize playback device." << std::endl;
    return false;
  }

  // Initialize Ring Buffer
  // Let's allocate 1 second worth of buffer
  size_t bufferSizeInFrames = sampleRate;
  size_t bufferSizeInBytes = bufferSizeInFrames * channels * sizeof(float);

  m_RBData.resize(bufferSizeInBytes);

  // ma_pcm_rb expects custom allocation if we don't use ma_pcm_rb_init_malloc
  // (which does malloc) But we want to control memory with vector if possible,
  // or just use the malloc version from miniaudio for simplicity. Let's use
  // ma_pcm_rb_init which requires a pre-allocated buffer.
  ma_result result = ma_pcm_rb_init(ma_format_f32, channels, bufferSizeInFrames,
                                    m_RBData.data(), nullptr, &m_RingBuffer);
  if (result != MA_SUCCESS) {
    std::cerr << "Failed to initialize ring buffer." << std::endl;
    ma_device_uninit(&m_Device);
    return false;
  }

  if (ma_device_start(&m_Device) != MA_SUCCESS) {
    std::cerr << "Failed to start playback device." << std::endl;
    ma_device_uninit(&m_Device);
    return false;
  }

  m_IsInitialized = true;
  std::cout << "[AudioContext] Initialized Audio: " << sampleRate << "Hz, "
            << channels << " Channels" << std::endl;
  return true;
}

void AudioContext::Close() {
  if (m_IsInitialized) {
    ma_device_uninit(&m_Device);
    ma_pcm_rb_uninit(&m_RingBuffer);
    m_IsInitialized = false;
  }
}

void AudioContext::Clear() {
  if (m_IsInitialized) {
    ma_device_stop(&m_Device);
    ma_pcm_rb_reset(&m_RingBuffer);
    ma_device_start(&m_Device);
  }
}

void AudioContext::PushAudio(const float *data, int frameCount) {
  if (!m_IsInitialized)
    return;

  // Write to ring buffer
  ma_uint32 framesToWrite = frameCount;
  ma_uint32 framesWritten = 0;

  while (framesWritten < framesToWrite) {
    void *pWriteBuffer;
    ma_uint32 framesThisIteration = framesToWrite - framesWritten;

    if (ma_pcm_rb_acquire_write(&m_RingBuffer, &framesThisIteration,
                                &pWriteBuffer) != MA_SUCCESS) {
      break;
    }

    memcpy(pWriteBuffer, data + (framesWritten * m_RingBuffer.channels),
           framesThisIteration * m_RingBuffer.channels * sizeof(float));

    if (ma_pcm_rb_commit_write(&m_RingBuffer, framesThisIteration) !=
        MA_SUCCESS) {
      break;
    }

    framesWritten += framesThisIteration;
    if (framesThisIteration ==
        0) { // Should not happen if acquire success, but safety
      break;
    }
  }
}

size_t AudioContext::GetAvailableWriteFrames() {
  if (!m_IsInitialized)
    return 0;
  return ma_pcm_rb_available_write(&m_RingBuffer);
}

void AudioContext::DataCallback(ma_device *pDevice, void *pOutput,
                                const void *pInput, ma_uint32 frameCount) {
  AudioContext *context = (AudioContext *)pDevice->pUserData;
  if (!context)
    return;

  // Read from ring buffer into output
  ma_uint32 framesToRead = frameCount;
  ma_uint32 framesReadTotal = 0;

  while (framesReadTotal < framesToRead) {
    void *pReadBuffer;
    ma_uint32 framesThisIteration = framesToRead - framesReadTotal;

    if (ma_pcm_rb_acquire_read(&context->m_RingBuffer, &framesThisIteration,
                               &pReadBuffer) != MA_SUCCESS) {
      break;
    }

    memcpy(
        (float *)pOutput + (framesReadTotal * context->m_RingBuffer.channels),
        pReadBuffer,
        framesThisIteration * context->m_RingBuffer.channels * sizeof(float));

    if (ma_pcm_rb_commit_read(&context->m_RingBuffer, framesThisIteration) !=
        MA_SUCCESS) {
      break;
    }

    framesReadTotal += framesThisIteration;
    if (framesThisIteration == 0) {
      break;
    }
  }

  // If not enough data, silence the rest
  if (framesReadTotal < frameCount) {
    // Calculate offset in bytes: framesReadTotal * channels * sizeof(float)
    size_t offset =
        framesReadTotal * pDevice->playback.channels * sizeof(float);
    size_t remainingBytes = (frameCount - framesReadTotal) *
                            pDevice->playback.channels * sizeof(float);

    memset((uint8_t *)pOutput + offset, 0, remainingBytes);
  }
}
