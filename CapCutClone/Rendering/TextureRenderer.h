#pragma once

#include <glad/glad.h>

#include <cstdint>
#include <vector>


class TextureRenderer {
public:
  TextureRenderer();
  ~TextureRenderer();

  bool Initialize();
  void Cleanup();

  // Texture management
  void CreateTexture(int width, int height);
  void UpdateTexture(const uint8_t *data, int width, int height);
  void DeleteTexture();

  // Rendering
  void RenderTexture(float x, float y, float width, float height);
  void RenderOverlay(unsigned int textureID, float x, float y, float w, float h,
                     float rotation = 0.0f, float opacity = 1.0f);

  void SetFilterParams(float brightness, float contrast, float saturation);
  void SetEffectParams(float vignette, float grain, float aberration,
                       bool sepia);

  // Advanced Effects (CapCut-like)
  void
  SetBlurEffect(float amount,
                int type = 0); // type: 0=Gaussian, 1=Motion, 2=Radial, 3=Zoom
  void SetGlitchEffect(float intensity);
  void SetRippleEffect(float frequency, float amplitude);
  void SetDistortionEffect(float amount);
  void SetEdgeGlowEffect(float intensity, float r, float g, float b);
  void SetFadeEffect(float amount); // 0=none, <0.5=fade in, >0.5=fade out
  void SetZoomEffect(float amount);
  void SetLightLeakEffect(float intensity);

  // Effect Getters
  float GetBlurAmount() const { return m_BlurAmount; }
  int GetBlurType() const { return m_BlurType; }
  float GetGlitchIntensity() const { return m_GlitchIntensity; }
  float GetRippleFreq() const { return m_RippleFreq; }
  float GetRippleAmp() const { return m_RippleAmp; }
  float GetDistortion() const { return m_Distortion; }
  float GetEdgeGlowIntensity() const { return m_EdgeGlowIntensity; }
  float GetFadeAmount() const { return m_FadeAmount; }
  float GetZoomAmount() const { return m_ZoomAmount; }
  float GetLightLeakIntensity() const { return m_LightLeakIntensity; }

  GLuint GetTextureID() const { return m_TextureID; }
  bool IsInitialized() const { return m_Initialized; }

  // Getters for Effect Params
  float GetBrightness() const { return m_Brightness; }
  float GetContrast() const { return m_Contrast; }
  float GetSaturation() const { return m_Saturation; }
  float GetVignette() const { return m_Vignette; }
  float GetGrain() const { return m_Grain; }
  float GetAberration() const { return m_Aberration; }
  bool GetSepia() const { return m_Sepia; }

  // Offscreen Rendering (FBO)
  bool CreateFramebuffer(int width, int height);
  void BindFramebuffer();
  void UnbindFramebuffer();
  void GetRGBPixels(std::vector<uint8_t> &buffer, int width, int height);

  // Copy visual settings from another renderer
  void CopySettingsFrom(const TextureRenderer *other);

  // Config
  void SetFlipY(bool flip);
  void SetFilterType(int type); // 0 = None, 1...N = Filters
  int GetFilterType() const { return m_FilterType; }

  // Generate a thumbnail with a specific filter applied (for UI)
  // Returns the new texture ID. Caller owns the texture.
  GLuint GenerateFilterThumbnail(GLuint inputTex, int filterType, int width,
                                 int height);

  // Render current texture with filter to FBO and return FBO texture ID (for
  // ImGui preview)
  GLuint GetFilteredTextureID(int width, int height);

  // YUV Export Support (Phase 1 optimization)
  struct YUVFramebuffer {
    GLuint yFBO;      // Framebuffer for Y plane
    GLuint yTexture;  // Y plane texture (full resolution)
    GLuint uvFBO;     // Framebuffer for UV plane
    GLuint uvTexture; // UV plane texture (half resolution)
    int width;
    int height;
  };

  bool CreateYUVFramebuffer(int width, int height);
  void DestroyYUVFramebuffer();
  void RenderToYUV(); // Render current RGB texture to YUV FBOs
  bool ReadYUVToAVFrame(struct AVFrame *frame); // Read YUV planes to AVFrame

private:
  GLuint m_TextureID;
  GLuint m_ShaderProgram;
  GLuint m_VAO;
  GLuint m_VBO;
  GLuint m_EBO;

  // FBO
  GLuint m_FBO;
  GLuint m_FBOTexture;
  GLuint m_RBO;

  // Preview FBO (separate from export FBO to avoid conflicts)
  GLuint m_PreviewFBO;
  GLuint m_PreviewTexture;
  int m_PreviewWidth;
  int m_PreviewHeight;

  bool m_FlipY;
  int m_FilterType;

  // PBO for async readback (Double buffering)
  GLuint m_PBO[2];
  int m_PBOIndex;
  int m_PBONextIndex;

  // YUV Export
  YUVFramebuffer m_YUVFbo;
  GLuint m_YUVShaderProgram; // Shader for RGB→YUV conversion
  GLuint m_YUVShaderY;       // Y plane extraction shader
  GLuint m_YUVShaderUV;      // UV plane extraction shader

  bool m_Initialized;

  // Filter Params
  float m_Brightness;
  float m_Contrast;
  float m_Saturation;

  // Effect Params
  float m_Vignette;
  float m_Grain;
  float m_Aberration;
  bool m_Sepia;

  // Advanced Effect Params
  float m_BlurAmount;
  int m_BlurType; // 0=Gaussian, 1=Motion, 2=Radial, 3=Zoom
  float m_GlitchIntensity;
  float m_RippleFreq;
  float m_RippleAmp;
  float m_Distortion;
  float m_EdgeGlowIntensity;
  float m_EdgeGlowColor[3]; // RGB
  float m_FadeAmount;
  float m_ZoomAmount;
  float m_LightLeakIntensity;

  // Helper methods
  bool CompileShader(GLuint shader, const char *source);
  bool CreateShaderProgram();
  void SetupQuad();
  bool CreateYUVShaders(); // Compile RGB→YUV shaders
};
