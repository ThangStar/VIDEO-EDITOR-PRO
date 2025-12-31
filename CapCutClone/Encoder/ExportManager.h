#pragma once

#include "VideoEncoder.h"
#include <string>
#include <thread>
#include <atomic>
#include <functional>

struct GLFWwindow;

class TimelineManager;
class VideoPlayer;

class ExportManager {
public:
    ExportManager(TimelineManager* timeline, VideoPlayer* player);
    ~ExportManager();
    
    // Set main window for context sharing
    void SetMainWindow(GLFWwindow* mainWindow) { m_MainWindow = mainWindow; }
    GLFWwindow* GetMainWindow() const { return m_MainWindow; }

    // Start export in a separate thread
    bool StartExport(const std::string& outputFile, int width, int height, int fps);

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
    
    void SetEffectParams(const EffectParams& params) { m_EffectParams = params; }

private:
    TimelineManager* m_TimelineManager;
    VideoPlayer* m_VideoPlayer;
    VideoEncoder* m_Encoder;
    GLFWwindow* m_MainWindow; // Reference to main window for context sharing
    
    EffectParams m_EffectParams;

    // Threading
    std::thread m_ExportThread;
    std::atomic<bool> m_IsExporting;
    std::atomic<bool> m_IsFinished;
    std::atomic<bool> m_CancelRequested;
    std::atomic<float> m_Progress;
    
    // Shared Context managed by Main Thread
    GLFWwindow* m_OffscreenWindow;

    void ExportThreadFunc(std::string outputFile, int width, int height, int fps);
    
    // PBO IDs for async transfer
    unsigned int m_PBOs[2]; // Using unsigned int directly to avoid GL dependency in header if possible, or include glad/glfw
};
