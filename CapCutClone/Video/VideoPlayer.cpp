#include "VideoPlayer.h"
#include <iostream>

VideoPlayer::VideoPlayer()
    : m_FormatContext(nullptr), m_CodecContext(nullptr),
      m_AudioCodecContext(nullptr), m_SwsContext(nullptr),
      m_SwrContext(nullptr), m_Frame(nullptr), m_FrameRGB(nullptr),
      m_AudioFrame(nullptr), m_Packet(nullptr), m_Buffer(nullptr),
      m_VideoStreamIndex(-1), m_AudioStreamIndex(-1), m_Width(0), m_Height(0),
      m_Duration(0.0), m_CurrentTime(0.0), m_FPS(0.0), m_IsLoaded(false),
      m_HardwareDeviceContext(nullptr) {}

VideoPlayer::~VideoPlayer() { Cleanup(); }

bool VideoPlayer::LoadVideo(const std::string &filepath) {
  Cleanup();

  // Completely suppress FFmpeg warnings (set to QUIET)
  av_log_set_level(AV_LOG_QUIET);

  // Open video file
  if (avformat_open_input(&m_FormatContext, filepath.c_str(), nullptr,
                          nullptr) < 0) {
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
  m_AudioStreamIndex = -1;
  for (unsigned int i = 0; i < m_FormatContext->nb_streams; i++) {
    if (m_FormatContext->streams[i]->codecpar->codec_type ==
            AVMEDIA_TYPE_VIDEO &&
        m_VideoStreamIndex == -1) {
      m_VideoStreamIndex = i;
    } else if (m_FormatContext->streams[i]->codecpar->codec_type ==
                   AVMEDIA_TYPE_AUDIO &&
               m_AudioStreamIndex == -1) {
      m_AudioStreamIndex = i;
    }
  }

  if (m_VideoStreamIndex == -1) {
    std::cerr << "Could not find video stream" << std::endl;
    Cleanup();
    return false;
  }

  // Get codec parameters
  AVCodecParameters *codecParams =
      m_FormatContext->streams[m_VideoStreamIndex]->codecpar;

  // Enable Hardware Acceleration (Modern Method)
  const AVCodec *codec = avcodec_find_decoder(codecParams->codec_id);
  if (!codec) {
    Cleanup();
    return false;
  }

  m_CodecContext = avcodec_alloc_context3(codec);
  if (!m_CodecContext) {
    Cleanup();
    return false;
  }

  if (avcodec_parameters_to_context(m_CodecContext, codecParams) < 0) {
    Cleanup();
    return false;
  }

  // MAXIMUM error concealment to fix broken frames
  m_CodecContext->error_concealment =
      FF_EC_GUESS_MVS | FF_EC_DEBLOCK | FF_EC_FAVOR_INTER;
  m_CodecContext->err_recognition = 0; // Be permissive - accept all frames
  m_CodecContext->workaround_bugs =
      FF_BUG_AUTODETECT | FF_BUG_XVID_ILACE | FF_BUG_UMP4 | FF_BUG_NO_PADDING |
      FF_BUG_AMV | FF_BUG_QPEL_CHROMA | FF_BUG_STD_QPEL |
      FF_BUG_DIRECT_BLOCKSIZE | FF_BUG_EDGE | FF_BUG_HPEL_CHROMA |
      FF_BUG_DC_CLIP | FF_BUG_MS | FF_BUG_TRUNCATED | FF_BUG_IEDGE;
  m_CodecContext->idct_algo = FF_IDCT_AUTO;
  m_CodecContext->debug = 0; // Disable debug output

  // Don't skip any frames - show everything
  m_CodecContext->skip_frame = AVDISCARD_NONE;
  m_CodecContext->skip_idct = AVDISCARD_NONE;
  m_CodecContext->skip_loop_filter = AVDISCARD_NONE;

  // Try to create Hardware Device Context (D3D11VA)
  int ret = av_hwdevice_ctx_create(
      &m_HardwareDeviceContext, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
  if (ret < 0) {
    std::cout << "D3D11VA not available, trying DXVA2..." << std::endl;
    ret = av_hwdevice_ctx_create(&m_HardwareDeviceContext,
                                 AV_HWDEVICE_TYPE_DXVA2, nullptr, nullptr, 0);
  }

  if (ret == 0) {
    std::cout << "[VideoPlayer] SUCCESS: Hardware Acceleration (D3D11VA/DXVA2) "
                 "Enabled!"
              << std::endl;
    m_CodecContext->hw_device_ctx = av_buffer_ref(m_HardwareDeviceContext);

    // This callback is required to negotiate the pixel format
    m_CodecContext->get_format =
        [](AVCodecContext * ctx,
           const enum AVPixelFormat *pix_fmts) -> enum AVPixelFormat {
      const enum AVPixelFormat *p;
      for (p = pix_fmts; *p != -1; p++) {
        if (*p == AV_PIX_FMT_D3D11 || *p == AV_PIX_FMT_DXVA2_VLD) {
          return *p;
        }
      }
      std::cerr << "Failed to get HW surface format." << std::endl;
      return AV_PIX_FMT_NONE;
    };
  } else {
    std::cerr
        << "[VideoPlayer] WARNING: Hardware Acceleration FAILED. using CPU."
        << std::endl;
  }

  if (avcodec_open2(m_CodecContext, codec, nullptr) < 0) {
    std::cerr << "Could not open codec" << std::endl;
    Cleanup();
    return false;
  }

  // Setup Audio
  if (m_AudioStreamIndex != -1) {
    AVCodecParameters *audioCodecParams =
        m_FormatContext->streams[m_AudioStreamIndex]->codecpar;
    const AVCodec *audioCodec =
        avcodec_find_decoder(audioCodecParams->codec_id);
    if (audioCodec) {
      m_AudioCodecContext = avcodec_alloc_context3(audioCodec);
      if (m_AudioCodecContext) {
        avcodec_parameters_to_context(m_AudioCodecContext, audioCodecParams);
        if (avcodec_open2(m_AudioCodecContext, audioCodec, nullptr) >= 0) {
          std::cout << "Audio Stream Found: "
                    << m_AudioCodecContext->sample_rate << "Hz, "
                    << m_AudioCodecContext->ch_layout.nb_channels << " channels"
                    << std::endl;

          // Initialize Audio Context
          m_AudioContext.Init(m_AudioCodecContext->sample_rate,
                              m_AudioCodecContext->ch_layout.nb_channels);
        }
      }
    }
  }

  std::cout << "Using decoder: " << codec->name << std::endl;

  // Get video properties
  m_Width = m_CodecContext->width;
  m_Height = m_CodecContext->height;

  AVStream *videoStream = m_FormatContext->streams[m_VideoStreamIndex];

  // Use container duration for accuracy (stream duration can be wrong for some
  // videos)
  if (m_FormatContext->duration != AV_NOPTS_VALUE) {
    m_Duration = m_FormatContext->duration / (double)AV_TIME_BASE;
  } else {
    // Fallback to stream duration if container doesn't have it
    m_Duration = videoStream->duration * av_q2d(videoStream->time_base);
  }

  // Get average frame rate (more reliable than r_frame_rate for VFR videos)
  m_FPS = av_q2d(videoStream->avg_frame_rate);
  if (m_FPS <= 0) {
    m_FPS = av_q2d(videoStream->r_frame_rate); // Fallback
  }

  // Allocate frames
  m_Frame = av_frame_alloc();
  m_FrameRGB = av_frame_alloc();
  m_AudioFrame = av_frame_alloc();
  m_Packet = av_packet_alloc();

  if (!m_Frame || !m_FrameRGB || !m_Packet || !m_AudioFrame) {
    std::cerr << "Could not allocate frames" << std::endl;
    Cleanup();
    return false;
  }

  // Allocate buffer for RGB frame
  int numBytes =
      av_image_get_buffer_size(AV_PIX_FMT_RGB24, m_Width, m_Height, 1);
  m_Buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
  av_image_fill_arrays(m_FrameRGB->data, m_FrameRGB->linesize, m_Buffer,
                       AV_PIX_FMT_RGB24, m_Width, m_Height, 1);

  // Initialize SWS context (Initial attempt, might fail for HW formats like
  // D3D11) This is fine, as DecodeNextFrame will fix it dynamically using
  // sws_getCachedContext
  m_SwsContext = sws_getContext(m_Width, m_Height, m_CodecContext->pix_fmt,
                                m_Width, m_Height, AV_PIX_FMT_RGB24,
                                SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

  // Warn but do NOT fail here. DecodeNextFrame handles the real SWS creation.
  if (!m_SwsContext) {
    std::cout << "[VideoPlayer] Note: Initial sws_getContext failed (likely HW "
                 "format). Will retry in DecodeNextFrame."
              << std::endl;
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
  if (!m_IsLoaded)
    return false;

  std::lock_guard<std::mutex> lock(m_PacketMutex);

  while (av_read_frame(m_FormatContext, m_Packet) >= 0) {
    // Video Stream
    if (m_Packet->stream_index == m_VideoStreamIndex) {
      int ret = avcodec_send_packet(m_CodecContext, m_Packet);
      if (ret < 0) {
        av_packet_unref(m_Packet);
        continue;
      }

      ret = avcodec_receive_frame(m_CodecContext, m_Frame);
      if (ret == 0) {
        // Determine final frame (SW or HW)
        AVFrame *finalFrame = m_Frame;
        AVFrame *swFrame = nullptr;

        if (m_Frame->format == AV_PIX_FMT_D3D11 ||
            m_Frame->format == AV_PIX_FMT_DXVA2_VLD) {
          swFrame = av_frame_alloc();
          if (av_hwframe_transfer_data(swFrame, m_Frame, 0) < 0) {
            std::cerr << "Error transferring HW frame to CPU" << std::endl;
            av_frame_free(&swFrame);
            av_packet_unref(m_Packet);
            return false;
          }
          finalFrame = swFrame;
        }

        // Re-init SWS if needed
        m_SwsContext = sws_getCachedContext(
            m_SwsContext, m_Width, m_Height, (AVPixelFormat)finalFrame->format,
            m_Width, m_Height, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, nullptr,
            nullptr, nullptr);

        if (m_SwsContext) {
          sws_scale(m_SwsContext, finalFrame->data, finalFrame->linesize, 0,
                    m_Height, m_FrameRGB->data, m_FrameRGB->linesize);
        }

        if (swFrame)
          av_frame_free(&swFrame);

        m_CurrentTime =
            m_Frame->pts *
            av_q2d(m_FormatContext->streams[m_VideoStreamIndex]->time_base);
        av_packet_unref(m_Packet);

        return true;
      }
    }
    // Audio Stream
    else if (m_Packet->stream_index == m_AudioStreamIndex &&
             m_AudioCodecContext) {
      int ret = avcodec_send_packet(m_AudioCodecContext, m_Packet);
      if (ret >= 0) {
        while (avcodec_receive_frame(m_AudioCodecContext, m_AudioFrame) == 0) {
          // Audio Processing
          if (!m_SwrContext) {
            av_channel_layout_default(
                &m_AudioFrame->ch_layout,
                m_AudioCodecContext->ch_layout.nb_channels);
            swr_alloc_set_opts2(&m_SwrContext, &m_AudioCodecContext->ch_layout,
                                AV_SAMPLE_FMT_FLT,
                                m_AudioCodecContext->sample_rate,
                                &m_AudioCodecContext->ch_layout,
                                m_AudioCodecContext->sample_fmt,
                                m_AudioCodecContext->sample_rate, 0, nullptr);
            swr_init(m_SwrContext);
          }

          if (m_SwrContext) {
            int dstSamples = av_rescale_rnd(
                swr_get_delay(m_SwrContext, m_AudioCodecContext->sample_rate) +
                    m_AudioFrame->nb_samples,
                m_AudioCodecContext->sample_rate,
                m_AudioCodecContext->sample_rate, AV_ROUND_UP);

            uint8_t **dstData = nullptr;
            int linesize = 0;
            av_samples_alloc_array_and_samples(
                &dstData, &linesize, m_AudioCodecContext->ch_layout.nb_channels,
                dstSamples, AV_SAMPLE_FMT_FLT, 0);

            int convRet = swr_convert(m_SwrContext, dstData, dstSamples,
                                      (const uint8_t **)m_AudioFrame->data,
                                      m_AudioFrame->nb_samples);
            if (convRet > 0) {
              m_AudioContext.PushAudio((float *)dstData[0], convRet);
            }

            if (dstData) {
              av_freep(&dstData[0]);
              av_freep(&dstData);
            }
          }
        }
      }
    }

    av_packet_unref(m_Packet);
  }

  return false;
}

void VideoPlayer::Seek(double timestamp, bool fastMode) {
  if (!m_IsLoaded)
    return;

  // Convert timestamp to PTS (Presentation Time Stamp)
  AVStream *videoStream = m_FormatContext->streams[m_VideoStreamIndex];
  int64_t seekTarget = (int64_t)(timestamp / av_q2d(videoStream->time_base));

  // Seek to nearest keyframe before target
  if (av_seek_frame(m_FormatContext, m_VideoStreamIndex, seekTarget,
                    AVSEEK_FLAG_BACKWARD) < 0) {
    std::cerr << "Error seeking to timestamp" << std::endl;
    return;
  }

  // Flush codec buffers to clear old frames
  avcodec_flush_buffers(m_CodecContext);
  if (m_AudioCodecContext) {
    avcodec_flush_buffers(m_AudioCodecContext);
  }
  m_AudioContext.Clear();

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
            sws_scale(m_SwsContext, m_Frame->data, m_Frame->linesize, 0,
                      m_Height, m_FrameRGB->data, m_FrameRGB->linesize);
            m_CurrentTime = m_Frame->pts * av_q2d(videoStream->time_base);
          }
        }
      }
      av_packet_unref(m_Packet);
    }
  } else {
    // PRECISE MODE: Decode frames until exact target timestamp (for
    // pause/resume)
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
            sws_scale(m_SwsContext, m_Frame->data, m_Frame->linesize, 0,
                      m_Height, m_FrameRGB->data, m_FrameRGB->linesize);

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

void VideoPlayer::Reset() { Seek(0.0); }

void VideoPlayer::Close() { Cleanup(); }

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

  if (m_AudioFrame) {
    av_frame_free(&m_AudioFrame);
  }

  if (m_Packet) {
    av_packet_free(&m_Packet);
  }

  if (m_CodecContext) {
    avcodec_free_context(&m_CodecContext);
  }

  if (m_AudioCodecContext) {
    avcodec_free_context(&m_AudioCodecContext);
  }

  if (m_SwrContext) {
    swr_free(&m_SwrContext);
  }

  m_AudioContext.Close();

  if (m_HardwareDeviceContext) {
    av_buffer_unref(&m_HardwareDeviceContext);
    m_HardwareDeviceContext = nullptr;
  }

  if (m_FormatContext) {
    avformat_close_input(&m_FormatContext);
  }

  m_IsLoaded = false;
  m_VideoStreamIndex = -1;
  m_AudioStreamIndex = -1;
  m_Width = 0;
  m_Height = 0;
  m_Duration = 0.0;
  m_CurrentTime = 0.0;
  m_FPS = 0.0;
}
