#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <map>
#include <memory>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#  include <windows.h>
#  include <winuser.h>
#else
#  include <dirent.h>
#endif

#if defined(__linux__) && defined(NY_HAS_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#endif

#include "ipc_hot_reload.hpp"
#include "nil_version.hpp"

// Base controller interface
class Controller {
public:
    virtual ~Controller() = default;
    virtual std::vector<std::map<std::string, std::string>> findWindows(const std::string& title) = 0;
    virtual bool clickAt(uintptr_t window_id, int x, int y) = 0;
    virtual bool typeText(const std::string& text) = 0;
    virtual bool closeWindow(uintptr_t window_id) = 0;
    virtual bool captureScreenshot(const std::string& filename) = 0;
};

// Windows Window Controller for direct window manipulation
#ifdef _WIN32
class WindowsController : public Controller {
private:
    // Windows API handles

public:
    WindowsController() {
        // Initialize Windows API
    }

    ~WindowsController() {
        // Cleanup
    }

    std::vector<std::map<std::string, std::string>> findWindows(const std::string& title) {
        std::vector<std::map<std::string, std::string>> windows;

        // Use Windows API to enumerate windows
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* windows = reinterpret_cast<std::vector<std::map<std::string, std::string>>*>(lParam);

            if (!IsWindowVisible(hwnd)) return TRUE;

            char title[256];
            GetWindowTextA(hwnd, title, sizeof(title));

            RECT rect;
            GetWindowRect(hwnd, &rect);

            std::map<std::string, std::string> info;
            info["id"] = std::to_string((uintptr_t)hwnd);
            info["title"] = title;
            info["x"] = std::to_string(rect.left);
            info["y"] = std::to_string(rect.top);
            info["width"] = std::to_string(rect.right - rect.left);
            info["height"] = std::to_string(rect.bottom - rect.top);
            info["visible"] = "true";

            windows->push_back(info);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&windows));

        return windows;
    }

    bool clickAt(uintptr_t window_id, int x, int y) {
        HWND hwnd = (HWND)window_id;

        // Set cursor position
        POINT pt = {x, y};
        ClientToScreen(hwnd, &pt);
        SetCursorPos(pt.x, pt.y);

        // Simulate mouse click
        INPUT input = {0};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &input, sizeof(INPUT));

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &input, sizeof(INPUT));

        return true;
    }

    bool typeText(const std::string& text) {
        for (char c : text) {
            INPUT input = {0};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = 0;
            input.ki.wScan = c;
            input.ki.dwFlags = KEYEVENTF_UNICODE;
            SendInput(1, &input, sizeof(INPUT));

            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }

    bool closeWindow(uintptr_t window_id) {
        HWND hwnd = (HWND)window_id;
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        return true;
    }

    bool captureScreenshot(const std::string& filename) {
        // Use Windows API to capture screenshot
        // For now, create a simple placeholder
        std::ofstream file(filename);
        if (file.is_open()) {
            file << "P3\n1 1\n255\n0 255 0\n"; // Simple green pixel
            file.close();
            return true;
        }
        return false;
    }
};
#endif

// X11 Window Controller for direct window manipulation
#if defined(__linux__) && defined(NY_HAS_X11)
class X11Controller : public Controller {
private:
    Display* display;
    Window root_window;

public:
    X11Controller() {
#if defined(__linux__) && defined(NY_HAS_X11)
        display = XOpenDisplay(nullptr);
        if (!display) {
            throw std::runtime_error("Cannot open X11 display");
        }
        root_window = DefaultRootWindow(display);
#endif
    }

    ~X11Controller() {
#if defined(__linux__) && defined(NY_HAS_X11)
        if (display) {
            XCloseDisplay(display);
        }
#endif
    }

