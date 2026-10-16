#include "window_interface.hpp"
#include <GLFW/glfw3.h>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// GLFW-based concrete implementation of IWindow interface
class GLFWWindow : public IWindow {
private:
    GLFWwindow* window_;
    WindowConfig config_;

    // Callback storage (using function pointers stored as static members for simplicity)
    static std::function<void(const KeyEvent&)> s_keyCallback;
    static std::function<void(const MouseEvent&)> s_mouseCallback;
    static std::function<void(int, int)> s_resizeCallback;
    static std::function<void()> s_closeCallback;

public:
    GLFWWindow() : window_(nullptr) {}
    virtual ~GLFWWindow() {
        destroy();
    }

    bool initialize(const WindowConfig& config) override {
        config_ = config;

        // Initialize GLFW if not already initialized
        if (!glfwInit()) {
            std::cerr << "[GLFWWindow] Failed to initialize GLFW" << std::endl;
            return false;
        }

        // Configure window
        glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Hidden initially

        // Create window
        window_ = glfwCreateWindow(config.width, config.height, config.title.c_str(),
                                  nullptr, nullptr);
        if (!window_) {
            std::cerr << "[GLFWWindow] Failed to create GLFW window" << std::endl;
            glfwTerminate();
            return false;
        }

        // Center on screen if requested
        if (config.center_on_screen) {
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            int xpos, ypos, width, height;
            glfwGetMonitorWorkarea(monitor, &xpos, &ypos, &width, &height);
            glfwSetWindowPos(window_, xpos + (width - config.width) / 2,
                           ypos + (height - config.height) / 2);
        }

        // Set fullscreen if requested
        if (config.fullscreen) {
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            glfwSetWindowMonitor(window_, monitor, 0, 0,
                              config.width, config.height, GLFW_DONT_CARE);
        }

        // Set callbacks
        glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int scancode, int action, int mods) {
            if (s_keyCallback) {
                KeyEvent e{key, scancode, action, mods};
                s_keyCallback(e);
            }
        });

        glfwSetCursorPosCallback(window_, [](GLFWwindow* w, double xpos, double ypos) {
            if (s_mouseCallback) {
                MouseEvent e{xpos, ypos, -1, -1, 0};
                s_mouseCallback(e);
            }
        });

        glfwSetMouseButtonCallback(window_, [](GLFWwindow* w, int button, int action, int mods) {
            if (s_mouseCallback) {
                double x, y;
                glfwGetCursorPos(w, &x, &y);
                MouseEvent e{x, y, button, action, mods};
                s_mouseCallback(e);
            }
        });

        glfwSetWindowSizeCallback(window_, [](GLFWwindow* w, int width, int height) {
            if (s_resizeCallback) {
                s_resizeCallback(width, height);
            }
        });

        glfwSetWindowCloseCallback(window_, [](GLFWwindow* w) {
            if (s_closeCallback) {
                s_closeCallback();
            }
        });

        // Make context current
        glfwMakeContextCurrent(window_);

        // Enable vsync to cap framerate and reduce CPU usage
        glfwSwapInterval(1);

        // Show window
        glfwShowWindow(window_);

        // Additional Wayland/Modern desktop fixes
        glfwFocusWindow(window_);

        // Force window to front and ensure visibility
        glfwPollEvents();

        return true;
    }

    void destroy() override {
        if (window_) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
    }

    bool shouldClose() override {
        return window_ ? glfwWindowShouldClose(window_) : true;
    }

    void pollEvents() override {
        // Wait up to 16ms for events (~60fps) to avoid busy-spinning
        glfwWaitEventsTimeout(0.016);
    }

    void swapBuffers() override {
        if (window_) {
            glfwSwapBuffers(window_);
        }
    }

    void setTitle(const std::string& title) override {
        config_.title = title;
        if (window_) {
            glfwSetWindowTitle(window_, title.c_str());
        }
    }

    std::string getTitle() const override {
        return config_.title;
    }

    Size getSize() const override {
        Size s;
        if (window_) {
            int w, h;
            glfwGetWindowSize(window_, &w, &h);
            s.width = w;
            s.height = h;
        }
        return s;
    }

    void setSize(int width, int height) override {
        config_.width = width;
        config_.height = height;
        if (window_) {
            glfwSetWindowSize(window_, width, height);
        }
    }

    Size getFramebufferSize() const override {
        Size s;
        if (window_) {
            int w, h;
            glfwGetFramebufferSize(window_, &w, &h);
            s.width = w;
            s.height = h;
        }
        return s;
    }

    void setPosition(int x, int y) override {
        if (window_) {
            glfwSetWindowPos(window_, x, y);
        }
    }

    void show() override {
        if (window_) {
            glfwShowWindow(window_);
        }
    }

    void hide() override {
        if (window_) {
            glfwHideWindow(window_);
        }
    }

    void focus() override {
        if (window_) {
            glfwFocusWindow(window_);
        }
    }

    bool isFocused() const override {
        return window_ ? glfwGetWindowAttrib(window_, GLFW_FOCUSED) : false;
    }

    void setKeyCallback(std::function<void(const KeyEvent&)> callback) override {
        s_keyCallback = callback;
    }

    void setMouseCallback(std::function<void(const MouseEvent&)> callback) override {
        s_mouseCallback = callback;
    }

    void setResizeCallback(std::function<void(int, int)> callback) override {
        s_resizeCallback = callback;
    }

    void setCloseCallback(std::function<void()> callback) override {
        s_closeCallback = callback;
    }

    bool isKeyPressed(int key) const override {
        return window_ ? glfwGetKey(window_, key) == GLFW_PRESS : false;
    }

    bool isMouseButtonPressed(int button) const override {
        return window_ ? glfwGetMouseButton(window_, button) == GLFW_PRESS : false;
    }

    void getCursorPosition(double& x, double& y) const override {
        if (window_) {
            glfwGetCursorPos(window_, &x, &y);
        }
    }

    void setCursorPosition(double x, double y) override {
        if (window_) {
            glfwSetCursorPos(window_, x, y);
        }
    }
};

// Static callback storage
std::function<void(const KeyEvent&)> GLFWWindow::s_keyCallback;
std::function<void(const MouseEvent&)> GLFWWindow::s_mouseCallback;
std::function<void(int, int)> GLFWWindow::s_resizeCallback;
std::function<void()> GLFWWindow::s_closeCallback;

// Platform-specific implementations
extern "C" {

IWindow* createWindow() {
    return new GLFWWindow();
}

void destroyWindow(IWindow* window) {
    delete window;
}

void initializePlatform() {
    glfwInit();
}

void shutdownPlatform() {
    glfwTerminate();
}

void pollPlatformEvents() {
    glfwPollEvents();
}

double getCurrentTime() {
    return glfwGetTime();
}

void platform_sleep(double seconds) {
#ifdef _WIN32
    Sleep((DWORD)(seconds * 1000));
#else
    usleep((useconds_t)(seconds * 1000000));
#endif
}

} // extern "C"
