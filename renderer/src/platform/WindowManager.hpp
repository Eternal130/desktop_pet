#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

struct GLFWwindow;

class WindowManager {
public:
    WindowManager();
    ~WindowManager();

    // Lifecycle
    bool Initialize(int width, int height, bool hasStartupPos, int startupX, int startupY,
                    bool hasStartupSize, int startupWidth, int startupHeight);
    void Release();

    // Window access
    GLFWwindow* GetWindow() const;
    void GetWindowSize(int& width, int& height) const;
    void SetWindowSize(int width, int height) const;
    void GetWindowPosition(int& x, int& y) const;
    void SetWindowPosition(int x, int y) const;
    void ShowWindow();
    bool ShouldClose() const;
    void SetShouldClose();

    // Window attributes
    void SetMousePassthrough(bool enable);
    void SetTargetFps(double fps);
    double GetTargetFps() const;
    void WaitEvents(double timeout);
    bool IsWindowShown() const;
    void SetWindowShown();

    // Input helpers (used by event callbacks in LAppDelegate)
    bool IsKeyPressed(int key) const;

#ifdef _WIN32
    // Win32 specific
    static void ApplyDesktopPetWindowStyle(HWND hwnd);
    static LRESULT CALLBACK WindowSubclassProc(HWND, UINT, WPARAM, LPARAM);
#endif

private:
    GLFWwindow* _window;
    int _windowWidth;
    int _windowHeight;
    bool _isDragging;
    double _dragStartX;
    double _dragStartY;
    double _targetFps;
    bool _windowShown;
#ifdef _WIN32
    static WNDPROC s_originalWndProc;
#endif
};
