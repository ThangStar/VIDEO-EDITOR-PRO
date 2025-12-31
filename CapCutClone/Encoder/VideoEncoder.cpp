#include "VideoEncoder.h"
#include <iostream>

VideoEncoder::VideoEncoder()
    : m_Width(0), m_Height(0), m_Fps(30)
    , m_FormatCtx(nullptr), m_CodecCtx(nullptr)
    , m_Stream(nullptr), m_SwsCtx(nullptr)
    , m_Frame(nullptr), m_Packet(nullptr)
    , m_FrameCount(0)
{
}

VideoEncoder::~VideoEncoder() {
    Cleanup();
}

const AVCodec* VideoEncoder::FindHardwareCodec(const std::string& codecName) {
    const AVCodec* codec = nullptr;
    
    // List acceptable candidates in order of preference
    std::vector<std::string> candidates;
    candidates.push_back(codecName + "_nvenc"); // NVIDIA
    candidates.push_back(codecName + "_qsv");   // Intel
    candidates.push_back(codecName + "_amf");   // AMD
    
    if (codecName == "h264") {
        candidates.push_back("libx264");        // Software (GPL)
        candidates.push_back("libopenh264");    // Software
        candidates.push_back("h264_mf");        // Windows Media Foundation
    }
    
    candidates.push_back(codecName);            // Default name (e.g. h264)
    candidates.push_back("mpeg4");              // Fallback for simple testing

    // Debug: List all relevant encoders to help specific diagnosis
    std::cout << "[VideoEncoder] Checking available encoders for: " << codecName << std::endl;
    void* debugIter = nullptr;
    const AVCodec* debugCodec;
    while ((debugCodec = av_codec_iterate(&debugIter))) {
        if (av_codec_is_encoder(debugCodec)) {
            std::string name = debugCodec->name;
            if (name.find("h264") != std::string::npos || name.find("nvenc") != std::string::npos) {
                std::cout << "  - " << debugCodec->name << ": " << debugCodec->long_name << std::endl;
            }
        }
    }

    for (const auto& name : candidates) {
        codec = avcodec_find_encoder_by_name(name.c_str());
        if (codec) {
            std::cout << "[VideoEncoder] Found Encoder: " << name << " (" << codec->long_name << ")" << std::endl;
            return codec;
        }
    }

    std::cerr << "[VideoEncoder] CRITICAL: Failed to find ANY video encoder." << std::endl;
    std::cerr << "Available encoders:" << std::endl;
    void* iterator = nullptr;
    const AVCodec* c;
    while ((c = av_codec_iterate(&iterator))) {
        if (av_codec_is_encoder(c)) {
            std::cout << " - " << c->name << ": " << c->long_name << std::endl;
        }
    }
    
    return nullptr;
}

