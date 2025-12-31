#include "TimelineManager.h"
#include "../Video/VideoPlayer.h"
#include <iostream>

TimelineManager::TimelineManager() 
    : m_CurrentTime(0.0)
    , m_VideoPlayer(nullptr)
    , m_NextClipId(1)
    , m_NextEffectId(1)
    , m_ActiveClip(nullptr)
{
    // Start with one empty track
    AddTrack();
}

TimelineManager::~TimelineManager() {
}

void TimelineManager::SetVideoPlayer(VideoPlayer* videoPlayer) {
    m_VideoPlayer = videoPlayer;
}

void TimelineManager::AddTrack() {
    if (m_Tracks.size() < MAX_TRACKS) {
        m_Tracks.emplace_back(static_cast<int>(m_Tracks.size()));
    }
}

void TimelineManager::AddClipToTrack(const std::string& filepath, int trackIndex, double startTime) {
    if (trackIndex < 0 || trackIndex >= m_Tracks.size()) return;

    // To get the duration, we might need to rely on the VideoPlayer or a separate metadata reader.
    // For now, let's assume we can ask VideoPlayer to probe it, or we already know it.
    // Since we don't have a separate metadata reader, we might have to briefly load it in VideoPlayer
    // or assume it's passed in.
    // IMPROVEMENT: Add a separate helper to just read file duration without full load.
    // For this prototype, if VideoPlayer is available, we'll try to use it, OR just default to a placeholder if not loaded.
    
    double duration = 10.0; // Default fallback
    if (m_VideoPlayer) {
        // This is a bit heavy-handed for just checking duration, but works for Phase 3 prototype
        bool wasLoaded = m_VideoPlayer->LoadVideo(filepath);
        if (wasLoaded) {
            duration = m_VideoPlayer->GetDuration();
            // We shouldn't leave the player loaded with this new file if we were playing something else,
            // but for "AddClip", usually the user intends to import.
            // For now, let's just keep the value and let the Update loop handle actual playback loading.
        }
    }

    Clip newClip;
    newClip.id = GenerateClipId();
    newClip.filepath = filepath;
    newClip.startTime = startTime;
    newClip.duration = duration;
    newClip.inPoint = 0.0;
    newClip.outPoint = duration;
    newClip.trackIndex = trackIndex;

    m_Tracks[trackIndex].AddClip(newClip);
}

void TimelineManager::RemoveClip(int trackIndex, int clipId) {
    if (trackIndex >= 0 && trackIndex < m_Tracks.size()) {
        m_Tracks[trackIndex].RemoveClip(clipId);
    }
}

void TimelineManager::SplitClip(int trackIndex, int clipId, double splitTime) {
    // 1. Find the clip
    if (trackIndex < 0 || trackIndex >= m_Tracks.size()) return;
    
    Track& track = m_Tracks[trackIndex];
    auto it = std::find_if(track.clips.begin(), track.clips.end(), [clipId](const Clip& c) {
        return c.id == clipId;
    });

    if (it == track.clips.end()) return;
    
    Clip originalClip = *it;
    
    // Check if split time is valid within the clip's timeline specific duration
    if (!originalClip.ContainsTime(splitTime)) {
         return;
    }

    // Calculate the localized split point (time within the source video)
    double timeInVideo = originalClip.ToLocalTime(splitTime);
    
    // 2. Modify original clip (Part 1)
    // New outPoint is the split point
    // Note: We need to modify the actual clip in the vector
    // Using simple erase/insert approach to avoid iterator invalidation issues is safest
    
    // Define Part 1
    Clip part1 = originalClip;
    part1.outPoint = timeInVideo; // New end point internal to video
    
    // Define Part 2
    Clip part2 = originalClip;
    part2.id = GenerateClipId();
    part2.startTime = splitTime; // Starts on timeline where split happened
    part2.inPoint = timeInVideo; // Starts internal to video where split happened
    // outPoint remains the same (original end)
    
    // 3. Replace in track
    track.RemoveClip(clipId);
    track.AddClip(part1);
    track.AddClip(part2);
}

void TimelineManager::MoveClip(int trackIndex, int clipId, double newStartTime) {
    if (trackIndex < 0 || trackIndex >= m_Tracks.size()) return;
    Track& track = m_Tracks[trackIndex];
    
    auto it = std::find_if(track.clips.begin(), track.clips.end(), [clipId](const Clip& c) {
        return c.id == clipId;
    });
    
    if (it != track.clips.end()) {
        it->startTime = newStartTime;
        if (it->startTime < 0) it->startTime = 0;
        
        // Re-sort to maintain order
        std::sort(track.clips.begin(), track.clips.end(), [](const Clip& a, const Clip& b) {
            return a.startTime < b.startTime;
        });
    }
}

void TimelineManager::Update(float deltaTime) {
    // Only advance time if playing? 
    // Actually UIManager controls "IsPlaying" application-wide generally,
    // but TimelineManager should be the source of truth for "Current Timeline Position".
    // For now, we assume SetCurrentTime is called by UIManager or the main loop logic.
    
    SyncVideoPlayer();
}

