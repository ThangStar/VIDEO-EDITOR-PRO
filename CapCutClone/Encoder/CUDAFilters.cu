#include "CUDAFilters.h"
#include <cuda_runtime.h>
#include <iostream>

// CUDA kernel for RGB24 to NV12 conversion
__global__ void RGB24ToNV12Kernel(const uint8_t *__restrict__ rgb,
                                  uint8_t *__restrict__ y_plane,
                                  uint8_t *__restrict__ uv_plane, int width,
                                  int height) {
  int x = blockIdx.x * blockDim.x + threadIdx.x;
  int y = blockIdx.y * blockDim.y + threadIdx.y;

  if (x >= width || y >= height)
    return;

  // BT.709 coefficients for HD video
  const float Wr = 0.2126f;
  const float Wg = 0.7152f;
  const float Wb = 0.0722f;

  int rgbIdx = (y * width + x) * 3;
  float r = rgb[rgbIdx] / 255.0f;
  float g = rgb[rgbIdx + 1] / 255.0f;
  float b = rgb[rgbIdx + 2] / 255.0f;

  // Compute Y (luma)
  float Y = Wr * r + Wg * g + Wb * b;
  y_plane[y * width + x] =
      (uint8_t)fminf(fmaxf(16.0f + 219.0f * Y, 16.0f), 235.0f);

  // Compute UV (chroma) with 4:2:0 subsampling
  if ((x % 2 == 0) && (y % 2 == 0)) {
    float U = (b - Y) / (2.0f * (1.0f - Wb));
    float V = (r - Y) / (2.0f * (1.0f - Wr));

    int uvIdx = (y / 2) * width + x;
    uv_plane[uvIdx] =
        (uint8_t)fminf(fmaxf(128.0f + 224.0f * U, 16.0f), 240.0f); // U
    uv_plane[uvIdx + 1] =
        (uint8_t)fminf(fmaxf(128.0f + 224.0f * V, 16.0f), 240.0f); // V
  }
}

CUDAConverter::CUDAConverter()
    : m_Initialized(false), m_RGBDevice(nullptr), m_Width(0), m_Height(0) {}

CUDAConverter::~CUDAConverter() { Cleanup(); }

bool CUDAConverter::Initialize(int width, int height) {
  // Reset any previous CUDA state
  cudaDeviceReset();

  // Initialize CUDA device
  int deviceCount = 0;
  cudaError_t err = cudaGetDeviceCount(&deviceCount);

  std::cout << "[CUDAConverter] CUDA initialization debug:" << std::endl;
  std::cout << "  cudaGetDeviceCount returned: " << cudaGetErrorString(err)
            << std::endl;
  std::cout << "  Device count: " << deviceCount << std::endl;

  if (err != cudaSuccess || deviceCount == 0) {
    std::cerr << "[CUDAConverter] No CUDA device found: "
              << cudaGetErrorString(err) << std::endl;
    std::cerr << "[CUDAConverter] Device count: " << deviceCount << std::endl;

    // Try to get more info
    int driverVersion = 0, runtimeVersion = 0;
    cudaDriverGetVersion(&driverVersion);
    cudaRuntimeGetVersion(&runtimeVersion);
    std::cerr << "[CUDAConverter] Driver: " << driverVersion
              << ", Runtime: " << runtimeVersion << std::endl;
    return false;
  }

  // Use first device
  err = cudaSetDevice(0);
  if (err != cudaSuccess) {
    std::cerr << "[CUDAConverter] Failed to set CUDA device: "
              << cudaGetErrorString(err) << std::endl;
    return false;
  }

  // Get device properties
  cudaDeviceProp prop;
  cudaGetDeviceProperties(&prop, 0);

  // Check available memory
  size_t freeMem, totalMem;
  cudaMemGetInfo(&freeMem, &totalMem);

  m_Width = width;
  m_Height = height;

  // No pre-allocation needed - we'll allocate temporarily
  m_Initialized = true;
  std::cout << "[CUDAConverter] Initialized for " << width << "x" << height
            << " (on-demand GPU memory)" << std::endl;
  std::cout << "[CUDAConverter] GPU: " << prop.name
            << ", Free: " << (freeMem / 1024 / 1024) << " MB"
            << ", Total: " << (totalMem / 1024 / 1024) << " MB" << std::endl;
  return true;
}

