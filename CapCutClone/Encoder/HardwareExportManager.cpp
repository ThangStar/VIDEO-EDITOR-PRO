#include "HardwareExportManager.h"
#include "../Rendering/TextureRenderer.h"
#include "../Timeline/EffectLayer.h"
#include "../Timeline/TimelineManager.h"
#include "../Video/VideoPlayer.h"
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <iostream>

#ifdef USE_VULKAN
#include "../Vulkan/VulkanExportManager.h"
#endif

// ============================================================================
// Constructor / Destructor
// ============================================================================

HardwareExportManager::HardwareExportManager(TimelineManager *timeline,
                                             VideoPlayer *player)
    : m_TimelineManager(timeline), m_VideoPlayer(player), m_MainWindow(nullptr),
      m_OffscreenWindow(nullptr), m_IsExporting(false), m_IsFinished(false),
      m_CancelRequested(false), m_Progress(0.0f), m_FormatCtx(nullptr),
      m_CodecCtx(nullptr), m_Codec(nullptr), m_Stream(nullptr),
      m_SwsCtx(nullptr), m_Packet(nullptr), m_FrameCount(0),
      m_HwDeviceCtx(nullptr), m_UsingHardwareAccel(false) {}

HardwareExportManager::~HardwareExportManager() {
  CancelExport();

  // Wait for all threads to finish
  if (m_RenderThread.joinable())
    m_RenderThread.join();
  if (m_EncoderThread.joinable())
    m_EncoderThread.join();

  Cleanup();
}

// ============================================================================
// Initialization
// ============================================================================

bool HardwareExportManager::Initialize(const Config &config) {
  m_Config = config;

  // Validate configuration
  if (m_Config.width <= 0 || m_Config.height <= 0 || m_Config.fps <= 0) {
    m_ErrorMessage = "Invalid export dimensions or FPS";
    return false;
  }

  // Align dimensions to 16 for hardware encoder
  m_Config.width = (m_Config.width + 15) & ~15;
  m_Config.height = (m_Config.height + 15) & ~15;

  // Set default max bitrate if not specified (VBR mode)
  if (m_Config.rateControl == RateControl::VBR && m_Config.maxBitrate == 0) {
    m_Config.maxBitrate = m_Config.bitrate * 5 / 4; // 25% headroom
  }

  std::cout << "[HardwareExportManager] Initialized with:" << std::endl;
  std::cout << "  Resolution: " << m_Config.width << "x" << m_Config.height
            << std::endl;
  std::cout << "  FPS: " << m_Config.fps << std::endl;
  std::cout << "  Codec: "
            << (m_Config.codec == Codec::H264 ? "H.264" : "H.265") << std::endl;
  std::cout << "  Bitrate Control: ";
  switch (m_Config.rateControl) {
  case RateControl::VBR:
    std::cout << "VBR (" << m_Config.bitrate / 1000000 << " Mbps avg, "
              << m_Config.maxBitrate / 1000000 << " Mbps max)" << std::endl;
    break;
  case RateControl::CBR:
    std::cout << "CBR (" << m_Config.bitrate / 1000000 << " Mbps)" << std::endl;
    break;
  case RateControl::CQP:
    std::cout << "CQP (Quality: " << m_Config.quality << ")" << std::endl;
    break;
  }

#ifdef USE_VULKAN
  // Initialize Vulkan RGB→NV12 converter
  m_VulkanExporter = std::make_unique<VulkanExportManager>();
  if (!m_VulkanExporter->Initialize(m_Config.width, m_Config.height)) {
    std::cerr << "[HardwareExportManager] Vulkan init failed, will use fallback"
              << std::endl;
    m_VulkanExporter.reset(); // Disable Vulkan if init fails
  } else {
    std::cout << "[HardwareExportManager] ✅ Vulkan RGB→NV12 converter active"
              << std::endl;
  }
#endif

  return true;
}

