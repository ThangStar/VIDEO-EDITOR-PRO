#pragma once
#include <string>

struct Clip {
    std::string filepath;
    double startTime;      // Position on the timeline (seconds)
    double duration;       // Total duration of the source file (seconds)
    double inPoint;        // Start point within the source file (seconds)
    double outPoint;       // End point within the source file (seconds)
    int trackIndex;        // Index of the track this clip belongs to (0-based)
    int id;                // Unique ID for selection/identification

    // Helper to get the actual duration of the clip on the timeline
    double GetDisplayDuration() const {
        return outPoint - inPoint;
    }
    
    // Helper to get the end time on the timeline
    double GetEndTime() const {
        return startTime + GetDisplayDuration();
    }

    // Check if a time on the timeline falls within this clip
    bool ContainsTime(double time) const {
        return time >= startTime && time < GetEndTime();
    }

    // Convert timeline time to local clip video time
    double ToLocalTime(double timelineTime) const {
        return inPoint + (timelineTime - startTime);
    }
};
