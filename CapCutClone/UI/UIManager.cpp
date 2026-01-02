#define NOMINMAX
#include "UIManager.h"
#include "../Application.h"
#include "../Encoder/HardwareExportManager.h"
#include "../Rendering/TextureRenderer.h"
#include "../Timeline/Clip.h"
#include "../Timeline/TimelineManager.h"
#include "../Timeline/Track.h"
#include "../Video/VideoPlayer.h"
#include "TimelineThumbnails.h"
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "Configuration.h"
#include <imgui.h>
// #define IMGUI_DEFINE_MATH_OPERATORS
// #include <imgui_internal.h>
#include "IconsFontAwesome6.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <stdio.h>
#include <string>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

extern class Application *g_Application;

// Helper for centering text
static void TextCentered(const char *text) {
  float winWidth = ImGui::GetWindowSize().x;
  float textWidth = ImGui::CalcTextSize(text).x;
  ImGui::SetCursorPosX((winWidth - textWidth) * 0.5f);
  ImGui::Text("%s", text);
}

// Local ImVec2 operators to avoid imgui_internal dependency
static inline ImVec2 operator+(const ImVec2 &lhs, const ImVec2 &rhs) {
  return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
}
static inline ImVec2 operator-(const ImVec2 &lhs, const ImVec2 &rhs) {
  return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y);
}
static inline ImVec2 operator*(const ImVec2 &lhs, float rhs) {
  return ImVec2(lhs.x * rhs, lhs.y * rhs);
}

