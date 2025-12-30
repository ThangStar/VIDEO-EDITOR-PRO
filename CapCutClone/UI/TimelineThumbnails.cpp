#include "TimelineThumbnails.h"
#include "../Video/VideoPlayer.h"
#include <iostream>
#include <cmath>

TimelineThumbnails::TimelineThumbnails() {
}

TimelineThumbnails::~TimelineThumbnails() {
    Clear();
}

bool TimelineThumbnails::GenerateThumbnails(VideoPlayer* player, int thumbnailHeight, int maxThumbnails) {
    if (!player || !player->IsLoaded()) {
        std::cerr << "Cannot generate thumbnails: invalid player" << std::endl;
        return false;
    }

    Clear();

    double duration = player->GetDuration();
    double interval = duration / maxThumbnails;
    
    // Store current playback position to restore later
    double originalTime = player->GetCurrentTime();
    
    std::cout << "Generating " << maxThumbnails << " thumbnails..." << std::endl;

    // Generate thumbnails at regular intervals
    for (int i = 0; i < maxThumbnails; i++) {
        double timestamp = i * interval;
        
        // Seek to position (use fast mode)
        player->Seek(timestamp, true);
        
        // Get frame data
        const uint8_t* frameData = player->GetFrameData();
        if (!frameData) {
            std::cerr << "Failed to get frame data at " << timestamp << "s" << std::endl;
            continue;
        }
        
        int videoWidth = player->GetWidth();
        int videoHeight = player->GetHeight();
        
        // Calculate thumbnail dimensions maintaining aspect ratio
        int thumbWidth = static_cast<int>(thumbnailHeight * (static_cast<float>(videoWidth) / videoHeight));
        
        // Create thumbnail texture
        GLuint textureID = CreateTextureFromRGB(frameData, videoWidth, videoHeight);
        
        if (textureID == 0) {
            std::cerr << "Failed to create texture for thumbnail at " << timestamp << "s" << std::endl;
            continue;
        }
        
        // Store thumbnail
        Thumbnail thumb;
        thumb.timestamp = timestamp;
        thumb.textureID = textureID;
        thumb.width = thumbWidth;
        thumb.height = thumbnailHeight;
        m_Thumbnails.push_back(thumb);
    }
    
    // Restore original playback position
    player->Seek(originalTime, false);
    
    std::cout << "Generated " << m_Thumbnails.size() << " thumbnails successfully" << std::endl;
    return !m_Thumbnails.empty();
}

const Thumbnail* TimelineThumbnails::GetThumbnailAt(double timestamp, double tolerance) {
    const Thumbnail* closest = nullptr;
    double closestDist = tolerance;
    
    for (const auto& thumb : m_Thumbnails) {
        double dist = std::abs(thumb.timestamp - timestamp);
        if (dist < closestDist) {
            closestDist = dist;
            closest = &thumb;
        }
    }
    
    return closest;
}

void TimelineThumbnails::Clear() {
    // Delete all OpenGL textures
    for (auto& thumb : m_Thumbnails) {
        if (thumb.textureID != 0) {
            glDeleteTextures(1, &thumb.textureID);
        }
    }
    m_Thumbnails.clear();
}

GLuint TimelineThumbnails::CreateTextureFromRGB(const uint8_t* data, int width, int height) {
    if (!data) return 0;
    
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    // Upload RGB data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    return textureID;
}
