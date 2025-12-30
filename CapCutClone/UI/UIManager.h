#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include "../Timeline/Sticker.h"

// Forward declarations
class VideoPlayer;
class TextureRenderer;
class TimelineThumbnails;
class TimelineManager;

class UIManager {
public:
    UIManager();
    ~UIManager();

    void Update(float deltaTime);
    void Render();

    // Keyboard shortcuts callback
    void OnSpacePressed();

    // Video integration
    void SetVideoPlayer(VideoPlayer* player);
    void SetTextureRenderer(TextureRenderer* renderer) { m_TextureRenderer = renderer; }
    void OnVideoLoaded(const std::string& filepath);
    void OnOpenVideoClicked();

private:
    // Video references
    VideoPlayer* m_VideoPlayer;
    TextureRenderer* m_TextureRenderer;
    TimelineThumbnails* m_TimelineThumbnails;
    TimelineManager* m_TimelineManager;

    // UI State
    bool m_IsPlaying;
    float m_CurrentTime;
    float m_TotalDuration;
    float m_TimelineZoom;
    float m_SeekPosition;
    double m_LastFrameTime;
    double m_PlaybackStartTime;
    int m_AspectRatioMode;  // 0=Original, 1=9:16, 2=16:9
    int m_SelectedClipId;   // -1 if none
    int m_SelectedTrackIndex; // Added for Split/Delete operations

    // Sticker State
    std::vector<Sticker> m_Stickers;
    int m_SelectedStickerId = -1;
    unsigned int m_DefaultStickerTexture = 0;
    void CreateDefaultStickerTexture();
    void AddSticker();

    // Interaction State
    int m_DragClipId = -1;
    double m_DragOriginalStartTime = 0.0;
    int m_DragTrackIndex = -1;
    bool m_IsDragging = false;
    
    // Trim State
    int m_TrimClipId = -1;
    bool m_IsTrimmingStart = false; // true if trimming start, false if trimming end
    bool m_IsTrimming = false;

    // Export State
    class ExportManager* m_ExportManager = nullptr;
    bool m_ShowExportDialog = false;
    bool m_ShowExportProgress = false;
    float m_ExportProgress = 0.0f;
    char m_ExportFilename[256] = "output.mp4";
    int m_ExportWidth = 1920;
    int m_ExportHeight = 1080;
    int m_ExportFps = 30;

    // UI Rendering methods
    void RenderMenuBar();
    void RenderMediaPanel(float x, float y, float w, float h);
    void RenderPreviewPanel(float x, float y, float w, float h);
    void RenderPropertiesPanel(float x, float y, float w, float h);
    void RenderTimelinePanel(float x, float y, float w, float h);
    void RenderTimelineTracks(); // Renamed from RenderTimelinePanel()
    
    void RenderExportDialog();
    void RenderExportProgress();

    // Helper methods
    const char* FormatTime(float seconds);
};