// Helper for icon buttons in toolbar
static bool IconButton(const char *id, const char *icon,
                       bool selected = false) {
  ImGui::PushStyleColor(ImGuiCol_Text, selected
                                           ? ImVec4(0.00f, 0.78f, 0.84f, 1.00f)
                                           : ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  bool clicked = ImGui::Button(id);
  ImGui::PopStyleColor(2);

  // Draw icon centered over button
  ImVec2 rectMin = ImGui::GetItemRectMin();
  ImVec2 rectMax = ImGui::GetItemRectMax();
  ImVec2 center = (rectMin + rectMax) * 0.5f;

  ImGui::GetWindowDrawList()->AddText(
      ImGui::GetFont(), 16.0f,
      ImVec2(center.x - ImGui::CalcTextSize(icon).x * 0.5f,
             center.y - ImGui::CalcTextSize(icon).y * 0.5f),
      selected ? IM_COL32(0, 200, 215, 255) : IM_COL32(200, 200, 200, 255),
      icon);

  return clicked;
}

UIManager::UIManager()
    : m_VideoPlayer(nullptr), m_TextureRenderer(nullptr),
      m_TimelineThumbnails(nullptr), m_TimelineManager(nullptr),
      m_ExportManager(nullptr), m_FilterGenerationAttempted(false),
      m_IsPlaying(false), m_CurrentTime(0.0f), m_TotalDuration(330.0f),
      m_TimelineZoom(1.0f), m_SeekPosition(0.0f), m_LastFrameTime(0.0f),
      m_PlaybackStartTime(0.0), m_AspectRatioMode(0), m_SelectedClipId(-1),
      m_SelectedTrackIndex(-1), m_SelectedStickerId(-1),
      m_DefaultStickerTexture(0) {
  m_TimelineThumbnails = new TimelineThumbnails();
  m_TimelineManager = new TimelineManager();
}

UIManager::~UIManager() {
  if (m_TimelineThumbnails)
    delete m_TimelineThumbnails;
  if (m_TimelineManager)
    delete m_TimelineManager;
  if (m_ExportManager)
    delete m_ExportManager;
  if (m_DefaultStickerTexture)
    glDeleteTextures(1, &m_DefaultStickerTexture);
}

void UIManager::SetVideoPlayer(VideoPlayer *player) {
  m_VideoPlayer = player;
  if (m_TimelineManager)
    m_TimelineManager->SetVideoPlayer(player);
  if (!m_ExportManager && m_TimelineManager) {
    m_ExportManager =
        new HardwareExportManager(m_TimelineManager, m_VideoPlayer);
    // Set main window for OpenGL context sharing
    GLFWwindow *mainWindow = glfwGetCurrentContext();
    if (mainWindow) {
      m_ExportManager->SetMainWindow(mainWindow);
    }
  }
}

void UIManager::Update(float deltaTime) {
  if (m_TimelineManager) {
    m_TimelineManager->SetCurrentTime(m_CurrentTime);
    m_TimelineManager->Update(deltaTime);
    m_TotalDuration =
        std::max(10.0f, (float)m_TimelineManager->GetTotalDuration() + 5.0f);
  }

  if (m_ExportManager && m_ShowExportProgress) {
    m_ExportProgress = m_ExportManager->GetProgress();
  }

  if (m_IsPlaying && m_VideoPlayer && m_VideoPlayer->IsLoaded()) {
    double currentTime = glfwGetTime();
    double playbackTime = currentTime - m_PlaybackStartTime;
    double videoPTS = m_VideoPlayer->GetCurrentTime();

    if (playbackTime >= videoPTS) {
      if (m_VideoPlayer->DecodeNextFrame()) {
        if (m_TextureRenderer) {
          m_TextureRenderer->UpdateTexture(m_VideoPlayer->GetFrameData(),
                                           m_VideoPlayer->GetWidth(),
                                           m_VideoPlayer->GetHeight());
        }
        m_CurrentTime = (float)m_VideoPlayer->GetCurrentTime();
        m_SeekPosition = m_CurrentTime / m_TotalDuration;
      } else {
        if (m_CurrentTime >= m_TotalDuration)
          m_IsPlaying = false;
      }
    } else {
      m_CurrentTime =
          (float)playbackTime; // Sync to wall clock if video is faster
    }
  } else if (m_IsPlaying) {
    m_CurrentTime += deltaTime;
    if (m_CurrentTime >= m_TotalDuration) {
      m_CurrentTime = m_TotalDuration;
      m_IsPlaying = false;
    }
  }
}

void UIManager::Render() {
  RenderMenuBar();
  RenderExportDialog();
  RenderExportProgress();

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImVec2 workPos = viewport->WorkPos;
  ImVec2 workSize = viewport->WorkSize;

  // Layout configuration
  // const float headerHeight = 45.0f; // Unused
  const float topBarHeight = 40.0f; // Height of RenderMenuBar
  const float timelineHeight = 350.0f;
  const float mainAreaHeight = workSize.y - timelineHeight - topBarHeight;

  // Panel Widths
  const float leftSidebarWidth =
      60.0f;                           // The icon strip (Media, Audio, Text...)
  const float leftPanelWidth = 320.0f; // The drawer content
  const float rightPanelWidth = 300.0f; // Details/Properties
  const float centerWidth =
      workSize.x - (leftSidebarWidth + leftPanelWidth + rightPanelWidth);

  // 1. LEFT SIDEBAR + DRAWER (Combined visually into "Media Panel" logic)
  RenderMediaPanel(workPos.x, workPos.y + topBarHeight,
                   leftSidebarWidth + leftPanelWidth, mainAreaHeight);

  // 2. PREVIEW PANEL (Center)
  RenderPreviewPanel(workPos.x + leftSidebarWidth + leftPanelWidth,
                     workPos.y + topBarHeight, centerWidth, mainAreaHeight);

  // 3. PROPERTIES PANEL (Right)
  RenderPropertiesPanel(
      workPos.x + leftSidebarWidth + leftPanelWidth + centerWidth,
      workPos.y + topBarHeight, rightPanelWidth, mainAreaHeight);

  // 4. TIMELINE PANEL (Bottom)
  RenderTimelinePanel(workPos.x, workPos.y + topBarHeight + mainAreaHeight,
                      workSize.x, timelineHeight);
}

void UIManager::RenderMediaPanel(float x, float y, float w, float h) {
  ImGui::SetNextWindowPos(ImVec2(x, y));
  ImGui::SetNextWindowSize(ImVec2(w, h));

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

  ImGui::Begin("MediaContainer", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  // A. Icon Sidebar (Leftmost strip)
  float sidebarW = 70.0f;
  ImGui::BeginChild("SidebarIcons", ImVec2(sidebarW, 0), false,
                    ImGuiWindowFlags_NoScrollbar);
  // Background for strip
  ImGui::GetWindowDrawList()->AddRectFilled(
      ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(),
      IM_COL32(25, 25, 25, 255));

  static int activeTab = 0; // 0=Media, 1=Audio, 2=Text, 3=Sticker, 4=Effects,
                            // 5=Transitions, 6=Filters, 7=Adjust

  const char *labels[] = {"Media",   "Audio",       "Text",    "Stickers",
                          "Effects", "Transitions", "Filters", "Adjustment"};
  // Fixed FontAwesome 6 icon names (or close approximations)
  const char *icons[] = {ICON_FA_FILM,
                         ICON_FA_MUSIC,
                         ICON_FA_FONT,
                         ICON_FA_FACE_SMILE,
                         ICON_FA_WAND_MAGIC_SPARKLES,
                         ICON_FA_HOURGLASS_HALF,
                         ICON_FA_FILTER,
                         ICON_FA_SLIDERS};

  for (int i = 0; i < 8; ++i) {
    bool active = (activeTab == i);
    ImGui::PushID(i);

    // Custom Button Look
    ImVec2 size(sidebarW, sidebarW);
    ImGui::SetCursorPosX(0);

    if (active) {
      ImGui::GetWindowDrawList()->AddRectFilled(
          ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + size,
          IM_COL32(45, 45, 50, 255));
      ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetCursorScreenPos(),
                                                ImGui::GetCursorScreenPos() +
                                                    ImVec2(3, size.y),
                                                IM_COL32(0, 200, 215, 255));
    }

    if (ImGui::InvisibleButton("##btn", size))
      activeTab = i;

    // Icon & Text
    ImVec2 center = ImGui::GetItemRectMin() + size * 0.5f;
    ImU32 textColor =
        active ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 150, 150, 255);

    ImFont *iconFont = ImGui::GetIO().Fonts->Fonts.Size > 1
                           ? ImGui::GetIO().Fonts->Fonts[1]
                           : ImGui::GetFont();
    ImVec2 iconSize = iconFont->CalcTextSizeA(24.0f, FLT_MAX, 0.0f, icons[i]);
    ImGui::GetWindowDrawList()->AddText(
        iconFont, 24.0f, center - iconSize * 0.5f, textColor, icons[i]);
    ImGui::GetWindowDrawList()->AddText(
        ImGui::GetFont(), 12.0f,
        center - ImVec2(ImGui::CalcTextSize(labels[i]).x * 0.5f, -10),
        textColor, labels[i]);

    ImGui::PopID();
  }
  ImGui::EndChild();

  ImGui::SameLine();

  // B. Content Drawer
  ImGui::BeginChild("DrawerContent", ImVec2(0, 0), false);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));

  if (activeTab == 0) { // MEDIA
    ImVec2 btnSize(ImGui::GetContentRegionAvail().x, 32);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.78f, 0.84f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
    if (ImGui::Button(ICON_FA_PLUS " Import", btnSize)) {
      if (g_Application)
        g_Application->OpenVideoFile();
    }
    ImGui::PopStyleColor(2);
    ImGui::Separator();
    ImGui::TextDisabled("Local");
    // Grid of files
    // Mockup items
    float itemSz = 90;
    int cols = (int)(ImGui::GetContentRegionAvail().x / (itemSz + 10));
    if (cols < 1)
      cols = 1;

    if (ImGui::BeginTable("MediaGrid", cols)) {
      for (int i = 0; i < 5; i++) {
        ImGui::TableNextColumn();
        ImGui::Button("##Thumb", ImVec2(itemSz, itemSz));
        ImGui::Text("Clip %d", i + 1);
      }
      ImGui::EndTable();
    }
  } else if (activeTab == 4 || activeTab == 6) { // Effects or Filters
    ImGui::Text(activeTab == 4 ? "Video Effects" : "Filters");

    // Category pills
    ImGui::Button("Trending", ImVec2(60, 24));
    ImGui::SameLine();
    ImGui::Button("Basic", ImVec2(60, 24));
    ImGui::SameLine();
    ImGui::Button("Party", ImVec2(60, 24));
    ImGui::Separator();

    if (activeTab == 6) { // FILTERS TAB
      // Generate Thumbnails if needed or if they're invalid
      bool thumbnailsValid = !m_FilterThumbnails.empty();
      if (thumbnailsValid) {
        // Verify first thumbnail is still valid
        GLboolean isTexture = glIsTexture(m_FilterThumbnails[0]);
        thumbnailsValid = (isTexture == GL_TRUE);

        // Also check if demo image is still valid
        if (thumbnailsValid && m_DemoImageTexture != 0) {
          GLboolean isDemoValid = glIsTexture(m_DemoImageTexture);
          if (isDemoValid != GL_TRUE) {
            std::cout << "[UIManager] Demo image texture became invalid, "
                         "regenerating..."
                      << std::endl;
            thumbnailsValid = false;
          }
        }
      }

      if (!thumbnailsValid) {
        // Clear old thumbnails and regenerate
        std::cout << "[UIManager] Regenerating filter thumbnails (valid="
                  << thumbnailsValid << ", count=" << m_FilterThumbnails.size()
                  << ")" << std::endl;
        m_FilterThumbnails.clear();
        m_FilterGenerationAttempted = false;
        m_DemoImageTexture = 0; // Reset to force reload
        LoadDemoImage();
        GenerateFilterThumbnails();
      }

      float itemSz = 90;
      int cols = (int)(ImGui::GetContentRegionAvail().x / (itemSz + 10));
      if (cols < 2)
        cols = 2;

      if (ImGui::BeginTable("FilterGrid", cols)) {
        // Filter definitions
        const char *filterNames[] = {
            "Normal",         "Light Green", "80s Holiday",    "Milky Tone",
            "Cinematic Dusk", "Ice City",    "Flash CCD",      "LA Classic",
            "Warlock",        "Brighten Up", "Hollywood Past", "Fade",
            "Maldives",       "Clear",       "Azure Morning",  "Hasselblad"};
        int filterCount = 16;
        // Reuse size of filter thumbnails if matches
        if (m_FilterThumbnails.size() < (size_t)filterCount)
          filterCount = (int)m_FilterThumbnails.size();

        for (int i = 0; i < filterCount; i++) {
          ImGui::TableNextColumn();
          ImGui::PushID(i);

          // Filter Card
          ImVec2 pMin = ImGui::GetCursorScreenPos();
          // Image Button
          if (ImGui::ImageButton("##filterBtn",
                                 (ImTextureID)(intptr_t)m_FilterThumbnails[i],
                                 ImVec2(itemSz, itemSz))) {
            // Apply Filter
            if (m_TextureRenderer) {
              m_TextureRenderer->SetFilterType(i); // 0 = Normal
              std::cout << "[UIManager] Applied filter: " << filterNames[i]
                        << " (index " << i << ")" << std::endl;
            }
          }

          // Selection Highlight
          if (m_TextureRenderer && m_TextureRenderer->GetFilterType() == i) {
            ImGui::GetWindowDrawList()->AddRect(
                pMin, pMin + ImVec2(itemSz, itemSz) + ImVec2(5, 5),
                IM_COL32(0, 200, 215, 255), 4.0f, 0, 3.0f);
          }

          ImGui::TextWrapped("%s", filterNames[i]);
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
    } else { // EFFECTS TAB
      ImGui::Text("Video Effects");
      ImGui::Separator();

      // Blur Effects Section
      ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.85f, 1.0f), "Blur Effects");
      ImGui::Spacing();

      float itemSz = 80;
      int cols = (int)(ImGui::GetContentRegionAvail().x / (itemSz + 10));
      if (cols < 1)
        cols = 1;

      if (ImGui::BeginTable("BlurGrid", cols)) {
        const char *blurNames[] = {"Gaussian", "Motion", "Radial", "Zoom"};

        for (int i = 0; i < 4; i++) {
          ImGui::TableNextColumn();
          ImGui::PushID(i);

          if (ImGui::Button("##BlurBtn", ImVec2(itemSz, itemSz))) {
            // Add effect to timeline at current playhead position
            if (m_TimelineManager) {
              EffectLayer::EffectType effectType =
                  (EffectLayer::EffectType)(EffectLayer::BLUR_GAUSSIAN + i);
              double startTime = m_CurrentTime;
              double duration = 2.0; // Default 2 seconds

              int effectId = m_TimelineManager->AddEffectLayer(
                  effectType, startTime, duration);
              m_SelectedEffectId = effectId; // Select the newly added effect

              std::cout << "[UIManager] Added " << blurNames[i]
                        << " effect to timeline at " << startTime << "s"
                        << std::endl;
            }
          }

          ImGui::TextWrapped("%s", blurNames[i]);
          ImGui::PopID();
        }
        ImGui::EndTable();
      }

      // Show parameter controls if an effect is selected
      if (m_SelectedEffectId >= 0 && m_TimelineManager) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Find selected effect
        auto &effects = m_TimelineManager->GetEffectLayers();
        auto it = std::find_if(effects.begin(), effects.end(),
                               [this](const EffectLayer &e) {
                                 return e.id == m_SelectedEffectId;
                               });

        if (it != effects.end()) {
          ImGui::Text("Selected: %s", it->GetEffectName());
          ImGui::Text("Time: %.1fs - %.1fs", it->startTime,
                      it->startTime + it->duration);

          ImGui::Spacing();

          // Intensity slider
          float intensity =
              it->params.count("intensity") ? it->params.at("intensity") : 0.5f;
          if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 1.0f, "%.2f")) {
            m_TimelineManager->UpdateEffectParam(m_SelectedEffectId,
                                                 "intensity", intensity);
          }

          ImGui::Spacing();
          if (ImGui::Button("Delete Effect", ImVec2(-1, 30))) {
            m_TimelineManager->RemoveEffectLayer(m_SelectedEffectId);
            m_SelectedEffectId = -1;
          }
        }
      }

      // Placeholder for other effects
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::TextDisabled("More effects coming soon...");
    }
  } else {
    ImGui::TextDisabled("Content not implemented yet");
  }

  ImGui::PopStyleVar();
  ImGui::EndChild();

  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}