bool VideoEncoder::Initialize(const std::string& outputFile, int width, int height, int fps, int bitrate) {
    m_OutputFile = outputFile;
    m_Width = width;
    m_Height = height;
    m_Fps = fps;
    m_FrameCount = 0;

    // 1. Allocation Output Context
    avformat_alloc_output_context2(&m_FormatCtx, nullptr, nullptr, outputFile.c_str());
    if (!m_FormatCtx) {
        std::cerr << "Could not deduce format from file extension: " << outputFile << std::endl;
        avformat_alloc_output_context2(&m_FormatCtx, nullptr, "mp4", outputFile.c_str());
    }
    if (!m_FormatCtx) return false;

    // 2. Find Codec (H.264)
    const AVCodec* codec = FindHardwareCodec("h264");
    if (!codec) {
        std::cerr << "Codec not found!" << std::endl;
        return false;
    }

    // 3. Create Stream
    m_Stream = avformat_new_stream(m_FormatCtx, nullptr);
    if (!m_Stream) return false;
    m_Stream->id = m_FormatCtx->nb_streams - 1;

    // 4. Create Codec Context
    m_CodecCtx = avcodec_alloc_context3(codec);
    if (!m_CodecCtx) return false;

    // 5. Configure Codec
    m_CodecCtx->width = width;
    m_CodecCtx->height = height;
    m_CodecCtx->time_base = {1, fps};
    m_Stream->time_base = m_CodecCtx->time_base;
    m_CodecCtx->framerate = {fps, 1};
    
    // Intelligent Pixel Format Selection
    m_CodecCtx->pix_fmt = AV_PIX_FMT_YUV420P; // Default fallback
    if (codec->pix_fmts) {
        bool foundNV12 = false;
        bool foundYUV420P = false;
        
        for (const enum AVPixelFormat* p = codec->pix_fmts; *p != -1; p++) {
            if (*p == AV_PIX_FMT_NV12) foundNV12 = true;
            if (*p == AV_PIX_FMT_YUV420P) foundYUV420P = true;
        }
        
        if (foundNV12) {
             m_CodecCtx->pix_fmt = AV_PIX_FMT_NV12;
        } else if (foundYUV420P) {
             m_CodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
        } else {
             // Take the first supported one if neither common format is found
             m_CodecCtx->pix_fmt = codec->pix_fmts[0];
        }
    }
    std::cout << "[VideoEncoder] Selected Pixel Format: " << av_get_pix_fmt_name(m_CodecCtx->pix_fmt) << std::endl;

    m_CodecCtx->bit_rate = bitrate;
    m_CodecCtx->gop_size = 12;
    m_CodecCtx->max_b_frames = 2;
    
    // Explicitly set Profile (High is widely supported and good quality)
    // Helps avoid "Invalid Argument" with MF if it defaults to Baseline/Main on High Res
    m_CodecCtx->profile = AV_PROFILE_H264_HIGH;

    // Global header for MP4
    if (m_FormatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        m_CodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // Open Codec
    int ret = avcodec_open2(m_CodecCtx, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Could not open codec! Error: " << errbuf << " (" << ret << ")" << std::endl;
        return false;
    }

    // Copy parameters to stream
    avcodec_parameters_from_context(m_Stream->codecpar, m_CodecCtx);

    // 6. Open File
    if (!(m_FormatCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_FormatCtx->pb, outputFile.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Could not open output file: " << outputFile << std::endl;
            return false;
        }
    }

    // 7. Write Header
    if (avformat_write_header(m_FormatCtx, nullptr) < 0) {
        return false;
    }

    // Alloc Frames
    m_Frame = av_frame_alloc();
    m_Frame->format = m_CodecCtx->pix_fmt;
    m_Frame->width = m_CodecCtx->width;
    m_Frame->height = m_CodecCtx->height;
    if (av_frame_get_buffer(m_Frame, 32) < 0) return false;

    m_Packet = av_packet_alloc();

    // SwsContext for RGB24 -> YUV
    m_SwsCtx = sws_getContext(
        width, height, AV_PIX_FMT_RGB24,
        width, height, m_CodecCtx->pix_fmt,
        SWS_BICUBIC, nullptr, nullptr, nullptr
    );

    return true;
}

bool VideoEncoder::EncodeFrame(const uint8_t* rgbData) {
    if (!m_CodecCtx) return false;

    // 1. Make frame writable
    if (av_frame_make_writable(m_Frame) < 0) return false;

    // 2. Convert RGB to YUV
    // Stride for RGB24 is width * 3
    int srcStride[1] = { m_Width * 3 };
    const uint8_t* srcSlice[1] = { rgbData };
    
    sws_scale(m_SwsCtx, srcSlice, srcStride, 0, m_Height, m_Frame->data, m_Frame->linesize);

    // 3. Set PTS
    m_Frame->pts = m_FrameCount++;

    // 4. Send frame to encoder
    if (avcodec_send_frame(m_CodecCtx, m_Frame) < 0) {
        std::cerr << "Error sending frame to encoder" << std::endl;
        return false;
    }

    // 5. Receive packets
    while (true) {
        int ret = avcodec_receive_packet(m_CodecCtx, m_Packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return false;

        // Rescale packet timestamp to stream time base
        av_packet_rescale_ts(m_Packet, m_CodecCtx->time_base, m_Stream->time_base);
        m_Packet->stream_index = m_Stream->index;

        // Write packet
        av_interleaved_write_frame(m_FormatCtx, m_Packet);
        av_packet_unref(m_Packet);
    }

    return true;
}

bool VideoEncoder::Finalize() {
    if (!m_CodecCtx) return false;

    // Flush encoder
    avcodec_send_frame(m_CodecCtx, nullptr);

    while (true) {
        int ret = avcodec_receive_packet(m_CodecCtx, m_Packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return false;

        av_packet_rescale_ts(m_Packet, m_CodecCtx->time_base, m_Stream->time_base);
        m_Packet->stream_index = m_Stream->index;
        av_interleaved_write_frame(m_FormatCtx, m_Packet);
        av_packet_unref(m_Packet);
    }

    av_write_trailer(m_FormatCtx);
    return true;
}

void VideoEncoder::Cleanup() {
    if (m_FormatCtx) {
        if (!(m_FormatCtx->oformat->flags & AVFMT_NOFILE) && m_FormatCtx->pb) {
            avio_closep(&m_FormatCtx->pb);
        }
        avformat_free_context(m_FormatCtx);
        m_FormatCtx = nullptr;
    }
    if (m_CodecCtx) {
        avcodec_free_context(&m_CodecCtx);
        m_CodecCtx = nullptr;
    }
    if (m_Frame) {
        av_frame_free(&m_Frame);
        m_Frame = nullptr;
    }
    if (m_Packet) {
        av_packet_free(&m_Packet);
        m_Packet = nullptr;
    }
    if (m_SwsCtx) {
        sws_freeContext(m_SwsCtx);
        m_SwsCtx = nullptr;
    }
}
