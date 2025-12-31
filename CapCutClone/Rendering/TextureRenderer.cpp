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
uniform int filterType;    // 0=None, 1=LightGreen, 2=80s, 3=Milky, etc.
uniform float time;        // For animated grain

float rand(vec2 co){
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

// Color conversion helpers
vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
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
    
    // --- FILTERS ---
    if (filterType == 1) { // Light Green
        // Tint green, slightly faded
        texColor = mix(texColor, vec3(0.8, 1.0, 0.8) * dot(texColor, vec3(0.33)), 0.3);
        texColor *= vec3(0.9, 1.1, 0.9); 
    }
    else if (filterType == 2) { // 80s Holiday
        // Warm, slightly saturated, pinkish highlight
        texColor = texColor * vec3(1.1, 0.9, 0.9);
        texColor = mix(texColor, vec3(1.0, 0.8, 0.8), 0.1);
    }
    else if (filterType == 3) { // Milky Tone
        // Low contrast, bright, whiter
        texColor = (texColor - 0.5) * 0.8 + 0.5; // low contrast
        texColor += 0.1; // brightness
        texColor = mix(texColor, vec3(1.0), 0.1); // milky
    }
    else if (filterType == 4) { // Cinematic Dusk
        // Teal/Orange look basic
        vec3 gray = vec3(dot(texColor, vec3(0.299, 0.587, 0.114)));
        texColor = mix(gray, texColor, 1.2); // boost sat
        texColor *= vec3(0.9, 0.95, 1.1); // cool shadows
        texColor += vec3(0.1, 0.05, 0.0); // warm highlights simulation (simple)
    }
    else if (filterType == 5) { // Ice City
        // Cool blue, high contrast
        texColor = (texColor - 0.5) * 1.2 + 0.5;
        texColor *= vec3(0.8, 0.9, 1.1);
    }
    else if (filterType == 6) { // Flash CCD
        // High exposure, bloom-like
        texColor = (texColor - 0.5) * 1.3 + 0.6;
    }
    else if (filterType == 7) { // LA Classic
        // Warm, sunny, vintage
        texColor *= vec3(1.1, 1.0, 0.8);
        texColor -= 0.05;
    }
    else if (filterType == 8) { // Warlock
        // Dark, green/purple tint
        texColor = (texColor - 0.5) * 1.3 + 0.4;
        texColor *= vec3(0.9, 1.1, 0.8);
    }
    else if (filterType == 9) { // Brighten Up
        texColor += 0.15;
        texColor *= 1.1;
    }
    else if (filterType == 10) { // Hollywood Past
        // B&W high contrast
        float g = dot(texColor, vec3(0.299, 0.587, 0.114));
        texColor = vec3((g - 0.5) * 1.5 + 0.5);
    }
    else if (filterType == 11) { // Fade
        // Low saturation, raised blacks
        float g = dot(texColor, vec3(0.299, 0.587, 0.114));
        texColor = mix(vec3(g), texColor, 0.6);
        texColor = texColor * 0.8 + 0.1; // lift blacks
    }
    else if (filterType == 12) { // Maldives
        // Aqua boost
        texColor *= vec3(0.9, 1.2, 1.2);
        texColor = (texColor - 0.5) * 1.1 + 0.5;
    }
    else if (filterType == 13) { // Clear
        // Neutral clean
        texColor = (texColor - 0.5) * 1.05 + 0.5;
        texColor *= 1.05;
    }
    else if (filterType == 14) { // Azure Morning
        // Soft blue tint
        texColor = mix(texColor, vec3(0.8, 0.9, 1.0), 0.15);
        texColor *= 1.1;
    }
    else if (filterType == 15) { // Hasselblad
        // Natural, deep properties
        texColor = (texColor - 0.5) * 1.1 + 0.5;
        texColor *= vec3(1.05, 1.02, 1.0);
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
    , m_PreviewFBO(0)
    , m_PreviewTexture(0)
    , m_PreviewWidth(0)
    , m_PreviewHeight(0)
    , m_FlipY(false)
    , m_FilterType(0)
{
}

void TextureRenderer::SetFlipY(bool flip) {
    m_FlipY = flip;
}

void TextureRenderer::SetFilterType(int type) {
    m_FilterType = type;
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
    m_FilterType = other->m_FilterType;
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
}

void TextureRenderer::UnbindFramebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void TextureRenderer::GetRGBPixels(std::vector<uint8_t>& buffer, int width, int height) {
    // Caller should have already bound the FBO - don't rebind here
    // Ensure GPU rendering is complete before reading
    glFinish();
    
    // Buffer size check
    if (buffer.size() < (size_t)(width * height * 3)) {
        buffer.resize(width * height * 3);
    }
    
    // Pixel alignment
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, buffer.data());
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
    
    // Cleanup preview FBO
    if (m_PreviewFBO) { glDeleteFramebuffers(1, &m_PreviewFBO); m_PreviewFBO = 0; }
    if (m_PreviewTexture) { glDeleteTextures(1, &m_PreviewTexture); m_PreviewTexture = 0; }
    
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

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_TextureID);

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
    if ((loc = glGetUniformLocation(m_ShaderProgram, "filterType")) >= 0) glUniform1i(loc, m_FilterType);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "time")) >= 0) glUniform1f(loc, (float)glfwGetTime());

    // Dynamic projection based on render dimensions
    float pW = width > 0 ? width : 1280.0f;
    float pH = height > 0 ? height : 720.0f;
    
    // Set Viewport to match
    glViewport(0, 0, (GLsizei)pW, (GLsizei)pH);
    
    float projY = -2.0f / pH;
    float transY = 1.0f;
    
    if (m_FlipY) {
        projY = 2.0f / pH;  // Invert Y scale
        transY = -1.0f;     // Move origin to -1
    }

    float projection[16] = {
        2.0f / pW, 0.0f, 0.0f, 0.0f,
        0.0f, projY, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, 0.0f,
        -1.0f, transY, 0.0f, 1.0f
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

GLuint TextureRenderer::GenerateFilterThumbnail(GLuint inputTex, int filterType, int width, int height) {
    if (!m_Initialized || !inputTex) return 0;
    
    // Save current state
    GLint oldViewport[4];
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    GLint oldFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
    
    GLuint fbo, tex; // Temporary FBO
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[TextureRenderer] Failed to create FBO for filter thumbnail!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
        glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        return 0;
    }
    
    // Set viewport and clear
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glUseProgram(m_ShaderProgram);
    
    // Set Uniforms - Only enable the specific filter loop
    GLint loc;
    if ((loc = glGetUniformLocation(m_ShaderProgram, "alpha")) >= 0) glUniform1f(loc, 1.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "brightness")) >= 0) glUniform1f(loc, 0.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "contrast")) >= 0) glUniform1f(loc, 1.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "saturation")) >= 0) glUniform1f(loc, 1.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "vignette")) >= 0) glUniform1f(loc, 0.0f); // Default values
    if ((loc = glGetUniformLocation(m_ShaderProgram, "grain")) >= 0) glUniform1f(loc, 0.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "aberration")) >= 0) glUniform1f(loc, 0.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "sepia")) >= 0) glUniform1i(loc, 0);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "filterType")) >= 0) glUniform1i(loc, filterType);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "time")) >= 0) glUniform1f(loc, 0.0f);
    
    // Standard projection (Non-flipped for FBO internal storage usually)
    float projection[16] = {
        2.0f / width, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / height, 0.0f, 0.0f, // Positive Y because texture coords will match
        0.0f, 0.0f, -1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f
    };
    if ((loc = glGetUniformLocation(m_ShaderProgram, "projection")) >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, projection);

    // Full screen quad
    float vertices[] = {
        0.0f, 0.0f,          0.0f, 0.0f, // Use standard UVs
        (float)width, 0.0f,  1.0f, 0.0f,
        (float)width, (float)height, 1.0f, 1.0f,
        0.0f, (float)height, 0.0f, 1.0f
    };
    
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTex);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "texture1")) >= 0) glUniform1i(loc, 0);
    
    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    
    // Restore state
    glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    glDeleteFramebuffers(1, &fbo);
   
    return tex;
}