// Note: RenderPreviewPanel, RenderPropertiesPanel, RenderTimelinePanel need
// similar small fixes below but I will output the whole block for
// RenderMediaPanel and earlier above, and then subsequent calls for others if
// needed. Actually, I can do one large replacement for the missing operators
// and identifiers across the file. The replacement above covered up to
// RenderMediaPanel. I need to make sure I cover the rest or use multi-replace.
// I will just use ReplaceFileContent to fix the TOP part (operators) and then
// use another call or just assume the tool call covered enough context. Wait,
// the tool only allows contiguous blocks. I will update the entire first half
// of the file including the class methods.

void UIManager::RenderPreviewPanel(float x, float y, float w, float h) {
  ImGui::SetNextWindowPos(ImVec2(x, y));
  ImGui::SetNextWindowSize(ImVec2(w, h));

  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // Pitch black preview
  ImGui::Begin("Preview", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove);

  // Header strip for preview (Ratio, Zoom level)
  ImGui::BeginGroup();
  ImGui::TextDisabled("Player");
  ImGui::SameLine(ImGui::GetWindowWidth() - 40);
  ImGui::Text(ICON_FA_BARS); // Menu icon
  ImGui::EndGroup();

  // Video Area
  ImVec2 winSize = ImGui::GetWindowSize();
  float availableH = winSize.y - 60; // Space for controls

  float targetAspectRatio = 16.0f / 9.0f;
  if (m_AspectRatioMode == 1)
    targetAspectRatio = 9.0f / 16.0f;
  else if (m_VideoPlayer && m_VideoPlayer->IsLoaded()) {
    targetAspectRatio =
        (float)m_VideoPlayer->GetWidth() / (float)m_VideoPlayer->GetHeight();
  }

  float previewW, previewH;
  if (winSize.x / availableH > targetAspectRatio) {
    previewH = availableH * 0.9f;
    previewW = previewH * targetAspectRatio;
  } else {
    previewW = winSize.x * 0.9f;
    previewH = previewW / targetAspectRatio;
  }

  float offsetX = (winSize.x - previewW) * 0.5f;
  float offsetY = (availableH - previewH) * 0.5f + 20;

  // Draw Video
  if (m_VideoPlayer && m_VideoPlayer->IsLoaded() && m_TextureRenderer) {
    // CRITICAL: Apply timeline effects at current playback time
    if (m_TimelineManager) {
      // Clear all effects first
      m_TextureRenderer->SetBlurEffect(0.0f, 0);

      // Get effects active at current time
      auto activeEffects = m_TimelineManager->GetActiveEffects(m_CurrentTime);

      // Apply each active effect
      for (auto *effect : activeEffects) {
        if (!effect)
          continue;

        // Apply based on effect type
        if (effect->type >= EffectLayer::BLUR_GAUSSIAN &&
            effect->type <= EffectLayer::BLUR_ZOOM) {
          // Blur effects
          float intensity = effect->params.count("intensity")
                                ? effect->params.at("intensity")
                                : 0.5f;
          int blurType = effect->params.count("blurType")
                             ? (int)effect->params.at("blurType")
                             : 0;
          m_TextureRenderer->SetBlurEffect(intensity, blurType);
        }
        // Future: Add other effect types (glitch, distortion, etc.)
      }
    }

    ImGui::SetCursorPos(ImVec2(offsetX, offsetY));
    // Use filtered texture ID so filters are applied in preview
    GLuint previewTexture =
        m_TextureRenderer->GetFilteredTextureID((int)previewW, (int)previewH);
    ImGui::Image((ImTextureID)(intptr_t)previewTexture,
                 ImVec2(previewW, previewH));
  } else {
    // Placeholder
    ImGui::SetCursorPos(ImVec2(offsetX, offsetY));
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetCursorScreenPos(),
                                              ImGui::GetCursorScreenPos() +
                                                  ImVec2(previewW, previewH),
                                              IM_COL32(20, 20, 20, 255));
    ImGui::SetCursorPos(
        ImVec2(offsetX + previewW * 0.5f - 40, offsetY + previewH * 0.5f - 10));
    ImGui::TextDisabled("No Source");
  }

  // Bottom Controls (Play, Timecode)
  ImGui::SetCursorPos(ImVec2(10, winSize.y - 40));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0.8f, 0.85f, 1.0f));
  ImGui::Text("%s", FormatTime(m_CurrentTime));
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::TextDisabled(" / %s", FormatTime(m_TotalDuration));

  // Centered Play Button
  ImGui::SetCursorPos(ImVec2(winSize.x * 0.5f - 15, winSize.y - 45));
  if (ImGui::Button(m_IsPlaying ? ICON_FA_PAUSE : ICON_FA_PLAY,
                    ImVec2(30, 30))) {
    OnSpacePressed();
  }

  // Right side controls (Ratio, Fullscreen)
  ImGui::SetCursorPos(ImVec2(winSize.x - 100, winSize.y - 40));
  if (ImGui::Button("Ratio")) {
    m_AspectRatioMode = (m_AspectRatioMode + 1) % 3;
  }
  ImGui::SameLine();
  ImGui::Button(ICON_FA_EXPAND);

  ImGui::End();
  ImGui::PopStyleColor();
}

