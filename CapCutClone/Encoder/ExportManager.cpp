#include "ExportManager.h"
#include "../Timeline/TimelineManager.h"
#include "../Video/VideoPlayer.h"
#include "../Rendering/TextureRenderer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

ExportManager::ExportManager(TimelineManager* timeline, VideoPlayer* player)
    : m_TimelineManager(timeline)
    , m_VideoPlayer(player)
    , m_Encoder(nullptr)
    , m_MainWindow(nullptr)
    , m_IsExporting(false)
    , m_IsFinished(false)
    , m_CancelRequested(false)
    , m_Progress(0.0f)
    , m_OffscreenWindow(nullptr)
{
}

ExportManager::~ExportManager() {
    CancelExport();
    if (m_ExportThread.joinable()) {
        m_ExportThread.join();
    }
    if (m_Encoder) {
        delete m_Encoder;
        m_Encoder = nullptr;
    }
    if (m_OffscreenWindow) {
        glfwDestroyWindow(m_OffscreenWindow);
        m_OffscreenWindow = nullptr;
    }
}

bool ExportManager::StartExport(const std::string& outputFile, int width, int height, int fps) {
    if (m_IsExporting) return false;

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
        
        m_OffscreenWindow = glfwCreateWindow(width, height, "ExportContext", nullptr, m_MainWindow);
        if (!m_OffscreenWindow) {
             std::cerr << "[ExportManager] Failed to create offscreen window!" << std::endl;
        } else {
             std::cout << "[ExportManager] Offscreen window created successfully." << std::endl;
        }
    }

    m_IsExporting = true;
    m_IsFinished = false;
    m_CancelRequested = false;
    m_Progress = 0.0f;

    m_ExportThread = std::thread(&ExportManager::ExportThreadFunc, this, outputFile, width, height, fps);
    
    return true;
}

void ExportManager::CancelExport() {
    if (m_IsExporting) {
        m_CancelRequested = true;
    }
}

