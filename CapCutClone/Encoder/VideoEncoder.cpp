#include "VideoEncoder.h"
#include <iostream>

VideoEncoder::VideoEncoder()
    : m_Width(0), m_Height(0), m_Fps(30), m_FormatCtx(nullptr),
      m_CodecCtx(nullptr), m_Stream(nullptr), m_SwsCtx(nullptr),
      m_Frame(nullptr), m_Packet(nullptr), m_FrameCount(0) {}

VideoEncoder::~VideoEncoder() { Cleanup(); }

const AVCodec *VideoEncoder::FindHardwareCodec(const std::string &codecName) {
  const AVCodec *codec = nullptr;

  std::vector<std::string> candidates;
  candidates.push_back(codecName + "_nvenc");
  candidates.push_back(codecName + "_qsv");
  candidates.push_back(codecName + "_amf");

  if (codecName == "h264") {
    candidates.push_back("libx264");
    candidates.push_back("libopenh264");
    candidates.push_back("h264_mf");
  }

  candidates.push_back(codecName);
  candidates.push_back("mpeg4");

  std::cout << "[VideoEncoder] FFmpeg Version: " << av_version_info()
            << std::endl;
  std::cout << "[VideoEncoder] Checking available encoders for: " << codecName
            << std::endl;

  void *debugIter = nullptr;
  const AVCodec *debugCodec;
  while ((debugCodec = av_codec_iterate(&debugIter))) {
    if (av_codec_is_encoder(debugCodec)) {
      // Only print video encoders to reduce spam, or relevant ones
      if (debugCodec->type == AVMEDIA_TYPE_VIDEO) {
        std::string name = debugCodec->name;
        if (name.find("nvenc") != std::string::npos ||
            name.find("h264") != std::string::npos ||
            name.find("hevc") != std::string::npos) {
          std::cout << "  - Found: " << debugCodec->name << ": "
                    << debugCodec->long_name << std::endl;
        }
      }
    }
  }

  for (const auto &name : candidates) {
    codec = avcodec_find_encoder_by_name(name.c_str());
    if (codec) {
      std::cout << "[VideoEncoder] Found Encoder: " << name << " ("
                << codec->long_name << ")" << std::endl;
      return codec;
    }
  }

  std::cerr << "[VideoEncoder] CRITICAL: Failed to find ANY video encoder."
            << std::endl;
  return nullptr;
}

bool VideoEncoder::Initialize(const std::string &outputFile, int width,
                              int height, int fps, int bitrate) {
  m_OutputFile = outputFile;
  m_Width = width;
  m_Height = height;
  m_Fps = fps;
  m_FrameCount = 0;

  avformat_alloc_output_context2(&m_FormatCtx, nullptr, nullptr,
                                 outputFile.c_str());
  if (!m_FormatCtx) {
    std::cerr << "Could not deduce format from file extension: " << outputFile
              << std::endl;
    avformat_alloc_output_context2(&m_FormatCtx, nullptr, "mp4",
                                   outputFile.c_str());
  }
  if (!m_FormatCtx)
    return false;

  const AVCodec *codec = FindHardwareCodec("h264");
  if (!codec) {
    std::cerr << "Codec not found!" << std::endl;
    return false;
  }

  m_Stream = avformat_new_stream(m_FormatCtx, nullptr);
  if (!m_Stream)
    return false;
  m_Stream->id = m_FormatCtx->nb_streams - 1;

  m_CodecCtx = avcodec_alloc_context3(codec);
  if (!m_CodecCtx)
    return false;

  m_CodecCtx->width = width;
  m_CodecCtx->height = height;
  m_CodecCtx->time_base = {1, fps};
  m_Stream->time_base = m_CodecCtx->time_base;
  m_CodecCtx->framerate = {fps, 1};

  m_CodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
  if (codec->pix_fmts) {
    bool foundNV12 = false;
    bool foundYUV420P = false;

    for (const enum AVPixelFormat *p = codec->pix_fmts; *p != -1; p++) {
      if (*p == AV_PIX_FMT_NV12)
        foundNV12 = true;
      if (*p == AV_PIX_FMT_YUV420P)
        foundYUV420P = true;
    }

    if (foundNV12) {
      m_CodecCtx->pix_fmt = AV_PIX_FMT_NV12;
    } else if (foundYUV420P) {
      m_CodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    } else {
      m_CodecCtx->pix_fmt = codec->pix_fmts[0];
    }
  }
  std::cout << "[VideoEncoder] Selected Pixel Format: "
            << av_get_pix_fmt_name(m_CodecCtx->pix_fmt) << std::endl;

  m_CodecCtx->bit_rate = bitrate;
  m_CodecCtx->gop_size = 12;
  m_CodecCtx->max_b_frames = 2;
  m_CodecCtx->profile = AV_PROFILE_H264_HIGH;

  if (m_FormatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
    m_CodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  AVDictionary *opts = nullptr;
  std::string codecNameStr = codec->name;

  if (codecNameStr.find("nvenc") != std::string::npos) {
    std::cout
        << "[VideoEncoder] Configuring NVENC for maximum GPU performance..."
        << std::endl;
    av_dict_set(&opts, "preset", "p1", 0);
    av_dict_set(&opts, "tune", "hq", 0);
    av_dict_set(&opts, "rc", "vbr", 0);
    av_dict_set(&opts, "gpu", "0", 0);
    av_dict_set(&opts, "async_depth", "2", 0);
    std::cout << "[VideoEncoder] NVENC configured: preset=p1, tune=hq, rc=vbr"
              << std::endl;
  } else if (codecNameStr.find("qsv") != std::string::npos) {
    av_dict_set(&opts, "preset", "veryfast", 0);
    std::cout << "[VideoEncoder] QuickSync configured: preset=veryfast"
              << std::endl;
  } else if (codecNameStr.find("amf") != std::string::npos) {
    av_dict_set(&opts, "quality", "speed", 0);
    std::cout << "[VideoEncoder] AMF configured: quality=speed" << std::endl;
  } else if (codecNameStr.find("x264") != std::string::npos) {
    av_dict_set(&opts, "preset", "ultrafast", 0);
    av_dict_set(&opts, "tune", "zerolatency", 0);
    std::cout
        << "[VideoEncoder] x264 configured: preset=ultrafast, tune=zerolatency"
        << std::endl;
  }

  int ret = avcodec_open2(m_CodecCtx, codec, &opts);
  if (opts)
    av_dict_free(&opts);

  if (ret < 0) {
    char errbuf[256];
    av_strerror(ret, errbuf, sizeof(errbuf));
    std::cerr << "Could not open codec! Error: " << errbuf << " (" << ret << ")"
              << std::endl;
    return false;
  }

  std::cout
      << "[VideoEncoder] Codec opened successfully with hardware acceleration"
      << std::endl;

  avcodec_parameters_from_context(m_Stream->codecpar, m_CodecCtx);

  if (!(m_FormatCtx->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&m_FormatCtx->pb, outputFile.c_str(), AVIO_FLAG_WRITE) < 0) {
      std::cerr << "Could not open output file: " << outputFile << std::endl;
      return false;
    }
  }

  if (avformat_write_header(m_FormatCtx, nullptr) < 0) {
    return false;
  }

  m_Frame = av_frame_alloc();
  m_Frame->format = m_CodecCtx->pix_fmt;
  m_Frame->width = m_CodecCtx->width;
  m_Frame->height = m_CodecCtx->height;
  if (av_frame_get_buffer(m_Frame, 32) < 0)
    return false;

  m_Packet = av_packet_alloc();

  // CRITICAL FIX: Handle OpenGL bottom-up RGB data
  // OpenGL reads pixels from bottom-left, but video expects top-left
  // Use SWS_FAST_BILINEAR for speed, add srcSliceY offset handling
  m_SwsCtx = sws_getContext(width, height, AV_PIX_FMT_RGB24, width, height,
                            m_CodecCtx->pix_fmt, SWS_FAST_BILINEAR, nullptr,
                            nullptr, nullptr);

  if (!m_SwsCtx) {
    std::cerr << "[VideoEncoder] Failed to create SwsContext!" << std::endl;
    return false;
  }

  return true;
}

