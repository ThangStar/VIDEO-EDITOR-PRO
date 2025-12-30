#pragma once

#include <vector>
#include <glad/glad.h>

class VideoPlayer;

struct Thumbnail {
    double timestamp;
    GLuint textureID;
    int width;
    int height;
};

class TimelineThumbnails {
public:
    TimelineThumbnails();
    ~TimelineThumbnails();

    // Generate thumbnails from video
    bool GenerateThumbnails(VideoPlayer* player, int thumbnailHeight, int maxThumbnails = 20);
    
    // Get thumbnail at or near timestamp
    const Thumbnail* GetThumbnailAt(double timestamp, double tolerance = 0.5);
    
    // Get all thumbnails
    const std::vector<Thumbnail>& GetAllThumbnails() const { return m_Thumbnails; }
    
    // Clear all thumbnails
    void Clear();
    
    // Get count
    int GetCount() const { return static_cast<int>(m_Thumbnails.size()); }

private:
    std::vector<Thumbnail> m_Thumbnails;
    
    // Helper to create OpenGL texture from RGB data
    GLuint CreateTextureFromRGB(const uint8_t* data, int width, int height);
};
