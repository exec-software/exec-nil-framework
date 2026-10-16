#include "window_interface.hpp"
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

// =========================================================================
// Global state
// =========================================================================

static IWindow* g_window = nullptr;
static GLFWwindow* g_glfw_window = nullptr;
static int g_window_width = 1024;
static int g_window_height = 700;

// =========================================================================
// Event queue (supports multiple pending events)
// =========================================================================

struct ny_event {
    int event_type;
    int key_code;
    int mouse_x;
    int mouse_y;
    int width;
    int height;
    int modifiers;
};

#define MAX_EVENTS 64
static ny_event g_event_queue[MAX_EVENTS];
static int g_event_count = 0;
static ny_event g_popped_event = {0, 0, 0, 0, 0, 0, 0};

#define EVENT_NONE 0
#define EVENT_KEY_PRESS 1
#define EVENT_MOUSE_MOVE 2
#define EVENT_WINDOW_CLOSE 3
#define EVENT_MOUSE_CLICK 4
#define EVENT_WINDOW_RESIZE 5
#define EVENT_MOUSE_PRESS 6
#define EVENT_MOUSE_RELEASE 7
#define EVENT_MOUSE_MOTION 8
#define EVENT_MOUSE_WHEEL 9

static void push_event(ny_event e) {
    if (g_event_count < MAX_EVENTS) {
        g_event_queue[g_event_count++] = e;
    }
}

// Convert GLFW modifiers to NY modifiers (Shift=1, Ctrl=2, Alt=4)
static int convert_mods(int glfw_mods) {
    int mods = 0;
    if (glfw_mods & GLFW_MOD_SHIFT) mods |= 1;
    if (glfw_mods & GLFW_MOD_CONTROL) mods |= 2;
    if (glfw_mods & GLFW_MOD_ALT) mods |= 4;
    return mods;
}

// Convert GLFW key codes to NY widget key codes
// Special keys use 0x10000 flag; printable chars use raw ASCII
// Returns 0 for keys that should be handled by the char callback
static int convert_key(int glfw_key, int mods) {
    // Special keys - flagged with 0x10000 to match widget.ny expectations
    switch (glfw_key) {
        case GLFW_KEY_BACKSPACE: return 0x10008;
        case GLFW_KEY_TAB:      return 0x10009;
        case GLFW_KEY_ENTER:    return 0x1000D;
        case GLFW_KEY_ESCAPE:   return 0x1001B;
        case GLFW_KEY_DELETE:   return 0x1002E;
        case GLFW_KEY_LEFT:     return 0x10025;
        case GLFW_KEY_RIGHT:    return 0x10027;
        case GLFW_KEY_UP:       return 0x10026;
        case GLFW_KEY_DOWN:     return 0x10028;
        case GLFW_KEY_HOME:     return 0x10024;
        case GLFW_KEY_END:      return 0x10023;
        case GLFW_KEY_PAGE_UP:  return 0x10021;
        case GLFW_KEY_PAGE_DOWN:return 0x10022;
        case GLFW_KEY_F1:       return 0x10070;
        case GLFW_KEY_F2:       return 0x10071;
        case GLFW_KEY_F3:       return 0x10072;
        case GLFW_KEY_F4:       return 0x10073;
        case GLFW_KEY_F5:       return 0x10074;
        default: break;
    }
    // Ctrl+letter → return raw letter code; modifiers field carries CTRL flag
    // This avoids conflicts with special keys (e.g., Ctrl+H=0x10008 vs Backspace=0x10008)
    if ((mods & GLFW_MOD_CONTROL) && glfw_key >= GLFW_KEY_A && glfw_key <= GLFW_KEY_Z) {
        return glfw_key;  // GLFW_KEY_A=65, etc. matches ASCII uppercase
    }
    // Printable chars handled by char callback - return 0 to skip
    return 0;
}

// =========================================================================
// Bitmap font for text rendering (simple 8x16 built-in)
// =========================================================================

