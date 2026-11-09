/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cwchar>
#include <iterator>
#include <stdexcept>
#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#if defined(__APPLE__)
#include <cstdlib>
#include <filesystem>
#endif
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Test-only, independent observation of the toolkit's real live top-level
// windows, so a lifecycle test can tell an actually released window from a
// merely invalidated handle. GTK is asked for its registry; Win32 has no such
// registry, so this counts the operating system's own top-level windows.
class UiRegistryObserver {
#if !defined(_WIN32)
  void *(*list_)() = nullptr;
  unsigned (*size_)(void *) = nullptr;
#endif
#if defined(__linux__) || defined(__APPLE__)
  void *library_ = nullptr;
#if defined(__APPLE__)
  void *gio_ = nullptr;
  int (*initialized_)() = nullptr;
  static std::filesystem::path selected(const char *name) {
    const char *directory = std::getenv("NIL_UI_GTK_DIRECTORY");
    if (!directory || !*directory || !std::filesystem::path(directory).is_absolute())
      throw std::runtime_error("GTK registry observer requires an absolute selected provider");
    auto file = std::filesystem::canonical(std::filesystem::path(directory) / name);
    if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("GTK registry dependency is not a regular file");
    return file;
  }
  static bool matches(void *symbol, const std::filesystem::path &file) {
    Dl_info info{};
    std::error_code error;
    return symbol && dladdr(symbol, &info) && info.dli_fname &&
        std::filesystem::path(info.dli_fname).is_absolute() &&
        std::filesystem::equivalent(file, info.dli_fname, error) && !error;
  }
#endif
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
#elif defined(__APPLE__)
    const auto gtk = selected("libgtk-4.1.dylib");
    const auto gio = selected("libgio-2.0.0.dylib");
    // Test-only GIO alias must identify GTK's already loaded dependency. Never
    // introduce another provider while trying to independently count windows.
    library_ = dlopen(gtk.c_str(), RTLD_NOLOAD | RTLD_NOW | RTLD_LOCAL);
    gio_ = dlopen(gio.c_str(), RTLD_NOLOAD | RTLD_NOW | RTLD_LOCAL);
    if (library_ && gio_) {
      auto list = dlsym(library_, "gtk_window_get_toplevels");
      auto size = dlsym(gio_, "g_list_model_get_n_items");
      auto initialized = dlsym(library_, "gtk_is_initialized");
      if (matches(list, gtk) && matches(initialized, gtk) && matches(size, gio) &&
          matches(dlsym(library_, "g_list_model_get_n_items"), gio)) {
        list_ = reinterpret_cast<decltype(list_)>(list);
        size_ = reinterpret_cast<decltype(size_)>(size);
        initialized_ = reinterpret_cast<decltype(initialized_)>(initialized);
      }
    }
#endif
#if !defined(_WIN32)
    if (!list_ || !size_) {
#if defined(__linux__) || defined(__APPLE__)
      if (library_) dlclose(library_);
      library_ = nullptr;
#if defined(__APPLE__)
      if (gio_) dlclose(gio_);
      gio_ = nullptr;
#endif
#endif
      throw std::runtime_error("GTK registry observer symbols unavailable");
    }
#endif
  }
  ~UiRegistryObserver() {
#if defined(__linux__) || defined(__APPLE__)
    if (library_) dlclose(library_);
#if defined(__APPLE__)
    if (gio_) dlclose(gio_);
#endif
#endif
  }
  UiRegistryObserver(const UiRegistryObserver &) = delete;
  UiRegistryObserver &operator=(const UiRegistryObserver &) = delete;
#if defined(_WIN32)
  unsigned count() const {
    unsigned total = 0;
    EnumWindows(&tally, reinterpret_cast<LPARAM>(&total));
    return total;
  }
#else
  unsigned count() const { return size_(list_()); }
#endif
#if defined(__APPLE__)
  bool initialized() const { return initialized_() != 0; }
#endif
};
