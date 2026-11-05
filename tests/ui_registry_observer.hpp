/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#pragma once
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

// Test-only, independent observation of GTK's real top-level registry. The
// provider must already be loaded; Windows never performs a second DLL search.
class GtkRegistryObserver {
  void *(*list_)() = nullptr;
  unsigned (*size_)(void *) = nullptr;
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
  static HMODULE loaded(const wchar_t *msvc, const wchar_t *mingw) {
    const auto first = GetModuleHandleW(msvc), second = GetModuleHandleW(mingw);
    if (first && second) throw std::runtime_error("ambiguous loaded GTK registry dependency");
    return first ? first : second;
  }
#endif
public:
  GtkRegistryObserver() {
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
#elif defined(_WIN32)
    const auto gtk = loaded(L"gtk-4-1.dll", L"libgtk-4-1.dll");
    const auto gio = loaded(L"gio-2.0-0.dll", L"libgio-2.0-0.dll");
    if (gtk) list_ = reinterpret_cast<decltype(list_)>(GetProcAddress(gtk, "gtk_window_get_toplevels"));
    if (gio) size_ = reinterpret_cast<decltype(size_)>(GetProcAddress(gio, "g_list_model_get_n_items"));
#endif
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
  }
  ~GtkRegistryObserver() {
#if defined(__linux__) || defined(__APPLE__)
    if (library_) dlclose(library_);
#if defined(__APPLE__)
    if (gio_) dlclose(gio_);
#endif
#endif
  }
  GtkRegistryObserver(const GtkRegistryObserver &) = delete;
  GtkRegistryObserver &operator=(const GtkRegistryObserver &) = delete;
  unsigned count() const { return size_(list_()); }
#if defined(__APPLE__)
  bool initialized() const { return initialized_() != 0; }
#endif
};
