#include "OpenGLBackend.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>

OpenGLBackend::OpenGLBackend() : _pbo(0), _isClickThrough(false) {}

OpenGLBackend::~OpenGLBackend() {}

bool OpenGLBackend::InitializeGraphics(GLFWwindow* window)
{
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) {
        return false;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glGenBuffers(1, &_pbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, _pbo);
    GLubyte zeros[4] = {0, 0, 0, 0};
    glBufferData(GL_PIXEL_PACK_BUFFER, 4, zeros, GL_STREAM_READ);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    return true;
}

void OpenGLBackend::ReleaseGraphics()
{
    if (_pbo) {
        glDeleteBuffers(1, &_pbo);
        _pbo = 0;
    }
}

void OpenGLBackend::BeginFrame(int width, int height)
{
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearDepth(1.0);
}

void OpenGLBackend::EndFrame(GLFWwindow* window)
{
    glfwSwapBuffers(window);
}

uint64_t OpenGLBackend::CreateTexture(const void* data, int width, int height, int channels)
{
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return static_cast<uint64_t>(textureId);
}

void OpenGLBackend::DeleteTexture(uint64_t handle)
{
    GLuint textureId = static_cast<GLuint>(handle);
    glDeleteTextures(1, &textureId);
}

bool OpenGLBackend::IsPixelTransparent(int x, int y, int windowHeight)
{
    glBindBuffer(GL_PIXEL_PACK_BUFFER, _pbo);
    GLubyte* ptr = static_cast<GLubyte*>(glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
    GLubyte alpha = 0;
    if (ptr) {
        alpha = ptr[3];
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }

    int fbY = windowHeight - 1 - y;
    glReadPixels(x, fbY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    _isClickThrough = (alpha == 0);
    return _isClickThrough;
}
