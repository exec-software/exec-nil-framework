/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cwchar>
#include <iterator>
#include <stdexcept>
#if defined(__linux__)
#include <dlfcn.h>
#elif defined(__APPLE__)
#import <AppKit/AppKit.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Test-only, independent observation of the toolkit's real live top-level
// windows, so a lifecycle test can tell an actually released window from a
// merely invalidated handle. The question is the same on every platform and
// only the answerer differs. GTK is asked for its registry. AppKit is asked
// for the application's windows, which, like that registry, counts them
// whether or not they are on screen. Win32 has no registry at all, so the
// operating system's own top-level windows are counted instead.
//
// On macOS this header is Objective-C++, so whatever includes it is compiled
// as Objective-C++ too.
class UiRegistryObserver {
#if defined(__linux__)
  void *(*list_)() = nullptr;
  unsigned (*size_)(void *) = nullptr;
  void *library_ = nullptr;
#elif defined(_WIN32)
  // The provider's own window class, which must match window_class in
  // nilcompiler/src/ui_runtime_win32.cpp. Counting by class keeps the console
  // window and any other process window out of the tally.
  static constexpr const wchar_t *provider_class = L"NyUi.Window";
  static BOOL CALLBACK tally(HWND window, LPARAM parameter) {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return TRUE;
    wchar_t name[64] = {};
    const int length = GetClassNameW(window, name, static_cast<int>(std::size(name)));
    if (length > 0 && std::wcscmp(name, provider_class) == 0)
      ++*reinterpret_cast<unsigned *>(parameter);
    return TRUE;
  }
#endif
public:
  UiRegistryObserver() {
#if defined(__linux__)
    library_ = dlopen("libgtk-4.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library_) throw std::runtime_error("load GTK registry observer");
    list_ = reinterpret_cast<decltype(list_)>(dlsym(library_, "gtk_window_get_toplevels"));
    size_ = reinterpret_cast<decltype(size_)>(dlsym(library_, "g_list_model_get_n_items"));
    if (!list_ || !size_) {
      dlclose(library_);
      library_ = nullptr;
      throw std::runtime_error("GTK registry observer symbols unavailable");
    }
#elif defined(__APPLE__)
    // The provider creates the shared application before any window, so a nil
    // NSApp means nothing has been observed yet rather than an empty registry.
    if (!NSApp)
      throw std::runtime_error("AppKit registry observer requires an initialized application");
#endif
  }
  ~UiRegistryObserver() {
#if defined(__linux__)
    if (library_) dlclose(library_);
#endif
  }
  UiRegistryObserver(const UiRegistryObserver &) = delete;
  UiRegistryObserver &operator=(const UiRegistryObserver &) = delete;
#if defined(__linux__)
  unsigned count() const { return size_(list_()); }
#elif defined(__APPLE__)
  unsigned count() const {
    // AppKit completes window teardown on the run loop, so a window the
    // provider has already released is still listed if it is counted in the
    // same turn. Give the loop one bounded, nonblocking turn first; this is
    // the observer accommodating AppKit's bookkeeping, not the provider
    // holding anything.
    @autoreleasepool {
      [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate distantPast]];
    }
    return static_cast<unsigned>(NSApp ? NSApp.windows.count : 0);
  }
#elif defined(_WIN32)
  unsigned count() const {
    unsigned total = 0;
    EnumWindows(&tally, reinterpret_cast<LPARAM>(&total));
    return total;
  }
#else
  unsigned count() const { throw std::runtime_error("no registry observer for this platform"); }
#endif
};