GLuint TextureRenderer::GetFilteredTextureID(int width, int height) {
    if (!m_Initialized || !m_TextureID) return m_TextureID; // Fallback to original
    
    // Recreate if size changed or FBO doesn't exist
    if (!m_PreviewFBO || m_PreviewWidth != width || m_PreviewHeight != height) {
        if (m_PreviewFBO) {
            glDeleteFramebuffers(1, &m_PreviewFBO);
            glDeleteTextures(1, &m_PreviewTexture);
            m_PreviewFBO = 0;
            m_PreviewTexture = 0;
        }
        
        glGenFramebuffers(1, &m_PreviewFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_PreviewFBO);
        
        glGenTextures(1, &m_PreviewTexture);
        glBindTexture(GL_TEXTURE_2D, m_PreviewTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_PreviewTexture, 0);
        
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "[TextureRenderer] Failed to create preview FBO" << std::endl;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return m_TextureID;
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_PreviewWidth = width;
        m_PreviewHeight = height;
    }
    
    // Save current OpenGL state
    GLint oldViewport[4];
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    GLint oldFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
    
    // Bind our preview FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_PreviewFBO);
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Render texture with filter
    glUseProgram(m_ShaderProgram);
    
    // Set all uniforms with current filter settings
    GLint loc;
    if ((loc = glGetUniformLocation(m_ShaderProgram, "alpha")) >= 0) glUniform1f(loc, 1.0f);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "brightness")) >= 0) glUniform1f(loc, m_Brightness);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "contrast")) >= 0) glUniform1f(loc, m_Contrast);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "saturation")) >= 0) glUniform1f(loc, m_Saturation);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "vignette")) >= 0) glUniform1f(loc, m_Vignette);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "grain")) >= 0) glUniform1f(loc, m_Grain);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "aberration")) >= 0) glUniform1f(loc, m_Aberration);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "sepia")) >= 0) glUniform1i(loc, m_Sepia ? 1 : 0);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "filterType")) >= 0) glUniform1i(loc, m_FilterType);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "time")) >= 0) glUniform1f(loc, (float)glfwGetTime());
    
    // Projection matrix for FBO
    float projection[16] = {
        2.0f / width, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / height, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f
    };
    if ((loc = glGetUniformLocation(m_ShaderProgram, "projection")) >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, projection);
    
    // Full screen quad
    float vertices[] = {
        0.0f, 0.0f,          0.0f, 0.0f,
        (float)width, 0.0f,  1.0f, 0.0f,
        (float)width, (float)height, 1.0f, 1.0f,
        0.0f, (float)height, 0.0f, 1.0f
    };
    
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_TextureID);
    if ((loc = glGetUniformLocation(m_ShaderProgram, "texture1")) >= 0) glUniform1i(loc, 0);
    
    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    
    // Restore state
    glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    
    return m_PreviewTexture;
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
    if ((loc = glGetUniformLocation(m_ShaderProgram, "filterType")) >= 0) glUniform1i(loc, 0); // No filter for overlays

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
