#define NOMINMAX
#include "UIManager.h"
#include "../Video/VideoPlayer.h"
#include "../Rendering/TextureRenderer.h"
#include "../Application.h"
#include "TimelineThumbnails.h"
#include "../Timeline/TimelineManager.h"
#include "../Timeline/Track.h"
#include "../Timeline/Clip.h"
#include "../Encoder/ExportManager.h"
#include <imgui.h>
#include <algorithm> // For std::max
#include <stdio.h>
#include <cmath>
#include <GLFW/glfw3.h>
#include <string>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

extern class Application* g_Application;

UIManager::UIManager()
    : m_VideoPlayer(nullptr)
    , m_TextureRenderer(nullptr)
    , m_TimelineThumbnails(nullptr)
    , m_TimelineManager(nullptr)
    , m_ExportManager(nullptr)
    , m_IsPlaying(false)
    , m_CurrentTime(0.0f)
    , m_TotalDuration(330.0f)
    , m_TimelineZoom(1.0f)
    , m_SeekPosition(0.0f)
    , m_LastFrameTime(0.0f)
    , m_PlaybackStartTime(0.0)
    , m_AspectRatioMode(0)
    , m_SelectedClipId(-1)
    , m_SelectedTrackIndex(-1)
    , m_SelectedStickerId(-1)
    , m_DefaultStickerTexture(0)
{
    m_TimelineThumbnails = new TimelineThumbnails();
    m_TimelineManager = new TimelineManager();
}

UIManager::~UIManager() {
    if (m_TimelineThumbnails) delete m_TimelineThumbnails;
    if (m_TimelineManager) delete m_TimelineManager;
    if (m_ExportManager) delete m_ExportManager;
    if (m_DefaultStickerTexture) glDeleteTextures(1, &m_DefaultStickerTexture);
}

void UIManager::SetVideoPlayer(VideoPlayer* player) {
    m_VideoPlayer = player;
    if (m_TimelineManager) m_TimelineManager->SetVideoPlayer(player);
    if (!m_ExportManager && m_TimelineManager) {
        m_ExportManager = new ExportManager(m_TimelineManager, m_VideoPlayer);
    }
}

void UIManager::Update(float deltaTime) {
    if (m_TimelineManager) {
        m_TimelineManager->SetCurrentTime(m_CurrentTime);
        m_TimelineManager->Update(deltaTime);
        m_TotalDuration = std::max(10.0, m_TimelineManager->GetTotalDuration() + 5.0);
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
                    m_TextureRenderer->UpdateTexture(
                        m_VideoPlayer->GetFrameData(),
                        m_VideoPlayer->GetWidth(),
                        m_VideoPlayer->GetHeight()
                    );
                }
                m_CurrentTime = m_VideoPlayer->GetCurrentTime();
                m_SeekPosition = m_CurrentTime / m_TotalDuration;
            } else {
                 if (m_CurrentTime >= m_TotalDuration) m_IsPlaying = false;
            }
        } else {
            m_CurrentTime = playbackTime;
            m_SeekPosition = m_CurrentTime / m_TotalDuration;
        }
    } else if (m_IsPlaying) {
        m_CurrentTime += deltaTime;
        if (m_CurrentTime >= m_TotalDuration) {
            m_CurrentTime = m_TotalDuration;
            m_IsPlaying = false;
        }
        m_SeekPosition = m_CurrentTime / m_TotalDuration;
    }
}

void UIManager::Render() {
    RenderMenuBar();
    RenderExportDialog();
    RenderExportProgress();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 workPos = viewport->WorkPos;
    ImVec2 workSize = viewport->WorkSize;

    float bottomHeight = 350.0f;
    float topHeight = workSize.y - bottomHeight;
    float leftWidth = 350.0f;
    float rightWidth = 320.0f;
    float centerWidth = workSize.x - leftWidth - rightWidth;

    RenderMediaPanel(workPos.x, workPos.y, leftWidth, topHeight);
    RenderPreviewPanel(workPos.x + leftWidth, workPos.y, centerWidth, topHeight);
    RenderPropertiesPanel(workPos.x + leftWidth + centerWidth, workPos.y, rightWidth, topHeight);
    RenderTimelinePanel(workPos.x, workPos.y + topHeight, workSize.x, bottomHeight);
}