void UIManager::RenderPropertiesPanel(float x, float y, float w, float h) {
  ImGui::SetNextWindowPos(ImVec2(x, y));
  ImGui::SetNextWindowSize(ImVec2(w, h));
  ImGui::Begin("Details", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove);

  ImGui::Text("Details");
  ImGui::Separator();

  if (ImGui::BeginTabBar("PropTabs")) {
    if (ImGui::BeginTabItem("Video")) {
      ImGui::Spacing();
      ImGui::TextDisabled("Basic");

      ImGui::Dummy(ImVec2(0, 10));
      ImGui::Text("Scale");
      ImGui::SameLine(80);
      static float scale = 100.0f;
      ImGui::DragFloat("##Scale", &scale, 1.0f, 10.0f, 500.0f, "%.0f%%");

      ImGui::Text("Pos");
      ImGui::SameLine(80);
      static float pos[2] = {0, 0};
      ImGui::DragFloat2("##Pos", pos);

      ImGui::Text("Rotation");
      ImGui::SameLine(80);
      static float rot = 0.0f;
      ImGui::DragFloat("##Rot", &rot);

      ImGui::Separator();
      ImGui::TextDisabled("Blend");
      ImGui::Text("Opacity");
      ImGui::SameLine(80);
      static float opacity = 100.0f;
      ImGui::SliderFloat("##Op", &opacity, 0.0f, 100.0f, "%.0f%%");

      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Speed")) {
      ImGui::Text("Curvet");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Animation")) {
      ImGui::Text("In / Out / Combo");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Adjustments")) {
      if (m_TextureRenderer) {
        // Determine current values if possible, or use statics.
        // Ideally we read from renderer so state is preserved if tab
        // creates/destroys. But RenderPropertiesPanel is called every frame. We
        // use static vars initialized once? Or local vars? Better: Read from
        // renderer (Getters added), modify, set back.

        float brightness = m_TextureRenderer->GetBrightness();
        float contrast = m_TextureRenderer->GetContrast();
        float saturation = m_TextureRenderer->GetSaturation();
        float vignette = m_TextureRenderer->GetVignette();
        float grain = m_TextureRenderer->GetGrain();
        float aberration = m_TextureRenderer->GetAberration();
        bool sepia = m_TextureRenderer->GetSepia();

        bool changed = false;
        ImGui::Separator();
        ImGui::TextDisabled("Color Correction");
        ImGui::Text("Brightness");
        ImGui::SameLine(100);
        if (ImGui::SliderFloat("##Br", &brightness, -1.0f, 1.0f))
          changed = true;

        ImGui::Text("Contrast");
        ImGui::SameLine(100);
        if (ImGui::SliderFloat("##Co", &contrast, 0.0f, 2.0f))
          changed = true;

        ImGui::Text("Saturation");
        ImGui::SameLine(100);
        if (ImGui::SliderFloat("##Sa", &saturation, 0.0f, 2.0f))
          changed = true;

        ImGui::Separator();
        ImGui::TextDisabled("Effects");

        ImGui::Text("Vignette");
        ImGui::SameLine(100);
        if (ImGui::SliderFloat("##Vi", &vignette, 0.0f, 1.0f))
          changed = true;

        ImGui::Text("Film Grain");
        ImGui::SameLine(100);
        if (ImGui::SliderFloat("##Gr", &grain, 0.0f, 1.0f))
          changed = true;

        ImGui::Text("Aberration");
        ImGui::SameLine(100);
        if (ImGui::SliderFloat("##Ab", &aberration, 0.0f, 0.05f))
          changed = true;

        ImGui::Text("Sepia");
        ImGui::SameLine(100);
        if (ImGui::Checkbox("##Se", &sepia))
          changed = true;

        if (changed) {
          m_TextureRenderer->SetFilterParams(brightness, contrast, saturation);
          m_TextureRenderer->SetEffectParams(vignette, grain, aberration,
                                             sepia);
        }
      } else {
        ImGui::TextDisabled("Renderer not available");
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}

void UIManager::RenderTimelinePanel(float x, float y, float w, float h) {
  ImGui::SetNextWindowPos(ImVec2(x, y));
  ImGui::SetNextWindowSize(ImVec2(w, h));

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("TimelinePanel", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove);

  // 1. Toolbar Strip
  float toolbarH = 40.0f;
  ImGui::BeginChild("TimelineToolbar", ImVec2(0, toolbarH), false);
  ImGui::SetCursorPos(ImVec2(10, 5));

  // Tools
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(15, 0));
  IconButton("##Sel", ICON_FA_ARROW_POINTER, true);
  ImGui::SameLine();
  IconButton("##Split", ICON_FA_SCISSORS);
  ImGui::SameLine(); // Split
  IconButton("##Del", ICON_FA_TRASH);
  ImGui::SameLine();
  IconButton("##Freeze", ICON_FA_SNOWFLAKE);

  // Right side: Zoom, Magnet etc
  ImGui::SameLine(ImGui::GetWindowWidth() - 200);
  ImGui::Text(ICON_FA_MAGNET);
  ImGui::SameLine();
  ImGui::Text(ICON_FA_MAGNIFYING_GLASS_MINUS);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100);
  ImGui::SliderFloat("##Zoom", &m_TimelineZoom, 0.5f, 5.0f, "");
  ImGui::SameLine();
  ImGui::Text(ICON_FA_MAGNIFYING_GLASS_PLUS);

  ImGui::PopStyleVar();
  ImGui::EndChild();

  ImGui::Separator();

  // 2. Tracks rendering (Call the fix from before but adapted)
  RenderTimelineTracks();

  ImGui::End();
  ImGui::PopStyleVar();
}

void UIManager::RenderTimelineTracks() {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
  ImGui::BeginChild("TrackArea", ImVec2(0, 0), false,
                    ImGuiWindowFlags_HorizontalScrollbar);

  ImDrawList *drawList = ImGui::GetWindowDrawList();
  ImVec2 cursor = ImGui::GetCursorScreenPos(); // Top left of track area
  ImVec2 contentAvail = ImGui::GetContentRegionAvail();

  float pixelsPerSecond = 20.0f * m_TimelineZoom;
  float rulerHeight = 30.0f;
  float trackHeight = 40.0f;
  float gap = 10.0f;
  float totalWidth =
      std::max(contentAvail.x, m_TotalDuration * pixelsPerSecond);

  // A. Time Ruler
  drawList->AddRectFilled(cursor,
                          ImVec2(cursor.x + totalWidth, cursor.y + rulerHeight),
                          IM_COL32(30, 30, 30, 255));

  // Ticks
  int step = (m_TimelineZoom < 1.0f) ? 5 : 1;
  for (float t = 0; t <= m_TotalDuration; t += (float)step) {
    float px = cursor.x + t * pixelsPerSecond;
    float tickH = (fmod(t, 5.0f) == 0) ? 15.0f : 8.0f;
    drawList->AddLine(ImVec2(px, cursor.y + rulerHeight - tickH),
                      ImVec2(px, cursor.y + rulerHeight),
                      IM_COL32(150, 150, 150, 255));

    if (fmod(t, 5.0f) == 0) {
      char buf[16];
      snprintf(buf, 16, "%s", FormatTime(t));
      drawList->AddText(ImVec2(px + 4, cursor.y), IM_COL32(100, 100, 100, 255),
                        buf);
    }
  }

  // Seek logic
  ImGui::SetCursorScreenPos(cursor);
  ImGui::InvisibleButton("##RulerHit", ImVec2(totalWidth, rulerHeight));
  if (ImGui::IsItemActive() || ImGui::IsItemClicked()) {
    float mx = ImGui::GetMousePos().x;
    m_CurrentTime = (mx - cursor.x) / pixelsPerSecond;
    if (m_CurrentTime < 0)
      m_CurrentTime = 0;
    if (m_VideoPlayer)
      m_VideoPlayer->Seek(m_CurrentTime, true);
  }

  float startY = cursor.y + rulerHeight + 10;

  // B. Tracks
  int trackCount = 0;
  if (m_TimelineManager) {
    auto &tracks = m_TimelineManager->GetTracks();
    for (auto &track : tracks) {
      // Track Header visualization (Left side usually, but we are scrolling
      // horizontal only for now)

      // Clips
      for (auto &clip : track.clips) {
        float x1 = cursor.x + (float)clip.startTime * pixelsPerSecond;
        float width = (float)clip.GetDisplayDuration() * pixelsPerSecond;
        float x2 = x1 + width;
        float y1 = startY + trackCount * (trackHeight + gap);
        float y2 = y1 + trackHeight;

        bool selected = (clip.id == m_SelectedClipId);

        // Display main clip rect
        drawList->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2),
                                selected ? IM_COL32(100, 200, 200, 255)
                                         : IM_COL32(60, 60, 70, 255),
                                4.0f);
        if (selected)
          drawList->AddRect(ImVec2(x1, y1), ImVec2(x2, y2),
                            IM_COL32(255, 255, 255, 255), 4.0f, 0, 2.0f);

        // Clip Thumbnails (Mockup)
        drawList->PushClipRect(ImVec2(x1, y1), ImVec2(x2, y2), true);
        // If we had real thumbnails, we'd loop and draw Image here
        // Text
        drawList->AddText(ImVec2(x1 + 5, y1 + 12), IM_COL32(255, 255, 255, 255),
                          clip.filepath.c_str());
        drawList->PopClipRect();

        // Input handling
        ImGui::SetCursorScreenPos(ImVec2(x1, y1));
        std::string btnId = "##Clip" + std::to_string(clip.id);
        ImGui::InvisibleButton(btnId.c_str(), ImVec2(width, trackHeight));

        if (ImGui::IsItemClicked()) {
          m_SelectedClipId = clip.id;
          m_SelectedTrackIndex = trackCount;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
          float delta = ImGui::GetMouseDragDelta(0).x;
          if (abs(delta) > 0) {
            // Drag logic update
            m_TimelineManager->MoveClip(
                trackCount, clip.id, clip.startTime + delta / pixelsPerSecond);
            ImGui::ResetMouseDragDelta(0);
          }
        }
      }
      trackCount++;
    }
  }

  // C. Effect Track (NEW!)
  if (m_TimelineManager) {
    float effectTrackY = startY + trackCount * (trackHeight + gap) + gap;
    float effectTrackHeight = 30.0f;

    // Effect track background
    drawList->AddRectFilled(
        ImVec2(cursor.x, effectTrackY),
        ImVec2(cursor.x + totalWidth, effectTrackY + effectTrackHeight),
        IM_COL32(25, 25, 30, 255), 2.0f);

    // Track label
    drawList->AddText(ImVec2(cursor.x + 5, effectTrackY + 8),
                      IM_COL32(150, 150, 150, 255), "Effects");

    // Render effect layers
    auto &effectLayers = m_TimelineManager->GetEffectLayers();
    for (auto &effect : effectLayers) {
      float x1 = cursor.x + (float)effect.startTime * pixelsPerSecond;
      float width = (float)effect.duration * pixelsPerSecond;
      float x2 = x1 + width;
      float y1 = effectTrackY + 2;
      float y2 = effectTrackY + effectTrackHeight - 2;

      // Effect color based on type
      ImU32 effectColor = IM_COL32(70, 130, 180, 220); // Default blue for blur
      if (effect.type >= EffectLayer::GLITCH &&
          effect.type <= EffectLayer::DISTORTION) {
        effectColor =
            IM_COL32(150, 70, 180, 220); // Purple for glitch/distortion
      } else if (effect.type >= EffectLayer::EDGE_GLOW) {
        effectColor = IM_COL32(180, 130, 70, 220); // Orange for creative
      }

      bool isSelected = (m_SelectedEffectId == effect.id);

      // Draw effect bar
      drawList->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2),
                              isSelected ? IM_COL32_WHITE : effectColor, 3.0f);

      // Selection border
      if (isSelected) {
        drawList->AddRect(ImVec2(x1, y1), ImVec2(x2, y2),
                          IM_COL32(0, 200, 215, 255), 3.0f, 0, 2.0f);
      }

      // Effect label
      drawList->PushClipRect(ImVec2(x1, y1), ImVec2(x2, y2), true);
      drawList->AddText(ImVec2(x1 + 4, y1 + 6),
                        isSelected ? IM_COL32_BLACK : IM_COL32_WHITE,
                        effect.GetEffectName());
      drawList->PopClipRect();

      // Resize handles
      float handleWidth = 6.0f;
      if (isSelected) {
        // Left handle
        drawList->AddRectFilled(ImVec2(x1, y1), ImVec2(x1 + handleWidth, y2),
                                IM_COL32(255, 255, 255, 200));
        // Right handle
        drawList->AddRectFilled(ImVec2(x2 - handleWidth, y1), ImVec2(x2, y2),
                                IM_COL32(255, 255, 255, 200));
      }

      // Interaction - Main body (move)
      ImGui::SetCursorScreenPos(ImVec2(x1 + handleWidth, y1));
      std::string btnId = "##Effect" + std::to_string(effect.id);
      ImGui::InvisibleButton(btnId.c_str(), ImVec2(width - handleWidth * 2,
                                                   effectTrackHeight - 4));

      if (ImGui::IsItemClicked()) {
        m_SelectedEffectId = effect.id;
        std::cout << "[UIManager] Selected effect: " << effect.GetEffectName()
                  << " (ID: " << effect.id << ")" << std::endl;
      }

      // Dragging - move effect
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        float delta = ImGui::GetMouseDragDelta(0).x;
        if (std::abs(delta) > 0) {
          double newStartTime = effect.startTime + delta / pixelsPerSecond;
          m_TimelineManager->MoveEffectLayer(effect.id, newStartTime);
          ImGui::ResetMouseDragDelta(0);
        }
      }

      // Left resize handle interaction
      if (isSelected) {
        ImGui::SetCursorScreenPos(ImVec2(x1, y1));
        std::string leftHandleId =
            "##EffectLeftHandle" + std::to_string(effect.id);
        ImGui::InvisibleButton(leftHandleId.c_str(),
                               ImVec2(handleWidth, effectTrackHeight - 4));

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
          float delta = ImGui::GetMouseDragDelta(0).x;
          if (std::abs(delta) > 0) {
            double newStartTime = effect.startTime + delta / pixelsPerSecond;
            double newDuration = effect.duration - delta / pixelsPerSecond;
            if (newDuration >= 0.1) { // Min duration
              m_TimelineManager->MoveEffectLayer(effect.id, newStartTime);
              m_TimelineManager->ResizeEffectLayer(effect.id, newDuration);
            }
            ImGui::ResetMouseDragDelta(0);
          }
        }

        // Right resize handle interaction
        ImGui::SetCursorScreenPos(ImVec2(x2 - handleWidth, y1));
        std::string rightHandleId =
            "##EffectRightHandle" + std::to_string(effect.id);
        ImGui::InvisibleButton(rightHandleId.c_str(),
                               ImVec2(handleWidth, effectTrackHeight - 4));

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
          float delta = ImGui::GetMouseDragDelta(0).x;
          if (std::abs(delta) > 0) {
            double newDuration = effect.duration + delta / pixelsPerSecond;
            if (newDuration >= 0.1) { // Min duration
              m_TimelineManager->ResizeEffectLayer(effect.id, newDuration);
            }
            ImGui::ResetMouseDragDelta(0);
          }
        }
      }
    }
  }

  // D. Playhead
  float phX = cursor.x + m_CurrentTime * pixelsPerSecond;
  float phY = cursor.y;
  drawList->AddLine(ImVec2(phX, phY), ImVec2(phX, phY + 500),
                    IM_COL32(255, 255, 255, 255), 1.0f);
  // Head
  drawList->AddTriangleFilled(ImVec2(phX - 5, phY), ImVec2(phX + 5, phY),
                              ImVec2(phX, phY + 10),
                              IM_COL32(255, 255, 255, 255));

  ImGui::EndChild();
  ImGui::PopStyleColor();
}