bool VideoEncoder::EncodeFrame(const uint8_t *rgbData) {
  if (!m_CodecCtx || !rgbData)
    return false;

  if (av_frame_make_writable(m_Frame) < 0)
    return false;

  // 2. Convert RGB to YUV
  int srcStride[1] = {m_Width * 3};
  const uint8_t *srcSlice[1] = {rgbData};

  int result = sws_scale(m_SwsCtx, srcSlice, srcStride, 0, m_Height,
                         m_Frame->data, m_Frame->linesize);

  if (result <= 0) {
    std::cerr << "[VideoEncoder] sws_scale failed!" << std::endl;
    return false;
  }

  m_Frame->pts = m_FrameCount++;

  // 3. Encode
  int ret = avcodec_send_frame(m_CodecCtx, m_Frame);
  if (ret < 0) {
    char errbuf[256];
    av_strerror(ret, errbuf, sizeof(errbuf));
    std::cerr << "Error sending frame to encoder: " << errbuf << std::endl;
    return false;
  }

  while (ret >= 0) {
    ret = avcodec_receive_packet(m_CodecCtx, m_Packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    else if (ret < 0) {
      std::cerr << "Error receiving packet from encoder" << std::endl;
      return false;
    }

    // Rescale packet timestamp
    av_packet_rescale_ts(m_Packet, m_CodecCtx->time_base, m_Stream->time_base);
    m_Packet->stream_index = m_Stream->index;

    // Write packet
    ret = av_interleaved_write_frame(m_FormatCtx, m_Packet);
    av_packet_unref(m_Packet);
    if (ret < 0) {
      // Warning only - don't fail the whole export for one dropped frame
      // std::cerr << "Error writing frame" << std::endl;
    }
  }
  return true;
}

bool VideoEncoder::EncodeYUVFrame(AVFrame *frame) {
  if (!m_CodecCtx || !frame)
    return false;

  frame->pts = m_FrameCount++;

  // Encode
  int ret = avcodec_send_frame(m_CodecCtx, frame);
  if (ret < 0) {
    char errbuf[256];
    av_strerror(ret, errbuf, sizeof(errbuf));
    std::cerr << "Error sending frame to encoder: " << errbuf << std::endl;
    return false;
  }

  while (ret >= 0) {
    ret = avcodec_receive_packet(m_CodecCtx, m_Packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    else if (ret < 0) {
      std::cerr << "Error receiving packet from encoder" << std::endl;
      return false;
    }

    // Rescale packet timestamp
    av_packet_rescale_ts(m_Packet, m_CodecCtx->time_base, m_Stream->time_base);
    m_Packet->stream_index = m_Stream->index;

    // Write packet
    ret = av_interleaved_write_frame(m_FormatCtx, m_Packet);
    av_packet_unref(m_Packet);
  }
  return true;
}

AVPixelFormat VideoEncoder::GetPixFormat() const {
  if (m_CodecCtx) {
    return m_CodecCtx->pix_fmt;
  }
  return AV_PIX_FMT_NONE;
}

bool VideoEncoder::Finalize() {
  if (!m_CodecCtx)
    return false;

  avcodec_send_frame(m_CodecCtx, nullptr);

  while (true) {
    int ret = avcodec_receive_packet(m_CodecCtx, m_Packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    if (ret < 0)
      return false;

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