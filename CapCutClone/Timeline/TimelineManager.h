#pragma once
#include "Track.h"
#include "EffectLayer.h"
#include <vector>
#include <string>

class VideoPlayer; // Forward declaration

class TimelineManager {
public:
    TimelineManager();
    ~TimelineManager();

    // Setup
    void SetVideoPlayer(VideoPlayer* videoPlayer);

    // Core Actions
    void AddTrack();
    void AddClipToTrack(const std::string& filepath, int trackIndex, double startTime);
    void RemoveClip(int trackIndex, int clipId);
    void SplitClip(int trackIndex, int clipId, double splitTime);
    void MoveClip(int trackIndex, int clipId, double newStartTime);
    
    // Effect Layer Management
    int AddEffectLayer(EffectLayer::EffectType type, double startTime, double duration);
    void RemoveEffectLayer(int effectId);
    void MoveEffectLayer(int effectId, double newStartTime);
    void ResizeEffectLayer(int effectId, double newDuration);
    void UpdateEffectParam(int effectId, const std::string& paramName, float value);
    
    // Get effects active at specific time
    std::vector<EffectLayer*> GetActiveEffects(double time);
    
    // Get all effect layers for UI rendering
    std::vector<EffectLayer>& GetEffectLayers() { return m_EffectLayers; }
    
    // Playback integration
    void Update(float deltaTime); // Called every frame
    void SetCurrentTime(double time);
    double GetCurrentTime() const { return m_CurrentTime; }
    double GetTotalDuration() const;

    // Data Access for UI
    std::vector<Track>& GetTracks() { return m_Tracks; }
    
    // Constants
    static const int MAX_TRACKS = 10;

private:
    std::vector<Track> m_Tracks;
    std::vector<EffectLayer> m_EffectLayers; // NEW: Effect layers
    
    double m_CurrentTime;
    VideoPlayer* m_VideoPlayer;
    int m_NextClipId;
    int m_NextEffectId; // NEW: For generating unique effect IDs

    Clip* m_ActiveClip; // The clip currently supplying video to the player
    
    int GenerateClipId() { return m_NextClipId++; }
    int GenerateEffectId() { return m_NextEffectId++; } // NEW
    
    // Internal helper to sync video player state with timeline
    void SyncVideoPlayer();
};
