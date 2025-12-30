#pragma once

#include <string>
#include <vector>
#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    // Video loading
    bool LoadVideo(const std::string& filepath);
    void Close();

    // Playback control
    bool DecodeNextFrame();
    void Seek(double timestamp, bool fastMode = false);
    void Reset();

    // Getters
    bool IsLoaded() const { return m_IsLoaded; }
    double GetDuration() const { return m_Duration; }
    double GetCurrentTime() const { return m_CurrentTime; }
    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }
    double GetFPS() const { return m_FPS; }
    const uint8_t* GetFrameData() const { return m_FrameRGB ? m_FrameRGB->data[0] : nullptr; }

private:
    // FFmpeg structures  
    AVFormatContext* m_FormatContext;
    AVCodecContext* m_CodecContext;  // Video codec
    SwsContext* m_SwsContext;  // Video scaler
    
    // Frames
    AVFrame* m_Frame;  // Video frame
    AVFrame* m_FrameRGB;  // Converted RGB frame
    AVPacket* m_Packet;
    uint8_t* m_Buffer;  // Video buffer
    
    // Video properties
    int m_VideoStreamIndex;
    int m_Width;
    int m_Height;
    double m_Duration;
    double m_CurrentTime;
    double m_FPS;
    bool m_IsLoaded;
    
    // Thread safety for concurrent audio/video decode
    mutable std::mutex m_PacketMutex;

    // Helper methods
    void Cleanup();
};
