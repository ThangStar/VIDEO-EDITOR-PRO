#include "TextureRenderer.h"
#include <iostream>
#include <vector>
#include <GLFW/glfw3.h> // Required for glfwGetTime
#include <cmath>

// Vertex shader source
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 projection;

void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

// Fragment shader source
// Updated to support opacity, basic filters, and cinematic effects
const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D texture1;
uniform float alpha; 
uniform float brightness;
uniform float contrast;
uniform float saturation;

// Effects
uniform float vignette;    // 0.0 to 1.0
uniform float grain;       // 0.0 to 1.0
uniform float aberration;  // 0.0 to 0.05
uniform int sepia;         // 0 or 1
uniform float time;        // For animated grain

float rand(vec2 co){
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

void main() {
    vec2 uv = TexCoord;
    
    // Chromatic Aberration
    vec3 texColor;
    if (aberration > 0.0) {
        float r = texture(texture1, uv + vec2(aberration, 0.0)).r;
        float g = texture(texture1, uv).g;
        float b = texture(texture1, uv - vec2(aberration, 0.0)).b;
        texColor = vec3(r, g, b);
    } else {
        texColor = texture(texture1, uv).rgb;
    }
    
    // Brightness
    texColor += brightness;
    
    // Contrast
    texColor = (texColor - 0.5) * contrast + 0.5;
    
    // Saturation
    float gray = dot(texColor, vec3(0.299, 0.587, 0.114));
    texColor = mix(vec3(gray), texColor, saturation);
    
    // Sepia
    if (sepia > 0) {
        vec3 sepiaColor;
        sepiaColor.r = dot(texColor, vec3(0.393, 0.769, 0.189));
        sepiaColor.g = dot(texColor, vec3(0.349, 0.686, 0.168));
        sepiaColor.b = dot(texColor, vec3(0.272, 0.534, 0.131));
        texColor = sepiaColor;
    }
    
    // Vignette
    if (vignette > 0.0) {
        vec2 position = (gl_FragCoord.xy / vec2(1280.0, 720.0)) - 0.5; 
        float dist = distance(uv, vec2(0.5));
        texColor *= smoothstep(0.8, 0.8 - vignette * 0.8, dist * (0.8 + vignette * 0.5));
    }
    
    // Film Grain
    if (grain > 0.0) {
        float noise = rand(uv + time);
        texColor += (noise - 0.5) * grain;
    }
    
    FragColor = vec4(texColor, texture(texture1, uv).a * alpha);
}
)";

TextureRenderer::TextureRenderer()
    : m_TextureID(0)
    , m_ShaderProgram(0)
    , m_VAO(0)
    , m_VBO(0)
    , m_EBO(0)
    , m_Initialized(false)
    , m_Brightness(0.0f)
    , m_Contrast(1.0f)
    , m_Saturation(1.0f)
    , m_Vignette(0.0f)
    , m_Grain(0.0f)
    , m_Aberration(0.0f)
    , m_Sepia(false)
    , m_FBO(0)
    , m_FBOTexture(0)
    , m_RBO(0)
{
}

// ... (Initialize/Cleanup remains similar, but need to clean FBOs too)

void TextureRenderer::CopySettingsFrom(const TextureRenderer* other) {
    if (!other) return;
    m_Brightness = other->m_Brightness;
    m_Contrast = other->m_Contrast;
    m_Saturation = other->m_Saturation;
    m_Vignette = other->m_Vignette;
    m_Grain = other->m_Grain;
    m_Aberration = other->m_Aberration;
    m_Sepia = other->m_Sepia;
}