void CUDAConverter::Cleanup() {
  // No persistent buffers to clean up
  m_Initialized = false;
}

bool CUDAConverter::ConvertRGB24ToNV12(const uint8_t *rgb_host,
                                       uint8_t *y_plane, uint8_t *uv_plane,
                                       int width, int height) {
  if (!m_Initialized) {
    std::cerr << "[CUDAConverter] Not initialized!" << std::endl;
    return false;
  }

  // Allocate temporary RGB device memory
  uint8_t *rgb_device = nullptr;
  size_t rgbSize = width * height * 3;

  // Check available memory before allocation
  size_t freeMem, totalMem;
  cudaMemGetInfo(&freeMem, &totalMem);

  if (rgbSize > freeMem) {
    std::cerr << "[CUDAConverter] Insufficient GPU memory: need "
              << (rgbSize / 1024 / 1024) << " MB, available "
              << (freeMem / 1024 / 1024) << " MB" << std::endl;
    return false;
  }

  // LOG: Confirm CUDA is being used
  static int frameCount = 0;
  if (frameCount++ % 30 == 0) {
    std::cout << "[CUDA] GPU conversion active (frame " << frameCount << ")"
              << std::endl;
  }

  cudaError_t err = cudaMalloc(&rgb_device, rgbSize);
  if (err != cudaSuccess) {
    std::cerr << "[CUDAConverter] Temp RGB allocation failed ("
              << (rgbSize / 1024 / 1024) << " MB): " << cudaGetErrorString(err)
              << std::endl;
    std::cerr << "[CUDAConverter] GPU mem: " << (freeMem / 1024 / 1024)
              << " MB free / " << (totalMem / 1024 / 1024) << " MB total"
              << std::endl;
    return false;
  }

  // Copy RGB from host to device
  err = cudaMemcpy(rgb_device, rgb_host, rgbSize, cudaMemcpyHostToDevice);
  if (err != cudaSuccess) {
    std::cerr << "[CUDAConverter] RGB memcpy failed: "
              << cudaGetErrorString(err) << std::endl;
    cudaFree(rgb_device);
    return false;
  }

  // Allocate device memory for output
  uint8_t *y_device, *uv_device;
  size_t ySize = width * height;
  size_t uvSize = width * height / 2; // NV12: UV is half size

  err = cudaMalloc(&y_device, ySize);
  if (err != cudaSuccess) {
    std::cerr << "[CUDAConverter] Y plane allocation failed" << std::endl;
    return false;
  }

  err = cudaMalloc(&uv_device, uvSize);
  if (err != cudaSuccess) {
    cudaFree(y_device);
    std::cerr << "[CUDAConverter] UV plane allocation failed" << std::endl;
    return false;
  }

  // Launch kernel
  dim3 blockSize(16, 16);
  dim3 gridSize((width + blockSize.x - 1) / blockSize.x,
                (height + blockSize.y - 1) / blockSize.y);

  RGB24ToNV12Kernel<<<gridSize, blockSize>>>(rgb_device, y_device, uv_device,
                                             width, height);

  // Check for kernel errors
  err = cudaGetLastError();
  if (err != cudaSuccess) {
    std::cerr << "[CUDAConverter] Kernel launch failed: "
              << cudaGetErrorString(err) << std::endl;
    cudaFree(rgb_device);
    cudaFree(y_device);
    cudaFree(uv_device);
    return false;
  }

  // Wait for kernel to finish
  cudaDeviceSynchronize();

  // Copy results back to host
  err = cudaMemcpy(y_plane, y_device, ySize, cudaMemcpyDeviceToHost);
  if (err != cudaSuccess) {
    std::cerr << "[CUDAConverter] Y plane copy failed" << std::endl;
    cudaFree(y_device);
    cudaFree(uv_device);
    return false;
  }

  err = cudaMemcpy(uv_plane, uv_device, uvSize, cudaMemcpyDeviceToHost);
  if (err != cudaSuccess) {
    std::cerr << "[CUDAConverter] UV plane copy failed" << std::endl;
    cudaFree(y_device);
    cudaFree(uv_device);
    return false;
  }

  // Cleanup
  cudaFree(rgb_device);
  cudaFree(y_device);
  cudaFree(uv_device);

  return true;
}
