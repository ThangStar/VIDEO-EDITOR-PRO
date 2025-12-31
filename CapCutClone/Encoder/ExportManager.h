#pragma once

#include "VideoEncoder.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <queue>
#include <string>
#include <thread>

struct GLFWwindow;

class TimelineManager;
class VideoPlayer;
struct AVFrame;
struct SwsContext;

class ExportManager {
public:
  ExportManager(TimelineManager *timeline, VideoPlayer *player);
  ~ExportManager();

  // Set main window for context sharing
  void SetMainWindow(GLFWwindow *mainWindow) { m_MainWindow = mainWindow; }
  GLFWwindow *GetMainWindow() const { return m_MainWindow; }

  // Start export in a separate thread
  bool StartExport(const std::string &outputFile, int width, int height,
                   int fps);

  // Stop/Cancel export
  void CancelExport();

  // Stats
  float GetProgress() const { return m_Progress; } // 0.0 to 1.0
  bool IsExporting() const { return m_IsExporting; }
  bool IsFinished() const { return m_IsFinished; }

  struct EffectParams {
    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float vignette = 0.0f;
    float grain = 0.0f;
    float aberration = 0.0f;
    bool sepia = false;
    int filterType = 0; // 0 = None, 1-15 = Various filters
  };

  void SetEffectParams(const EffectParams &params) { m_EffectParams = params; }

private:
  TimelineManager *m_TimelineManager;
  VideoPlayer *m_VideoPlayer;
  VideoEncoder *m_Encoder;
  GLFWwindow *m_MainWindow; // Reference to main window for context sharing

  EffectParams m_EffectParams;

  // Threading
  std::thread m_ExportThread;
  std::atomic<bool> m_IsExporting;
  std::atomic<bool> m_IsFinished;
  std::atomic<bool> m_CancelRequested;
  std::atomic<float> m_Progress;

  // Asynchronous Encoding
  std::thread m_EncodingThread;
  std::mutex m_QueueMutex;
  std::condition_variable m_QueueCondVar;
  std::vector<uint8_t>
      m_CurrentEncodingBuffer; // Reuse buffer to avoid allocs if possible
  struct EncodedFrameRequest {
    std::vector<uint8_t> data;
    bool isLast = false;
  };
  std::vector<EncodedFrameRequest>
      m_EncodingQueue; // Using vector as queue for easier memory reuse
                       // management? No, std::queue or list. Let's use
                       // std::queue.
  // Actually, let's use a simpler queue of raw buffers.
  // But we need to handle "Last Frame" signal.

  // Better Queue Definition:
  struct AsyncPacket {
    std::vector<uint8_t> data; // Pixel data
    bool is_stop_signal = false;
  };
  std::vector<AsyncPacket> m_AsyncQueue; // Using vector + mutex
  bool m_WorkerRunning = false;

  void EncodingWorkerFunc();
  void ConversionWorkerFunc(); // CPU YUV Conversion Worker

  // Shared Context managed by Main Thread
  GLFWwindow *m_OffscreenWindow;

  void ExportThreadFunc(std::string outputFile, int width, int height, int fps);

  // Conversion/Encoding Pipeline
  std::thread m_ConversionThread;
  std::mutex m_YuvMutex;
  std::condition_variable m_YuvCondVar;

  // YUV Queue
  struct YuvPacket {
    AVFrame *frame = nullptr;
    bool is_stop_signal = false;
  };
  std::vector<YuvPacket> m_YuvQueue;

  // Frame Pool to avoid re-allocation
  std::vector<AVFrame *> m_FramePool;
  std::mutex m_PoolMutex;

  AVFrame *AcquireFrame();
  void ReleaseFrame(AVFrame *frame);

  // Multi-threaded Conversion
  std::vector<struct SwsContext *> m_SliceContexts;
  int m_NumThreads = 8; // Use 8 threads for aggressive CPU usage
  std::vector<std::thread> m_ThreadPool;

  // PBO IDs for async transfer
  unsigned int m_PBOs[2]; // Using unsigned int directly to avoid GL dependency
                          // in header if possible, or include glad/glfw
};