void UIManager::RenderMenuBar() {
  // Custom Main Menu Bar matching Screenshot
  // Custom Top Bar (Status Bar / Title Bar)
  ImGuiViewport *viewport = ImGui::GetMainViewport();
  float menuBarHeight = 40.0f;

  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, menuBarHeight));

  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        ImVec4(0.08f, 0.08f, 0.09f, 1.0f)); // Very dark top bar
  // Vertical centering: (40 - 24) / 2 = 8px padding. Button height 24.
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

  ImGui::Begin("TopBar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  // Window Drag Logic moved to end to respect button usage

  // Left: Logo & Menu
  ImGui::SetCursorPosY(10); // Center text vertically (Text is ~16-20px)
  ImGui::TextColored(ImVec4(0, 0.8f, 0.85f, 1), "CapCut");
  ImGui::SameLine(0, 20);

  // Menu Button (Custom size to match height)
  ImGui::SetCursorPosY(6);                     // Slightly up for button
  if (ImGui::Button("Menu", ImVec2(50, 24))) { // Fixed size 24 height
    ImGui::OpenPopup("MenuPopup");
  }
  if (ImGui::BeginPopup("MenuPopup")) {
    if (ImGui::MenuItem("Open Project")) {
      if (g_Application)
        g_Application->OpenVideoFile();
    }
    if (ImGui::MenuItem("Save Project")) {
    }
    if (ImGui::MenuItem("Export")) {
      m_ShowExportDialog = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Exit")) {
      GLFWwindow *window = glfwGetCurrentContext();
      if (window)
        glfwSetWindowShouldClose(window, true);
    }
    ImGui::EndPopup();
  }

  // Center: Project Name
  ImGui::SameLine();
  float availW = ImGui::GetContentRegionAvail().x;
  const char *title = "My Awesome Video - Draft";
  float titleW = ImGui::CalcTextSize(title).x;
  ImGui::SetCursorPosX((viewport->WorkSize.x - titleW) * 0.5f);
  ImGui::SetCursorPosY(10); // Center text
  ImGui::Text("%s", title);

  // Right: Window Controls (Min, Max, Close) & Export
  // Calculate precise width: Keyboard(30) + Gap(10) + Export(80) + Gap(15) +
  // Min(24) + Gap(5) + Max(24) + Gap(5) + Close(24) = 217 + Padding(10) = 227
  float rightGroupWidth = 230.0f;
  ImGui::SameLine(viewport->WorkSize.x - rightGroupWidth);

  ImGui::SetCursorPosY(8); // Reset Y to padding top (8px)

  // Shortcut icon (Fixed size)
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::Button(ICON_FA_KEYBOARD, ImVec2(30, 24));
  ImGui::PopStyleColor();

  ImGui::SameLine();

  // Export Button
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.78f, 0.84f, 1.00f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
  if (ImGui::Button("Export", ImVec2(80, 24))) {
    m_ShowExportDialog = true;
  }
  ImGui::PopStyleColor(2);

  // Custom Window Controls
  ImGui::SameLine(0, 15);
  bool isMaximized =
      glfwGetWindowAttrib(glfwGetCurrentContext(), GLFW_MAXIMIZED);

  if (ImGui::Button(ICON_FA_MINUS, ImVec2(24, 24))) { // Minimize
    glfwIconifyWindow(glfwGetCurrentContext());
  }
  ImGui::SameLine(0, 5);
  if (ImGui::Button(isMaximized ? ICON_FA_COMPRESS : ICON_FA_EXPAND,
                    ImVec2(24, 24))) { // Maximize/Restore
    if (isMaximized)
      glfwRestoreWindow(glfwGetCurrentContext());
    else
      glfwMaximizeWindow(glfwGetCurrentContext());
  }
  ImGui::SameLine(0, 5);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
  if (ImGui::Button(ICON_FA_XMARK, ImVec2(24, 24))) { // Close
    glfwSetWindowShouldClose(glfwGetCurrentContext(), true);
  }
  ImGui::PopStyleColor(); // Button color

  // Window Drag Logic (Corrected: Check if NO items are hovered)
  static bool isDragging = false;
  static double startX = 0, startY = 0;

  // Only start dragging if hovering window AND NOT hovering any interactive
  // button
  if (ImGui::IsWindowHovered() &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    if (!ImGui::IsAnyItemHovered()) {
      isDragging = true;
      GLFWwindow *window = glfwGetCurrentContext();
      if (window)
        glfwGetCursorPos(window, &startX, &startY);
    }
  }

  if (isDragging) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      GLFWwindow *window = glfwGetCurrentContext();
      if (window) {
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        int winX, winY;
        glfwGetWindowPos(window, &winX, &winY);
        glfwSetWindowPos(window, winX + (int)(x - startX),
                         winY + (int)(y - startY));
      }
    } else {
      isDragging = false;
    }
  }

  ImGui::End();
  ImGui::PopStyleVar(2);  // WindowPadding, WindowBorderSize
  ImGui::PopStyleColor(); // WindowBg
}

