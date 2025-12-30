#pragma once

#include <glad/glad.h>
#include <string>

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

private:
    GLuint m_TextureID;
    GLuint m_ShaderProgram;
    GLuint m_VAO;
    GLuint m_VBO;
    GLuint m_EBO;
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
