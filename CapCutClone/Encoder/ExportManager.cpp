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
    , m_IsExporting(false)
    , m_IsFinished(false)
    , m_CancelRequested(false)
    , m_Progress(0.0f)
{
}

ExportManager::~ExportManager() {
    CancelExport();
    if (m_ExportThread.joinable()) {
        m_ExportThread.join();
    }
    if (m_Encoder) {
        delete m_Encoder;
    }
}

bool ExportManager::StartExport(const std::string& outputFile, int width, int height, int fps) {
    if (m_IsExporting) return false;

    m_IsExporting = true;
    m_IsFinished = false;
    m_CancelRequested = false;
    m_Progress = 0.0f;

    // Launch thread
    m_ExportThread = std::thread(&ExportManager::ExportThreadFunc, this, outputFile, width, height, fps);
    
    return true;
}

void ExportManager::CancelExport() {
    if (m_IsExporting) {
        m_CancelRequested = true;
    }
}

void ExportManager::ExportThreadFunc(std::string outputFile, int width, int height, int fps) {
    m_Encoder = new VideoEncoder();
    
    // Align dimensions to 16 (required for some hardware encoders like h264_mf)
    int alignedWidth = (width + 15) & ~15;
    int alignedHeight = (height + 15) & ~15;
    
    if (alignedWidth != width || alignedHeight != height) {
        std::cout << "[ExportManager] Aligning resolution from " << width << "x" << height 
                  << " to " << alignedWidth << "x" << alignedHeight << " for hardware encoding compatibility." << std::endl;
        width = alignedWidth;
        height = alignedHeight;
    }

    // 1. Initialize Encoder
    if (!m_Encoder->Initialize(outputFile, width, height, fps)) {
        std::cerr << "Failed to initialize export encoder!" << std::endl;
        delete m_Encoder;
        m_Encoder = nullptr;
        m_IsExporting = false;
        m_IsFinished = true; // Mark as finished (failed)
        return;
    }

    double duration = m_TimelineManager->GetTotalDuration();
    if (duration <= 0.001) duration = 1.0; // Avoid div by zero

    int totalFrames = static_cast<int>(duration * fps);
    double frameDuration = 1.0 / fps;

    // 2. Rendering Loop
    // Initialize OpenGL Context for this thread (Hidden window)
    // Note: On Windows this usually works. On macOS it would fail (must be main thread).
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* offscreenWindow = glfwCreateWindow(width, height, "ExportContext", nullptr, nullptr);
    if (!offscreenWindow) {
        std::cerr << "[ExportManager] Failed to create offscreen window context!" << std::endl;
        // Continue without effects (fallback)? Or abort?
        // Fallback to raw video
    } else {
        glfwMakeContextCurrent(offscreenWindow);
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
             std::cerr << "[ExportManager] Failed to initialize GLAD" << std::endl;
        }
    }
    
    // Setup Renderer
    TextureRenderer* renderer = nullptr;
    if (offscreenWindow) {
        renderer = new TextureRenderer();
        if (renderer->Initialize()) {
            renderer->CreateFramebuffer(width, height);
            
            // Apply effects
            renderer->SetFilterParams(m_EffectParams.brightness, m_EffectParams.contrast, m_EffectParams.saturation);
            renderer->SetEffectParams(m_EffectParams.vignette, m_EffectParams.grain, m_EffectParams.aberration, m_EffectParams.sepia);
            
            std::cout << "[ExportManager] Offscreen renderer initialized." << std::endl;
        } else {
             delete renderer;
             renderer = nullptr;
        }
    }

    VideoPlayer tempPlayer;
    std::string currentLoadedFile = "";
    std::vector<uint8_t> pixelBuffer; // For reading back from FBO

    std::cout << "[ExportManager] Starting export loop. Total frames: " << totalFrames << std::endl;

    for (int i = 0; i < totalFrames; ++i) {
        if (m_CancelRequested) {
            std::cout << "[ExportManager] Cancel requested." << std::endl;
            break;
        }
 
        double currentTime = i * frameDuration;
        
        bool frameEncoded = false;
        Clip* currentClip = nullptr;
        
        auto& tracks = m_TimelineManager->GetTracks();
        if (!tracks.empty()) {
             currentClip = tracks[0].GetClipAtTime(currentTime);
        }
 
        if (currentClip) {
            // Load if not loaded or file changed
            if (!tempPlayer.IsLoaded() || currentClip->filepath != currentLoadedFile) {
                 std::cout << "[ExportManager] Loading clip: " << currentClip->filepath << std::endl;
                 if (tempPlayer.LoadVideo(currentClip->filepath)) {
                     currentLoadedFile = currentClip->filepath;
                 } else {
                     std::cerr << "[ExportManager] Failed to load clip!" << std::endl;
                     currentLoadedFile = ""; // Failed
                 }
            } 
            
            if (tempPlayer.IsLoaded()) {
                double localTime = currentClip->ToLocalTime(currentTime);
                bool needsSeek = true;
                
                double playerCurrentTime = tempPlayer.GetCurrentTime();
                double playerFPS = tempPlayer.GetFPS();
                if (playerFPS <= 0) playerFPS = 30.0;
                
                double expectedNextTime = playerCurrentTime + (1.0 / playerFPS);
                double diff = std::abs(localTime - expectedNextTime);
                
                if (diff < 0.1) needsSeek = false;
                if (i == 0 || currentClip->filepath != currentLoadedFile) needsSeek = true;
 
                if (needsSeek) {
                    tempPlayer.Seek(localTime, false);
                }
                
                bool decodeSuccess = tempPlayer.DecodeNextFrame();
                // If decode fails, try using last frame (if we didn't seek just now)
                
                const uint8_t* data = tempPlayer.GetFrameData(); 
                if (data) {
                    if (renderer) {
                        // RENDER WITH EFFECTS
                        // Ensure input texture exists and matches video frame dimensions
                        static int lastTexW = 0, lastTexH = 0;
                        if (renderer->GetTextureID() == 0 || lastTexW != tempPlayer.GetWidth() || lastTexH != tempPlayer.GetHeight()) {
                            renderer->CreateTexture(tempPlayer.GetWidth(), tempPlayer.GetHeight());
                            lastTexW = tempPlayer.GetWidth();
                            lastTexH = tempPlayer.GetHeight();
                        }

                        renderer->UpdateTexture(data, tempPlayer.GetWidth(), tempPlayer.GetHeight()); // Update with original dimensions
                        renderer->BindFramebuffer();
                        
                        // Render to FBO (sized to export width/height)
                        // Clear
                        glClearColor(0,0,0,1);
                        glClear(GL_COLOR_BUFFER_BIT);
                        
                        renderer->RenderTexture(-1.0f, -1.0f, 2.0f, 2.0f); // Render Fullscreen Quad in Normalized Device Coordinates? 
                        // Wait, RenderTexture uses x,y,w,h in pixels or NDC?
                        // The loop in TextureRenderer uses pixels: x, y, width, height. 
                        // AND it uses Dynamic Projection now: 2.0/pW... so 0,0 is correct?
                        // If projection is 0..W, 0..H -> -1..1, then passing 0,0, W, H works.
                        renderer->RenderTexture(0, 0, (float)width, (float)height);
                        
                        // Read Pixels
                        renderer->GetRGBPixels(pixelBuffer, width, height);
                        renderer->UnbindFramebuffer();
                        
                        m_Encoder->EncodeFrame(pixelBuffer.data());
                    } else {
                        // Raw encoding (fallback)
                        // Resize if needed? NVENC might handle it, or we send raw.
                        // If dimensions mismatch, we might crash. 
                        // VideoEncoder Initialize set context width/height.
                        // If data is different size, sws_scale in EncodeFrame handles it?
                        // VideoEncoder::EncodeFrame expects data matching its context? 
                        // No, VideoEncoder::EncodeFrame takes rgbData and uses sws_scale which assumes Input size == m_Width/m_Height?
                        // Let's check VideoEncoder.cpp: 
                        // It uses `m_Width * 3` as stride. It assumes input is `m_Width` x `m_Height`.
                        // If clip resolution != export resolution, this CRASHES.
                        // So we MUST use renderer to resize, or fix VideoEncoder to accept input dimensions.
                        // Given we implemented Renderer, we rely on it for resizing.
                        // If renderer failed, we are in trouble.
                        m_Encoder->EncodeFrame(data); 
                    }
                    frameEncoded = true;
                }
                
            }
        }
 
        if (!frameEncoded) {
            // Black frame
            if (renderer) {
                renderer->BindFramebuffer();
                glClearColor(0,0,0,1);
                glClear(GL_COLOR_BUFFER_BIT);
                renderer->GetRGBPixels(pixelBuffer, width, height);
                renderer->UnbindFramebuffer();
                m_Encoder->EncodeFrame(pixelBuffer.data());
            } else {
                 std::vector<uint8_t> blackFrame(width * height * 3, 0);
                 m_Encoder->EncodeFrame(blackFrame.data());
            }
        }
 
        m_Progress = (float)(i + 1) / totalFrames;
        
        if (i % 30 == 0) {
             std::cout << "[ExportManager] Processed " << i << "/" << totalFrames << " frames." << std::endl;
        }
    }

    // Cleanup Renderer
    if (renderer) {
        delete renderer;
        renderer = nullptr;
    }
    if (offscreenWindow) {
        glfwDestroyWindow(offscreenWindow);
    }

    // 3. Finalize
    m_Encoder->Finalize();
    delete m_Encoder;
    m_Encoder = nullptr;

    m_IsExporting = false;
    m_IsFinished = true;
}
