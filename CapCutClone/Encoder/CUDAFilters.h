#pragma once
#include <cstdint>

class CUDAConverter {
public:
  CUDAConverter();
  ~CUDAConverter();

  // Initialize CUDA resources
  bool Initialize(int width, int height);
  void Cleanup();

  // Convert RGB24 to NV12 on GPU
  // rgb_host: RGB24 input buffer (host memory)
  // y_plane: Y plane output (host memory)
  // uv_plane: UV plane output (host memory, interleaved U/V)
  bool ConvertRGB24ToNV12(const uint8_t *rgb_host, uint8_t *y_plane,
                          uint8_t *uv_plane, int width, int height);

  bool IsAvailable() const { return m_Initialized; }

private:
  bool m_Initialized;
  uint8_t *m_RGBDevice; // Device memory for RGB input
  int m_Width;
  int m_Height;
};