    std::vector<std::map<std::string, std::string>> findWindows(const std::string& title) {
        std::vector<std::map<std::string, std::string>> windows;

#if defined(__linux__) && defined(NY_HAS_X11)
        Window root_return, parent_return;
        Window* children;
        unsigned int nchildren;

        if (XQueryTree(display, root_window, &root_return, &parent_return, &children, &nchildren)) {
            for (unsigned int i = 0; i < nchildren; i++) {
                char* window_name = nullptr;
                if (XFetchName(display, children[i], &window_name)) {
                    std::string name = window_name ? window_name : "";
                    if (window_name) XFree(window_name);

                    if (title.empty() || name.find(title) != std::string::npos) {
                        XWindowAttributes attr;
                        if (XGetWindowAttributes(display, children[i], &attr)) {
                            std::map<std::string, std::string> info;
                            info["id"] = std::to_string(children[i]);
                            info["title"] = name;
                            info["x"] = std::to_string(attr.x);
                            info["y"] = std::to_string(attr.y);
                            info["width"] = std::to_string(attr.width);
                            info["height"] = std::to_string(attr.height);
                            info["visible"] = (attr.map_state == IsViewable) ? "true" : "false";
                            windows.push_back(info);
                        }
                    }
                }
            }
            XFree(children);
        }
#endif

        return windows;
    }

    bool clickAt(uintptr_t window_id, int x, int y) {
#if defined(__linux__) && defined(NY_HAS_X11)
        XWarpPointer(display, None, (unsigned long)window_id, 0, 0, 0, 0, x, y);
        XFlush(display);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Note: XTestFakeButtonEvent would be used here if available
        // For now, simulate with XSendEvent
        XEvent event;
        std::memset(&event, 0, sizeof(event));
        event.type = ButtonPress;
        event.xbutton.button = Button1;
        event.xbutton.same_screen = True;
        event.xbutton.window = (unsigned long)window_id;
        event.xbutton.x = x;
        event.xbutton.y = y;
        XSendEvent(display, (unsigned long)window_id, True, ButtonPressMask, &event);
        XFlush(display);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        event.type = ButtonRelease;
        XSendEvent(display, (unsigned long)window_id, True, ButtonReleaseMask, &event);
        XFlush(display);

        return true;
#else
        return false;
#endif
    }

    bool typeText(const std::string& text) {
#if defined(__linux__) && defined(NY_HAS_X11)
        for (char c : text) {
            std::string char_str(1, c);
            KeySym keysym = XStringToKeysym(char_str.c_str());
            KeyCode keycode = XKeysymToKeycode(display, keysym);

            if (keycode != 0) {
                XEvent event;
                std::memset(&event, 0, sizeof(event));
                event.type = KeyPress;
                event.xkey.keycode = keycode;
                XSendEvent(display, InputFocus, True, KeyPressMask, &event);
                XFlush(display);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                event.type = KeyRelease;
                XSendEvent(display, InputFocus, True, KeyReleaseMask, &event);
                XFlush(display);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        return true;
#else
        return false;
#endif
    }

    bool closeWindow(uintptr_t window_id) {
#if defined(__linux__) && defined(NY_HAS_X11)
        XDestroyWindow(display, (unsigned long)window_id);
        XFlush(display);
        return true;
#else
        return false;
#endif
    }

    bool captureScreenshot(const std::string& filename) {
        // Placeholder until image capture is implemented without shelling out
        // with a caller-controlled filename.
        std::ofstream file(filename);
        if (file.is_open()) {
            file << "P3\n1 1\n255\n255 0 0\n"; // Simple red pixel
            file.close();
            return true;
        }

        return false;
    }
};
#endif

// Interactive JSON command controller. This is a local prototype, not an MCP
// or network server.
class AgentServer {
private:
    bool is_running;
    std::unique_ptr<Controller> controller;
    std::unique_ptr<ny::ipc::IpcClient> ipc_client;
    int connected_pid = 0;

    struct Command {
        std::string id;
        std::string method;
        std::map<std::string, std::string> params;
        long long timestamp;
    };

    struct CommandResponse {
        std::string id;
        std::string result;
        std::string error;
        long long timestamp;
    };

    std::string extract_value(const std::string& json, const std::string& key) {
        size_t pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";

        pos = json.find(":", pos);
        if (pos == std::string::npos) return "";

        pos = json.find_first_not_of(" \t\n", pos + 1);
        if (pos == std::string::npos) return "";

        if (json[pos] == '"') {
            size_t start = pos + 1;
            size_t end = json.find("\"", start);
            if (end == std::string::npos) return "";
            return json.substr(start, end - start);
        }

        // Handle non-string values
        size_t end = json.find_first_of(",}", pos);
        if (end == std::string::npos) return json.substr(pos);
        return json.substr(pos, end - pos);
    }

    std::string generate_response(const CommandResponse& resp) {
        std::ostringstream json;
        json << "{"
              << "\"id\":\"" << resp.id << "\","
              << "\"result\":" << resp.result << ","
              << "\"error\":\"" << resp.error << "\","
              << "\"timestamp\":" << resp.timestamp
              << "}";
        return json.str();
    }

public:
    AgentServer() : is_running(false) {
#ifdef _WIN32
        controller = std::make_unique<WindowsController>();
#elif defined(__linux__) && defined(NY_HAS_X11)
        try {
            controller = std::make_unique<X11Controller>();
        } catch (const std::exception& e) {
            std::cerr << "[nilagent] Failed to initialize controller: " << e.what() << std::endl;
        }
#endif
        ipc_client = std::make_unique<ny::ipc::IpcClient>();
    }

    std::vector<int> find_running_vms() {
        std::vector<int> pids;
#ifndef _WIN32
        const std::string base = "/tmp";
        DIR* dir = opendir(base.c_str());
        if (!dir) return pids;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.find("ny_ai_") == 0 && name.find(".sock") != std::string::npos) {
                size_t start = 6;
                size_t end = name.find(".sock");
                if (end > start) {
                    try { pids.push_back(std::stoi(name.substr(start, end - start))); } catch (...) {}
                }
            }
        }
        closedir(dir);
#endif
        return pids;
    }