void TimelineManager::SetCurrentTime(double time) {
    m_CurrentTime = time;
    if (m_CurrentTime < 0) m_CurrentTime = 0;
    
    // Clamp to total duration? Optional.
    // double total = GetTotalDuration();
    // if (m_CurrentTime > total) m_CurrentTime = total;

    SyncVideoPlayer();
}

double TimelineManager::GetTotalDuration() const {
    double maxTime = 0.0;
    for (const auto& track : m_Tracks) {
        for (const auto& clip : track.clips) {
            double end = clip.GetEndTime();
            if (end > maxTime) maxTime = end;
        }
    }
    return maxTime;
}

void TimelineManager::SyncVideoPlayer() {
    if (!m_VideoPlayer) return;

    // Simple strategy: Check Track 0 for now (Video Track)
    // If we have multi-track, we need a compositor. For Phase 3, we might strictly allow
    // overlap but only render the top-most or first-found video.
    // Let's iterate tracks from top to bottom (or 0..N).
    
    Clip* foundClip = nullptr;
    
    for (auto& track : m_Tracks) {
        Clip* c = track.GetClipAtTime(m_CurrentTime);
        if (c) {
            foundClip = c;
            break; // Found top-most clip at this time
        }
    }

    if (foundClip) {
        // If we switched clips, load the new one
        bool needLoad = false;
        if (m_ActiveClip == nullptr || m_ActiveClip->id != foundClip->id || m_ActiveClip->filepath != foundClip->filepath) {
            needLoad = true;
        }

        if (needLoad) {
             m_VideoPlayer->LoadVideo(foundClip->filepath);
             m_ActiveClip = foundClip;
        }

        // Seek to correct time
        double localTime = foundClip->ToLocalTime(m_CurrentTime);
        
        // Optimisation: Only seek if diff is significant to avoid stutter? 
        // VideoPlayer::Seek usually handles this, but let's be explicit.
        // Also handling 'isPlaying' sync is tricky here. 
        // We usually just set the time. usage: player.Seek(localTime)
        
        // We need to know if the player internal time is drifting.
        double playerCur = m_VideoPlayer->GetCurrentTime();
        if (std::abs(playerCur - localTime) > 0.1) {
            m_VideoPlayer->Seek(localTime, true); // Fast seek for scrubbing
        }
        
    } else {
        // No clip at this time
        // Maybe clear the video player or show black?
        m_ActiveClip = nullptr;
        // In the future: m_VideoPlayer->Clear();
    }
}

// ============= EFFECT LAYER MANAGEMENT =============

int TimelineManager::AddEffectLayer(EffectLayer::EffectType type, double startTime, double duration) {
    int newId = GenerateEffectId();
    EffectLayer newEffect(newId, type, startTime, duration);
    m_EffectLayers.push_back(newEffect);
    
    std::cout << "[TimelineManager] Added effect layer: " << newEffect.GetEffectName() 
              << " (ID: " << newId << ", " << startTime << "s - " << (startTime + duration) << "s)" << std::endl;
    
    return newId;
}

void TimelineManager::RemoveEffectLayer(int effectId) {
    auto it = std::find_if(m_EffectLayers.begin(), m_EffectLayers.end(), 
        [effectId](const EffectLayer& e) { return e.id == effectId; });
    
    if (it != m_EffectLayers.end()) {
        std::cout << "[TimelineManager] Removed effect layer: " << it->GetEffectName() 
                  << " (ID: " << effectId << ")" << std::endl;
        m_EffectLayers.erase(it);
    }
}

void TimelineManager::MoveEffectLayer(int effectId, double newStartTime) {
    auto it = std::find_if(m_EffectLayers.begin(), m_EffectLayers.end(), 
        [effectId](const EffectLayer& e) { return e.id == effectId; });
    
    if (it != m_EffectLayers.end()) {
        it->startTime = newStartTime;
        if (it->startTime < 0) it->startTime = 0;
        
        std::cout << "[TimelineManager] Moved effect " << it->GetEffectName() 
                  << " to " << newStartTime << "s" << std::endl;
    }
}

void TimelineManager::ResizeEffectLayer(int effectId, double newDuration) {
    auto it = std::find_if(m_EffectLayers.begin(), m_EffectLayers.end(), 
        [effectId](const EffectLayer& e) { return e.id == effectId; });
    
    if (it != m_EffectLayers.end()) {
        it->duration = newDuration;
        if (it->duration < 0.1) it->duration = 0.1; // Minimum 0.1s
        
        std::cout << "[TimelineManager] Resized effect " << it->GetEffectName() 
                  << " to " << newDuration << "s" << std::endl;
    }
}

void TimelineManager::UpdateEffectParam(int effectId, const std::string& paramName, float value) {
    auto it = std::find_if(m_EffectLayers.begin(), m_EffectLayers.end(), 
        [effectId](const EffectLayer& e) { return e.id == effectId; });
    
    if (it != m_EffectLayers.end()) {
        it->params[paramName] = value;
    }
}

std::vector<EffectLayer*> TimelineManager::GetActiveEffects(double time) {
    std::vector<EffectLayer*> activeEffects;
    
    for (auto& effect : m_EffectLayers) {
        if (effect.IsActiveAtTime(time)) {
            activeEffects.push_back(&effect);
        }
    }
    
    return activeEffects;
}