// Helper to get export values
int GetWidthFromIndex(int index) {
  switch (index) {
  case 0:
    return 854; // 480p
  case 1:
    return 1280; // 720p
  case 2:
    return 1920; // 1080p
  case 3:
    return 2560; // 2k
  case 4:
    return 3840; // 4k
  default:
    return 1920;
  }
}
int GetHeightFromIndex(int index) {
  switch (index) {
  case 0:
    return 480;
  case 1:
    return 720;
  case 2:
    return 1080;
  case 3:
    return 1440;
  case 4:
    return 2160;
  default:
    return 1080;
  }
}
int GetFpsFromIndex(int index) {
  int fps[] = {24, 25, 30, 50, 60};
  if (index >= 0 && index < 5)
    return fps[index];
  return 30;
}

void UIManager::RenderExportDialog() {
  if (m_ShowExportDialog) {
    ImGui::OpenPopup("Export Project");
  }

  // Center the dialog
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(800, 550)); // Larger size for new layout

  if (ImGui::BeginPopupModal("Export Project", &m_ShowExportDialog,
                             ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove)) {

    // Use 2 Columns: Left (Cover), Right (Settings)
    if (ImGui::BeginTable("ExportLayout", 2,
                          ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_Resizable)) {
      ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed,
                              300.0f);
      ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthStretch);

      ImGui::TableNextRow();

      // --- LEFT COLUMN: COVER PREVIEW ---
      ImGui::TableSetColumnIndex(0);

      // Draw Cover Image
      float coverWidth = 280.0f;
      float coverHeight = 400.0f; // Approx 9:16 aspect ratio or slightly wider
      if (m_TextureRenderer && m_TextureRenderer->IsInitialized()) {
        // Determine Access Ratio of Source
        // If source is 16:9, displaying it in vertical box is tricky.
        // CapCut clone image shows a vertical phone-like preview.
        // We will fit-center.
        ImGui::Image((ImTextureID)(intptr_t)m_TextureRenderer->GetTextureID(),
                     ImVec2(coverWidth, coverHeight));
      } else {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetCursorScreenPos(),
            ImVec2(ImGui::GetCursorScreenPos().x + coverWidth,
                   ImGui::GetCursorScreenPos().y + coverHeight),
            IM_COL32(20, 20, 20, 255));
        ImGui::SetCursorPos(
            ImVec2(ImGui::GetCursorPosX() + 80, ImGui::GetCursorPosY() + 180));
        ImGui::TextDisabled("No Preview");
      }

      // "Edit cover" button overlay (Visual only)
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() - coverHeight + 10);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
      ImGui::Button(ICON_FA_PEN " Edit cover");

      // --- RIGHT COLUMN: SETTINGS ---
      ImGui::TableSetColumnIndex(1);

      ImGui::PushItemWidth(-1); // Use full width

      // Name
      ImGui::Text("Name");
      ImGui::InputText("##Name", m_ExportName, 256);

      // Export To (Path)
      ImGui::Text("Export to");
      ImGui::InputText("##Path", m_ExportPath, 512);
      // TODO: Add folder button to open dialog

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Video Section
      static bool exportVideo = true;
      ImGui::Checkbox("Video", &exportVideo);

      if (exportVideo) {
        ImGui::Indent();

        // Resolution
        ImGui::Text("Resolution");
        ImGui::SameLine(100);
        const char *resolutions[] = {"480P", "720P", "1080P", "2K", "4K"};
        ImGui::Combo("##Res", &m_ExportResIndex, resolutions,
                     IM_ARRAYSIZE(resolutions));

        // Bitrate
        ImGui::Text("Bit rate");
        ImGui::SameLine(100);
        const char *bitrates[] = {"Recommended", "Higher", "Lower"};
        ImGui::Combo("##Bitrate", &m_ExportBitrateIndex, bitrates,
                     IM_ARRAYSIZE(bitrates));

        // Codec
        ImGui::Text("Codec");
        ImGui::SameLine(100);
        const char *codecs[] = {"H.264", "HEVC", "AV1"};
        ImGui::Combo("##Codec", &m_ExportCodecIndex, codecs,
                     IM_ARRAYSIZE(codecs));

        // Format
        ImGui::Text("Format");
        ImGui::SameLine(100);
        const char *formats[] = {"mp4", "mov"};
        ImGui::Combo("##Format", &m_ExportFormatIndex, formats,
                     IM_ARRAYSIZE(formats));

        // Frame rate
        ImGui::Text("Frame rate");
        ImGui::SameLine(100);
        const char *fpsList[] = {"24fps", "25fps", "30fps", "50fps", "60fps"};
        ImGui::Combo("##Fps", &m_ExportFpsIndex, fpsList,
                     IM_ARRAYSIZE(fpsList));

        ImGui::Spacing();
        ImGui::TextDisabled("Color space: Rec. 709 SDR");

        ImGui::Unindent();
      }

      ImGui::Spacing();

      // Audio Section (Placeholder)
      static bool exportAudio = true;
      ImGui::Checkbox("Audio", &exportAudio);

      // GIF Section (Placeholder)
      static bool exportGif = false;
      ImGui::Checkbox("Export GIF", &exportGif);

      ImGui::PopItemWidth();
      ImGui::EndTable();
    }

    // --- FOOTER ---
    ImGui::Separator();

    // Stats
    float duration = m_VideoPlayer ? m_VideoPlayer->GetDuration() : 0.0f;
    ImGui::Text(ICON_FA_FILM " Duration: %s", FormatTime(duration));
    ImGui::SameLine();
    ImGui::Text("| Size: estimated %d MB",
                (int)(duration * 5)); // Dummy calculation

    // Buttons (Right Aligned)
    float footerHeight = 40.0f;
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - footerHeight - 10);

    float btnWidth = 120.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - btnWidth * 2 - 30);

    if (ImGui::Button("Cancel", ImVec2(btnWidth, 30))) {
      m_ShowExportDialog = false;
    }

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(0, 0.8f, 0.85f, 1.0f)); // CapCut Teal
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    if (ImGui::Button("Export", ImVec2(btnWidth, 30))) {
      if (m_ExportManager) {
        // Ensure Main Window is set (Critical for context sharing)
        if (!m_ExportManager->GetMainWindow()) {
          m_ExportManager->SetMainWindow(glfwGetCurrentContext());
        }

        // Apply Effects logic
        if (m_TextureRenderer) {
          HardwareExportManager::EffectParams params;
          params.brightness = m_TextureRenderer->GetBrightness();
          params.contrast = m_TextureRenderer->GetContrast();
          params.saturation = m_TextureRenderer->GetSaturation();
          params.vignette = m_TextureRenderer->GetVignette();
          params.grain = m_TextureRenderer->GetGrain();
          params.aberration = m_TextureRenderer->GetAberration();
          params.sepia = m_TextureRenderer->GetSepia();
          params.filterType =
              m_TextureRenderer->GetFilterType(); // Include active filter!
          m_ExportManager->SetEffectParams(params);
        }

        // Construct Filename
        // std::string fullPath = std::string(m_ExportPath) +
        // std::string(m_ExportName) + ".mp4";
        // For safety, just use m_ExportFilename buffer or copy to it
        strcpy_s(m_ExportFilename, m_ExportName);
        if (strstr(m_ExportFilename, ".mp4") == nullptr)
          strcat_s(m_ExportFilename, ".mp4");

        int w = GetWidthFromIndex(m_ExportResIndex);
        int h = GetHeightFromIndex(m_ExportResIndex);
        int f = GetFpsFromIndex(m_ExportFpsIndex);

        m_LastExportPath = m_ExportFilename; // Save for success dialog

        // Initialize export configuration
        HardwareExportManager::Config config;
        config.outputFile = m_ExportFilename;
        config.width = w;
        config.height = h;
        config.fps = f;
        config.codec = HardwareExportManager::Codec::H264;
        config.rateControl = HardwareExportManager::RateControl::VBR;
        config.bitrate = 8000000; // 8 Mbps
        config.preset = 1;        // p1 = fastest

        if (m_ExportManager->Initialize(config)) {
          m_ExportManager->StartExport();
        }
      }
      m_ShowExportDialog = false;
      m_ShowExportProgress = true;
    }
    ImGui::PopStyleColor(2);

    ImGui::EndPopup();
  }
}
void UIManager::RenderExportProgress() {
  if (m_ShowExportProgress && m_ExportManager) {
    ImGui::OpenPopup("Exporting...");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Exporting...", NULL,
                               ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoMove)) {
      ImGui::Text("Exporting video, please wait...");
      ImGui::ProgressBar(m_ExportProgress, ImVec2(350, 0));

      ImGui::Spacing();
      ImGui::Text("Progress: %.1f%%", m_ExportProgress * 100.0f);

      ImGui::Spacing();
      if (ImGui::Button("Cancel Export", ImVec2(350, 30))) {
        if (m_ExportManager) {
          m_ExportManager->CancelExport();
        }
      }

      if (!m_ExportManager->IsExporting()) {
        // Export finished - show success dialog
        m_ShowExportProgress = false;
        m_ShowExportSuccess = true;
      }
      ImGui::EndPopup();
    }
  }

  // Export Success Dialog (CapCut style)
  if (m_ShowExportSuccess) {
    ImGui::OpenPopup("Export Complete");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Export Complete", &m_ShowExportSuccess,
                               ImGuiWindowFlags_NoResize)) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0.8f, 0.85f, 1));
      ImGui::Text(ICON_FA_CIRCLE_CHECK " Export Successful!");
      ImGui::PopStyleColor();

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::TextWrapped("Video is saved to:");
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1));
      ImGui::TextWrapped("%s", m_LastExportPath.c_str());
      ImGui::PopStyleColor();

      ImGui::Spacing();
      ImGui::Spacing();

      // Open Folder Button
      if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Open Folder", ImVec2(200, 40))) {
        // Extract directory from full path
        std::string directory = m_LastExportPath;
        size_t lastSlash = directory.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
          directory = directory.substr(0, lastSlash);
        }

