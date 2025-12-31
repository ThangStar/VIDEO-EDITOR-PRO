#include "ExportManager.h"
#include "../Rendering/TextureRenderer.h"
#include "../Timeline/EffectLayer.h"
#include "../Timeline/TimelineManager.h"
#include "../Video/VideoPlayer.h"
#include <GLFW/glfw3.h>
#include <future>
#include <glad/glad.h>
#include <iostream>
#include <vector>


ExportManager::ExportManager(TimelineManager *timeline, VideoPlayer *player)
    : m_TimelineManager(timeline), m_VideoPlayer(player), m_Encoder(nullptr),
      m_MainWindow(nullptr), m_IsExporting(false), m_IsFinished(false),
      m_CancelRequested(false), m_Progress(0.0f), m_OffscreenWindow(nullptr) {}

ExportManager::~ExportManager() {
  CancelExport();
  if (m_ExportThread.joinable()) {
    m_ExportThread.join();
  }
  // Conversion thread handling
  if (m_ConversionThread.joinable()) {
    m_ConversionThread.join();
  }
  // Free Frame Pool
  for (auto *frame : m_FramePool) {
    av_frame_free(&frame);
  }
  m_FramePool.clear();

  if (m_Encoder) {
    delete m_Encoder;
    m_Encoder = nullptr;
  }
  if (m_OffscreenWindow) {
    glfwDestroyWindow(m_OffscreenWindow);
    m_OffscreenWindow = nullptr;
  }
}

bool ExportManager::StartExport(const std::string &outputFile, int width,
                                int height, int fps) {
  if (m_IsExporting)
    return false;

  // CRITICAL: Wait for previous export thread to finish completely
  if (m_ExportThread.joinable()) {
    m_ExportThread.join();
  }

  // Now safe to destroy offscreen window
  if (m_OffscreenWindow) {
    glfwDestroyWindow(m_OffscreenWindow);
    m_OffscreenWindow = nullptr;
  }

  if (m_MainWindow) {
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_OffscreenWindow =
        glfwCreateWindow(width, height, "ExportContext", nullptr, m_MainWindow);
    if (!m_OffscreenWindow) {
      std::cerr << "[ExportManager] Failed to create offscreen window!"
                << std::endl;
    } else {
      std::cout << "[ExportManager] Offscreen window created successfully."
                << std::endl;
    }
  }

  m_IsExporting = true;
  m_IsFinished = false;
  m_CancelRequested = false;
  m_Progress = 0.0f;

  m_ExportThread = std::thread(&ExportManager::ExportThreadFunc, this,
                               outputFile, width, height, fps);

  return true;
}

void ExportManager::CancelExport() {
  if (m_IsExporting) {
    m_CancelRequested = true;
  }
}

// ... (Existing includes and constructor/destructor)

// Helper for Worker
void ExportManager::EncodingWorkerFunc() {
  std::cout << "[ExportManager] Encoding Worker Started" << std::endl;
  m_WorkerRunning = true;

  while (true) {
    YuvPacket packet;
    {
      std::unique_lock<std::mutex> lock(m_YuvMutex);
      m_YuvCondVar.wait(
          lock, [this] { return !m_YuvQueue.empty() || m_CancelRequested; });

      if (m_CancelRequested)
        break;
      if (m_YuvQueue.empty())
        continue;

      packet = m_YuvQueue.front();
      m_YuvQueue.erase(m_YuvQueue.begin());
    }

    if (packet.is_stop_signal) {
      break;
    }

    if (packet.frame && m_Encoder) {
      m_Encoder->EncodeYUVFrame(packet.frame);
      ReleaseFrame(packet.frame); // Return to pool
    }
  }

  m_WorkerRunning = false;
  std::cout << "[ExportManager] Encoding Worker Finished" << std::endl;
}

