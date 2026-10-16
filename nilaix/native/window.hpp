#pragma once

// Platform-specific window interface for Nycromicon
// Minimal C++ interface that will be wrapped by Ny code

#ifdef _WIN32
    #define PLATFORM_WINDOWS
    #include <windows.h>
#elif defined(__linux__)
    #define PLATFORM_LINUX
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <GL/gl.h>
    #include <GL/glx.h>
#elif defined(__APPLE__)
    #define PLATFORM_MACOS
    #include <Cocoa/Cocoa.h>
#endif

#include <string>
#include <functional>

// Forward declarations
struct WindowConfig;
struct KeyEvent;
struct MouseEvent;
struct Size;

// Window configuration structure
struct WindowConfig {
    std::string title;
    int width;
    int height;
    bool resizable;
    bool fullscreen;
    bool center_on_screen;

    WindowConfig() : title("Nycromicon"), width(1200), height(800),
                     resizable(true), fullscreen(false), center_on_screen(true) {}
};

// Size structure
struct Size {
    int width;
    int height;
    Size(int w = 0, int h = 0) : width(w), height(h) {}
};

// Key event structure
struct KeyEvent {
    int key;
    int scancode;
    int action;  // 0=press, 1=release, 2=repeat
    int mods;    // modifier keys
};

// Mouse event structure
struct MouseEvent {
    double x, y;
    int button;
    int action;
    int mods;
};

// Abstract window interface
class IWindow {
public:
    virtual ~IWindow() = default;

    // Core window operations
    virtual bool initialize(const WindowConfig& config) = 0;
    virtual void destroy() = 0;
    virtual bool shouldClose() = 0;
    virtual void pollEvents() = 0;
    virtual void swapBuffers() = 0;

    // Window properties
    virtual void setTitle(const std::string& title) = 0;
    virtual std::string getTitle() const = 0;
    virtual Size getSize() const = 0;
    virtual void setSize(int width, int height) = 0;
    virtual Size getFramebufferSize() const = 0;
    virtual void setPosition(int x, int y) = 0;

    // Window state
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void focus() = 0;
    virtual bool isFocused() const = 0;

    // Event callbacks (using std::function for flexibility)
    virtual void setKeyCallback(std::function<void(const KeyEvent&)> callback) = 0;
    virtual void setMouseCallback(std::function<void(const MouseEvent&)> callback) = 0;
    virtual void setResizeCallback(std::function<void(int, int)> callback) = 0;
    virtual void setCloseCallback(std::function<void()> callback) = 0;

    // Input state
    virtual bool isKeyPressed(int key) const = 0;
    virtual bool isMouseButtonPressed(int button) const = 0;
    virtual void getCursorPosition(double& x, double& y) const = 0;
    virtual void setCursorPosition(double x, double y) = 0;
};

// Factory function for creating platform-specific windows
extern "C" {
    IWindow* createWindow();
    void destroyWindow(IWindow* window);

    // Utility functions
    void initializePlatform();
    void shutdownPlatform();
    void pollPlatformEvents();

    // Time functions
    double getCurrentTime();
    void platform_sleep(double seconds);
}