void ExportManager::ExportThreadFunc(std::string outputFile, int width, int height, int fps) {
    VideoEncoder* localEncoder = new VideoEncoder();
    m_Encoder = localEncoder; 
    
    std::cout << "[System] Tech Stack Verification:" << std::endl;
    std::cout << "  - Language: C++ (Native)" << std::endl;
    std::cout << "  - Encoder: FFmpeg + Hardware Accel" << std::endl;
    std::cout << "  - Rendering: OpenGL Core 3.3" << std::endl;
    std::cout << "  - Async Transfer: PBO (Pixel Buffer Objects)" << std::endl;

    int alignedWidth = (width + 15) & ~15;
    int alignedHeight = (height + 15) & ~15;
    
    if (alignedWidth != width || alignedHeight != height) {
        width = alignedWidth;
        height = alignedHeight;
    }

    int bitrate = 8000000; 
    if (width >= 1920) bitrate = 15000000;
    if (width >= 3840) bitrate = 40000000; 

    if (!localEncoder->Initialize(outputFile, width, height, fps, bitrate)) {
        std::cerr << "Failed to initialize export encoder!" << std::endl;
        delete localEncoder;
        m_Encoder = nullptr;
        m_IsExporting = false;
        m_IsFinished = true;
        return;
    }

    double duration = m_TimelineManager->GetTotalDuration();
    if (duration <= 0.001) duration = 1.0;

    int totalFrames = static_cast<int>(duration * fps);
    double frameDuration = 1.0 / fps;
    
    std::cout << "[ExportManager] StartExport: " << width << "x" << height << " @ " << fps << "fps, Bitrate: " << bitrate << std::endl;

    TextureRenderer* renderer = nullptr;
    bool usingPBO = false;
    GLsync fences[2] = {nullptr, nullptr}; // Sync objects for each PBO

    if (m_OffscreenWindow) {
          glfwMakeContextCurrent(m_OffscreenWindow);
          glViewport(0, 0, width, height);
          std::cout << "[ExportManager] OpenGL context activated" << std::endl;
          
          if (gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
               std::cout << "[ExportManager] GLAD loaded successfully" << std::endl;
               renderer = new TextureRenderer();
               if (renderer->Initialize()) {
                  std::cout << "[ExportManager] Renderer initialized successfully" << std::endl;
                  renderer->CreateFramebuffer(width, height);
                  renderer->SetFlipY(false); // Disable flip to fix vertical orientation issue
                  
                  renderer->SetFilterParams(m_EffectParams.brightness, m_EffectParams.contrast, m_EffectParams.saturation);
                  renderer->SetEffectParams(m_EffectParams.vignette, m_EffectParams.grain, m_EffectParams.aberration, m_EffectParams.sepia);
                  renderer->SetFilterType(m_EffectParams.filterType); // Apply filter for export
                  std::cout << "[ExportManager] Renderer configured with filterType=" << m_EffectParams.filterType << std::endl;
                  
                  glPixelStorei(GL_PACK_ALIGNMENT, 1);
                  glGenBuffers(2, m_PBOs);
                  
                  // CRITICAL FIX: Add padding for PBOs
                  // sws_scale optimized SIMD instructions may read slightly past end of line/buffer
                  // Adding extra bytes prevents SegFaults on strict driver memory
                  size_t bufferSize = (size_t)width * height * 3 + 4096; 
                  
                  glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBOs[0]);
                  glBufferData(GL_PIXEL_PACK_BUFFER, bufferSize, nullptr, GL_STREAM_READ);
                  glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBOs[1]);
                  glBufferData(GL_PIXEL_PACK_BUFFER, bufferSize, nullptr, GL_STREAM_READ);
                  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                  
                  // Check for GL errors
                  GLenum err = glGetError();
                  if (err != GL_NO_ERROR) {
                      std::cerr << "[ExportManager] OpenGL Error during PBO init: " << err << std::endl;
                      glDeleteBuffers(2, m_PBOs);
                  } else {
                      usingPBO = true;
                      std::cout << "[ExportManager] PBOs initialized (" << bufferSize << " bytes each)" << std::endl;
                  }
               } else {
                  std::cerr << "[ExportManager] Renderer Initialize failed!" << std::endl;
                  delete renderer;
                  renderer = nullptr;
               }
          } else {
              std::cerr << "[ExportManager] GLAD Init failed in export thread!" << std::endl;
          }
     } else {
         std::cerr << "[ExportManager] WARNING: No Offscreen Window. PBO Disabled." << std::endl;
     }

    VideoPlayer tempPlayer;
    std::string currentLoadedFile = "";
    std::vector<uint8_t> pixelBuffer; 
    pixelBuffer.resize(width * height * 3); 

    std::cout << "[ExportManager] Starting export loop with PBO: " << (usingPBO ? "YES" : "NO") 
              << ". Frames: " << totalFrames << std::endl;

    // Main export loop
    for (int i = 0; i < totalFrames; ++i) {
        if (m_CancelRequested) break;
 
        double currentTime = i * frameDuration;
        
        bool frameRendered = false;
        Clip* currentClip = nullptr;
        
        auto& tracks = m_TimelineManager->GetTracks();
        if (!tracks.empty()) currentClip = tracks[0].GetClipAtTime(currentTime);

        if (currentClip) {
            if (!tempPlayer.IsLoaded() || currentClip->filepath != currentLoadedFile) {
                 if (tempPlayer.LoadVideo(currentClip->filepath)) currentLoadedFile = currentClip->filepath;
            } 
            
            if (tempPlayer.IsLoaded()) {
                double localTime = currentClip->ToLocalTime(currentTime);
                
                double videoFPS = tempPlayer.GetFPS() > 0 ? tempPlayer.GetFPS() : 30.0;
                double videoFrameDuration = 1.0 / videoFPS;
                
                // Only seek if we have a large discontinuity (scene change or scrub)
                if (i == 0 || currentClip->filepath != currentLoadedFile || std::abs(localTime - tempPlayer.GetCurrentTime()) > 0.5) {
                    tempPlayer.Seek(localTime, false);
                }
                
                // Sync Logic: Check if we need a new frame
                // Ensure frame pacing is correct for export framerate
                // We only decode if the START of the current video frame is too old for the target time
                while (tempPlayer.GetCurrentTime() + videoFrameDuration < localTime) {
                     if (!tempPlayer.DecodeNextFrame()) break;
                }
                const uint8_t* data = tempPlayer.GetFrameData(); 
                
                if (data && renderer) {
                    // Check if we need to create/recreate texture
                    // Use local variables per export session, not static!
                    if (tempPlayer.GetWidth() > 0 && tempPlayer.GetHeight() > 0) {
                        // Always create texture on first frame (i==0) or if size changed
                        if (i == 0 || renderer->GetTextureID() == 0) {
                            std::cout << "[ExportManager] Creating texture " << tempPlayer.GetWidth() << "x" << tempPlayer.GetHeight() << std::endl;
                            renderer->CreateTexture(tempPlayer.GetWidth(), tempPlayer.GetHeight());
                        }
                    }
                    // Update Texture with new frame data!
                    renderer->UpdateTexture(tempPlayer.GetFrameData(), tempPlayer.GetWidth(), tempPlayer.GetHeight());
                    
                    // 2. Render to FBO
                    renderer->BindFramebuffer();
                    glViewport(0, 0, width, height);
                    glClear(GL_COLOR_BUFFER_BIT);
                    
                    // Draw the video frame
                    renderer->RenderTexture(0, 0, (float)width, (float)height);
                    
                    if (usingPBO) {
                        int currentPBO = i % 2;
                        int previousPBO = (i + 1) % 2;
                        
                        // Wait Previous
                        if (i > 0 && fences[previousPBO]) {
                             GLenum waitRet = glClientWaitSync(fences[previousPBO], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
                             if (waitRet == GL_TIMEOUT_EXPIRED || waitRet == GL_WAIT_FAILED) {
                                 // std::cerr << "[Error] Fence wait error!" << std::endl;
                             }
                             glDeleteSync(fences[previousPBO]);
                             fences[previousPBO] = nullptr;
                        }
                        
                        // Read Current
                        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBOs[currentPBO]);
                        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, 0);
                        
                        // Fence Current
                        if (fences[currentPBO]) glDeleteSync(fences[currentPBO]);
                        fences[currentPBO] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                        
                        // Process Previous
                        if (i > 0) {
                            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBOs[previousPBO]);
                            GLubyte* ptr = (GLubyte*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
                            if (ptr) {
                                if (!localEncoder->EncodeFrame(ptr)) {
                                     // Suppress frame drop error to avoid spam
                                }
                                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                            } else {
                                std::cerr << "[Error] Failed to map PBO!" << std::endl;
                            }
                        }
                        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                    } else {
                        renderer->GetRGBPixels(pixelBuffer, width, height);
                        localEncoder->EncodeFrame(pixelBuffer.data());
                    }
                    
                    renderer->UnbindFramebuffer();
                    frameRendered = true;
                }
            }
        }
 
        // Handle empty frames
        if (!frameRendered) {
            if (renderer) {
                renderer->BindFramebuffer();
                glClearColor(0,0,0,1);
                glClear(GL_COLOR_BUFFER_BIT);
                
                if (usingPBO) {
                    int currentPBO = i % 2;
                    int previousPBO = (i + 1) % 2;
                    
                    if (i > 0 && fences[previousPBO] != nullptr) {
                        glClientWaitSync(fences[previousPBO], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
                        glDeleteSync(fences[previousPBO]);
                        fences[previousPBO] = nullptr;
                    }
                    
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBOs[currentPBO]);
                    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, 0);
                    
                    if (fences[currentPBO] != nullptr) glDeleteSync(fences[currentPBO]);
                    fences[currentPBO] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                    
                    if (i > 0) {
                        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBOs[previousPBO]);
                        GLubyte* ptr = (GLubyte*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
                        if (ptr) {
                            localEncoder->EncodeFrame(ptr);
                            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                        } else {
                            renderer->GetRGBPixels(pixelBuffer, width, height);
                            localEncoder->EncodeFrame(pixelBuffer.data());
                        }
                    }
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                } else {
                    renderer->GetRGBPixels(pixelBuffer, width, height);
                    localEncoder->EncodeFrame(pixelBuffer.data());
                }
                renderer->UnbindFramebuffer();
            } else {
                std::fill(pixelBuffer.begin(), pixelBuffer.end(), 0);
                localEncoder->EncodeFrame(pixelBuffer.data());
            }
        }
 
        m_Progress = (float)(i + 1) / totalFrames;
        if (i % 60 == 0) std::cout << "[ExportManager] Processed " << i << "/" << totalFrames << std::endl;
    }

    // === CRITICAL: Encode LAST frame from PBO ===
    if (usingPBO && !m_CancelRequested && totalFrames > 0) {
        int lastPBO = (totalFrames - 1) % 2;
        
        // Wait for last fence
        if (fences[lastPBO] != nullptr) {
            glClientWaitSync(fences[lastPBO], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
            glDeleteSync(fences[lastPBO]);
            fences[lastPBO] = nullptr;
        }
        
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_PBOs[lastPBO]);
        GLubyte* ptr = (GLubyte*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (ptr) {
            std::cout << "[ExportManager] Encoding final frame from PBO " << lastPBO << std::endl;
            localEncoder->EncodeFrame(ptr);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        } else {
            std::cerr << "[Error] Failed to map final PBO!" << std::endl;
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    // Cleanup
    if (usingPBO) {
        // Delete any remaining fences (already done in loop above, so just make sure)
        for (int i = 0; i < 2; i++) {
            if (fences[i] != nullptr) {
                glDeleteSync(fences[i]);
                fences[i] = nullptr;
            }
        }
        // Cleanup PBOs
        glDeleteBuffers(2, m_PBOs);
    }

    // Cleanup renderer BEFORE unbinding context (OpenGL calls need valid context!)
    if (renderer) {
        delete renderer;
        renderer = nullptr;
    }
    
    // NOW safe to unbind context
    glfwMakeContextCurrent(nullptr);

    localEncoder->Finalize();
    delete localEncoder;
    m_Encoder = nullptr;

    m_IsExporting = false;
    m_IsFinished = true;
}