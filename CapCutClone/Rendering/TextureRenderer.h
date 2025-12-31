#pragma once

#include <glad/glad.h>

#include <vector>
#include <cstdint>

class TextureRenderer {
public:
    TextureRenderer();
    ~TextureRenderer();

    bool Initialize();
    void Cleanup();

    // Texture management
    void CreateTexture(int width, int height);
    void UpdateTexture(const uint8_t* data, int width, int height);
    void DeleteTexture();

    // Rendering
    void RenderTexture(float x, float y, float width, float height);
    void RenderOverlay(unsigned int textureID, float x, float y, float w, float h, float rotation = 0.0f, float opacity = 1.0f);

    void SetFilterParams(float brightness, float contrast, float saturation);
    void SetEffectParams(float vignette, float grain, float aberration, bool sepia);

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
    void GetRGBPixels(std::vector<uint8_t>& buffer, int width, int height);
    
    // Copy visual settings from another renderer
    void CopySettingsFrom(const TextureRenderer* other);
    
    // Config
    void SetFlipY(bool flip);
    void SetFilterType(int type); // 0 = None, 1...N = Filters
    int GetFilterType() const { return m_FilterType; }

    // Generate a thumbnail with a specific filter applied (for UI)
    // Returns the new texture ID. Caller owns the texture.
    GLuint GenerateFilterThumbnail(GLuint inputTex, int filterType, int width, int height);
    
    // Render current texture with filter to FBO and return FBO texture ID (for ImGui preview)
    GLuint GetFilteredTextureID(int width, int height);

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

    // Helper methods
    bool CompileShader(GLuint shader, const char* source);
    bool CreateShaderProgram();
    void SetupQuad();
};
