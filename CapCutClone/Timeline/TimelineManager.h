#pragma once
#include "Track.h"
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
    double m_CurrentTime;
    VideoPlayer* m_VideoPlayer;
    int m_NextClipId;

    Clip* m_ActiveClip; // The clip currently supplying video to the player
    
    int GenerateClipId() { return m_NextClipId++; }
    
    // Internal helper to sync video player state with timeline
    void SyncVideoPlayer();
};