void ExportManager::ConversionWorkerFunc() {
  std::cout << "[ExportManager] Conversion Worker Started" << std::endl;
  // We need SWS context from Encoder. Wait until encoder is ready?
  // It should be initialized by the time we push packets.

  // NOTE: m_Encoder is initialized in ExportThread, but this thread starts same
  // time. However, we only process queue items. ExportThread pushes items AFTER
  // init. So logic is safe.

  while (true) {
    AsyncPacket rgbPacket;
    {
      std::unique_lock<std::mutex> lock(m_QueueMutex);
      m_QueueCondVar.wait(
          lock, [this] { return !m_AsyncQueue.empty() || m_CancelRequested; });

      if (m_CancelRequested)
        break;
      if (m_AsyncQueue.empty())
        continue;

      rgbPacket = std::move(m_AsyncQueue.front());
      m_AsyncQueue.erase(m_AsyncQueue.begin());
    }

    if (rgbPacket.is_stop_signal) {
      // Forward stop signal to Encoding Queue
      std::lock_guard<std::mutex> lock(m_YuvMutex);
      m_YuvQueue.push_back({nullptr, true});
      m_YuvCondVar.notify_one();
      break;
    }

    if (!m_Encoder)
      continue;

    // Convert RGB -> YUV
    AVFrame *yuvFrame = AcquireFrame();
    if (!yuvFrame) {
      std::cerr << "[ExportManager] Failed to acquire frame from pool!"
                << std::endl;
      continue;
    }

    // sws_scale
    SwsContext *swsCtx = m_Encoder->GetSwsContext();
    if (swsCtx) {
      const uint8_t *srcSlice[1] = {rgbPacket.data.data()};
      int srcStride[1] = {m_Encoder->m_Width * 3};

      int result =
          sws_scale(swsCtx, srcSlice, srcStride, 0, m_Encoder->m_Height,
                    yuvFrame->data, yuvFrame->linesize);
    }

    // Push to Encoding Queue
    {
      std::unique_lock<std::mutex> lock(m_YuvMutex);
      m_YuvQueue.push_back({yuvFrame, false});
      // Backpressure could be added here if YUV queue gets too big
      while (m_YuvQueue.size() > 10 &&
             !m_CancelRequested) { // Cap at 10 YUV frames
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        lock.lock();
      }
    }
    m_YuvCondVar.notify_one();
  }
  std::cout << "[ExportManager] Conversion Worker Finished" << std::endl;
}

AVFrame *ExportManager::AcquireFrame() {
  std::lock_guard<std::mutex> lock(m_PoolMutex);
  if (!m_FramePool.empty()) {
    AVFrame *frame = m_FramePool.back();
    m_FramePool.pop_back();
    return frame;
  }

  // Allocate new
  if (!m_Encoder)
    return nullptr;
  AVFrame *frame = av_frame_alloc();
  frame->format = m_Encoder->GetPixFormat();
  frame->width = m_Encoder->m_Width;
  frame->height = m_Encoder->m_Height;
  if (av_frame_get_buffer(frame, 32) < 0) {
    av_frame_free(&frame);
    return nullptr;
  }
  return frame;
}

void ExportManager::ReleaseFrame(AVFrame *frame) {
  if (!frame)
    return;
  // Reset?
  std::lock_guard<std::mutex> lock(m_PoolMutex);
  m_FramePool.push_back(frame);
}

