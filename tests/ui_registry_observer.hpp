/*
 * Copyright (C) 2008 - 2026 Execute Software. All rights reserved.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdexcept>
#if defined(__linux__)
#include <dlfcn.h>
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
#if defined(__linux__)
  void *library_ = nullptr;
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
#elif defined(_WIN32)
    const auto gtk = loaded(L"gtk-4-1.dll", L"libgtk-4-1.dll");
    const auto gio = loaded(L"gio-2.0-0.dll", L"libgio-2.0-0.dll");
    if (gtk) list_ = reinterpret_cast<decltype(list_)>(GetProcAddress(gtk, "gtk_window_get_toplevels"));
    if (gio) size_ = reinterpret_cast<decltype(size_)>(GetProcAddress(gio, "g_list_model_get_n_items"));
#endif
    if (!list_ || !size_) {
#if defined(__linux__)
      if (library_) dlclose(library_);
      library_ = nullptr;
#endif
      throw std::runtime_error("GTK registry observer symbols unavailable");
    }
  }
  ~GtkRegistryObserver() {
#if defined(__linux__)
    if (library_) dlclose(library_);
#endif
  }
  GtkRegistryObserver(const GtkRegistryObserver &) = delete;
  GtkRegistryObserver &operator=(const GtkRegistryObserver &) = delete;
  unsigned count() const { return size_(list_()); }
};
