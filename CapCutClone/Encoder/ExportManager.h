#pragma once

#include "VideoEncoder.h"
#include <string>
#include <thread>
#include <atomic>
#include <functional>

class TimelineManager;
class VideoPlayer;

class ExportManager {
public:
    ExportManager(TimelineManager* timeline, VideoPlayer* player);
    ~ExportManager();

    // Start export in a separate thread
    bool StartExport(const std::string& outputFile, int width, int height, int fps);

    // Stop/Cancel export
    void CancelExport();

    // Stats
    float GetProgress() const { return m_Progress; } // 0.0 to 1.0
    bool IsExporting() const { return m_IsExporting; }
    bool IsFinished() const { return m_IsFinished; }

private:
    TimelineManager* m_TimelineManager;
    VideoPlayer* m_VideoPlayer;
    VideoEncoder* m_Encoder;

    // Threading
    std::thread m_ExportThread;
    std::atomic<bool> m_IsExporting;
    std::atomic<bool> m_IsFinished;
    std::atomic<bool> m_CancelRequested;
    std::atomic<float> m_Progress;

    void ExportThreadFunc(std::string outputFile, int width, int height, int fps);
};
