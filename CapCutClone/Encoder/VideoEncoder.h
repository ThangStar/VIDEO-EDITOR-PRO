#pragma once

#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    // Initialize the encoder
    // width/height: Output resolution
    // fps: Output frames per second
    // bitrate: Target bitrate (e.g., 5000000 for 5Mbps)
    bool Initialize(const std::string& outputFile, int width, int height, int fps, int bitrate = 5000000);

    // Encode a single frame
    // rgbData: Pointer to raw RGB24 pixel data (width * height * 3 bytes)
    bool EncodeFrame(const uint8_t* rgbData);

    // Finalize encoding (flush buffers and write trailer)
    bool Finalize();

private:
    std::string m_OutputFile;
    int m_Width;
    int m_Height;
    int m_Fps;
    
    // FFmpeg context
    AVFormatContext* m_FormatCtx;
    AVCodecContext* m_CodecCtx;
    AVStream* m_Stream;
    SwsContext* m_SwsCtx;
    
    // Frames and packets
    AVFrame* m_Frame;      // YUV frame for encoding
    AVPacket* m_Packet;
    
    int64_t m_FrameCount;  // Monotonic frame counter for PTS

    // Helper functions
    const AVCodec* FindHardwareCodec(const std::string& codecName);
    void Cleanup();
};
