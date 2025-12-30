#include "ExportManager.h"
#include "../Timeline/TimelineManager.h"
#include "../Video/VideoPlayer.h"
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
    
    // 1. Initialize Encoder
    if (!m_Encoder->Initialize(outputFile, width, height, fps)) {
        std::cerr << "Failed to initialize export encoder!" << std::endl;
        m_IsExporting = false;
        return;
    }

    double duration = m_TimelineManager->GetTotalDuration();
    if (duration <= 0.001) duration = 1.0; // Avoid div by zero

    int totalFrames = static_cast<int>(duration * fps);
    double frameDuration = 1.0 / fps;

    // 2. Rendering Loop
    VideoPlayer tempPlayer;
    std::string currentLoadedFile = "";

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
                
                // Check if we are sequential
                double playerCurrentTime = tempPlayer.GetCurrentTime();
                double playerFPS = tempPlayer.GetFPS();
                if (playerFPS <= 0) playerFPS = 30.0; // Safety
                
                double expectedNextTime = playerCurrentTime + (1.0 / playerFPS);
                double diff = std::abs(localTime - expectedNextTime);
                
                // std::cout << "Target: " << localTime << " | Curr: " << playerCurrentTime << " | ExpNext: " << expectedNextTime << " | Diff: " << diff << std::endl;

                if (diff < 0.1) { // Increased tolerance to 100ms
                    // Sequential: Don't seek
                    needsSeek = false;
                }
                
                // Force seek on first frame of clip
                if (i == 0 || currentClip->filepath != currentLoadedFile) needsSeek = true;

                if (needsSeek) {
                    // std::cout << "[ExportManager] Frame " << i << ": Seeking to " << localTime << " (Diff: " << diff << ")" << std::endl;
                    tempPlayer.Seek(localTime, false);
                } else {
                     // We are sequential, but we still need to consume frames until we reach target time
                     // In case the export FPS < video FPS, we might need to skip some decoded frames
                     // Simple implementation: just decode one frame and hope it matches closest? 
                     // No, if we drift too far we must seek.
                     // For now, let's just decode once.
                }
                
                // std::cout << "[ExportManager] Frame " << i << ": Decoding..." << std::endl;
                bool decodeSuccess = tempPlayer.DecodeNextFrame();
                if (decodeSuccess) {
                    // Get Data
                    const uint8_t* data = tempPlayer.GetFrameData(); // RGB data
                    if (data) {
                         // std::cout << "[ExportManager] Frame " << i << ": Encoding..." << std::endl;
                        m_Encoder->EncodeFrame(data);
                        frameEncoded = true;
                    }
                } 
                
                if (!frameEncoded) {
                    // If decode failed (EOF?) or no data, try to use LAST frame from player if available
                    // This handles the case where audio/timeline is slightly longer than video
                     const uint8_t* data = tempPlayer.GetFrameData();
                     if (data) {
                         // Use last known frame
                         m_Encoder->EncodeFrame(data);
                         frameEncoded = true; 
                     } else {
                         std::cerr << "[ExportManager] Frame " << i << ": Decode failed and no previous frame!" << std::endl;
                     }
                }
            }
        }

        if (!frameEncoded) {
            // Black frame
            std::vector<uint8_t> blackFrame(width * height * 3, 0);
            // RGB24: 0, 0, 0 is black
            m_Encoder->EncodeFrame(blackFrame.data());
        }

        m_Progress = (float)(i + 1) / totalFrames;
        
        if (i % 30 == 0) {
             std::cout << "[ExportManager] Processed " << i << "/" << totalFrames << " frames." << std::endl;
        }
    }

    // 3. Finalize
    m_Encoder->Finalize();
    delete m_Encoder;
    m_Encoder = nullptr;

    m_IsExporting = false;
    m_IsFinished = true;
}
