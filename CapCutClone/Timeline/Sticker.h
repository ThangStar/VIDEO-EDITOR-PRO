#pragma once
#include <string>

struct StickerVec2 {
    float x, y;
    StickerVec2(float _x = 0, float _y = 0) : x(_x), y(_y) {}
};

struct Sticker {
    int id;
    std::string filepath;
    unsigned int textureID;
    
    // Timeline properties
    double startTime;
    double duration;
    
    // Transform properties
    StickerVec2 position; // Normalized coordinates (0.0 - 1.0) relative to video frame
    float scale;
    float rotation; // Degrees
    float opacity;
    
    bool isSelected;

    Sticker() 
        : id(-1), textureID(0), startTime(0.0), duration(5.0), 
          position(0.5f, 0.5f), scale(1.0f), rotation(0.0f), opacity(1.0f), isSelected(false) {}
};