bool HardwareExportManager::StartExport() {
  if (m_IsExporting)
    return false;

  // Wait for previous export if any
  if (m_RenderThread.joinable())
    m_RenderThread.join();
  if (m_EncoderThread.joinable())
    m_EncoderThread.join();

  // Clean up previous offscreen window
  if (m_OffscreenWindow) {
    glfwDestroyWindow(m_OffscreenWindow);
    m_OffscreenWindow = nullptr;
  }

#ifdef USE_CUDA
  // Initialize CUDA converter BEFORE OpenGL context to avoid device conflicts
  m_CUDAConverter = std::make_unique<CUDAConverter>();
  if (m_CUDAConverter->Initialize(m_Config.width, m_Config.height)) {
    std::cout
        << "[HardwareExportManager] CUDA converter initialized successfully"
        << std::endl;
  } else {
    std::cerr << "[HardwareExportManager] CUDA init failed, falling back to CPU"
              << std::endl;
    m_CUDAConverter.reset();
  }
#endif

  // Create offscreen rendering context
  if (m_MainWindow) {
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_OffscreenWindow =
        glfwCreateWindow(m_Config.width, m_Config.height, "ExportContext",
                         nullptr, m_MainWindow);
    if (!m_OffscreenWindow) {
      m_ErrorMessage = "Failed to create offscreen OpenGL context";
      std::cerr << "[HardwareExportManager] " << m_ErrorMessage << std::endl;
      return false;
    }
    std::cout << "[HardwareExportManager] Offscreen window created successfully"
              << std::endl;
  }

  // Reset state
  m_IsExporting = true;
  m_IsFinished = false;
  m_CancelRequested = false;
  m_Progress = 0.0f;
  m_ErrorMessage.clear();
  m_FrameCount = 0;

  // Clear YUV queue
  {
    std::lock_guard<std::mutex> lock(m_YUVMutex);
    while (!m_YUVQueue.empty()) {
      if (m_YUVQueue.front().frame)
        ReleaseFrame(m_YUVQueue.front().frame);
      m_YUVQueue.pop();
    }
  }

  // Launch threads (Phase 1 optimization: removed ProcessThread)
  m_RenderThread = std::thread(&HardwareExportManager::RenderThreadFunc, this);
  m_EncoderThread =
      std::thread(&HardwareExportManager::EncoderThreadFunc, this);

  return true;
}

void HardwareExportManager::CancelExport() {
  if (m_IsExporting) {
    m_CancelRequested = true;

    // Wake up waiting encoder thread
    m_YUVCondVar.notify_all();
  }
}

// ============================================================================
// Thread A: Render Thread (Decode + Render to RGB)
// ============================================================================