// Open folder in Windows Explorer
#ifdef _WIN32
        std::string command = "explorer \"" + directory + "\"";
        system(command.c_str());
#endif
      }

      ImGui::SameLine();

      if (ImGui::Button("Close", ImVec2(100, 40))) {
        m_ShowExportSuccess = false;
      }

      ImGui::EndPopup();
    }
  }
}

void UIManager::CreateDefaultStickerTexture() {} // Placeholder
void UIManager::AddSticker() {}                  // Placeholder
const char *UIManager::FormatTime(float seconds) {
  static char buffer[32];
  int min = (int)seconds / 60;
  int sec = (int)seconds % 60;
  int ms = (int)((seconds - (float)((int)seconds)) * 100);
  snprintf(buffer, 32, "%02d:%02d:%02d", min, sec, ms);
  return buffer;
}
void UIManager::OnSpacePressed() {
  m_IsPlaying = !m_IsPlaying;
  if (m_IsPlaying && m_VideoPlayer)
    m_PlaybackStartTime = glfwGetTime() - m_CurrentTime;
}
void UIManager::OnVideoLoaded(const std::string &filepath) {
  if (m_TimelineManager) {
    m_TimelineManager->AddClipToTrack(filepath, 0, 0.0);
    m_TotalDuration =
        std::max(10.0f, (float)m_TimelineManager->GetTotalDuration() + 20.0f);
    m_CurrentTime = 0.0f;
  }
}
void UIManager::OnOpenVideoClicked() {}

