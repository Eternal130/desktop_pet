#include "WindowManager.hpp"
#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <windows.h>
#else
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

#ifdef _WIN32
WNDPROC WindowManager::s_originalWndProc = NULL;
#else
// X11 counterpart of the Win32-only WindowManager::ApplyDesktopPetWindowStyle —
// defined at the bottom of this file, next to the Win32 implementation.
static void ApplyDesktopPetWindowStyleX11(GLFWwindow* glfwWindow);
#endif

WindowManager::WindowManager()
    : _window(NULL)
    , _windowWidth(0)
    , _windowHeight(0)
    , _isDragging(false)
    , _dragStartX(0.0)
    , _dragStartY(0.0)
    , _targetFps(0.0)
    , _windowShown(false)
{
}

WindowManager::~WindowManager()
{
    Release();
}

bool WindowManager::Initialize(int width, int height, bool hasStartupPos, int startupX, int startupY,
                               bool hasStartupSize, int startupWidth, int startupHeight)
{
    if (glfwInit() == GL_FALSE)
    {
        return false;
    }

#ifdef USE_VULKAN
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif

    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    int initWidth = hasStartupSize ? startupWidth : width;
    int initHeight = hasStartupSize ? startupHeight : height;
    _window = glfwCreateWindow(initWidth, initHeight, "desktop-pet-renderer", NULL, NULL);
    if (_window == NULL)
    {
        glfwTerminate();
        return false;
    }

    if (hasStartupPos)
    {
        glfwSetWindowPos(_window, startupX, startupY);
    }

#ifdef _WIN32
    HWND hwnd = FindWindow("GLFW30", NULL);
    if (hwnd)
    {
        ApplyDesktopPetWindowStyle(hwnd);
        s_originalWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowSubclassProc)));
    }
#else
    // X11: GLFW owns the native window — glfwGetX11Window() replaces FindWindow().
    ApplyDesktopPetWindowStyleX11(_window);
#endif

    int w, h;
    glfwGetWindowSize(_window, &w, &h);
    _windowWidth = w;
    _windowHeight = h;

    return true;
}

void WindowManager::Release()
{
#ifdef _WIN32
    if (s_originalWndProc)
    {
        HWND hwnd = FindWindow("GLFW30", NULL);
        if (hwnd)
        {
            SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_originalWndProc));
        }
        s_originalWndProc = NULL;
    }
#endif

    if (_window)
    {
        glfwDestroyWindow(_window);
        _window = NULL;
    }

    glfwTerminate();
}

GLFWwindow* WindowManager::GetWindow() const
{
    return _window;
}

void WindowManager::GetWindowSize(int& width, int& height) const
{
    glfwGetWindowSize(_window, &width, &height);
}

void WindowManager::SetWindowSize(int width, int height) const
{
    glfwSetWindowSize(_window, width, height);
}

void WindowManager::GetWindowPosition(int& x, int& y) const
{
    glfwGetWindowPos(_window, &x, &y);
}

void WindowManager::SetWindowPosition(int x, int y) const
{
    glfwSetWindowPos(_window, x, y);
}

void WindowManager::ShowWindow()
{
    glfwShowWindow(_window);
    _windowShown = true;
}

bool WindowManager::ShouldClose() const
{
    return glfwWindowShouldClose(_window) != GL_FALSE;
}

void WindowManager::SetShouldClose()
{
    glfwSetWindowShouldClose(_window, GLFW_TRUE);
}

void WindowManager::SetMousePassthrough(bool enable)
{
    glfwSetWindowAttrib(_window, GLFW_MOUSE_PASSTHROUGH, enable ? GLFW_TRUE : GLFW_FALSE);
}

void WindowManager::SetTargetFps(double fps)
{
    _targetFps = fps;
#ifndef USE_VULKAN
    if (fps <= 0.0)
    {
        glfwSwapInterval(1);
    }
    else
    {
        glfwSwapInterval(0);
    }
#endif
}

double WindowManager::GetTargetFps() const
{
    return _targetFps;
}

void WindowManager::WaitEvents(double timeout)
{
    glfwWaitEventsTimeout(timeout);
}

bool WindowManager::IsWindowShown() const
{
    return _windowShown;
}

void WindowManager::SetWindowShown()
{
    _windowShown = true;
}

bool WindowManager::IsKeyPressed(int key) const
{
    return glfwGetKey(_window, key) == GLFW_PRESS;
}

#ifdef _WIN32
void WindowManager::ApplyDesktopPetWindowStyle(HWND hwnd)
{
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_TOOLWINDOW;
    exStyle &= ~WS_EX_APPWINDOW;
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style &= ~WS_MINIMIZEBOX;
    style &= ~WS_SYSMENU;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

LRESULT CALLBACK WindowManager::WindowSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_MINIMIZE)
    {
        return 0;
    }
    if (msg == WM_SIZE && wParam == SIZE_MINIMIZED)
    {
        ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        return 0;
    }
    return CallWindowProc(s_originalWndProc, hwnd, msg, wParam, lParam);
}
#else

// Linux: anti-minimize not implemented; window is taskbar-hidden (SKIP_TASKBAR) so the minimize entry point is unreachable.

// Sends an EWMH _NET_WM_STATE ClientMessage (action=ADD) for one or two states.
static void SendNetWmStateAdd(Display* display, ::Window window, Atom firstState, Atom secondState = None)
{
    XEvent event = {};
    event.xclient.type = ClientMessage;
    event.xclient.send_event = True;
    event.xclient.display = display;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display, "_NET_WM_STATE", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1; // _NET_WM_STATE_ADD
    event.xclient.data.l[1] = static_cast<long>(firstState);
    event.xclient.data.l[2] = static_cast<long>(secondState);
    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &event);
}

// X11 counterpart of WindowManager::ApplyDesktopPetWindowStyle.
static void ApplyDesktopPetWindowStyleX11(GLFWwindow* glfwWindow)
{
    Display* display = glfwGetX11Display();
    ::Window window = glfwGetX11Window(glfwWindow);
    if (display == NULL || window == None)
    {
        return;
    }

    // WS_EX_TOOLWINDOW / ~WS_EX_APPWINDOW counterpart: hide from taskbar and pager.
    SendNetWmStateAdd(display, window,
                      XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False),
                      XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False));

    // WS_MINIMIZEBOX / WS_SYSMENU strip has no X11 counterpart; the minimize
    // entry point is already unreachable once the window is off the taskbar.

    // SetWindowPos(HWND_TOPMOST) counterpart (re-insurance on top of GLFW_FLOATING).
    SendNetWmStateAdd(display, window,
                      XInternAtom(display, "_NET_WM_STATE_ABOVE", False));

    XFlush(display);
}
#endif