void HardwareExportManager::RenderThreadFunc() {
  std::cout << "[RenderThread] Started" << std::endl;

  if (!m_OffscreenWindow) {
    m_ErrorMessage = "No offscreen window available";
    m_IsExporting = false;
    m_IsFinished = true;
    return;
  }

  glfwMakeContextCurrent(m_OffscreenWindow);
  glViewport(0, 0, m_Config.width, m_Config.height);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    m_ErrorMessage = "Failed to initialize GLAD";
    m_IsExporting = false;
    m_IsFinished = true;
    return;
  }

  // Create texture renderer
  TextureRenderer renderer;
  if (!renderer.Initialize()) {
    m_ErrorMessage = "Failed to initialize texture renderer";
    m_IsExporting = false;
    m_IsFinished = true;
    return;
  }

  renderer.CreateFramebuffer(m_Config.width, m_Config.height);
  renderer.SetFlipY(false);

  // Apply effect parameters
  renderer.SetFilterParams(m_EffectParams.brightness, m_EffectParams.contrast,
                           m_EffectParams.saturation);
  renderer.SetEffectParams(m_EffectParams.vignette, m_EffectParams.grain,
                           m_EffectParams.aberration, m_EffectParams.sepia);
  renderer.SetFilterType(m_EffectParams.filterType);

  // Setup PBOs for async readback
  unsigned int pbos[2];
  glGenBuffers(2, pbos);
  size_t bufferSize = m_Config.width * m_Config.height * 3 + 4096;

  glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos[0]);
  glBufferData(GL_PIXEL_PACK_BUFFER, bufferSize, nullptr, GL_STREAM_READ);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos[1]);
  glBufferData(GL_PIXEL_PACK_BUFFER, bufferSize, nullptr, GL_STREAM_READ);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

  bool usingPBO = (glGetError() == GL_NO_ERROR);
  GLsync fences[2] = {nullptr, nullptr};

  // Calculate total frames
  double duration = m_TimelineManager->GetTotalDuration();
  if (duration <= 0.001)
    duration = 1.0;
  int totalFrames = static_cast<int>(duration * m_Config.fps);
  double frameDuration = 1.0 / m_Config.fps;

  VideoPlayer tempPlayer;
  std::string currentLoadedFile = "";
  std::vector<uint8_t> pixelBuffer;
  size_t frameSize = m_Config.width * m_Config.height * 3;

  // Main rendering loop
  for (int i = 0; i < totalFrames && !m_CancelRequested; ++i) {
    double currentTime = i * frameDuration;
    bool frameRendered = false;

    // Get current clip from timeline
    auto &tracks = m_TimelineManager->GetTracks();
    Clip *currentClip = nullptr;
    if (!tracks.empty())
      currentClip = tracks[0].GetClipAtTime(currentTime);

    if (currentClip) {
      // Load video if needed
      if (!tempPlayer.IsLoaded() ||
          currentClip->filepath != currentLoadedFile) {
        if (tempPlayer.LoadVideo(currentClip->filepath))
          currentLoadedFile = currentClip->filepath;
      }

      if (tempPlayer.IsLoaded()) {
        double localTime = currentClip->ToLocalTime(currentTime);
        double videoFPS = tempPlayer.GetFPS() > 0 ? tempPlayer.GetFPS() : 30.0;
        double videoFrameDuration = 1.0 / videoFPS;

        // Seek if necessary
        if (i == 0 || currentClip->filepath != currentLoadedFile ||
            std::abs(localTime - tempPlayer.GetCurrentTime()) > 0.5) {
          tempPlayer.Seek(localTime, false);
        }

        // Decode to target frame (decode ahead in larger batches)
        int decodeAttempts = 0;
        while (tempPlayer.GetCurrentTime() + videoFrameDuration < localTime &&
               decodeAttempts < 10) {
          if (!tempPlayer.DecodeNextFrame())
            break;
          decodeAttempts++;
        }

        const uint8_t *data = tempPlayer.GetFrameData();
        if (data) {
          // Create texture if needed
          if (i == 0 || renderer.GetTextureID() == 0) {
            renderer.CreateTexture(tempPlayer.GetWidth(),
                                   tempPlayer.GetHeight());
          }

          // Update texture and render to framebuffer
          renderer.UpdateTexture(data, tempPlayer.GetWidth(),
                                 tempPlayer.GetHeight());

          // Apply blur effects if any
          auto activeEffects = m_TimelineManager->GetActiveEffects(currentTime);
          for (auto *effect : activeEffects) {
            if (effect && effect->type >= EffectLayer::BLUR_GAUSSIAN &&
                effect->type <= EffectLayer::BLUR_ZOOM) {
              float intensity = effect->params.count("intensity")
                                    ? effect->params.at("intensity")
                                    : 0.5f;
              int blurType =
                  effect->params.count("blurType")
                      ? static_cast<int>(effect->params.at("blurType"))
                      : 0;
              renderer.SetBlurEffect(intensity, blurType);
            }
          }

          // Render to FBO
          renderer.BindFramebuffer();
          glViewport(0, 0, m_Config.width, m_Config.height);
          glClear(GL_COLOR_BUFFER_BIT);
          renderer.RenderTexture(0, 0, static_cast<float>(m_Config.width),
                                 static_cast<float>(m_Config.height));

          // Read pixels using PBO double buffering
          if (usingPBO) {
            int currentPBO = i % 2;
            int previousPBO = (i + 1) % 2;

            // Process previous frame
            if (i > 0) {
              if (fences[previousPBO]) {
                glClientWaitSync(fences[previousPBO],
                                 GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
                glDeleteSync(fences[previousPBO]);
                fences[previousPBO] = nullptr;
              }

              glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos[previousPBO]);
              GLubyte *ptr =
                  (GLubyte *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
              if (ptr) {
                // Phase 3: CUDA GPU conversion in async PBO path
                AVFrame *yuvFrame = AcquireFrame();
                if (yuvFrame) {
                  bool converted = false;

#ifdef USE_VULKAN
                  // Try Vulkan GPU conversion first
                  if (m_VulkanExporter && m_VulkanExporter->IsInitialized()) {
                    if (m_VulkanExporter->ConvertRGBToNV12(
                            ptr, yuvFrame->data[0], yuvFrame->data[1],
                            m_Config.width, m_Config.height)) {
                      converted = true;
                    }
                  }
#endif

#ifdef USE_CUDA
                  if (!converted && m_CUDAConverter &&
                      m_CUDAConverter->IsAvailable()) {
                    // GPU conversion from PBO mapped memory
                    if (m_CUDAConverter->ConvertRGB24ToNV12(
                            ptr, yuvFrame->data[0], yuvFrame->data[1],
                            m_Config.width, m_Config.height)) {
                      converted = true;
                    }
                  }
#endif

                  if (!converted && m_SwsCtx) {
                    // CPU fallback
                    const uint8_t *srcSlice[1] = {ptr};
                    int srcStride[1] = {m_Config.width * 3};

                    int result = sws_scale(m_SwsCtx, srcSlice, srcStride, 0,
                                           m_Config.height, yuvFrame->data,
                                           yuvFrame->linesize);

                    if (result > 0) {
                      converted = true;
                    }
                  }

                  if (converted) {
                    yuvFrame->pts = i - 1;

                    // Push directly to encoder queue
                    {
                      std::unique_lock<std::mutex> lock(m_YUVMutex);
                      // Backpressure: wait if queue too large
                      while (m_YUVQueue.size() > 5 && !m_CancelRequested) {
                        lock.unlock();
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(5));
                        lock.lock();
                      }

                      YUVFrame yuvPacket;
                      yuvPacket.frame = yuvFrame;
                      m_YUVQueue.push(std::move(yuvPacket));
                    }
                    m_YUVCondVar.notify_one();
                  } else {
                    ReleaseFrame(yuvFrame);
                  }
                }

                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
              }
            }

            // Issue read for current frame
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos[currentPBO]);
            glReadPixels(0, 0, m_Config.width, m_Config.height, GL_RGB,
                         GL_UNSIGNED_BYTE, 0);

            if (fences[currentPBO])
              glDeleteSync(fences[currentPBO]);
            fences[currentPBO] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            // Synchronous fallback
            renderer.GetRGBPixels(pixelBuffer, m_Config.width, m_Config.height);

            // Phase 3: Use CUDA for RGB→YUV if available, otherwise CPU
            AVFrame *yuvFrame = AcquireFrame();
            if (yuvFrame) {
              bool converted = false;

#ifdef USE_VULKAN
              // Try Vulkan GPU conversion first
              if (m_VulkanExporter && m_VulkanExporter->IsInitialized()) {
                if (m_VulkanExporter->ConvertRGBToNV12(
                        pixelBuffer.data(), yuvFrame->data[0],
                        yuvFrame->data[1], m_Config.width, m_Config.height)) {
                  converted = true;
                }
              }
#endif

#ifdef USE_CUDA
              if (!converted && m_CUDAConverter &&
                  m_CUDAConverter->IsAvailable()) {
                // GPU conversion
                if (m_CUDAConverter->ConvertRGB24ToNV12(
                        pixelBuffer.data(), yuvFrame->data[0],
                        yuvFrame->data[1], m_Config.width, m_Config.height)) {
                  converted = true;
                }
              }
#endif

              if (!converted && m_SwsCtx) {
                // CPU fallback
                const uint8_t *srcSlice[1] = {pixelBuffer.data()};
                int srcStride[1] = {m_Config.width * 3};

                int result =
                    sws_scale(m_SwsCtx, srcSlice, srcStride, 0, m_Config.height,
                              yuvFrame->data, yuvFrame->linesize);

                if (result > 0) {
                  converted = true;
                }
              }

              if (converted) {
                yuvFrame->pts = i;

                {
                  std::lock_guard<std::mutex> lock(m_YUVMutex);
                  YUVFrame yuvPacket;
                  yuvPacket.frame = yuvFrame;
                  m_YUVQueue.push(std::move(yuvPacket));
                }
                m_YUVCondVar.notify_one();
              } else {
                ReleaseFrame(yuvFrame);
              }
            }
          }

          renderer.UnbindFramebuffer();
          frameRendered = true;
        }
      }
    }

    // Handle empty frames (black frame)
    if (!frameRendered) {
      // Create black YUV frame
      AVFrame *yuvFrame = AcquireFrame();
      if (yuvFrame) {
        // Fill with black (Y=16, UV=128 for video range)
        memset(yuvFrame->data[0], 16, yuvFrame->linesize[0] * m_Config.height);
        memset(yuvFrame->data[1], 128,
               yuvFrame->linesize[1] * (m_Config.height / 2));
        yuvFrame->pts = i;

        {
          std::lock_guard<std::mutex> lock(m_YUVMutex);
          YUVFrame yuvPacket;
          yuvPacket.frame = yuvFrame;
          m_YUVQueue.push(std::move(yuvPacket));
        }
        m_YUVCondVar.notify_one();
      }
    }

    // Update progress less frequently to reduce overhead
    if (i % 30 == 0) {
      m_Progress = static_cast<float>(i + 1) / totalFrames;
      std::cout << "[RenderThread] Progress: " << i << "/" << totalFrames
                << std::endl;
    }
  }

  // Capture last PBO frame if using async readback
  if (usingPBO && !m_CancelRequested && totalFrames > 0) {
    int lastPBO = (totalFrames - 1) % 2;
    if (fences[lastPBO]) {
      glClientWaitSync(fences[lastPBO], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
      glDeleteSync(fences[lastPBO]);
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbos[lastPBO]);
    GLubyte *ptr = (GLubyte *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    if (ptr) {
      AVFrame *yuvFrame = AcquireFrame();
      if (yuvFrame && m_SwsCtx) {
        const uint8_t *srcSlice[1] = {ptr};
        int srcStride[1] = {m_Config.width * 3};

        int result =
            sws_scale(m_SwsCtx, srcSlice, srcStride, 0, m_Config.height,
                      yuvFrame->data, yuvFrame->linesize);

        if (result > 0) {
          yuvFrame->pts = totalFrames - 1;

          {
            std::lock_guard<std::mutex> lock(m_YUVMutex);
            YUVFrame yuvPacket;
            yuvPacket.frame = yuvFrame;
            m_YUVQueue.push(std::move(yuvPacket));
          }
          m_YUVCondVar.notify_one();
        } else {
          ReleaseFrame(yuvFrame);
        }
      }

      glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  }

  // Send stop signal to encoder thread (Phase 1: directly to encoder)
  {
    std::lock_guard<std::mutex> lock(m_YUVMutex);
    YUVFrame stopFrame;
    stopFrame.isStopSignal = true;
    m_YUVQueue.push(std::move(stopFrame));
  }
  m_YUVCondVar.notify_one();

  // Cleanup
  if (usingPBO) {
    for (int i = 0; i < 2; ++i) {
      if (fences[i])
        glDeleteSync(fences[i]);
    }
    glDeleteBuffers(2, pbos);
  }

  glfwMakeContextCurrent(nullptr);
  std::cout << "[RenderThread] Finished" << std::endl;
}

// ============================================================================
// Thread C: Encoder Thread (YUV -> H.264/H.265)
// ============================================================================

void HardwareExportManager::EncoderThreadFunc() {
  std::cout << "[EncoderThread] Started" << std::endl;

  // Initialize FFmpeg encoder
  if (!InitializeFFmpeg()) {
    m_ErrorMessage = "Failed to initialize FFmpeg encoder";
    m_IsExporting = false;
    m_IsFinished = true;
    return;
  }

  // Main encoding loop
  while (true) {
    YUVFrame yuvFrame;

    // Wait for YUV frame
    {
      std::unique_lock<std::mutex> lock(m_YUVMutex);
      m_YUVCondVar.wait(
          lock, [this] { return !m_YUVQueue.empty() || m_CancelRequested; });

      if (m_CancelRequested)
        break;

      if (m_YUVQueue.empty())
        continue;

      yuvFrame = std::move(m_YUVQueue.front());
      m_YUVQueue.pop();
    }

    // Check for stop signal
    if (yuvFrame.isStopSignal) {
      break;
    }

    // Encode frame
    if (yuvFrame.frame && m_CodecCtx) {
      yuvFrame.frame->pts = m_FrameCount++;

      // Send frame to encoder
      int ret = avcodec_send_frame(m_CodecCtx, yuvFrame.frame);
      if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[EncoderThread] Error sending frame: " << errbuf
                  << std::endl;
        ReleaseFrame(yuvFrame.frame);
        continue;
      }

      // Receive encoded packets
      while (ret >= 0) {
        ret = avcodec_receive_packet(m_CodecCtx, m_Packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
          break;
        } else if (ret < 0) {
          std::cerr << "[EncoderThread] Error receiving packet" << std::endl;
          break;
        }

        // Write packet to file
        av_packet_rescale_ts(m_Packet, m_CodecCtx->time_base,
                             m_Stream->time_base);
        m_Packet->stream_index = m_Stream->index;

        ret = av_interleaved_write_frame(m_FormatCtx, m_Packet);
        av_packet_unref(m_Packet);
      }

      // Return frame to pool
      ReleaseFrame(yuvFrame.frame);
    }

    // Progress updated by render thread only
  }

  // Flush encoder
  if (m_CodecCtx) {
    avcodec_send_frame(m_CodecCtx, nullptr);

    while (true) {
      int ret = avcodec_receive_packet(m_CodecCtx, m_Packet);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      if (ret < 0)
        break;

      av_packet_rescale_ts(m_Packet, m_CodecCtx->time_base,
                           m_Stream->time_base);
      m_Packet->stream_index = m_Stream->index;
      av_interleaved_write_frame(m_FormatCtx, m_Packet);
      av_packet_unref(m_Packet);
    }

    av_write_trailer(m_FormatCtx);
  }

  Cleanup();

  m_Progress = 1.0f;
  m_IsExporting = false;
  m_IsFinished = true;

  std::cout << "[EncoderThread] Finished - Export complete!" << std::endl;
}

// ============================================================================
// FFmpeg Initialization
// ============================================================================

bool HardwareExportManager::InitializeFFmpeg() {
  // Allocate format context
  avformat_alloc_output_context2(&m_FormatCtx, nullptr, nullptr,
                                 m_Config.outputFile.c_str());
  if (!m_FormatCtx) {
    avformat_alloc_output_context2(&m_FormatCtx, nullptr, "mp4",
                                   m_Config.outputFile.c_str());
  }
  if (!m_FormatCtx) {
    std::cerr << "[HardwareExportManager] Failed to create format context"
              << std::endl;
    return false;
  }

  // Find codec
  m_Codec = FindBestCodec();
  if (!m_Codec) {
    std::cerr << "[HardwareExportManager] Failed to find codec" << std::endl;
    return false;
  }

  // Create stream
  m_Stream = avformat_new_stream(m_FormatCtx, nullptr);
  if (!m_Stream) {
    std::cerr << "[HardwareExportManager] Failed to create stream" << std::endl;
    return false;
  }
  m_Stream->id = m_FormatCtx->nb_streams - 1;

  // Allocate codec context
  m_CodecCtx = avcodec_alloc_context3(m_Codec);
  if (!m_CodecCtx) {
    std::cerr << "[HardwareExportManager] Failed to allocate codec context"
              << std::endl;
    return false;
  }

  // Configure encoder
  if (!ConfigureEncoder()) {
    std::cerr << "[HardwareExportManager] Failed to configure encoder"
              << std::endl;
    return false;
  }

  // Initialize hardware acceleration if requested
  if (m_Config.enableHardwareAccel) {
    InitializeHardwareAccel();
  }

  // Open codec
  AVDictionary *opts = nullptr;
  std::string codecName = m_Codec->name;

  if (codecName.find("nvenc") != std::string::npos) {
    // NVENC-specific options for maximum performance
    std::cout << "[HardwareExportManager] Configuring NVENC..." << std::endl;

    // Preset: p1 = fastest, p7 = slowest
    av_dict_set(&opts, "preset",
                ("p" + std::to_string(m_Config.preset)).c_str(), 0);
    av_dict_set(&opts, "tune", "hq", 0); // High quality tune

    // Rate control
    switch (m_Config.rateControl) {
    case RateControl::VBR:
      av_dict_set(&opts, "rc", "vbr", 0);
      break;
    case RateControl::CBR:
      av_dict_set(&opts, "rc", "cbr", 0);
      break;
    case RateControl::CQP:
      av_dict_set(&opts, "rc", "constqp", 0);
      av_dict_set(&opts, "qp", std::to_string(m_Config.quality).c_str(), 0);
      break;
    }

    av_dict_set(&opts, "gpu", "0", 0);
    av_dict_set(&opts, "delay", "0", 0); // Low latency
    av_dict_set(&opts, "async_depth", "2", 0);

    std::cout << "[HardwareExportManager] NVENC settings applied" << std::endl;
  } else if (codecName.find("x264") != std::string::npos) {
    av_dict_set(&opts, "preset", "ultrafast", 0);
    av_dict_set(&opts, "tune", "zerolatency", 0);
  }

  int ret = avcodec_open2(m_CodecCtx, m_Codec, &opts);
  if (opts)
    av_dict_free(&opts);

  if (ret < 0) {
    char errbuf[256];
    av_strerror(ret, errbuf, sizeof(errbuf));
    std::cerr << "[HardwareExportManager] Could not open codec: " << errbuf
              << std::endl;
    return false;
  }

  std::cout << "[HardwareExportManager] Codec opened: " << m_Codec->name
            << std::endl;

  // Copy codec parameters to stream
  avcodec_parameters_from_context(m_Stream->codecpar, m_CodecCtx);

  // Open output file
  if (!(m_FormatCtx->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&m_FormatCtx->pb, m_Config.outputFile.c_str(),
                  AVIO_FLAG_WRITE) < 0) {
      std::cerr << "[HardwareExportManager] Could not open output file: "
                << m_Config.outputFile << std::endl;
      return false;
    }
  }

  // Write header
  if (avformat_write_header(m_FormatCtx, nullptr) < 0) {
    std::cerr << "[HardwareExportManager] Error writing file header"
              << std::endl;
    return false;
  }

  // Allocate packet
  m_Packet = av_packet_alloc();

  // Create SwsContext for RGB->YUV conversion
  m_SwsCtx =
      sws_getContext(m_Config.width, m_Config.height, AV_PIX_FMT_RGB24,
                     m_Config.width, m_Config.height, m_CodecCtx->pix_fmt,
                     SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

  if (!m_SwsCtx) {
    std::cerr << "[HardwareExportManager] Failed to create SwsContext"
              << std::endl;
    return false;
  }

  // Initialize frame pool
  m_FramePool = std::make_unique<BufferPool<AVFrame>>(
      5, // Initial pool size
      [this]() {
        AVFrame *frame = av_frame_alloc();
        frame->format = m_CodecCtx->pix_fmt;
        frame->width = m_Config.width;
        frame->height = m_Config.height;
        av_frame_get_buffer(frame, 32);
        return frame;
      },
      [](AVFrame *frame) { av_frame_free(&frame); });

  std::cout << "[HardwareExportManager] FFmpeg encoder initialized successfully"
            << std::endl;
  return true;
}

bool HardwareExportManager::InitializeHardwareAccel() {
  // Try to initialize CUDA hardware context
  int ret = av_hwdevice_ctx_create(&m_HwDeviceCtx, AV_HWDEVICE_TYPE_CUDA,
                                   nullptr, nullptr, 0);
  if (ret < 0) {
    std::cerr
        << "[HardwareExportManager] Failed to create CUDA device context, "
           "falling back to software encoding"
        << std::endl;
    m_UsingHardwareAccel = false;
    return false;
  }

  m_CodecCtx->hw_device_ctx = av_buffer_ref(m_HwDeviceCtx);
  m_UsingHardwareAccel = true;

  std::cout
      << "[HardwareExportManager] Hardware acceleration enabled (CUDA/NVENC)"
      << std::endl;
  return true;
}

const AVCodec *HardwareExportManager::FindBestCodec() {
  std::vector<std::string> candidates;

  // Build codec candidate list
  if (m_Config.codec == Codec::H264) {
    if (m_Config.enableHardwareAccel) {
      candidates.push_back("h264_nvenc");
      candidates.push_back("h264_qsv");
      candidates.push_back("h264_amf");
    }
    candidates.push_back("libx264");
    candidates.push_back("h264");
  } else { // H265
    if (m_Config.enableHardwareAccel) {
      candidates.push_back("hevc_nvenc");
      candidates.push_back("hevc_qsv");
      candidates.push_back("hevc_amf");
    }
    candidates.push_back("libx265");
    candidates.push_back("hevc");
  }

  // Try each candidate
  for (const auto &name : candidates) {
    const AVCodec *codec = avcodec_find_encoder_by_name(name.c_str());
    if (codec) {
      std::cout << "[HardwareExportManager] Found encoder: " << name << " ("
                << codec->long_name << ")" << std::endl;
      return codec;
    }
  }

  std::cerr << "[HardwareExportManager] No suitable codec found!" << std::endl;
  return nullptr;
}

bool HardwareExportManager::ConfigureEncoder() {
  m_CodecCtx->width = m_Config.width;
  m_CodecCtx->height = m_Config.height;
  m_CodecCtx->time_base = {1, m_Config.fps};
  m_CodecCtx->framerate = {m_Config.fps, 1};
  m_Stream->time_base = m_CodecCtx->time_base;

  // Select pixel format
  m_CodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
  if (m_Codec->pix_fmts) {
    // Prefer NV12 for NVENC
    for (const enum AVPixelFormat *p = m_Codec->pix_fmts; *p != -1; p++) {
      if (*p == AV_PIX_FMT_NV12) {
        m_CodecCtx->pix_fmt = AV_PIX_FMT_NV12;
        break;
      } else if (*p == AV_PIX_FMT_YUV420P) {
        m_CodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
      }
    }
  }

  std::cout << "[HardwareExportManager] Pixel format: "
            << av_get_pix_fmt_name(m_CodecCtx->pix_fmt) << std::endl;

  // Bitrate settings
  switch (m_Config.rateControl) {
  case RateControl::VBR:
    m_CodecCtx->bit_rate = m_Config.bitrate;
    m_CodecCtx->rc_max_rate = m_Config.maxBitrate;
    m_CodecCtx->rc_buffer_size = m_Config.bitrate / m_Config.fps * 2;
    break;
  case RateControl::CBR:
    m_CodecCtx->bit_rate = m_Config.bitrate;
    m_CodecCtx->rc_max_rate = m_Config.bitrate;
    m_CodecCtx->rc_min_rate = m_Config.bitrate;
    m_CodecCtx->rc_buffer_size = m_Config.bitrate / m_Config.fps;
    break;
  case RateControl::CQP:
    // Quality will be set via codec options
    break;
  }

  m_CodecCtx->gop_size = m_Config.fps * 2; // 2 second GOP
  m_CodecCtx->max_b_frames = 2;

  if (m_Config.codec == Codec::H264) {
    m_CodecCtx->profile = AV_PROFILE_H264_HIGH;
  }

  if (m_FormatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
    m_CodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  return true;
}

// ============================================================================
// Buffer Pool Management
// ============================================================================

AVFrame *HardwareExportManager::AcquireFrame() {
  if (!m_FramePool)
    return nullptr;

  // Get shared_ptr and store it to keep frame alive
  auto framePtr = m_FramePool->Acquire();
  if (!framePtr)
    return nullptr;

  // Store in map for later release
  AVFrame *rawPtr = framePtr.get();
  {
    std::lock_guard<std::mutex> lock(m_PoolMutex);
    m_ActiveFrames[rawPtr] = framePtr;
  }

  return rawPtr;
}

void HardwareExportManager::ReleaseFrame(AVFrame *frame) {
  if (!frame)
    return;

  std::lock_guard<std::mutex> lock(m_PoolMutex);
  m_ActiveFrames.erase(frame);
  // shared_ptr will be destroyed, returning frame to pool
}

// ============================================================================
// Cleanup
// ============================================================================

void HardwareExportManager::Cleanup() {
#ifdef USE_VULKAN
  if (m_VulkanExporter) {
    m_VulkanExporter->Cleanup();
    m_VulkanExporter.reset();
  }
#endif

  if (m_SwsCtx) {
    sws_freeContext(m_SwsCtx);
    m_SwsCtx = nullptr;
  }

  if (m_Packet) {
    av_packet_free(&m_Packet);
    m_Packet = nullptr;
  }

  if (m_CodecCtx) {
    avcodec_free_context(&m_CodecCtx);
    m_CodecCtx = nullptr;
  }

  if (m_FormatCtx) {
    if (!(m_FormatCtx->oformat->flags & AVFMT_NOFILE) && m_FormatCtx->pb) {
      avio_closep(&m_FormatCtx->pb);
    }
    avformat_free_context(m_FormatCtx);
    m_FormatCtx = nullptr;
  }

  if (m_HwDeviceCtx) {
    av_buffer_unref(&m_HwDeviceCtx);
    m_HwDeviceCtx = nullptr;
  }

  if (m_OffscreenWindow) {
    glfwDestroyWindow(m_OffscreenWindow);
    m_OffscreenWindow = nullptr;
  }

  m_FramePool.reset();
}
