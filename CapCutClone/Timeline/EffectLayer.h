#pragma once

#include <string>
#include <map>

class EffectLayer {
public:
    enum EffectType {
        BLUR_GAUSSIAN = 0,
        BLUR_MOTION = 1,
        BLUR_RADIAL = 2,
        BLUR_ZOOM = 3,
        // Future effects
        GLITCH = 10,
        RIPPLE = 11,
        DISTORTION = 12,
        EDGE_GLOW = 20,
        LIGHT_LEAK = 21,
        FADE = 30,
        ZOOM_EFFECT = 31
    };

    int id;
    EffectType type;
    double startTime;    // In seconds
    double duration;     // In seconds
    
    // Generic parameters storage
    // e.g. params["intensity"] = 0.5f, params["blurType"] = 0.0f (for Gaussian)
    std::map<std::string, float> params;
    
    // Constructor
    EffectLayer(int effectId, EffectType effectType, double start, double dur);
    
    // Check if effect is active at given time
    bool IsActiveAtTime(double time) const;
    
    // Get end time
    double GetEndTime() const { return startTime + duration; }
    
    // Get effect name for UI display
    const char* GetEffectName() const;
    
    // Get default parameters for this effect type
    static std::map<std::string, float> GetDefaultParams(EffectType type);
};
