#pragma once
#include "Clip.h"
#include <vector>
#include <algorithm>

class Track {
public:
    std::vector<Clip> clips;
    int trackIndex;

    Track(int index) : trackIndex(index) {}

    bool IsTimeOccupied(double startTime, double duration) const {
        double endTime = startTime + duration;
        for (const auto& clip : clips) {
            // Check for overlap
            if (startTime < clip.GetEndTime() && endTime > clip.startTime) {
                return true;
            }
        }
        return false;
    }

    void AddClip(const Clip& clip) {
        clips.push_back(clip);
        // Keep clips sorted by start time? Often useful but dragging might temporarily break it.
        // For now, let's sort purely for rendering efficiency.
        std::sort(clips.begin(), clips.end(), [](const Clip& a, const Clip& b) {
            return a.startTime < b.startTime;
        });
    }

    bool RemoveClip(int clipId) {
        auto it = std::remove_if(clips.begin(), clips.end(), [clipId](const Clip& c) {
            return c.id == clipId;
        });
        if (it != clips.end()) {
            clips.erase(it, clips.end());
            return true;
        }
        return false;
    }
    
    Clip* GetClipAtTime(double time) {
        for (auto& clip : clips) {
            if (clip.ContainsTime(time)) {
                return &clip;
            }
        }
        return nullptr;
    }
};
