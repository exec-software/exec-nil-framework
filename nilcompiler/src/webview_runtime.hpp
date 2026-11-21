/* Copyright (C) 2026 Execute Software. SPDX-License-Identifier: MIT */
#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace ny::webview {
struct Result {
  std::int64_t number = 0;
  std::string text, error;
  explicit operator bool() const noexcept { return error.empty(); }
};

// Each execution owns a context on the UI thread. The explicit absolute path
// selects trusted native code; the loader is not a sandbox for untrusted DLLs.
// Loaded libraries stay pinned for process lifetime, including late browser
// callbacks. Context handles and copied events never expose native addresses.
class Context {
public:
  Context();
  ~Context();
  Context(const Context &) = delete;
  Context &operator=(const Context &) = delete;
  void clear() noexcept;
  Result configure(const std::string &absolute_provider_path);
  Result window(const std::string &title, std::int64_t width, std::int64_t height);
  Result show(std::int64_t window);
  Result navigate(std::int64_t window, const std::string &url);
  Result load_html(std::int64_t window, const std::string &html);
  Result evaluate_function(std::int64_t window, const std::string &body);
  Result poll(std::int64_t window);
  Result event_number(std::int64_t snapshot, const std::string &field);
  Result event_text(std::int64_t snapshot, const std::string &field);
  Result release_event(std::int64_t snapshot);
  Result close(std::int64_t window);
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace ny::webview
