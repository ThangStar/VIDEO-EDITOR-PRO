#include "EffectLayer.h"

EffectLayer::EffectLayer(int effectId, EffectType effectType, double start, double dur)
    : id(effectId)
    , type(effectType)
    , startTime(start)
    , duration(dur)
{
    // Initialize with default parameters for this effect type
    params = GetDefaultParams(type);
}

bool EffectLayer::IsActiveAtTime(double time) const {
    return time >= startTime && time < (startTime + duration);
}

const char* EffectLayer::GetEffectName() const {
    switch (type) {
        case BLUR_GAUSSIAN: return "Gaussian Blur";
        case BLUR_MOTION: return "Motion Blur";
        case BLUR_RADIAL: return "Radial Blur";
        case BLUR_ZOOM: return "Zoom Blur";
        case GLITCH: return "Glitch";
        case RIPPLE: return "Ripple";
        case DISTORTION: return "Distortion";
        case EDGE_GLOW: return "Edge Glow";
        case LIGHT_LEAK: return "Light Leak";
        case FADE: return "Fade";
        case ZOOM_EFFECT: return "Zoom";
        default: return "Unknown Effect";
    }
}

std::map<std::string, float> EffectLayer::GetDefaultParams(EffectType type) {
    std::map<std::string, float> defaultParams;
    
    switch (type) {
        case BLUR_GAUSSIAN:
        case BLUR_MOTION:
        case BLUR_RADIAL:
        case BLUR_ZOOM:
            defaultParams["intensity"] = 0.5f;
            defaultParams["blurType"] = (float)(type - BLUR_GAUSSIAN); // 0-3
            break;
            
        case GLITCH:
            defaultParams["intensity"] = 0.3f;
            break;
            
        case RIPPLE:
            defaultParams["frequency"] = 10.0f;
            defaultParams["amplitude"] = 0.02f;
            break;
            
        case DISTORTION:
            defaultParams["amount"] = 0.2f;
            break;
            
        case EDGE_GLOW:
            defaultParams["intensity"] = 0.5f;
            defaultParams["colorR"] = 1.0f;
            defaultParams["colorG"] = 1.0f;
            defaultParams["colorB"] = 1.0f;
            break;
            
        case LIGHT_LEAK:
            defaultParams["intensity"] = 0.4f;
            break;
            
        case FADE:
            defaultParams["amount"] = 0.5f;
            break;
            
        case ZOOM_EFFECT:
            defaultParams["amount"] = 0.3f;
            break;
            
        default:
            defaultParams["intensity"] = 0.5f;
            break;
    }
    
    return defaultParams;
}