// Complete 8x16 bitmap font for ASCII 32-126 (standard VGA-style)
static const unsigned char g_font_data[95][16] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, //   (32)
  {0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00}, // ! (33)
  {0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // " (34)
  {0x00,0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0x6C,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00}, // # (35)
  {0x00,0x18,0x18,0x7C,0xC6,0xC0,0x78,0x3C,0x06,0xC6,0x7C,0x18,0x18,0x00,0x00,0x00}, // $ (36)
  {0x00,0x00,0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x60,0xC6,0x86,0x00,0x00,0x00,0x00}, // % (37)
  {0x00,0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // & (38)
  {0x00,0x30,0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' (39)
  {0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00}, // ( (40)
  {0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00}, // ) (41)
  {0x00,0x00,0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00}, // * (42)
  {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00}, // + (43)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00}, // , (44)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // - (45)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00}, // . (46)
  {0x00,0x00,0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00}, // / (47)
  {0x00,0x00,0x7C,0xC6,0xC6,0xCE,0xDE,0xF6,0xE6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 0 (48)
  {0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00}, // 1 (49)
  {0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00}, // 2 (50)
  {0x00,0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 3 (51)
  {0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00}, // 4 (52)
  {0x00,0x00,0xFE,0xC0,0xC0,0xC0,0xFC,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 5 (53)
  {0x00,0x00,0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 6 (54)
  {0x00,0x00,0xFE,0xC6,0x06,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00}, // 7 (55)
  {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // 8 (56)
  {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00}, // 9 (57)
  {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00}, // : (58)
  {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00}, // ; (59)
  {0x00,0x00,0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00}, // < (60)
  {0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // = (61)
  {0x00,0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00}, // > (62)
  {0x00,0x00,0x7C,0xC6,0xC6,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00}, // ? (63)
  {0x00,0x00,0x00,0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0x7C,0x00,0x00,0x00,0x00}, // @ (64)
  {0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, // A (65)
  {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00}, // B (66)
  {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00}, // C (67)
  {0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00}, // D (68)
  {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00}, // E (69)
  {0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, // F (70)
  {0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00}, // G (71)
  {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, // H (72)
  {0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // I (73)
  {0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00}, // J (74)
  {0x00,0x00,0xE6,0x66,0x6C,0x6C,0x78,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00}, // K (75)
  {0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00}, // L (76)
  {0x00,0x00,0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, // M (77)
  {0x00,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00}, // N (78)
  {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // O (79)
  {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, // P (80)
  {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0E,0x00,0x00,0x00}, // Q (81)
  {0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00}, // R (82)
  {0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // S (83)
  {0x00,0x00,0xFF,0xDB,0x99,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // T (84)
  {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // U (85)
  {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00}, // V (86)
  {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xFE,0xEE,0x6C,0x00,0x00,0x00,0x00}, // W (87)
  {0x00,0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x38,0x7C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00}, // X (88)
  {0x00,0x00,0xC3,0xC3,0x66,0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // Y (89)
  {0x00,0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00}, // Z (90)
  {0x00,0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00}, // [ (91)
  {0x00,0x00,0x00,0x80,0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00,0x00,0x00,0x00,0x00}, // \ (92)
  {0x00,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00}, // ] (93)
  {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ^ (94)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00}, // _ (95)
  {0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ` (96)
  {0x00,0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // a (97)
  {0x00,0x00,0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00}, // b (98)
  {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00}, // c (99)
  {0x00,0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // d (100)
  {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00}, // e (101)
  {0x00,0x00,0x1C,0x36,0x32,0x30,0x78,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00}, // f (102)
  {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00,0x00}, // g (103)
  {0x00,0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00}, // h (104)
  {0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // i (105)
  {0x00,0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00,0x00}, // j (106)
  {0x00,0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00}, // k (107)
  {0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00}, // l (108)
  {0x00,0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0x00,0x00,0x00,0x00}, // m (109)
  {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00}, // n (110)
  {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00}, // o (111)
  {0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00,0x00}, // p (112)
  {0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00,0x00}, // q (113)
  {0x00,0x00,0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00}, // r (114)
  {0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00}, // s (115)
  {0x00,0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0x00,0x00,0x00,0x00}, // t (116)
  {0x00,0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00}, // u (117)
  {0x00,0x00,0x00,0x00,0x00,0xC3,0xC3,0x66,0x66,0x3C,0x3C,0x18,0x00,0x00,0x00,0x00}, // v (118)
  {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xFE,0xEE,0x6C,0x00,0x00,0x00,0x00}, // w (119)
  {0x00,0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, // x (120)
  {0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0xF8,0x00,0x00}, // y (121)
  {0x00,0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00}, // z (122)
  {0x00,0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00}, // { (123)
  {0x00,0x00,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00}, // | (124)
  {0x00,0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00}, // } (125)
  {0x00,0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ~ (126)
};

// Batched character renderer - single glBegin/glEnd per draw_text call
static void draw_char_gl(int x, int y, char ch, float r, float g, float b) {
    if (ch < 32 || ch > 126) return;
    const unsigned char* glyph = g_font_data[ch - 32];
    // Color is set once by caller in batched mode
    for (int row = 0; row < 16; row++) {
        unsigned char bits = glyph[row];
        if (!bits) continue;
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                float px = (float)(x + col);
                float py = (float)(y + row);
                glVertex2f(px, py);
                glVertex2f(px + 1, py);
                glVertex2f(px + 1, py + 1);
                glVertex2f(px, py + 1);
            }
        }
    }
}

// =========================================================================
// extern "C" - NY interop functions
// =========================================================================

extern "C" {

// ---- Window management ----

int create_window_impl(const char* title, int width, int height) {
    if (g_window) return 0;
    g_window_width = width;
    g_window_height = height;

    g_window = createWindow();
    WindowConfig cfg;
    cfg.title = title;
    cfg.width = width;
    cfg.height = height;
    cfg.resizable = true;
    cfg.fullscreen = false;
    cfg.center_on_screen = true;

    if (g_window->initialize(cfg)) {
        // Get the GLFW window handle for clipboard access
        // (stored during initialization in GLFWWindow)

        // Set up event callbacks
        g_window->setKeyCallback([](const KeyEvent& e) {
            if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
                int code = convert_key(e.key, e.mods);
                if (code != 0) {
                    // Special key or Ctrl+key - push event
                    ny_event ev = {0};
                    ev.event_type = EVENT_KEY_PRESS;
                    ev.key_code = code;
                    ev.modifiers = convert_mods(e.mods);
                    push_event(ev);
                }
                // Printable chars (code == 0) handled by char callback below
            }
        });

        g_window->setMouseCallback([](const MouseEvent& e) {
            if (e.button >= 0) {
                // Mouse button event
                if (e.action == GLFW_PRESS) {
                    ny_event ev = {0};
                    ev.event_type = EVENT_MOUSE_CLICK;
                    ev.mouse_x = (int)e.x;
                    ev.mouse_y = (int)e.y;
                    ev.modifiers = convert_mods(e.mods);
                    push_event(ev);
                }
            } else {
                // Mouse motion event
                ny_event ev = {0};
                ev.event_type = EVENT_MOUSE_MOTION;
                ev.mouse_x = (int)e.x;
                ev.mouse_y = (int)e.y;
                push_event(ev);
            }
        });

        g_window->setResizeCallback([](int w, int h) {
            g_window_width = w;
            g_window_height = h;
            ny_event ev = {0};
            ev.event_type = EVENT_WINDOW_RESIZE;
            ev.width = w;
            ev.height = h;
            push_event(ev);
        });

        g_window->setCloseCallback([]() {
            ny_event ev = {0};
            ev.event_type = EVENT_WINDOW_CLOSE;
            push_event(ev);
        });

        // Scroll wheel callback
        GLFWwindow* glfw_win = glfwGetCurrentContext();
        if (glfw_win) {
            glfwSetScrollCallback(glfw_win, [](GLFWwindow* w, double xoffset, double yoffset) {
                ny_event ev = {0};
                ev.event_type = EVENT_MOUSE_WHEEL;
                double mx, my;
                glfwGetCursorPos(w, &mx, &my);
                ev.mouse_x = (int)mx;
                ev.mouse_y = (int)my;
                ev.key_code = (int)(yoffset * 3); // Scroll delta (positive = up)
                push_event(ev);
            });
        }

        // Character callback for printable text input (proper lowercase/uppercase)
        glfw_win = glfwGetCurrentContext();
        if (glfw_win) {
            glfwSetCharCallback(glfw_win, [](GLFWwindow* w, unsigned int codepoint) {
                if (codepoint >= 32 && codepoint < 127) {
                    ny_event ev = {0};
                    ev.event_type = EVENT_KEY_PRESS;
                    ev.key_code = (int)codepoint;
                    ev.modifiers = 0;
                    push_event(ev);
                }
            });
        }

        // Initialize OpenGL 2D projection
        glViewport(0, 0, width, height);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, width, height, 0, -1, 1); // Top-left origin
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDisable(GL_DEPTH_TEST);

        return 1;
    } else {
        delete g_window;
        g_window = nullptr;
        return 0;
    }
}

void destroy_window_impl(int handle) {
    if (g_window) {
        g_window->destroy();
        delete g_window;
        g_window = nullptr;
    }
}

int begin_draw_impl(int handle) {
    if (!g_window) return 0;
    // Update projection on each frame in case of resize
    glViewport(0, 0, g_window_width, g_window_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, g_window_width, g_window_height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    return 1;
}

void clear_window_impl(int dc, int color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void end_draw_impl(int dc) {
    if (g_window) {
        g_window->swapBuffers();
    }
}

int show_window_impl(int handle) {
    if (g_window) {
        g_window->show();
        return 0;
    }
    return 1;
}

void poll_events_impl() {
    if (g_window) {
        g_window->pollEvents();
    }
}

void wait_events_timeout_impl(int timeout_ms) {
    if (g_window) {
        double seconds = timeout_ms / 1000.0;
        if (seconds < 0.001) seconds = 0.001;
        glfwWaitEventsTimeout(seconds);
    }
}

void post_empty_event_impl() {
    glfwPostEmptyEvent();
}

int get_window_width_impl() {
    return g_window_width;
}

int get_window_height_impl() {
    return g_window_height;
}

int should_close_impl() {
    if (g_window) {
        return g_window->shouldClose() ? 1 : 0;
    }
    return 1;
}

// ---- Event queue ----

int pop_event_impl() {
    if (g_event_count <= 0) {
        g_popped_event = {0};
        return 0;
    }
    g_popped_event = g_event_queue[0];
    // Shift events forward
    for (int i = 1; i < g_event_count; i++) {
        g_event_queue[i-1] = g_event_queue[i];
    }
    g_event_count--;
    return 1;
}

int get_event_type_impl() { return g_popped_event.event_type; }
int get_event_key_impl() { return g_popped_event.key_code; }
int get_event_x_impl() { return g_popped_event.mouse_x; }
int get_event_y_impl() { return g_popped_event.mouse_y; }
int get_event_modifiers_impl() { return g_popped_event.modifiers; }

// ---- Rendering ----

void fill_rect_impl(int handle, int x, int y, int width, int height, int color) {
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;

    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    glVertex2f((float)x, (float)y);
    glVertex2f((float)(x + width), (float)y);
    glVertex2f((float)(x + width), (float)(y + height));
    glVertex2f((float)x, (float)(y + height));
    glEnd();
}

void draw_text_impl(int handle, int x, int y, const char* text_str, int color) {
    if (!text_str || !*text_str) return;
    float r = ((color >> 16) & 0xFF) / 255.0f;
    float g = ((color >> 8) & 0xFF) / 255.0f;
    float b = (color & 0xFF) / 255.0f;

    // Batch all character pixels in a single GL draw call
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    int cx = x;
    for (const char* p = text_str; *p; p++) {
        if (*p == '\n') break;
        draw_char_gl(cx, y, *p, r, g, b);
        cx += 8;
    }
    glEnd();
}

// ---- Time ----

int get_simulated_time_ms_impl() {
    return (int)(glfwGetTime() * 1000.0);
}

// ---- Clipboard ----

void set_clipboard_text_impl(const char* text) {
    // Use GLFW clipboard - need the window handle
    // Try to get it from g_window
    // For now, use a static buffer fallback
    static std::string clipboard_buffer;
    clipboard_buffer = text ? text : "";
    // TODO: Use glfwSetClipboardString when we have the GLFW window pointer
}

static std::string g_clipboard_result;
const char* get_clipboard_text_impl() {
    // TODO: Use glfwGetClipboardString when we have the GLFW window pointer
    return g_clipboard_result.c_str();
}

// ---- File I/O ----

static std::string g_file_read_result;
const char* read_file_impl(const char* path) {
    g_file_read_result.clear();
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    g_file_read_result = ss.str();
    return g_file_read_result.c_str();
}

int write_file_impl(const char* path, const char* content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return 0;
    }
    file << (content ? content : "");
    return file.good() ? 1 : 0;
}

int file_exists_impl(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
}

static std::string g_list_dir_result;
const char* list_directory_impl(const char* path) {
    g_list_dir_result.clear();
    DIR* dir = opendir(path);
    if (!dir) return "";

    struct dirent* entry;
    bool first = true;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!first) g_list_dir_result += "|";
        first = false;
        // Check if directory
        std::string full_path = std::string(path) + "/" + entry->d_name;
        struct stat st;
        int is_dir = 0;
        if (stat(full_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            is_dir = 1;
        }
        g_list_dir_result += std::to_string(is_dir) + ":" + entry->d_name;
    }
    closedir(dir);
    return g_list_dir_result.c_str();
}

// =========================================================================
// String utility FFI functions (for char_utils / string_utils modules)
// =========================================================================

static std::string g_str_result;

const char* _char_from_int(int32_t i) {
    g_str_result = std::string(1, static_cast<char>(i));
    return g_str_result.c_str();
}

const char* _substring(const char* s, int32_t start, int32_t length) {
    std::string src(s ? s : "");
    if (start < 0) start = 0;
    if (start >= (int32_t)src.size()) { g_str_result = ""; return g_str_result.c_str(); }
    g_str_result = src.substr(start, length);
    return g_str_result.c_str();
}

int32_t _string_length(const char* s) {
    return s ? (int32_t)strlen(s) : 0;
}

const char* _char_at(const char* s, int32_t index) {
    if (!s || index < 0 || index >= (int32_t)strlen(s)) {
        g_str_result = "";
    } else {
        g_str_result = std::string(1, s[index]);
    }
    return g_str_result.c_str();
}

int32_t _char_code(const char* c) {
    return (c && c[0]) ? (int32_t)(unsigned char)c[0] : 0;
}

const char* _char_from_code(int32_t code) {
    g_str_result = std::string(1, static_cast<char>(code));
    return g_str_result.c_str();
}

const char* _int_to_string(int32_t value) {
    g_str_result = std::to_string(value);
    return g_str_result.c_str();
}

// =========================================================================
// Process execution (for build system integration)
// =========================================================================

static std::string g_command_output;

// Run a command and capture its output
// Returns the stdout/stderr output as a string
const char* run_command_impl(const char* cmd) {
    g_command_output = "";
    if (!cmd) return g_command_output.c_str();

    // Use popen to run command and capture output
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        g_command_output = "Error: Failed to run command";
        return g_command_output.c_str();
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        g_command_output += buffer;
    }

    int status = pclose(pipe);

    // Append exit code info
    if (status != 0) {
        g_command_output += "\n[Exit code: " + std::to_string(WEXITSTATUS(status)) + "]";
    }

    return g_command_output.c_str();
}

// Get just the exit code of the last run_command
static int g_last_exit_code = 0;

int run_command_exit_code_impl() {
    return g_last_exit_code;
}

// =========================================================================
// IPC Hot Reload Client Implementation
// =========================================================================

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#endif

// IPC client state
static int g_ipc_client_fd = -1;
static bool g_ipc_connected = false;

// Read exact bytes from socket
static bool read_exact(int fd, void* buf, size_t len) {
    char* ptr = static_cast<char*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = read(fd, ptr, remaining);
        if (n <= 0) return false;
        ptr += n;
        remaining -= n;
    }
    return true;
}

// Write exact bytes to socket
static bool write_exact(int fd, const void* buf, size_t len) {
    const char* ptr = static_cast<const char*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = write(fd, ptr, remaining);
        if (n <= 0) return false;
        ptr += n;
        remaining -= n;
    }
    return true;
}

// Read length-prefixed frame
static bool read_frame(int fd, std::string& out) {
    uint32_t len_be;
    if (!read_exact(fd, &len_be, 4)) return false;
    uint32_t len = ntohl(len_be);
    if (len > 10 * 1024 * 1024) return false; // 10MB max
    out.resize(len);
    return read_exact(fd, out.data(), len);
}

// Write length-prefixed frame
static bool write_frame(int fd, const std::string& data) {
    uint32_t len_be = htonl(static_cast<uint32_t>(data.size()));
    if (!write_exact(fd, &len_be, 4)) return false;
    return write_exact(fd, data.data(), data.size());
}

int ipc_hot_reload_client_connect_impl(const char* socket_path) {
    if (g_ipc_connected) {
        return 1; // Already connected
    }

    if (!socket_path) {
        return 0;
    }

#ifdef _WIN32
    // Windows implementation (placeholder)
    return 0;
#else
    // Unix domain socket
    g_ipc_client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_ipc_client_fd < 0) {
        return 0;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(g_ipc_client_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(g_ipc_client_fd);
        g_ipc_client_fd = -1;
        return 0;
    }

    g_ipc_connected = true;
    return 1;
#endif
}

void ipc_hot_reload_client_disconnect_impl() {
    if (g_ipc_connected && g_ipc_client_fd >= 0) {
        close(g_ipc_client_fd);
        g_ipc_client_fd = -1;
        g_ipc_connected = false;
    }
}

int ipc_hot_reload_client_is_connected_impl() {
    return g_ipc_connected ? 1 : 0;
}

int ipc_hot_reload_client_send_reload_impl(const char* target) {
    if (!g_ipc_connected || g_ipc_client_fd < 0) {
        return 0;
    }

    // Build JSON reload command
    static int next_id = 1;
    std::string json = "{\"id\":" + std::to_string(next_id++) +
                       ",\"type\":\"reload\",\"target\":\"" +
                       (target ? target : "all") + "\"}";

    // Send request
    if (!write_frame(g_ipc_client_fd, json)) {
        ipc_hot_reload_client_disconnect_impl();
        return 0;
    }

    // Wait for response with timeout
    struct pollfd pfd;
    pfd.fd = g_ipc_client_fd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 5000); // 5 second timeout
    if (ret <= 0) {
        ipc_hot_reload_client_disconnect_impl();
        return 0;
    }

    // Read response
    std::string response;
    if (!read_frame(g_ipc_client_fd, response)) {
        ipc_hot_reload_client_disconnect_impl();
        return 0;
    }

    // Simple check for success (look for "ok":true)
    return response.find("\"ok\":true") != std::string::npos ? 1 : 0;
}

const char* ipc_hot_reload_client_get_socket_path_for_pid_impl(int32_t pid) {
    const char* runtime_dir = getenv("XDG_RUNTIME_DIR");
    std::string base = runtime_dir ? runtime_dir : "/tmp";
    g_str_result = base + "/ny_ai_" + std::to_string(pid) + ".sock";
    return g_str_result.c_str();
}

int32_t get_current_process_id_impl() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}

static int g_debug_socket = -1;
static std::string g_debug_socket_path;

int32_t debug_ipc_client_connect_impl(const char* socket_path) {
    if (g_debug_socket >= 0) {
        return 1;
    }

    g_debug_socket_path = socket_path;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    g_debug_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_debug_socket < 0) {
        return 0;
    }

    if (connect(g_debug_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(g_debug_socket);
        g_debug_socket = -1;
        return 0;
    }

    return 1;
}

void debug_ipc_client_disconnect_impl() {
    if (g_debug_socket >= 0) {
        close(g_debug_socket);
        g_debug_socket = -1;
    }
}

int32_t debug_ipc_client_is_connected_impl() {
    return g_debug_socket >= 0;
}

int32_t debug_ipc_client_send_command_impl(const char* command, const char* params) {
    if (g_debug_socket < 0) {
        return 0;
    }

    std::string json_cmd = "{\"command\":\"" + std::string(command) + "\"";
    if (strlen(params) > 0) {
        json_cmd += ",\"params\":\"" + std::string(params) + "\"";
    }
    json_cmd += "}";

    ssize_t sent = send(g_debug_socket, json_cmd.c_str(), json_cmd.length(), 0);
    return sent > 0 ? 1 : 0;
}

const char* debug_ipc_client_get_response_impl() {
    if (g_debug_socket < 0) {
        g_str_result = "{\"error\":\"Not connected\"}";
        return g_str_result.c_str();
    }

    char buffer[4096];
    ssize_t received = recv(g_debug_socket, buffer, sizeof(buffer) - 1, 0);
    if (received > 0) {
        buffer[received] = '\0';
        g_str_result = std::string(buffer);
    } else {
        g_str_result = "{\"error\":\"No response\"}";
    }

    return g_str_result.c_str();
}

static int g_debug_server_socket = -1;
static std::string g_debug_server_socket_path;
static std::vector<int> g_debug_client_sockets;

int32_t debug_ipc_server_start_impl(const char* socket_path) {
    if (g_debug_server_socket >= 0) {
        return 1;
    }

    g_debug_server_socket_path = socket_path;

    unlink(socket_path);

    g_debug_server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_debug_server_socket < 0) {
        return 0;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(g_debug_server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(g_debug_server_socket);
        g_debug_server_socket = -1;
        return 0;
    }

    if (listen(g_debug_server_socket, 5) < 0) {
        close(g_debug_server_socket);
        g_debug_server_socket = -1;
        return 0;
    }

    return 1;
}

void debug_ipc_server_stop_impl() {
    if (g_debug_server_socket >= 0) {
        close(g_debug_server_socket);
        g_debug_server_socket = -1;
        unlink(g_debug_server_socket_path.c_str());
    }

    for (int client_socket : g_debug_client_sockets) {
        close(client_socket);
    }
    g_debug_client_sockets.clear();
}

// =========================================================================
// LSP Client Implementation
// =========================================================================

#include <sstream>
#include <array>

// LSP client state
static FILE* g_lsp_server_process = nullptr;
static std::string g_lsp_server_path;
static std::string g_lsp_file_path;
static bool g_lsp_running = false;
static int g_lsp_next_id = 1;

void lsp_client_stop_impl();

// Read JSON response from LSP server
static std::string read_lsp_response() {
    std::string response;
    char line_buffer[1024];

    // Read Content-Length header
    while (fgets(line_buffer, sizeof(line_buffer), g_lsp_server_process)) {
        std::string line(line_buffer);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }

        if (line.find("Content-Length: ") == 0) {
            std::string length_str = line.substr(16);
            int content_length = std::stoi(length_str);

            fgets(line_buffer, sizeof(line_buffer), g_lsp_server_process);

            std::vector<char> buffer(content_length);
            if (fread(buffer.data(), 1, content_length, g_lsp_server_process) == content_length) {
                response = std::string(buffer.data(), content_length);
            }
            break;
        }
    }

    return response;
}

// Send JSON request to LSP server
static bool send_lsp_request(const std::string& json_request) {
    if (!g_lsp_server_process || !g_lsp_running) {
        return false;
    }

    // Send Content-Length header
    std::string header = "Content-Length: " + std::to_string(json_request.length()) + "\r\n\r\n";
    if (fwrite(header.c_str(), 1, header.length(), g_lsp_server_process) != header.length()) {
        return false;
    }

    // Send JSON content
    if (fwrite(json_request.c_str(), 1, json_request.length(), g_lsp_server_process) != json_request.length()) {
        return false;
    }

    fflush(g_lsp_server_process);
    return true;
}

int32_t lsp_client_start_impl(const char* server_path, const char* file_path) {
    if (g_lsp_running) {
        return 1; // Already running
    }

    if (!server_path || !file_path) {
        return 0;
    }

    g_lsp_server_path = server_path;
    g_lsp_file_path = file_path;

    // Start LSP server process
    std::string cmd = std::string(server_path) + " " + std::string(file_path);
    g_lsp_server_process = popen(cmd.c_str(), "r+");
    if (!g_lsp_server_process) {
        return 0;
    }

    g_lsp_running = true;
    g_lsp_next_id = 1;

    // Send initialize request
    std::string init_request = R"({
        "jsonrpc": "2.0",
        "id": )" + std::to_string(g_lsp_next_id++) + R"(,
        "method": "initialize",
        "params": {
            "rootUri": "file://)" + std::string(file_path) + R"(",
            "capabilities": {
                "textDocument": {
                    "completion": {},
                    "definition": {},
                    "hover": {}
                }
            }
        }
    })";

    if (!send_lsp_request(init_request)) {
        lsp_client_stop_impl();
        return 0;
    }

    // Read initialize response
    std::string response = read_lsp_response();
    if (response.empty()) {
        lsp_client_stop_impl();
        return 0;
    }

    // Send initialized notification
    std::string initialized_notification = R"({
        "jsonrpc": "2.0",
        "method": "initialized",
        "params": {}
    })";

    send_lsp_request(initialized_notification);

    return 1;
}

void lsp_client_stop_impl() {
    if (!g_lsp_running) {
        return;
    }

    // Send shutdown request
    std::string shutdown_request = R"({
        "jsonrpc": "2.0",
        "id": )" + std::to_string(g_lsp_next_id++) + R"(,
        "method": "shutdown"
    })";

    send_lsp_request(shutdown_request);

    // Send exit notification
    std::string exit_notification = R"({
        "jsonrpc": "2.0",
        "method": "exit"
    })";

    send_lsp_request(exit_notification);

    // Close process
    if (g_lsp_server_process) {
        pclose(g_lsp_server_process);
        g_lsp_server_process = nullptr;
    }

    g_lsp_running = false;
}

int32_t lsp_client_is_running_impl() {
    return g_lsp_running ? 1 : 0;
}

const char* lsp_client_send_request_impl(const char* request) {
    if (!g_lsp_running || !g_lsp_server_process || !request) {
        g_str_result = "";
        return g_str_result.c_str();
    }

    if (!send_lsp_request(std::string(request))) {
        g_str_result = "";
        return g_str_result.c_str();
    }

    std::string response = read_lsp_response();
    g_str_result = response;
    return g_str_result.c_str();
}

int32_t lsp_client_did_open_impl(const char* uri, const char* content) {
    if (!g_lsp_running || !uri || !content) {
        return 0;
    }

    // Escape content for JSON
    std::string escaped_content = content;
    size_t pos = 0;
    while ((pos = escaped_content.find("\"", pos)) != std::string::npos) {
        escaped_content.replace(pos, 1, "\\\"");
        pos += 2;
    }
    while ((pos = escaped_content.find("\n", pos)) != std::string::npos) {
        escaped_content.replace(pos, 1, "\\n");
        pos += 2;
    }

    std::string did_open_request = R"({
        "jsonrpc": "2.0",
        "method": "textDocument/didOpen",
        "params": {
            "textDocument": {
                "uri": ")" + std::string(uri) + R"(",
                "languageId": "ny",
                "version": 1,
                "text": ")" + escaped_content + R"("
            }
        }
    })";

    return send_lsp_request(did_open_request) ? 1 : 0;
}

int32_t lsp_client_did_change_impl(const char* uri, const char* content) {
    if (!g_lsp_running || !uri || !content) {
        return 0;
    }

    // Escape content for JSON
    std::string escaped_content = content;
    size_t pos = 0;
    while ((pos = escaped_content.find("\"", pos)) != std::string::npos) {
        escaped_content.replace(pos, 1, "\\\"");
        pos += 2;
    }
    while ((pos = escaped_content.find("\n", pos)) != std::string::npos) {
        escaped_content.replace(pos, 1, "\\n");
        pos += 2;
    }

    std::string did_change_request = R"({
        "jsonrpc": "2.0",
        "method": "textDocument/didChange",
        "params": {
            "textDocument": {
                "uri": ")" + std::string(uri) + R"(",
                "version": 2
            },
            "contentChanges": [{
                "text": ")" + escaped_content + R"("
            }]
        }
    })";

    return send_lsp_request(did_change_request) ? 1 : 0;
}

const char* lsp_client_completion_impl(const char* uri, int32_t line, int32_t column) {
    if (!g_lsp_running || !uri) {
        g_str_result = "";
        return g_str_result.c_str();
    }

    std::string completion_request = R"({
        "jsonrpc": "2.0",
        "id": )" + std::to_string(g_lsp_next_id++) + R"(,
        "method": "textDocument/completion",
        "params": {
            "textDocument": {
                "uri": ")" + std::string(uri) + R"("
            },
            "position": {
                "line": )" + std::to_string(line - 1) + R"(,
                "character": )" + std::to_string(column - 1) + R"(
            }
        }
    })";

    if (!send_lsp_request(completion_request)) {
        g_str_result = "";
        return g_str_result.c_str();
    }

    std::string response = read_lsp_response();
    g_str_result = response;
    return g_str_result.c_str();
}

const char* lsp_client_definition_impl(const char* uri, int32_t line, int32_t column) {
    if (!g_lsp_running || !uri) {
        g_str_result = "";
        return g_str_result.c_str();
    }

    std::string definition_request = R"({
        "jsonrpc": "2.0",
        "id": )" + std::to_string(g_lsp_next_id++) + R"(,
        "method": "textDocument/definition",
        "params": {
            "textDocument": {
                "uri": )" + std::string(uri) + R"("
            },
            "position": {
                "line": )" + std::to_string(line - 1) + R"(,
                "character": )" + std::to_string(column - 1) + R"(
            }
        }
    })";

    if (!send_lsp_request(definition_request)) {
        g_str_result = "";
        return g_str_result.c_str();
    }

    std::string response = read_lsp_response();
    g_str_result = response;
    return g_str_result.c_str();
}

const char* lsp_client_hover_impl(const char* uri, int32_t line, int32_t column) {
    if (!g_lsp_running || !uri) {
        g_str_result = "";
        return g_str_result.c_str();
    }

    std::string hover_request = R"({
        "jsonrpc": "2.0",
        "id": )" + std::to_string(g_lsp_next_id++) + R"(,
        "method": "textDocument/hover",
        "params": {
            "textDocument": {
                "uri": )" + std::string(uri) + R"("
            },
            "position": {
                "line": )" + std::to_string(line - 1) + R"(,
                "character": )" + std::to_string(column - 1) + R"(
            }
        }
    })";

    if (!send_lsp_request(hover_request)) {
        g_str_result = "";
        return g_str_result.c_str();
    }

    std::string response = read_lsp_response();
    g_str_result = response;
    return g_str_result.c_str();
}

const char* lsp_client_diagnostics_impl() {
    // For now, return empty diagnostics
    // In a real implementation, we'd store the latest diagnostic notifications
    g_str_result = "[]";
    return g_str_result.c_str();
}

} // extern "C"
