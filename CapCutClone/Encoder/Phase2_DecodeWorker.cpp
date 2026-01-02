// ============================================================================
// Phase 2: Decode Worker Thread
// ============================================================================

void HardwareExportManager::DecodeWorkerFunc() {
  std::cout << "[DecodeWorker-" << std::this_thread::get_id() << "] Started"
            << std::endl;

  VideoPlayer decoder;
  std::string currentFile = "";

  while (m_DecodeWorkersRunning) {
    DecodeJob job;

    // Wait for decode job
    {
      std::unique_lock<std::mutex> lock(m_DecodeJobMutex);
      m_DecodeJobCondVar.wait(lock, [this] {
        return !m_DecodeJobQueue.empty() || !m_DecodeWorkersRunning;
      });

      if (!m_DecodeWorkersRunning)
        break;

      if (m_DecodeJobQueue.empty())
        continue;

      job = m_DecodeJobQueue.front();
      m_DecodeJobQueue.pop();
    }

    if (job.isStopSignal)
      break;

    // Load video if needed
    if (currentFile != job.filepath) {
      if (!decoder.LoadVideo(job.filepath)) {
        std::cerr << "[DecodeWorker] Failed to load: " << job.filepath
                  << std::endl;
        continue;
      }
      currentFile = job.filepath;
    }

    // Seek and decode
    decoder.Seek(job.localTime, false);

    // Decode to exact time
    double videoFPS = decoder.GetFPS() > 0 ? decoder.GetFPS() : 30.0;
    double frameDuration = 1.0 / videoFPS;

    int attempts = 0;
    while (decoder.GetCurrentTime() + frameDuration < job.localTime &&
           attempts < 10) {
      if (!decoder.DecodeNextFrame())
        break;
      attempts++;
    }

    // Get RGB data
    const uint8_t *data = decoder.GetFrameData();
    if (data) {
      DecodedFrame result;
      result.frameIndex = job.frameIndex;
      result.width = decoder.GetWidth();
      result.height = decoder.GetHeight();

      size_t dataSize = result.width * result.height * 3;
      result.rgbData.resize(dataSize);
      memcpy(result.rgbData.data(), data, dataSize);
      result.valid = true;

      // Store result
      {
        std::lock_guard<std::mutex> lock(m_DecodedFramesMutex);
        m_DecodedFrames[job.frameIndex] = std::move(result);
      }
    }
  }

  std::cout << "[DecodeWorker-" << std::this_thread::get_id() << "] Finished"
            << std::endl;
}