    bool connect_to_vm(int pid) {
        if (ipc_client->is_connected()) {
            if (connected_pid == pid) return true;
            ipc_client->disconnect();
        }

        std::string socket_path = ny::ipc::get_socket_path_for_pid(pid);
        if (ipc_client->connect(socket_path)) {
            connected_pid = pid;
            std::cout << "[nilagent] Connected to NIL interpreter PID " << pid << std::endl;
            return true;
        }
        return false;
    }

    std::string handle_command(const std::string& json_str) {
        // Simple JSON parsing (in production, use proper JSON library)
        Command cmd;
        cmd.id = extract_value(json_str, "id");
        cmd.method = extract_value(json_str, "method");
        cmd.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        CommandResponse resp;
        resp.id = cmd.id;
        resp.timestamp = cmd.timestamp;

        std::cout << "[nilagent] Received command: " << cmd.method << " (ID: " << cmd.id << ")" << std::endl;

        if (cmd.method == "window.find") {
            std::string title = extract_value(json_str, "title");
            if (title.empty()) title = "Nycromicon";

            std::ostringstream result_json;
            result_json << "{\"success\":true,\"windows\":[";

            if (controller) {
                auto windows = controller->findWindows(title);
                for (size_t i = 0; i < windows.size(); i++) {
                    if (i > 0) result_json << ",";
                    result_json << "{\"id\":\"" << windows[i]["id"] << "\","
                                << "\"title\":\"" << windows[i]["title"] << "\","
                                << "\"x\":" << windows[i]["x"] << ","
                                << "\"y\":" << windows[i]["y"] << ","
                                << "\"width\":" << windows[i]["width"] << ","
                                << "\"height\":" << windows[i]["height"] << ","
                                << "\"visible\":" << windows[i]["visible"] << "}";
                }
            } else {
                // Mock response
                result_json << "{\"id\":\"123456\",\"title\":\"Nycromicon Enhanced IDE\",\"x\":100,\"y\":100,\"width\":800,\"height\":600,\"visible\":true}";
            }

            result_json << "]}";
            resp.result = result_json.str();
            std::cout << "[nilagent] Found windows for title: " << title << std::endl;
        }
        else if (cmd.method == "window.click") {
            std::string window_id_str = extract_value(json_str, "window_id");
            std::string x_str = extract_value(json_str, "x");
            std::string y_str = extract_value(json_str, "y");

            bool success = false;
            if (controller && !window_id_str.empty() && !x_str.empty() && !y_str.empty()) {
                uintptr_t window_id = std::stoull(window_id_str);
                int x = std::stoi(x_str);
                int y = std::stoi(y_str);
                success = controller->clickAt(window_id, x, y);
            }

            resp.result = "{\"success\":" + std::string(success ? "true" : "false") + ",\"window_id\":\"" + window_id_str + "\",\"x\":" + x_str + ",\"y\":" + y_str + "}";
            std::cout << "[nilagent] Click at: (" << x_str << ", " << y_str << ") on window " << window_id_str << std::endl;
        }
        else if (cmd.method == "window.close") {
            std::string window_id_str = extract_value(json_str, "window_id");
            bool success = false;
            if (controller && !window_id_str.empty()) {
                uintptr_t window_id = std::stoull(window_id_str);
                success = controller->closeWindow(window_id);
            }
            resp.result = "{\"success\":" + std::string(success ? "true" : "false") + ",\"window_id\":\"" + window_id_str + "\"}";
            std::cout << "[nilagent] Window close: " << window_id_str << std::endl;
        }
        else if (cmd.method == "text.type") {
            std::string text = extract_value(json_str, "text");
            bool success = false;
            if (controller && !text.empty()) {
                success = controller->typeText(text);
            }
            resp.result = "{\"success\":" + std::string(success ? "true" : "false") + ",\"text\":\"" + text + "\"}";
            std::cout << "[nilagent] Type text: " << text << std::endl;
        }
        else if (cmd.method == "text.erase") {
            std::string count_str = extract_value(json_str, "chars");
            int count = count_str.empty() ? 1 : std::stoi(count_str);

            // Send backspace characters
            bool success = false;
            if (controller) {
                std::string backspaces(count, '\b');
                success = controller->typeText(backspaces);
            }

            resp.result = "{\"success\":" + std::string(success ? "true" : "false") + ",\"chars\":" + std::to_string(count) + "}";
            std::cout << "[nilagent] Erase " << count << " characters" << std::endl;
        }
        else if (cmd.method == "screenshot") {
            std::string filename = extract_value(json_str, "filename");
            if (filename.empty()) filename = "nilagent_screenshot.ppm";

            bool success = false;
            if (controller) {
                success = controller->captureScreenshot(filename);
            }

            resp.result = "{\"success\":" + std::string(success ? "true" : "false") + ",\"filename\":\"" + filename + "\"}";
            std::cout << "[nilagent] Screenshot " << (success ? "saved" : "failed") << ": " << filename << std::endl;
        }
        else if (cmd.method == "app.reload") {
            std::string pid_str = extract_value(json_str, "pid");
            std::string target = extract_value(json_str, "target");
            if (target.empty()) target = "all";

            auto start_time = std::chrono::high_resolution_clock::now();
            bool success = false;
            std::string error_msg;
            int new_version = 0;

            int pid = pid_str.empty() ? 0 : std::stoi(pid_str);

            if (pid == 0) {
                auto pids = find_running_vms();
                if (pids.empty()) {
                    error_msg = "No running NIL interpreters found";
                } else {
                    pid = pids[0];
                }
            }

            if (error_msg.empty() && !connect_to_vm(pid)) {
                error_msg = "Failed to connect to NIL interpreter PID " + std::to_string(pid);
            }

            if (error_msg.empty()) {
                std::string response = ipc_client->send_reload(target);
                if (response.empty()) {
                    error_msg = "No response from NIL interpreter";
                } else {
                    std::string ok_str = ny::ipc::json_extract_string(response, "ok");
                    success = (ok_str == "true");
                    if (!success) {
                        error_msg = ny::ipc::json_extract_string(response, "error");
                        if (error_msg.empty()) error_msg = "Reload failed";
                    } else {
                        new_version = ny::ipc::json_extract_int(response, "version");
                    }
                }
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto reload_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

            std::ostringstream result_json;
            result_json << "{\"success\":" << (success ? "true" : "false")
                       << ",\"version\":" << new_version
                       << ",\"reload_time_ms\":" << reload_time_ms
                       << ",\"pid\":" << pid;
            if (!error_msg.empty()) {
                result_json << ",\"error\":\"" << error_msg << "\"";
            }
            result_json << "}";
            resp.result = result_json.str();
            std::cout << "[nilagent] Hot reload " << (success ? "completed" : "failed")
                      << " (pid=" << pid << ", v" << new_version << ", " << reload_time_ms << "ms)" << std::endl;
        }
        else if (cmd.method == "app.reload.status") {
            std::string pid_str = extract_value(json_str, "pid");
            int pid = pid_str.empty() ? connected_pid : std::stoi(pid_str);

            std::ostringstream result_json;

            if (pid == 0) {
                auto pids = find_running_vms();
                result_json << "{\"success\":true,\"connected\":false,\"running_vms\":[";
                for (size_t i = 0; i < pids.size(); i++) {
                    if (i > 0) result_json << ",";
                    result_json << pids[i];
                }
                result_json << "]}";
            } else if (!connect_to_vm(pid)) {
                result_json << "{\"success\":false,\"error\":\"Failed to connect to PID " << pid << "\"}";
            } else {
                std::string response = ipc_client->send_status();
                if (response.empty()) {
                    result_json << "{\"success\":false,\"error\":\"No response from NIL interpreter\"}";
                } else {
                    result_json << "{\"success\":true,\"connected\":true,\"pid\":" << pid
                               << ",\"status\":" << response << "}";
                }
            }

            resp.result = result_json.str();
            std::cout << "[nilagent] Reload status for PID " << pid << std::endl;
        }
        else if (cmd.method == "app.state.snapshot") {
            std::string pid_str = extract_value(json_str, "pid");
            int pid = pid_str.empty() ? connected_pid : std::stoi(pid_str);

            bool success = false;
            size_t size_bytes = 0;
            std::string error_msg;

            if (pid == 0 || !connect_to_vm(pid)) {
                error_msg = "Not connected to a NIL interpreter";
            } else {
                std::string response = ipc_client->send_snapshot();
                if (response.empty()) {
                    error_msg = "No response from NIL interpreter";
                } else {
                    std::string ok_str = ny::ipc::json_extract_string(response, "ok");
                    success = (ok_str == "true");
                    if (success) {
                        size_bytes = ny::ipc::json_extract_int(response, "size");
                    } else {
                        error_msg = ny::ipc::json_extract_string(response, "error");
                    }
                }
            }

            std::ostringstream result_json;
            result_json << "{\"success\":" << (success ? "true" : "false")
                       << ",\"size_bytes\":" << size_bytes;
            if (!error_msg.empty()) {
                result_json << ",\"error\":\"" << error_msg << "\"";
            }
            result_json << "}";
            resp.result = result_json.str();
            std::cout << "[nilagent] State snapshot: " << size_bytes << " bytes" << std::endl;
        }
        else if (cmd.method == "app.state.restore") {
            resp.result = "{\"success\":false,\"error\":\"Not implemented via IPC yet\"}";
            std::cout << "[nilagent] State restore not implemented" << std::endl;
        }
        else if (cmd.method == "app.list") {
            auto pids = find_running_vms();
            std::ostringstream result_json;
            result_json << "{\"success\":true,\"vms\":[";
            for (size_t i = 0; i < pids.size(); i++) {
                if (i > 0) result_json << ",";
                result_json << "{\"pid\":" << pids[i] << ",\"connected\":"
                           << (pids[i] == connected_pid && ipc_client->is_connected() ? "true" : "false") << "}";
            }
            result_json << "]}";
            resp.result = result_json.str();
            std::cout << "[nilagent] Found " << pids.size() << " running NIL interpreters" << std::endl;
        }
        else if (cmd.method == "app.connect") {
            std::string pid_str = extract_value(json_str, "pid");
            int pid = pid_str.empty() ? 0 : std::stoi(pid_str);

            if (pid == 0) {
                auto pids = find_running_vms();
                if (!pids.empty()) pid = pids[0];
            }

            bool success = false;
            std::string error_msg;

            if (pid == 0) {
                error_msg = "No NIL interpreter specified and none running";
            } else if (connect_to_vm(pid)) {
                success = true;
            } else {
                error_msg = "Failed to connect to PID " + std::to_string(pid);
            }

            std::ostringstream result_json;
            result_json << "{\"success\":" << (success ? "true" : "false") << ",\"pid\":" << pid;
            if (!error_msg.empty()) {
                result_json << ",\"error\":\"" << error_msg << "\"";
            }
            result_json << "}";
            resp.result = result_json.str();
            std::cout << "[nilagent] Connect to PID " << pid << ": " << (success ? "success" : "failed") << std::endl;
        }
        else if (cmd.method == "system.status") {
            std::ostringstream status_json;
            status_json << "{\"success\":true,\"status\":{"
                       << "\"server_running\":true,"
                       << "\"debug_mode\":true,"
                       << "\"controller_available\":" << (controller ? "true" : "false")
                       << "}}";
            resp.result = status_json.str();
            std::cout << "[nilagent] System status requested" << std::endl;
        }
        else if (cmd.method == "window.list") {
            std::ostringstream result_json;
            result_json << "{\"success\":true,\"windows\":[";

            if (controller) {
                auto windows = controller->findWindows("");  // Find all windows
                for (size_t i = 0; i < windows.size(); i++) {
                    if (i > 0) result_json << ",";
                    result_json << "{\"id\":\"" << windows[i]["id"] << "\","
                                << "\"title\":\"" << windows[i]["title"] << "\","
                                << "\"x\":" << windows[i]["x"] << ","
                                << "\"y\":" << windows[i]["y"] << ","
                                << "\"width\":" << windows[i]["width"] << ","
                                << "\"height\":" << windows[i]["height"] << ","
                                << "\"visible\":" << windows[i]["visible"] << "}";
                }
            }

            result_json << "]}";
            resp.result = result_json.str();
            std::cout << "[nilagent] Window list requested" << std::endl;
        }
        else if (cmd.method == "inspect_ui") {
            // Mock UI inspection response
            resp.result = "{\"success\":true,\"elements\":[{\"type\":\"text_editor\",\"id\":\"editor1\"},{\"type\":\"button\",\"id\":\"btn_close\"}]}";
            std::cout << "[nilagent] UI inspection requested" << std::endl;
        }
        else {
            resp.error = "Unknown command: " + cmd.method;
        }

        return generate_response(resp);
    }

    void start() {
        is_running = true;
        std::cout << "[nilagent] Interactive controller starting" << std::endl;
        std::cout << "[nilagent] Controller Available: " << (controller ? "Yes" : "No") << std::endl;
        std::cout << "[nilagent] Ready for local JSON commands" << std::endl;

        // Simple command loop for demonstration
        while (is_running) {
            std::string line;
            std::cout << "[nilagent] Enter JSON command (or 'quit' to exit): ";
            std::getline(std::cin, line);

            if (line == "quit") {
                is_running = false;
                break;
            }

            if (!line.empty()) {
                std::string response = handle_command(line);
                std::cout << "[nilagent] Response: " << response << std::endl;
            }
        }
    }

    void stop() {
        is_running = false;
    }
};

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "nilagent " << NIL_VERSION << '\n';
        return 0;
    }
    if (argc > 1) {
        const std::string arg(argv[1]);
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: nilagent [--help|--version]\n"
                      << "Reads local JSON commands from standard input.\n";
            return 0;
        }
        std::cerr << "nilagent does not accept positional arguments\n";
        return 1;
    }

    std::cout << "==================================================" << std::endl;
    std::cout << "NY AGENT CONTROLLER" << std::endl;
    std::cout << "Interactive JSON command interface" << std::endl;
    std::cout << "==================================================" << std::endl;

    AgentServer server;

    try {
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "[nilagent] Server stopped" << std::endl;
    return 0;
}