void UIManager::RenderMediaPanel(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    
    ImGui::Begin("Media Pool", nullptr, 
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    
    if (ImGui::BeginTabBar("MediaTabs")) {
        if (ImGui::BeginTabItem("Media")) {
             // Styled Primary Button for "Import"
             ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.78f, 0.84f, 1.00f)); // Cyan
             ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.85f, 0.90f, 1.00f));
             ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.00f, 0.65f, 0.70f, 1.00f));
             ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // Black text
             
             ImVec2 btnSize = ImVec2(ImGui::GetContentRegionAvail().x, 32);
             if (ImGui::Button("Import Media +", btnSize)) {
                 // Trigger file dialog
                 if(g_Application) g_Application->OpenVideoFile();
             }
             
             ImGui::PopStyleColor(4);
             
             ImGui::Separator();
             ImGui::Dummy(ImVec2(0, 5));
             ImGui::TextDisabled("Local Media");
             if (ImGui::Selectable("video_sample_01.mp4", false)) {}
             if (ImGui::Selectable("intro.mov", false)) {}
             ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Stickers")) {
            ImGui::Text("Click to add:");
            
            // Generate texture if not ready
            if (m_DefaultStickerTexture == 0) CreateDefaultStickerTexture();
            
            // Fix ImageButton arguments (ImTextureID, Size) - some older ImGui versions require more args
            if (ImGui::ImageButton("##stickerBtn", (ImTextureID)(intptr_t)m_DefaultStickerTexture, ImVec2(60, 60))) {
                AddSticker();
            }
            ImGui::Text("Smiley");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Effects")) {
             ImGui::Text("Cinematic Effects");
             static bool effectVignette = false;
             static bool effectGrain = false;
             static bool effectAberration = false;
             static bool effectSepia = false;
             
             if (ImGui::Checkbox("Vignette (Strong)", &effectVignette)) { 
                 if(effectVignette) m_TextureRenderer->SetEffectParams(0.5f, 0.0f, 0.0f, false);
             }
             if (ImGui::Checkbox("Film Grain (Strong)", &effectGrain)) {
                 if(effectGrain) m_TextureRenderer->SetEffectParams(0.0f, 0.5f, 0.0f, false);
             }
             if (ImGui::Checkbox("Glitch (Aberration)", &effectAberration)) {
                 if(effectAberration) m_TextureRenderer->SetEffectParams(0.0f, 0.0f, 0.015f, false);
             }
             if (ImGui::Checkbox("Sepia Tone", &effectSepia)) {
                 if(effectSepia) m_TextureRenderer->SetEffectParams(0.0f, 0.0f, 0.0f, true);
             }
             
             ImGui::TextDisabled("(Use 'Adjustments' for fine tuning)");
             ImGui::EndTabItem(); 
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void UIManager::RenderPreviewPanel(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    
    ImGui::Begin("Player", nullptr, 
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    
    ImGui::SetCursorPos(ImVec2(10, 10));
    ImGui::BeginGroup();
        ImVec4 normalColor = ImVec4(0.25f, 0.25f, 0.25f, 0.8f);
        ImVec4 selectedColor = ImVec4(0.00f, 0.82f, 0.85f, 0.9f);
        
        ImGui::PushStyleColor(ImGuiCol_Button, (m_AspectRatioMode == 0) ? selectedColor : normalColor);
        if (ImGui::Button("Original")) m_AspectRatioMode = 0;
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, (m_AspectRatioMode == 1) ? selectedColor : normalColor);
        if (ImGui::Button("9:16")) m_AspectRatioMode = 1;
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, (m_AspectRatioMode == 2) ? selectedColor : normalColor);
        if (ImGui::Button("16:9")) m_AspectRatioMode = 2;
        ImGui::PopStyleColor();
    ImGui::EndGroup();

    ImVec2 winSize = ImGui::GetWindowSize();
    float targetAspectRatio = 16.0f / 9.0f;
    if (m_AspectRatioMode == 1) targetAspectRatio = 9.0f / 16.0f;
    else if (m_VideoPlayer && m_VideoPlayer->IsLoaded()) {
        targetAspectRatio = (float)m_VideoPlayer->GetWidth() / (float)m_VideoPlayer->GetHeight();
    }

    float availableW = winSize.x;
    float availableH = winSize.y;
    float previewW, previewH;
    
    if (availableW / availableH > targetAspectRatio) {
        previewH = availableH * 0.8f;
        previewW = previewH * targetAspectRatio;
    } else {
        previewW = availableW * 0.8f;
        previewH = previewW / targetAspectRatio;
    }

    float offsetX = (availableW - previewW) * 0.5f;
    float offsetY = (availableH - previewH) * 0.5f;
    
    ImVec2 previewStartScreenPos = ImVec2(
        ImGui::GetWindowPos().x + offsetX,
        ImGui::GetWindowPos().y + offsetY
    );

    // Draw video background
    if (m_VideoPlayer && m_VideoPlayer->IsLoaded() && m_TextureRenderer && m_TextureRenderer->GetTextureID()) {
        ImGui::SetCursorPos(ImVec2(offsetX, offsetY));
        ImGui::Image((ImTextureID)(intptr_t)m_TextureRenderer->GetTextureID(), ImVec2(previewW, previewH));
    } else {
        ImGui::SetCursorPos(ImVec2(offsetX, offsetY));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,1));
        ImGui::Button("##Placeholder", ImVec2(previewW, previewH));
        ImGui::PopStyleColor();
        const char* text = "No Video Loaded";
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImGui::SetCursorPos(ImVec2((availableW - textSize.x)*0.5f, (availableH - textSize.y)*0.5f));
        ImGui::Text("%s", text);
    }
    
    // RENDER STICKERS
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (m_DefaultStickerTexture == 0) CreateDefaultStickerTexture();

    for (auto& sticker : m_Stickers) {
        if (m_CurrentTime >= sticker.startTime && m_CurrentTime <= sticker.startTime + sticker.duration) {
             float sW = 100.0f * sticker.scale;
             float sH = 100.0f * sticker.scale;
             float centerX = previewStartScreenPos.x + sticker.position.x * previewW;
             float centerY = previewStartScreenPos.y + sticker.position.y * previewH;
             
             ImVec2 p1, p2, p3, p4;
             float rad = sticker.rotation * 3.14159f / 180.0f;
             float c = cos(rad), s = sin(rad);
             float hw = sW * 0.5f, hh = sH * 0.5f;
             
             auto rot = [&](float x, float y) {
                 return ImVec2(centerX + x*c - y*s, centerY + x*s + y*c);
             };
             
             p1 = rot(-hw, -hh);
             p2 = rot(hw, -hh);
             p3 = rot(hw, hh);
             p4 = rot(-hw, hh);
             
             unsigned int texID = sticker.textureID ? sticker.textureID : m_DefaultStickerTexture;
             
             drawList->AddImageQuad(
                (ImTextureID)(intptr_t)texID,
                p1, p2, p3, p4,
                ImVec2(0,0), ImVec2(1,0), ImVec2(1,1), ImVec2(0,1),
                IM_COL32(255, 255, 255, (int)(sticker.opacity * 255))
             );
             
             if (sticker.isSelected) {
                 drawList->AddQuad(p1, p2, p3, p4, IM_COL32(0, 200, 255, 255), 2.0f);
             }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

void UIManager::RenderPropertiesPanel(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Properties", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    
    if (ImGui::BeginTabBar("PropTabs")) {
        if (ImGui::BeginTabItem("Video")) {
             ImGui::Text("Transforms");
             // Add video transforms here later
             ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sticker")) {
            if (m_SelectedStickerId != -1) {
                Sticker* selSticker = nullptr;
                for (auto& s : m_Stickers) { if (s.id == m_SelectedStickerId) { selSticker = &s; break; } }
                
                if (selSticker) {
                    ImGui::Text("Sticker ID: %d", selSticker->id);
                    ImGui::Separator();
                    ImGui::DragFloat2("Position", &selSticker->position.x, 0.01f);
                    ImGui::SliderFloat("Scale", &selSticker->scale, 0.1f, 5.0f);
                    ImGui::SliderFloat("Rotation", &selSticker->rotation, -180.0f, 180.0f);
                    ImGui::SliderFloat("Opacity", &selSticker->opacity, 0.0f, 1.0f);
                } else {
                    ImGui::Text("No sticker found (?)");
                }
            } else {
                ImGui::Text("Select a sticker to edit properties.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Adjustments")) {
             ImGui::Text("Basic Correction");
             static float brightness = 0.0f;
             ImGui::SliderFloat("Brightness", &brightness, -1.0f, 1.0f);
             static float contrast = 1.0f;
             ImGui::SliderFloat("Contrast", &contrast, 0.0f, 2.0f);
             static float saturation = 1.0f;
             ImGui::SliderFloat("Saturation", &saturation, 0.0f, 2.0f);
             
             ImGui::Separator();
             ImGui::Text("Advanced Effects");
             static float vignette = 0.0f;
             ImGui::SliderFloat("Vignette", &vignette, 0.0f, 1.0f);
             static float grain = 0.0f;
             ImGui::SliderFloat("Grain", &grain, 0.0f, 1.0f);
             static float aberration = 0.0f;
             ImGui::SliderFloat("Aberration", &aberration, 0.0f, 0.05f);
             static bool sepia = false;
             ImGui::Checkbox("Sepia", &sepia);
             
             if (m_TextureRenderer) {
                 m_TextureRenderer->SetFilterParams(brightness, contrast, saturation);
                 m_TextureRenderer->SetEffectParams(vignette, grain, aberration, sepia);
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
    ImGui::Begin("Timeline", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    
    ImGui::BeginChild("Toolbar", ImVec2(0, 40));
    ImGui::SameLine();
    
    // Split Button
    if (ImGui::Button("Split")) {
        if (m_TimelineManager && m_SelectedClipId != -1 && m_SelectedTrackIndex != -1) {
             m_TimelineManager->SplitClip(m_SelectedTrackIndex, m_SelectedClipId, m_CurrentTime);
             m_SelectedClipId = -1;
             m_SelectedTrackIndex = -1;
        }
    }
    
    ImGui::SameLine();
    
    // Delete Button
    if (ImGui::Button("Delete")) {
        if (m_TimelineManager && m_SelectedClipId != -1 && m_SelectedTrackIndex != -1) {
            m_TimelineManager->RemoveClip(m_SelectedTrackIndex, m_SelectedClipId);
            m_SelectedClipId = -1;
            m_SelectedTrackIndex = -1;
        }
    }
    
    ImGui::SameLine();
    ImGui::Button("|", ImVec2(1, 20));
    ImGui::SameLine();
    ImGui::SliderFloat("Zoom", &m_TimelineZoom, 0.1f, 5.0f, "");
    ImGui::EndChild();
    
    ImGui::Separator();
    RenderTimelineTracks();
    ImGui::End();
    ImGui::PopStyleVar();
}

void UIManager::RenderTimelineTracks() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
    ImGui::Begin("##Timeline", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
    if (ImGui::Button("|<<")) { m_CurrentTime = 0; }
    ImGui::SameLine();
    if (ImGui::Button(m_IsPlaying ? "||" : ">")) { OnSpacePressed(); }
    ImGui::SameLine();
    ImGui::Text("%s", FormatTime(m_CurrentTime));
    ImGui::PopStyleVar();
    
    ImGui::Separator();
    
    ImGui::BeginChild("TracksRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    
    float pixelsPerSecond = 20.0f * m_TimelineZoom;
    float rulerHeight = 25.0f;
    float totalWidth = std::max(avail.x, m_TotalDuration * pixelsPerSecond);
    
    // Rule
    drawList->AddRectFilled(cursor, ImVec2(cursor.x + totalWidth, cursor.y + rulerHeight), IM_COL32(30,30,30,255));
    for (float t=0; t<=m_TotalDuration; t+=1.0f) {
        float px = cursor.x + t * pixelsPerSecond;
        if (px > cursor.x + totalWidth) break;
        float h = ((int)t % 5 == 0) ? 15.0f : 8.0f;
        drawList->AddLine(ImVec2(px, cursor.y + rulerHeight - h), ImVec2(px, cursor.y + rulerHeight), IM_COL32(150,150,150,255));
    }
    
    ImGui::SetCursorScreenPos(cursor);
    ImGui::InvisibleButton("##Ruler", ImVec2(totalWidth, rulerHeight));
    if (ImGui::IsItemActive()) {
        float mx = ImGui::GetMousePos().x;
        m_CurrentTime = (mx - cursor.x) / pixelsPerSecond;
        if (m_CurrentTime < 0) m_CurrentTime = 0;
        if (m_VideoPlayer) m_VideoPlayer->Seek(m_CurrentTime, true);
    }

    // Tracks Render
    float y = cursor.y + rulerHeight + 10;
    
    // Delayed move operation
    int moveTrackIdx = -1;
    int moveClipId = -1;
    double moveNewTime = 0.0;
    
    // 1. Stickers Track (Fake Track 99?)
    drawList->AddText(ImVec2(cursor.x, y), IM_COL32(200,200,200,255), "Stickers");
    y += 20;
    float trackH = 40;
    drawList->AddRectFilled(ImVec2(cursor.x, y), ImVec2(cursor.x + totalWidth, y + trackH), IM_COL32(40,40,40,255));
    
    for (auto& s : m_Stickers) {
        float sx = cursor.x + s.startTime * pixelsPerSecond;
        float sw = s.duration * pixelsPerSecond;
        ImVec2 pMin(sx, y + 2);
        ImVec2 pMax(sx + sw, y + trackH - 2);
        
        bool selected = (s.id == m_SelectedStickerId);
        drawList->AddRectFilled(pMin, pMax, selected ? IM_COL32(100,200,100,255) : IM_COL32(80,180,80,255), 4.0f);
        
        // Interaction
        ImGui::SetCursorScreenPos(pMin);
        std::string sid = "##Sticker" + std::to_string(s.id);
        ImGui::InvisibleButton(sid.c_str(), ImVec2(sw, trackH-4));
        if (ImGui::IsItemActive()) {
            if (ImGui::IsMouseClicked(0)) {
                m_SelectedStickerId = s.id;
                m_SelectedClipId = -1;
            }
             if (ImGui::IsMouseDragging(0)) {
                 float deltaX = ImGui::GetMouseDragDelta(0).x;
                 if (std::abs(deltaX) > 0.0f) {
                     s.startTime += deltaX / pixelsPerSecond;
                     if (s.startTime < 0) s.startTime = 0;
                     ImGui::ResetMouseDragDelta(0);
                 }
             }
        }
    }
    y += trackH + 10;

    // 2. Video Tracks
    if (m_TimelineManager) {
        auto& tracks = m_TimelineManager->GetTracks();
        int trackIdx = 0;
        for (auto& track : tracks) {
             // Draw Clips
             for (auto& clip : track.clips) {
                 float cx = cursor.x + clip.startTime * pixelsPerSecond;
                 float cw = clip.GetDisplayDuration() * pixelsPerSecond;
                 
                 ImU32 clipColor = (clip.id == m_SelectedClipId) ? IM_COL32(100, 180, 255, 255) : IM_COL32(60, 60, 65, 255);
                 ImU32 clipBorder = (clip.id == m_SelectedClipId) ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 80, 80, 255);
                 
                 // Rounded Clips
                 drawList->AddRectFilled(ImVec2(cx, y), ImVec2(cx+cw, y+trackH), clipColor, 6.0f);
                 drawList->AddRect(ImVec2(cx, y), ImVec2(cx+cw, y+trackH), clipBorder, 6.0f, 0, 1.5f);
                 
                 // Clip Text
                 ImGui::PushClipRect(ImVec2(cx, y), ImVec2(cx+cw, y+trackH), true);
                 drawList->AddText(ImVec2(cx+8, y+10), IM_COL32(220,220,220,255), clip.filepath.c_str());
                 ImGui::PopClipRect();
                 ImGui::SetCursorScreenPos(ImVec2(cx, y));
                 std::string cid = "##Clip" + std::to_string(clip.id);
                 ImGui::InvisibleButton(cid.c_str(), ImVec2(cw, trackH));
                 if (ImGui::IsItemActive()) {
                     if (ImGui::IsMouseClicked(0)) {
                         m_SelectedClipId = clip.id;
                         m_SelectedTrackIndex = trackIdx; // TRACK INDEX!
                         m_SelectedStickerId = -1;
                     }
                     
                     if (ImGui::IsMouseDragging(0)) {
                         float deltaX = ImGui::GetMouseDragDelta(0).x;
                         if (std::abs(deltaX) > 0.0f) {
                             moveTrackIdx = trackIdx;
                             moveClipId = clip.id;
                             moveNewTime = clip.startTime + (deltaX / pixelsPerSecond);
                             ImGui::ResetMouseDragDelta(0);
                         }
                     }
                 }
             }
             y += trackH + 10;
             trackIdx++;
        }
    }
    
    // Apply Delayed Move
    if (moveClipId != -1 && m_TimelineManager) {
        m_TimelineManager->MoveClip(moveTrackIdx, moveClipId, moveNewTime);
    }
    
    // Playhead (White line with triangle top)
    float phX = cursor.x + m_CurrentTime * pixelsPerSecond;
    float phYTop = cursor.y;
    float phYBot = y;
    
    // Line
    drawList->AddLine(ImVec2(phX, phYTop), ImVec2(phX, phYBot), IM_COL32(255,255,255,255), 1.5f);
    
    // Triangle Handle
    float triSize = 6.0f;
    drawList->AddTriangleFilled(
        ImVec2(phX - triSize, phYTop), 
        ImVec2(phX + triSize, phYTop), 
        ImVec2(phX, phYTop + triSize), 
        IM_COL32(255,255,255,255)
    );

    ImGui::EndChild();
    ImGui::End();
}

void UIManager::CreateDefaultStickerTexture() {
    if (m_DefaultStickerTexture) return;
    const int w = 64;
    const int h = 64;
    unsigned char data[w * h * 4];
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = (y * w + x) * 4;
            bool white = ((x / 8) + (y / 8)) % 2 == 0;
            data[i] = white ? 255 : 240;
            data[i+1] = white ? 255 : 50; 
            data[i+2] = white ? 255 : 50;
            data[i+3] = 255;
        }
    }
    glGenTextures(1, &m_DefaultStickerTexture);
    glBindTexture(GL_TEXTURE_2D, m_DefaultStickerTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
}

void UIManager::AddSticker() {
    Sticker s;
    s.id = m_Stickers.size() + 1000;
    // Normalized position
    s.position.x = 0.5f;
    s.position.y = 0.5f;
    s.scale = 1.0f;
    s.rotation = 0.0f;
    s.startTime = m_CurrentTime;
    s.duration = 5.0f;
    s.opacity = 1.0f;
    s.textureID = m_DefaultStickerTexture;
    
    m_Stickers.push_back(s);
    m_SelectedStickerId = s.id;
    m_SelectedClipId = -1;
}

const char* UIManager::FormatTime(float seconds) {
    static char buffer[32];
    int hours = static_cast<int>(seconds) / 3600;
    int minutes = (static_cast<int>(seconds) % 3600) / 60;
    int secs = static_cast<int>(seconds) % 60;
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, secs);
    return buffer;
}
void UIManager::OnSpacePressed() { m_IsPlaying = !m_IsPlaying; if(m_IsPlaying && m_VideoPlayer) m_PlaybackStartTime = glfwGetTime() - m_CurrentTime; }
void UIManager::OnVideoLoaded(const std::string& filepath) {
    if(m_TimelineManager) {
        m_TimelineManager->AddClipToTrack(filepath, 0, 0.0);
        m_TotalDuration = std::max(10.0, m_TimelineManager->GetTotalDuration() + 20.0f);
        m_CurrentTime = 0.0f;
    }
}
void UIManager::RenderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
         if (ImGui::BeginMenu("File")) {
             if (ImGui::MenuItem("Open Video...")) { if(g_Application) g_Application->OpenVideoFile(); }
             if (ImGui::MenuItem("Export Control...")) { m_ShowExportDialog = true; }
             ImGui::EndMenu();
         }
         ImGui::EndMainMenuBar();
    }
}
void UIManager::RenderExportDialog() { if(m_ShowExportDialog) { ImGui::OpenPopup("Export"); if(ImGui::BeginPopupModal("Export")) { ImGui::Button("Close"); ImGui::EndPopup(); } } } 
void UIManager::RenderExportProgress() { } 
void UIManager::OnOpenVideoClicked() {}