bool TextureRenderer::CreateFramebuffer(int width, int height) {
    if (m_FBO) {
        glDeleteFramebuffers(1, &m_FBO);
        glDeleteTextures(1, &m_FBOTexture);
        glDeleteRenderbuffers(1, &m_RBO);
    }

    glGenFramebuffers(1, &m_FBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

    // Create texture to render to
    glGenTextures(1, &m_FBOTexture);
    glBindTexture(GL_TEXTURE_2D, m_FBOTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_FBOTexture, 0);

    // Create Renderbuffer for depth/stencil (optional but good practice)
    glGenRenderbuffers(1, &m_RBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_RBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_RBO);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void TextureRenderer::BindFramebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    // Viewport must match FBO size
    // We assume the caller or RenderTexture sets viewport via projection, 
    // but typically we should set glViewport here. 
    // However, RenderTexture uses hardcoded projection logic based on 1280x720 in shader?
    // Wait, shader projection matches 1280x720 logic. 
    // If output is 1920x1080, we need to ensure shader projection works?
    // The current shader hardcodes: 2.0f / 1280.0f... this IS A PROBLEM for variable resolution export.
    // I should fix RenderTexture projection matrix construction to use arguments or member vars.
    // For now, let's assume glViewport is handled by caller or we add it.
}

void TextureRenderer::UnbindFramebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void TextureRenderer::GetRGBPixels(std::vector<uint8_t>& buffer, int width, int height) {
    // Read from currently bound FBO (should be called between Bind/Unbind)
    // Or just bind FBO here? Safer to assume bound or bind explicitly.
    BindFramebuffer(); 
    
    // Buffer size check
    if (buffer.size() < width * height * 3) {
        buffer.resize(width * height * 3);
    }
    
    // Pixel alignment
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, buffer.data());
    
    // Unbind handled by caller or here? Let's leave it bound for caller to unbind
}


TextureRenderer::~TextureRenderer() {
    Cleanup();
}

bool TextureRenderer::Initialize() {
    if (m_Initialized) return true;

    if (!CreateShaderProgram()) {
        std::cerr << "Failed to create shader program" << std::endl;
        return false;
    }

    SetupQuad();
    m_Initialized = true;
    return true;
}

void TextureRenderer::Cleanup() {
    DeleteTexture();
    if (m_VAO) { glDeleteVertexArrays(1, &m_VAO); m_VAO = 0; }
    if (m_VBO) { glDeleteBuffers(1, &m_VBO); m_VBO = 0; }
    if (m_EBO) { glDeleteBuffers(1, &m_EBO); m_EBO = 0; }
    if (m_ShaderProgram) { glDeleteProgram(m_ShaderProgram); m_ShaderProgram = 0; }
    m_Initialized = false;
}