void ExportManager::ExportThreadFunc(std::string outputFile, int width,
                                     int height, int fps) {
  VideoEncoder *localEncoder = new VideoEncoder();
  m_Encoder = localEncoder;

  // ... [Same Tech Stack verification logs] ...

  int alignedWidth = (width + 15) & ~15;
  int alignedHeight = (height + 15) & ~15;
  if (alignedWidth != width || alignedHeight != height) {
    width = alignedWidth;
    height = alignedHeight;
  }

  int bitrate = 8000000;
  if (width >= 1920)
    bitrate = 15000000;
  if (width >= 3840)
    bitrate = 40000000;

  // Initialize Encoder with FASTEST settings prioritized
  if (!localEncoder->Initialize(outputFile, width, height, fps, bitrate)) {
    std::cerr << "Failed to initialize export encoder!" << std::endl;
    delete localEncoder;
    m_Encoder = nullptr;
    m_IsExporting = false;
    m_IsFinished = true;
    return;
  }

  double duration = m_TimelineManager->GetTotalDuration();
  if (duration <= 0.001)
    duration = 1.0;

  int totalFrames = static_cast<int>(duration * fps);
  double frameDuration = 1.0 / fps;

  // Start Encoding Worker
  m_EncodingThread = std::thread(&ExportManager::EncodingWorkerFunc, this);
  // Start Conversion Worker
  m_ConversionThread = std::thread(&ExportManager::ConversionWorkerFunc, this);

  TextureRenderer *renderer = nullptr;
  bool usingPBO = false;
  GLsync fences[2] = {nullptr, nullptr};

  if (m_OffscreenWindow) {
    glfwMakeContextCurrent(m_OffscreenWindow);
    glViewport(0, 0, width, height);

    if (gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
      renderer = new TextureRenderer();
      if (renderer->Initialize()) {
        renderer->CreateFramebuffer(width, height);
        renderer->SetFlipY(false);

        renderer->SetFilterParams(m_EffectParams.brightness,
                                  m_EffectParams.contrast,
                                  m_EffectParams.saturation);
        renderer->SetEffectParams(m_EffectParams.vignette, m_EffectParams.grain,
                                  m_EffectParams.aberration,
                                  m_EffectParams.sepia);
        renderer->SetFilterType(m_EffectParams.filterType);

        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGenBuffers(2, m_PBOs);

        // PBO Setup
        size_t bufferSize = (size_t)width * height * 3 + 4096;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBOs[0]);
        glBufferData(GL_PIXEL_PACK_BUFFER, bufferSize, nullptr, GL_STREAM_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBOs[1]);
        glBufferData(GL_PIXEL_PACK_BUFFER, bufferSize, nullptr, GL_STREAM_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        if (glGetError() == GL_NO_ERROR)
          usingPBO = true;
      } else {
        delete renderer;
        renderer = nullptr;
      }
    }
  }

  VideoPlayer tempPlayer;
  std::string currentLoadedFile = "";
  std::vector<uint8_t> pixelBuffer;
  size_t frameSize = width * height * 3;

  // Main Render Loop
  for (int i = 0; i < totalFrames; ++i) {
    if (m_CancelRequested)
      break;

    double currentTime = i * frameDuration;
    bool frameRendered = false;
    Clip *currentClip = nullptr;

    auto &tracks = m_TimelineManager->GetTracks();
    if (!tracks.empty())
      currentClip = tracks[0].GetClipAtTime(currentTime);

    if (currentClip) {
      if (!tempPlayer.IsLoaded() ||
          currentClip->filepath != currentLoadedFile) {
        if (tempPlayer.LoadVideo(currentClip->filepath))
          currentLoadedFile = currentClip->filepath;
      }

      if (tempPlayer.IsLoaded()) {
        double localTime = currentClip->ToLocalTime(currentTime);
        double videoFPS = tempPlayer.GetFPS() > 0 ? tempPlayer.GetFPS() : 30.0;
        double videoFrameDuration = 1.0 / videoFPS;

        // Seek Logic
        if (i == 0 || currentClip->filepath != currentLoadedFile ||
            std::abs(localTime - tempPlayer.GetCurrentTime()) > 0.5) {
          tempPlayer.Seek(localTime, false);
        }

        // Decode Loop
        while (tempPlayer.GetCurrentTime() + videoFrameDuration < localTime) {
          if (!tempPlayer.DecodeNextFrame())
            break;
        }
        const uint8_t *data = tempPlayer.GetFrameData();

        if (data && renderer) {
          if (tempPlayer.GetWidth() > 0 && tempPlayer.GetHeight() > 0) {
            if (i == 0 || renderer->GetTextureID() == 0) {
              renderer->CreateTexture(tempPlayer.GetWidth(),
                                      tempPlayer.GetHeight());
            }
          }
          renderer->UpdateTexture(tempPlayer.GetFrameData(),
                                  tempPlayer.GetWidth(),
                                  tempPlayer.GetHeight());

          // Apply Effects
          renderer->SetBlurEffect(0.0f, 0);
          auto activeEffects = m_TimelineManager->GetActiveEffects(currentTime);
          for (auto *effect : activeEffects) {
            if (!effect)
              continue;
            if (effect->type >= EffectLayer::BLUR_GAUSSIAN &&
                effect->type <= EffectLayer::BLUR_ZOOM) {
              float intensity = effect->params.count("intensity")
                                    ? effect->params.at("intensity")
                                    : 0.5f;
              int blurType = effect->params.count("blurType")
                                 ? (int)effect->params.at("blurType")
                                 : 0;
              renderer->SetBlurEffect(intensity, blurType);
            }
          }

          // Render to FBO
          renderer->BindFramebuffer();
          glViewport(0, 0, width, height);
          glClear(GL_COLOR_BUFFER_BIT);
          renderer->UpdateTexture(tempPlayer.GetFrameData(),
                                  tempPlayer.GetWidth(),
                                  tempPlayer.GetHeight());
          renderer->RenderTexture(0, 0, (float)width, (float)height);

          if (usingPBO) {
            int currentPBO = i % 2;
            int previousPBO = (i + 1) % 2;

            // Async Readback Pipeline

            // 1. Process Previous Frame (if ready)
            if (i > 0) {
              // Wait for fence of previous frame
              if (fences[previousPBO]) {
                glClientWaitSync(fences[previousPBO],
                                 GL_SYNC_FLUSH_COMMANDS_BIT,
                                 1000000000); // 1s timeout
                glDeleteSync(fences[previousPBO]);
                fences[previousPBO] = nullptr;
              }

              glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBOs[previousPBO]);
              GLubyte *ptr =
                  (GLubyte *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
              if (ptr) {
                // COPY to buffer and push to worker
                AsyncPacket packet;
                packet.data.resize(frameSize);
                memcpy(packet.data.data(), ptr, frameSize);

                {
                  std::lock_guard<std::mutex> lock(m_QueueMutex);
                  m_AsyncQueue.push_back(std::move(packet));
                }
                m_QueueCondVar.notify_one();

                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
              }
            }

            // 2. Issue Read for Current Frame
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBOs[currentPBO]);
            glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, 0);

            if (fences[currentPBO])
              glDeleteSync(fences[currentPBO]);
            fences[currentPBO] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
          } else {
            // Slow separate path
            renderer->GetRGBPixels(pixelBuffer, width, height);

            AsyncPacket packet;
            packet.data = pixelBuffer; // Copy
            {
              std::lock_guard<std::mutex> lock(m_QueueMutex);
              m_AsyncQueue.push_back(std::move(packet));
            }
            m_QueueCondVar.notify_one();
          }

          renderer->UnbindFramebuffer();
          frameRendered = true;
        }
      }
    }

    // Handle empty frames (Black)
    if (!frameRendered && renderer) {
      // ... [Logic for black frame similar to above, omitted for brevity but
      // should be same async copy pattern] For clone demo simplicity, let's
      // just push black frame
      AsyncPacket packet;
      packet.data.resize(frameSize, 0);
      {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        m_AsyncQueue.push_back(std::move(packet));
      }
      m_QueueCondVar.notify_one();
    }

    m_Progress = (float)(i + 1) / totalFrames;
    if (i % 60 == 0)
      std::cout << "[ExportManager] Rendered " << i << "/" << totalFrames
                << std::endl;

    // Flow Control: Don't let queue get too big (max 5 frames) to avoid OOM
    {
      std::unique_lock<std::mutex> lock(m_QueueMutex);
      // Spin-wait if queue is full (backpressure)
      // Ideally use another CV, but simple sleep is fine for this
      while (m_AsyncQueue.size() > 5 && !m_CancelRequested) {
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        lock.lock();
      }
    }
  }

  // Capture LAST frame from PBO if needed
  if (usingPBO && !m_CancelRequested && totalFrames > 0) {
    int lastPBO = (totalFrames - 1) % 2;
    if (fences[lastPBO]) {
      glClientWaitSync(fences[lastPBO], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
      glDeleteSync(fences[lastPBO]);
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBOs[lastPBO]);
    GLubyte *ptr = (GLubyte *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    if (ptr) {
      AsyncPacket packet;
      packet.data.resize(frameSize);
      memcpy(packet.data.data(), ptr, frameSize);

      {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        m_AsyncQueue.push_back(std::move(packet));
      }
      m_QueueCondVar.notify_one();
      glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  }

  // Send Stop Signal
  {
    std::lock_guard<std::mutex> lock(m_QueueMutex);
    AsyncPacket stopPacket;
    stopPacket.is_stop_signal = true;
    m_AsyncQueue.push_back(std::move(stopPacket));
  }
  m_QueueCondVar.notify_one();

  // Wait for Encoder Worker
  if (m_ConversionThread.joinable()) {
    m_ConversionThread.join();
  }
  if (m_EncodingThread.joinable()) {
    m_EncodingThread.join();
  }

  // Cleanup GL
  if (usingPBO) {
    // Cleanup Fences
    for (int i = 0; i < 2; i++) {
      if (fences[i])
        glDeleteSync(fences[i]);
    }
    glDeleteBuffers(2, m_PBOs);
  }

  if (renderer) {
    delete renderer;
    renderer = nullptr;
  }

  glfwMakeContextCurrent(nullptr);

  if (localEncoder) {
    localEncoder->Finalize();
    delete localEncoder;
  }
  m_Encoder = nullptr;

  m_IsExporting = false;
  m_IsFinished = true;
}