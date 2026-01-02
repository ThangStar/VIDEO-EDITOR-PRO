#pragma once

#include "../Timeline/Sticker.h"
#include <imgui.h>
#include <string>
#include <vector>


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
  void SetVideoPlayer(VideoPlayer *player);
  void SetTextureRenderer(TextureRenderer *renderer) {
    m_TextureRenderer = renderer;
  }
  void OnVideoLoaded(const std::string &filepath);
  void OnOpenVideoClicked();

private:
  // Video references
  VideoPlayer *m_VideoPlayer;
  TextureRenderer *m_TextureRenderer;
  TimelineThumbnails *m_TimelineThumbnails;
  TimelineManager *m_TimelineManager;

  // UI State
  bool m_IsPlaying;
  float m_CurrentTime;
  float m_TotalDuration;
  float m_TimelineZoom;
  float m_SeekPosition;
  double m_LastFrameTime;
  double m_PlaybackStartTime;
  int m_AspectRatioMode;    // 0=Original, 1=9:16, 2=16:9
  int m_SelectedClipId;     // -1 if none
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
  bool m_IsTrimmingStart =
      false; // true if trimming start, false if trimming end
  bool m_IsTrimming = false;

  // Effect Selection
  int m_SelectedEffectId = -1; // -1 = none selected

  // Export State
  class HardwareExportManager *m_ExportManager = nullptr;
  bool m_ShowExportDialog = false;
  bool m_ShowExportProgress = false;
  bool m_ShowExportSuccess = false;
  float m_ExportProgress = 0.0f;
  std::string m_LastExportPath = "";

  // Export Settings (UI State)
  char m_ExportName[256] = "My Video";
  char m_ExportPath[512] = "D:/Videos/";
  int m_ExportResIndex = 2;     // 0=480p, 1=720p, 2=1080p, 3=2k, 4=4k
  int m_ExportBitrateIndex = 0; // 0=Recommended, 1=Higher, 2=Lower
  int m_ExportCodecIndex = 0;   // 0=H.264, 1=HEVC
  int m_ExportFormatIndex = 0;  // 0=mp4, 1=mov
  int m_ExportFpsIndex = 2;     // 0=24, 1=25, 2=30, 3=50, 4=60

  // Internal use for export call
  char m_ExportFilename[256] =
      "output.mp4"; // Deprecated, derived from Name + Path
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
  const char *FormatTime(float seconds);

  // Filters Panel
  unsigned int m_DemoImageTexture = 0;
  std::vector<unsigned int> m_FilterThumbnails;
  bool m_FilterGenerationAttempted = false;
  void LoadDemoImage(); // Load cat.jpg
  void GenerateFilterThumbnails();

  // Effects Panel State
  int m_SelectedBlur = -1; // -1 = none, 0-3 = blur types
  float m_BlurIntensity = 0.5f;
};