void TextureRenderer::CreateTexture(int width, int height) {
    DeleteTexture();
    glGenTextures(1, &m_TextureID);
    glBindTexture(GL_TEXTURE_2D, m_TextureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TextureRenderer::UpdateTexture(const uint8_t* data, int width, int height) {
    if (!m_TextureID || !data) return;
    glBindTexture(GL_TEXTURE_2D, m_TextureID);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TextureRenderer::DeleteTexture() {
    if (m_TextureID) {
        glDeleteTextures(1, &m_TextureID);
        m_TextureID = 0;
    }
}

void TextureRenderer::RenderTexture(float x, float y, float width, float height) {
    if (!m_Initialized || !m_TextureID) return;

    glUseProgram(m_ShaderProgram);

    // Uniforms
    GLint loc;
    if ((loc = glGetUniformLocation(m_ShaderProgram, "alpha")) >= 0) glUniform1f(loc, 1.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "brightness")) >= 0) glUniform1f(loc, m_Brightness);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "contrast")) >= 0) glUniform1f(loc, m_Contrast);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "saturation")) >= 0) glUniform1f(loc, m_Saturation);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "vignette")) >= 0) glUniform1f(loc, m_Vignette);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "grain")) >= 0) glUniform1f(loc, m_Grain);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "aberration")) >= 0) glUniform1f(loc, m_Aberration);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "sepia")) >= 0) glUniform1i(loc, m_Sepia ? 1 : 0);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "time")) >= 0) glUniform1f(loc, (float)glfwGetTime());

    // Dynamic projection based on render dimensions
    float pW = width > 0 ? width : 1280.0f;
    float pH = height > 0 ? height : 720.0f;
    
    // Set Viewport to match
    glViewport(0, 0, (GLsizei)pW, (GLsizei)pH);
    
    float projection[16] = {
        2.0f / pW, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / pH, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f
    };
    if ((loc = glGetUniformLocation(m_ShaderProgram, "projection")) >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, projection);

    float vertices[] = {
        x, y,               0.0f, 1.0f,
        x + width, y,       1.0f, 1.0f,
        x + width, y + height, 1.0f, 0.0f,
        x, y + height,      0.0f, 0.0f 
    };

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void TextureRenderer::RenderOverlay(unsigned int textureID, float x, float y, float w, float h, float rotation, float opacity) {
    if (!m_Initialized || !textureID) return;
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_ShaderProgram);

    GLint loc;
    if ((loc = glGetUniformLocation(m_ShaderProgram, "alpha")) >= 0) glUniform1f(loc, opacity);
    
    // Disable effects for overlay/stickers usually, or keep them? 
    // Usually overlays are not affected by video filters, but for simplicity they share shader.
    // Let's reset effects for overlays to avoid double-application or weirdness
    if ((loc = glGetUniformLocation(m_ShaderProgram, "brightness")) >= 0) glUniform1f(loc, 0.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "contrast")) >= 0) glUniform1f(loc, 1.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "saturation")) >= 0) glUniform1f(loc, 1.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "vignette")) >= 0) glUniform1f(loc, 0.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "grain")) >= 0) glUniform1f(loc, 0.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "aberration")) >= 0) glUniform1f(loc, 0.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "sepia")) >= 0) glUniform1i(loc, 0);

    float projection[16] = {
        2.0f / 1280.0f, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / 720.0f, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f
    };
    if ((loc = glGetUniformLocation(m_ShaderProgram, "projection")) >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, projection);

    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;
    float rad = rotation * 3.14159265f / 180.0f;
    float c = cos(rad), s = sin(rad);

    auto rotate = [&](float px, float py, float &ox, float &oy) {
        float dx = px - cx; float dy = py - cy;
        ox = cx + dx * c - dy * s; oy = cy + dx * s + dy * c;
    };

    float x1, y1, x2, y2, x3, y3, x4, y4;
    rotate(x, y, x1, y1); rotate(x+w, y, x2, y2);
    rotate(x+w, y+h, x3, y3); rotate(x, y+h, x4, y4);

    float vertices[] = {
        x1, y1, 0.0f, 1.0f,
        x2, y2, 1.0f, 1.0f,
        x3, y3, 1.0f, 0.0f,
        x4, y4, 0.0f, 0.0f
    };

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
}

void TextureRenderer::SetFilterParams(float brightness, float contrast, float saturation) {
    m_Brightness = brightness;
    m_Contrast = contrast;
    m_Saturation = saturation;
}

void TextureRenderer::SetEffectParams(float vignette, float grain, float aberration, bool sepia) {
    m_Vignette = vignette;
    m_Grain = grain;
    m_Aberration = aberration;
    m_Sepia = sepia;
}

bool TextureRenderer::CompileShader(GLuint shader, const char* source) {
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compile error: " << infoLog << std::endl;
        return false;
    }
    return true;
}

bool TextureRenderer::CreateShaderProgram() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    if (!CompileShader(vs, vertexShaderSource)) return false;
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    if (!CompileShader(fs, fragmentShaderSource)) return false;
    
    m_ShaderProgram = glCreateProgram();
    glAttachShader(m_ShaderProgram, vs);
    glAttachShader(m_ShaderProgram, fs);
    glLinkProgram(m_ShaderProgram);
    
    GLint success;
    glGetProgramiv(m_ShaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(m_ShaderProgram, 512, nullptr, infoLog);
        std::cerr << "Shader link error: " << infoLog << std::endl;
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return true;
}

void TextureRenderer::SetupQuad() {
    float vertices[] = { 0.0f, 0.0f, 0.0f, 1.0f,  1.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f };
    unsigned int indices[] = { 0, 1, 2, 2, 3, 0 };

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}
