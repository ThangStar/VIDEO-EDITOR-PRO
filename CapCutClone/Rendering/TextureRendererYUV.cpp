#include "TextureRenderer.h"
#include <fstream>
#include <iostream>
#include <sstream>

extern "C" {
#include <libavutil/frame.h>
}

// ============================================================================
// YUV Shader Creation
// ============================================================================

bool TextureRenderer::CreateYUVShaders() {
  // Load and compile vertex shader
  std::ifstream vertFile("../../CapCutClone/Shaders/rgb_to_yuv.vert");
  if (!vertFile.is_open()) {
    std::cerr << "[TextureRenderer] Failed to open rgb_to_yuv.vert"
              << std::endl;
    return false;
  }
  std::stringstream vertBuffer;
  vertBuffer << vertFile.rdbuf();
  std::string vertSource = vertBuffer.str();
  const char *vertSrc = vertSource.c_str();

  GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertShader, 1, &vertSrc, nullptr);
  glCompileShader(vertShader);

  GLint success;
  glGetShaderiv(vertShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetShaderInfoLog(vertShader, 512, nullptr, infoLog);
    std::cerr << "[TextureRenderer] YUV Vertex shader compilation failed:\n"
              << infoLog << std::endl;
    return false;
  }

  // Load and compile Y fragment shader
  std::ifstream fragYFile("../../CapCutClone/Shaders/rgb_to_y.frag");
  if (!fragYFile.is_open()) {
    std::cerr << "[TextureRenderer] Failed to open rgb_to_y.frag" << std::endl;
    glDeleteShader(vertShader);
    return false;
  }
  std::stringstream fragYBuffer;
  fragYBuffer << fragYFile.rdbuf();
  std::string fragYSource = fragYBuffer.str();
  const char *fragYSrc = fragYSource.c_str();

  m_YUVShaderY = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(m_YUVShaderY, 1, &fragYSrc, nullptr);
  glCompileShader(m_YUVShaderY);

  glGetShaderiv(m_YUVShaderY, GL_COMPILE_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetShaderInfoLog(m_YUVShaderY, 512, nullptr, infoLog);
    std::cerr << "[TextureRenderer] Y shader compilation failed:\n"
              << infoLog << std::endl;
    glDeleteShader(vertShader);
    return false;
  }

  // Load and compile UV fragment shader
  std::ifstream fragUVFile("../../CapCutClone/Shaders/rgb_to_uv.frag");
  if (!fragUVFile.is_open()) {
    std::cerr << "[TextureRenderer] Failed to open rgb_to_uv.frag" << std::endl;
    glDeleteShader(vertShader);
    glDeleteShader(m_YUVShaderY);
    return false;
  }
  std::stringstream fragUVBuffer;
  fragUVBuffer << fragUVFile.rdbuf();
  std::string fragUVSource = fragUVBuffer.str();
  const char *fragUVSrc = fragUVSource.c_str();

  m_YUVShaderUV = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(m_YUVShaderUV, 1, &fragUVSrc, nullptr);
  glCompileShader(m_YUVShaderUV);

  glGetShaderiv(m_YUVShaderUV, GL_COMPILE_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetShaderInfoLog(m_YUVShaderUV, 512, nullptr, infoLog);
    std::cerr << "[TextureRenderer] UV shader compilation failed:\n"
              << infoLog << std::endl;
    glDeleteShader(vertShader);
    glDeleteShader(m_YUVShaderY);
    return false;
  }

  // Create shader program (will be used with different fragment shaders)
  m_YUVShaderProgram = glCreateProgram();
  glAttachShader(m_YUVShaderProgram, vertShader);
  // Note: We'll attach Y or UV shader dynamically when rendering

  glDeleteShader(vertShader);

  std::cout << "[TextureRenderer] YUV shaders compiled successfully"
            << std::endl;
  return true;
}

// ============================================================================
// YUV Framebuffer Creation
// ============================================================================

