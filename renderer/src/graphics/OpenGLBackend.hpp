#pragma once

#include "IGraphicsBackend.hpp"
#include <GL/glew.h>

struct GLFWwindow;

class OpenGLBackend : public IGraphicsBackend {
public:
    OpenGLBackend();
    ~OpenGLBackend() override;

    bool InitializeGraphics(GLFWwindow* window) override;
    void ReleaseGraphics() override;
    void BeginFrame(int width, int height) override;
    void EndFrame(GLFWwindow* window) override;
    uint64_t CreateTexture(const void* data, int width, int height, int channels) override;
    void DeleteTexture(uint64_t handle) override;
    bool IsPixelTransparent(int x, int y, int windowHeight) override;

private:
    GLuint _pbo;
    bool _isClickThrough;
};
