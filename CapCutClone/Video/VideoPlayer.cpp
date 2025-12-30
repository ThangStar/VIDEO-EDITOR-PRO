#include "VideoPlayer.h"
#include <iostream>

VideoPlayer::VideoPlayer()
    : m_FormatContext(nullptr)
    , m_CodecContext(nullptr)
    , m_SwsContext(nullptr)
    , m_Frame(nullptr)
    , m_FrameRGB(nullptr)
    , m_Packet(nullptr)
    , m_Buffer(nullptr)
    , m_VideoStreamIndex(-1)
    , m_Width(0)
    , m_Height(0)
    , m_Duration(0.0)
    , m_CurrentTime(0.0)
    , m_FPS(0.0)
    , m_IsLoaded(false)
{
}

VideoPlayer::~VideoPlayer() {
    Cleanup();
}

bool VideoPlayer::LoadVideo(const std::string& filepath) {
    Cleanup();
    
    // Completely suppress FFmpeg warnings (set to QUIET)
    av_log_set_level(AV_LOG_QUIET);
    
    // Open video file
    if (avformat_open_input(&m_FormatContext, filepath.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Could not open video file: " << filepath << std::endl;
        return false;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(m_FormatContext, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        Cleanup();
        return false;
    }

    // Find video and audio streams
    m_VideoStreamIndex = -1;
    for (unsigned int i = 0; i < m_FormatContext->nb_streams; i++) {
        if (m_FormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_VideoStreamIndex == -1) {
            m_VideoStreamIndex = i;
        }
    }

    if (m_VideoStreamIndex == -1) {
        std::cerr << "Could not find video stream" << std::endl;
        Cleanup();
        return false;
    }

    // Get codec parameters
    AVCodecParameters* codecParams = m_FormatContext->streams[m_VideoStreamIndex]->codecpar;
    
    // Try GPU hardware decoders first for better performance
    const AVCodec* codec = nullptr;
    const char* hwDecoderNames[] = {
        "h264_cuvid",    // NVIDIA CUDA
        "h264_qsv",      // Intel Quick Sync
        "h264_d3d11va",  // Direct3D 11 (Common on Windows)
        "h264_dxva2",    // DirectX Video Acceleration (Older Windows)
        "hevc_cuvid",    // NVIDIA HEVC
        "hevc_qsv",      // Intel HEVC
        nullptr
    };
    
    // Try hardware decoders
    for (int i = 0; hwDecoderNames[i] != nullptr; ++i) {
        codec = avcodec_find_decoder_by_name(hwDecoderNames[i]);
        if (codec) {
            std::cout << "Trying GPU decoder: " << hwDecoderNames[i] << std::endl;
            break;
        }
    }
    
    // Fallback to software decoder
    if (!codec) {
        std::cout << "GPU decoders not available, using software decoder" << std::endl;
        codec = avcodec_find_decoder(codecParams->codec_id);
    }
    
    if (!codec) {
        std::cerr << "Unsupported codec!" << std::endl;
        Cleanup();
        return false;
    }

    // Create codec context
    m_CodecContext = avcodec_alloc_context3(codec);
    if (!m_CodecContext) {
        std::cerr << "Could not allocate codec context" << std::endl;
        Cleanup();
        return false;
    }

    if (avcodec_parameters_to_context(m_CodecContext, codecParams) < 0) {
        std::cerr << "Could not copy codec parameters" << std::endl;
        Cleanup();
        return false;
    }
    
    // MAXIMUM error concealment to fix broken frames
    m_CodecContext->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK | FF_EC_FAVOR_INTER;
    m_CodecContext->err_recognition = 0;  // Be permissive - accept all frames
    m_CodecContext->workaround_bugs = FF_BUG_AUTODETECT | FF_BUG_XVID_ILACE | FF_BUG_UMP4 | FF_BUG_NO_PADDING | FF_BUG_AMV | FF_BUG_QPEL_CHROMA | FF_BUG_STD_QPEL | FF_BUG_DIRECT_BLOCKSIZE | FF_BUG_EDGE | FF_BUG_HPEL_CHROMA | FF_BUG_DC_CLIP | FF_BUG_MS | FF_BUG_TRUNCATED | FF_BUG_IEDGE;
    m_CodecContext->idct_algo = FF_IDCT_AUTO;
    m_CodecContext->debug = 0;  // Disable debug output
    
    // Don't skip any frames - show everything
    m_CodecContext->skip_frame = AVDISCARD_NONE;
    m_CodecContext->skip_idct = AVDISCARD_NONE;
    m_CodecContext->skip_loop_filter = AVDISCARD_NONE;

    // Open codec
    if (avcodec_open2(m_CodecContext, codec, nullptr) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        Cleanup();
        return false;
    }
    
    std::cout << "Using decoder: " << codec->name << std::endl;

    // Get video properties
    m_Width = m_CodecContext->width;
    m_Height = m_CodecContext->height;
    
    AVStream* videoStream = m_FormatContext->streams[m_VideoStreamIndex];
    
    // Use container duration for accuracy (stream duration can be wrong for some videos)
    if (m_FormatContext->duration != AV_NOPTS_VALUE) {
        m_Duration = m_FormatContext->duration / (double)AV_TIME_BASE;
    } else {
        // Fallback to stream duration if container doesn't have it
        m_Duration = videoStream->duration * av_q2d(videoStream->time_base);
    }
    
    // Get average frame rate (more reliable than r_frame_rate for VFR videos)
    m_FPS = av_q2d(videoStream->avg_frame_rate);
    if (m_FPS <= 0) {
        m_FPS = av_q2d(videoStream->r_frame_rate);  // Fallback
    }

    // Allocate frames
    m_Frame = av_frame_alloc();
    m_FrameRGB = av_frame_alloc();
    m_Packet = av_packet_alloc();

    if (!m_Frame || !m_FrameRGB || !m_Packet) {
        std::cerr << "Could not allocate frames" << std::endl;
        Cleanup();
        return false;
    }


    // Allocate buffer for RGB frame
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, m_Width, m_Height, 1);
    m_Buffer = (uint8_t*)av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(m_FrameRGB->data, m_FrameRGB->linesize, m_Buffer, 
                         AV_PIX_FMT_RGB24, m_Width, m_Height, 1);

    // Initialize SWS context for color conversion with HIGH QUALITY
    m_SwsContext = sws_getContext(
        m_Width, m_Height, m_CodecContext->pix_fmt,
        m_Width, m_Height, AV_PIX_FMT_RGB24,
        SWS_LANCZOS | SWS_ACCURATE_RND,  // High quality scaling - sharp & artifact-free
        nullptr, nullptr, nullptr
    );

    if (!m_SwsContext) {
        std::cerr << "Could not initialize SWS context" << std::endl;
        Cleanup();
        return false;
    }

    m_IsLoaded = true;
    m_CurrentTime = 0.0;

    std::cout << "Video loaded successfully!" << std::endl;
    std::cout << "Resolution: " << m_Width << "x" << m_Height << std::endl;
    std::cout << "Duration: " << m_Duration << "s" << std::endl;
    std::cout << "FPS: " << m_FPS << std::endl;

    return true;
}

bool VideoPlayer::DecodeNextFrame() {
    if (!m_IsLoaded) return false;

    std::lock_guard<std::mutex> lock(m_PacketMutex);  // CRITICAL: Lock for thread safety
    
    while (av_read_frame(m_FormatContext, m_Packet) >= 0) {
        // Check if packet is from video stream
        if (m_Packet->stream_index == m_VideoStreamIndex) {
            // Send packet to decoder
            int ret = avcodec_send_packet(m_CodecContext, m_Packet);
            if (ret < 0) {
                av_packet_unref(m_Packet);
                continue;
            }

            // Receive decoded frame
            ret = avcodec_receive_frame(m_CodecContext, m_Frame);
            if (ret == 0) {
                // Convert frame to RGB
                sws_scale(
                    m_SwsContext,
                    m_Frame->data, m_Frame->linesize, 0, m_Height,
                    m_FrameRGB->data, m_FrameRGB->linesize
                );

                // Update current time
                m_CurrentTime = m_Frame->pts * av_q2d(m_FormatContext->streams[m_VideoStreamIndex]->time_base);

                av_packet_unref(m_Packet);
                return true;
            }
        }
        av_packet_unref(m_Packet);
    }

    return false;
}

void VideoPlayer::Seek(double timestamp, bool fastMode) {
    if (!m_IsLoaded) return;

    // Convert timestamp to PTS (Presentation Time Stamp)
    AVStream* videoStream = m_FormatContext->streams[m_VideoStreamIndex];
    int64_t seekTarget = (int64_t)(timestamp / av_q2d(videoStream->time_base));
    
    // Seek to nearest keyframe before target
    if (av_seek_frame(m_FormatContext, m_VideoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
        std::cerr << "Error seeking to timestamp" << std::endl;
        return;
    }

    // Flush codec buffers to clear old frames
    avcodec_flush_buffers(m_CodecContext);
    
    if (fastMode) {
        // FAST MODE: Just decode one frame near the position (for scrubbing)
        // This is much faster but less precise
        int framesDecoded = 0;
        while (av_read_frame(m_FormatContext, m_Packet) >= 0 && framesDecoded < 2) {
            if (m_Packet->stream_index == m_VideoStreamIndex) {
                if (avcodec_send_packet(m_CodecContext, m_Packet) >= 0) {
                    if (avcodec_receive_frame(m_CodecContext, m_Frame) == 0) {
                        framesDecoded++;
                        // Convert last frame to RGB
                        sws_scale(
                            m_SwsContext,
                            m_Frame->data, m_Frame->linesize, 0, m_Height,
                            m_FrameRGB->data, m_FrameRGB->linesize
                        );
                        m_CurrentTime = m_Frame->pts * av_q2d(videoStream->time_base);
                    }
                }
            }
            av_packet_unref(m_Packet);
        }
    } else {
        // PRECISE MODE: Decode frames until exact target timestamp (for pause/resume)
        double frameDuration = 1.0 / m_FPS;
        double tolerance = frameDuration * 0.5;
        
        while (av_read_frame(m_FormatContext, m_Packet) >= 0) {
            if (m_Packet->stream_index == m_VideoStreamIndex) {
                int ret = avcodec_send_packet(m_CodecContext, m_Packet);
                if (ret < 0) {
                    av_packet_unref(m_Packet);
                    continue;
                }

                ret = avcodec_receive_frame(m_CodecContext, m_Frame);
                if (ret == 0) {
                    double frameTime = m_Frame->pts * av_q2d(videoStream->time_base);
                    
                    // If we've reached the target timestamp (within tolerance)
                    if (frameTime >= timestamp - tolerance) {
                        // Convert frame to RGB for display
                        sws_scale(
                            m_SwsContext,
                            m_Frame->data, m_Frame->linesize, 0, m_Height,
                            m_FrameRGB->data, m_FrameRGB->linesize
                        );
                        
                        m_CurrentTime = frameTime;
                        av_packet_unref(m_Packet);
                        break;
                    }
                }
            }
            av_packet_unref(m_Packet);
        }
    }
}



void VideoPlayer::Reset() {
    Seek(0.0);
}

void VideoPlayer::Close() {
    Cleanup();
}

void VideoPlayer::Cleanup() {
    // Free video resources
    if (m_SwsContext) {
        sws_freeContext(m_SwsContext);
        m_SwsContext = nullptr;
    }

    if (m_Buffer) {
        av_free(m_Buffer);
        m_Buffer = nullptr;
    }

    if (m_FrameRGB) {
        av_frame_free(&m_FrameRGB);
    }

    if (m_Frame) {
        av_frame_free(&m_Frame);
    }

    if (m_Packet) {
        av_packet_free(&m_Packet);
    }

    if (m_CodecContext) {
        avcodec_free_context(&m_CodecContext);
    }

    if (m_FormatContext) {
        avformat_close_input(&m_FormatContext);
    }

    m_IsLoaded = false;
    m_VideoStreamIndex = -1;
    m_Width = 0;
    m_Height = 0;
    m_Duration = 0.0;
    m_CurrentTime = 0.0;
    m_FPS = 0.0;
}