bool TextureRenderer::CreateYUVFramebuffer(int width, int height) {
  m_YUVFbo.width = width;
  m_YUVFbo.height = height;

  // Create Y plane FBO (full resolution)
  glGenFramebuffers(1, &m_YUVFbo.yFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, m_YUVFbo.yFBO);

  glGenTextures(1, &m_YUVFbo.yTexture);
  glBindTexture(GL_TEXTURE_2D, m_YUVFbo.yTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         m_YUVFbo.yTexture, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "[TextureRenderer] Y FBO creation failed" << std::endl;
    return false;
  }

  // Create UV plane FBO (half resolution for 4:2:0)
  int uvWidth = width / 2;
  int uvHeight = height / 2;

  glGenFramebuffers(1, &m_YUVFbo.uvFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, m_YUVFbo.uvFBO);

  glGenTextures(1, &m_YUVFbo.uvTexture);
  glBindTexture(GL_TEXTURE_2D, m_YUVFbo.uvTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, uvWidth, uvHeight, 0, GL_RG,
               GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         m_YUVFbo.uvTexture, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "[TextureRenderer] UV FBO creation failed" << std::endl;
    return false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  std::cout << "[TextureRenderer] YUV FBO created: " << width << "x" << height
            << std::endl;
  return true;
}

void TextureRenderer::DestroyYUVFramebuffer() {
  if (m_YUVFbo.yFBO) {
    glDeleteFramebuffers(1, &m_YUVFbo.yFBO);
    glDeleteTextures(1, &m_YUVFbo.yTexture);
    m_YUVFbo.yFBO = 0;
    m_YUVFbo.yTexture = 0;
  }

  if (m_YUVFbo.uvFBO) {
    glDeleteFramebuffers(1, &m_YUVFbo.uvFBO);
    glDeleteTextures(1, &m_YUVFbo.uvTexture);
    m_YUVFbo.uvFBO = 0;
    m_YUVFbo.uvTexture = 0;
  }
}

// ============================================================================
// YUV Rendering
// ============================================================================

void TextureRenderer::RenderToYUV() {
  if (!m_YUVFbo.yFBO || !m_YUVFbo.uvFBO || !m_TextureID) {
    std::cerr << "[TextureRenderer] YUV FBO or texture not initialized"
              << std::endl;
    return;
  }

  // Save current state
  GLint prevFBO;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

  GLint prevProgram;
  glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

  // Bind RGB texture
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_TextureID);

  // Render Y plane
  glBindFramebuffer(GL_FRAMEBUFFER, m_YUVFbo.yFBO);
  glViewport(0, 0, m_YUVFbo.width, m_YUVFbo.height);
  glClear(GL_COLOR_BUFFER_BIT);

  glUseProgram(m_YUVShaderProgram);
  glAttachShader(m_YUVShaderProgram, m_YUVShaderY);
  glLinkProgram(m_YUVShaderProgram);
  glUniform1i(glGetUniformLocation(m_YUVShaderProgram, "rgbTexture"), 0);

  glBindVertexArray(m_VAO);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

  glDetachShader(m_YUVShaderProgram, m_YUVShaderY);

  // Render UV plane
  glBindFramebuffer(GL_FRAMEBUFFER, m_YUVFbo.uvFBO);
  glViewport(0, 0, m_YUVFbo.width / 2, m_YUVFbo.height / 2);
  glClear(GL_COLOR_BUFFER_BIT);

  glAttachShader(m_YUVShaderProgram, m_YUVShaderUV);
  glLinkProgram(m_YUVShaderProgram);
  glUniform1i(glGetUniformLocation(m_YUVShaderProgram, "rgbTexture"), 0);

  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

  glDetachShader(m_YUVShaderProgram, m_YUVShaderUV);

  // Restore state
  glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
  glUseProgram(prevProgram);
}

// ============================================================================
// Read YUV to AVFrame
// ============================================================================

bool TextureRenderer::ReadYUVToAVFrame(AVFrame *frame) {
  if (!frame || !m_YUVFbo.yFBO || !m_YUVFbo.uvFBO) {
    std::cerr << "[TextureRenderer] Invalid frame or YUV FBO" << std::endl;
    return false;
  }

  // Read Y plane
  glBindFramebuffer(GL_FRAMEBUFFER, m_YUVFbo.yFBO);
  glReadPixels(0, 0, m_YUVFbo.width, m_YUVFbo.height, GL_RED, GL_UNSIGNED_BYTE,
               frame->data[0]);

  // Read UV plane (interleaved)
  glBindFramebuffer(GL_FRAMEBUFFER, m_YUVFbo.uvFBO);
  glReadPixels(0, 0, m_YUVFbo.width / 2, m_YUVFbo.height / 2, GL_RG,
               GL_UNSIGNED_BYTE, frame->data[1]);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  return true;
}