// Helper Methods Implementation

void UIManager::LoadDemoImage() {
  // Check if already loaded AND still valid
  if (m_DemoImageTexture != 0) {
    GLboolean isValid = glIsTexture(m_DemoImageTexture);
    if (isValid == GL_TRUE) {
      return; // Already loaded and valid
    } else {
      std::cout << "[UIManager] Demo image texture is invalid, reloading..."
                << std::endl;
      m_DemoImageTexture = 0; // Reset so we can reload
    }
  }

  // Try to get path from Configuration first
  std::string configPath =
      Configuration::GetInstance().GetString("DemoImagePath");
  if (configPath.empty())
    configPath = "Assets/Images/cat.jpg";

  // Robust search logic (matching Application.cpp)
  std::vector<std::string> searchPrefixes = {"",
                                             "../",
                                             "../CapCutClone/",
                                             "../../",
                                             "../../CapCutClone/",
                                             "CapCutClone/"};

  std::string path = configPath;
  bool found = false;

  for (const auto &prefix : searchPrefixes) {
    std::string testPath = prefix + configPath;
    FILE *f = nullptr;
    if (fopen_s(&f, testPath.c_str(), "rb") == 0 && f) {
      fclose(f);
      path = testPath;
      found = true;
      std::cout << "[UIManager] Found Demo Image at: " << path << std::endl;
      break;
    }
  }

  if (!found) {
    // Log warning but keep the config path so FFmpeg tries it (and prints
    // localized error)
    std::cerr << "[UIManager] Warning: Demo Image not found: " << configPath
              << " (searched relative paths)" << std::endl;
    path = configPath;
  }

  // Direct software decode using FFmpeg to avoid HW acceleration issues
  bool imageLoaded = false;
  int w = 0;
  int h = 0;
  std::vector<uint8_t> rgbData;

  AVFormatContext *fmtCtx = nullptr;
  AVFrame *frame = nullptr;
  AVFrame *frameRGB = nullptr;
  AVPacket *packet = nullptr;
  AVCodecContext *codecCtx = nullptr;
  SwsContext *swsCtx = nullptr;
  uint8_t *buffer = nullptr;

  // Use a clean scope for FFmpeg loading
  if (avformat_open_input(&fmtCtx, path.c_str(), nullptr, nullptr) >= 0) {
    if (avformat_find_stream_info(fmtCtx, nullptr) >= 0) {
      int videoStreamIdx = -1;
      for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
          videoStreamIdx = i;
          break;
        }
      }

      if (videoStreamIdx != -1) {
        AVCodecParameters *codecParams =
            fmtCtx->streams[videoStreamIdx]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(codecParams->codec_id);
        if (codec) {
          codecCtx = avcodec_alloc_context3(codec);
          avcodec_parameters_to_context(codecCtx, codecParams);

          if (avcodec_open2(codecCtx, codec, nullptr) >= 0) {
            frame = av_frame_alloc();
            frameRGB = av_frame_alloc();
            packet = av_packet_alloc();

            while (av_read_frame(fmtCtx, packet) >= 0) {
              if (packet->stream_index == videoStreamIdx) {
                if (avcodec_send_packet(codecCtx, packet) == 0) {
                  if (avcodec_receive_frame(codecCtx, frame) == 0) {
                    w = frame->width;
                    h = frame->height;

                    int numBytes =
                        av_image_get_buffer_size(AV_PIX_FMT_RGB24, w, h, 1);
                    buffer = (uint8_t *)av_malloc(numBytes);

                    av_image_fill_arrays(frameRGB->data, frameRGB->linesize,
                                         buffer, AV_PIX_FMT_RGB24, w, h, 1);

                    swsCtx = sws_getContext(w, h, codecCtx->pix_fmt, w, h,
                                            AV_PIX_FMT_RGB24, SWS_BILINEAR,
                                            nullptr, nullptr, nullptr);

                    sws_scale(swsCtx, frame->data, frame->linesize, 0, h,
                              frameRGB->data, frameRGB->linesize);

                    rgbData.resize(w * h * 3);
                    av_image_copy_to_buffer(rgbData.data(), rgbData.size(),
                                            frameRGB->data, frameRGB->linesize,
                                            AV_PIX_FMT_RGB24, w, h, 1);

                    std::cout << "[UIManager] Loaded Demo Image (" << w << "x"
                              << h << ") from: " << path << std::endl;
                    imageLoaded = true;
                  }
                }
              }
              av_packet_unref(packet);
              if (imageLoaded)
                break;
            }
          }
        }
      }
    }
  } else {
    std::cerr << "[UIManager] Failed to open image: " << path << std::endl;
  }

  // Cleanup
  if (packet)
    av_packet_free(&packet);
  if (frame)
    av_frame_free(&frame);
  if (frameRGB)
    av_frame_free(&frameRGB);
  if (codecCtx)
    avcodec_free_context(&codecCtx);
  if (fmtCtx)
    avformat_close_input(&fmtCtx);
  if (swsCtx)
    sws_freeContext(swsCtx);
  if (buffer)
    av_free(buffer);

  if (imageLoaded && m_TextureRenderer) {
    glGenTextures(1, &m_DemoImageTexture);
    glBindTexture(GL_TEXTURE_2D, m_DemoImageTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 rgbData.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return;
  }

  // Fallback
  std::cerr << "[UIManager] Failed to load Demo Image: " << path
            << ". Creating fallback." << std::endl;

  w = 256;
  h = 256;
  std::vector<uint8_t> fallbackData(w * h * 3);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      bool white = ((x / 32) + (y / 32)) % 2 == 0;
      fallbackData[(y * w + x) * 3 + 0] = white ? 200 : 100;
      fallbackData[(y * w + x) * 3 + 1] = white ? 200 : 100;
      fallbackData[(y * w + x) * 3 + 2] = white ? 200 : 100;
    }
  }

  if (m_TextureRenderer) {
    glGenTextures(1, &m_DemoImageTexture);
    glBindTexture(GL_TEXTURE_2D, m_DemoImageTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 fallbackData.data());
    glBindTexture(GL_TEXTURE_2D, 0);
  }
}

void UIManager::GenerateFilterThumbnails() {
  if (!m_TextureRenderer || !m_DemoImageTexture)
    return;

  // We want square thumbnails for the UI (e.g. 200x200)
  int thumbW = 200;
  int thumbH = 200;

  // Generate 16 thumbnails (0..15)
  for (int i = 0; i < 16; i++) {
    GLuint tex = m_TextureRenderer->GenerateFilterThumbnail(m_DemoImageTexture,
                                                            i, thumbW, thumbH);
    if (tex) {
      m_FilterThumbnails.push_back(tex);
    } else {
      // Fallback (should not happen if GL works)
      m_FilterThumbnails.push_back(m_DemoImageTexture);
    }
  }
  std::cout << "[UIManager] Generated " << m_FilterThumbnails.size()
            << " filter thumbnails." << std::endl;
}