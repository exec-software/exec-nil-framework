#include "../common/standard_streams.hpp"

#ifdef _WIN32
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

class handle {
public:
  explicit handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
  ~handle() { reset(); }
  handle(const handle &) = delete;
  handle &operator=(const handle &) = delete;
  HANDLE get() const { return value_; }
  HANDLE release() { return std::exchange(value_, INVALID_HANDLE_VALUE); }
  void reset(HANDLE value = INVALID_HANDLE_VALUE) {
    if (value_ && value_ != INVALID_HANDLE_VALUE)
      CloseHandle(value_);
    value_ = value;
  }
private:
  HANDLE value_;
};

handle duplicate(HANDLE source) {
  HANDLE result = nullptr;
  require(DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(),
                          &result, 0, FALSE, DUPLICATE_SAME_ACCESS),
          "duplicate fixture handle");
  return handle(result);
}

// Detaching this test process never changes the parent's console. All code
// pages, modes, screen-buffer writes and injected input belong to a newly
// allocated console, which is destroyed when this process detaches/exits.
class private_console {
public:
  private_console() {
    (void)FreeConsole();
    require(AllocConsole(), "allocate private test console");
  }
  ~private_console() { FreeConsole(); }
};

void bind(FILE *stream, HANDLE source, int flags) {
  require(fflush(stream) == 0, "flush stream before fixture redirection");
  auto copy = duplicate(source);
  const int descriptor = _open_osfhandle(
      reinterpret_cast<std::intptr_t>(copy.get()), flags);
  require(descriptor >= 0, "open CRT fixture descriptor");
  (void)copy.release(); // _open_osfhandle owns this duplicate.
  const int result = _dup2(descriptor, _fileno(stream));
  _close(descriptor);
  require(result == 0, "bind CRT fixture descriptor");
  clearerr(stream);
}

struct pipe {
  handle reader, writer;
  pipe() {
    HANDLE input = nullptr, output = nullptr;
    require(CreatePipe(&input, &output, nullptr, 4096), "create fixture pipe");
    reader.reset(input);
    writer.reset(output);
  }
};

void reset_pages() {
  require(SetConsoleCP(437) && SetConsoleOutputCP(437), "set private CP437 control");
  require(GetConsoleCP() == 437 && GetConsoleOutputCP() == 437,
          "private CP437 control was not established");
}

void require_pages(UINT input, UINT output, const char *message) {
  require(GetConsoleCP() == input && GetConsoleOutputCP() == output, message);
}

std::wstring screen_text(HANDLE output, SHORT row, DWORD length) {
  std::wstring result(length, L'\0');
  DWORD count = 0;
  require(ReadConsoleOutputCharacterW(output, result.data(), length, {0, row}, &count)
              && count != 0 && count <= length, "read Unicode screen-buffer cells");
  // The requested span is in screen cells. A double-width CJK character
  // consumes two cells but returns one UTF-16 character; reading only its
  // leading cell can yield a clipping space instead of the stored character.
  result.resize(count);
  return result;
}

std::string codepoints(const std::wstring &text) {
  std::string result;
  for (const wchar_t character : text) {
    std::array<char, 16> code{};
    std::snprintf(code.data(), code.size(), "U+%04X ", static_cast<unsigned>(character));
    result += code.data();
  }
  return result;
}

void write_console(FILE *stream, HANDLE output, SHORT row, const char *text) {
  require(SetConsoleCursorPosition(output, {0, row}), "set private console cursor");
  const auto size = std::strlen(text);
  require(fwrite(text, 1, size, stream) == size && fflush(stream) == 0,
          "write UTF-8 through the binary CRT console stream");
}

void require_console_input(HANDLE input) {
  // Unicode keyboard events are converted by the console input code page,
  // then read through the same binary CRT path used by the language runtime.
  // Keep line mode, but disable echo to avoid changing output assertions.
  require(SetConsoleMode(input, ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT),
          "set private console input fixture mode");
  require(FlushConsoleInputBuffer(input), "clear private console input fixture");
  const std::wstring text = L"caf\u00e9 \u4e2d\u6587\r";
  std::vector<INPUT_RECORD> events(text.size());
  for (std::size_t i = 0; i != text.size(); ++i) {
    auto &event = events[i];
    event.EventType = KEY_EVENT;
    event.Event.KeyEvent.bKeyDown = TRUE;
    event.Event.KeyEvent.wRepeatCount = 1;
    event.Event.KeyEvent.wVirtualKeyCode = text[i] == L'\r' ? VK_RETURN : 0;
    event.Event.KeyEvent.uChar.UnicodeChar = text[i];
  }
  DWORD written = 0;
  require(WriteConsoleInputW(input, events.data(), static_cast<DWORD>(events.size()),
                             &written) && written == events.size(),
          "inject private Unicode keyboard fixture");
  std::array<char, 128> bytes{};
  const int count = _read(_fileno(stdin), bytes.data(), static_cast<unsigned>(bytes.size()));
  const std::string expected = u8"caf\u00e9 \u4e2d\u6587\r\n";
  require(count == static_cast<int>(expected.size()) &&
              std::string(bytes.data(), static_cast<std::size_t>(count)) == expected,
          "console input was not exact UTF-8 with untranslated CRLF");
}

void console_contract(HANDLE input, HANDLE output) {
  bind(stdin, input, _O_RDONLY | _O_BINARY);
  bind(stdout, output, _O_WRONLY | _O_BINARY);
  bind(stderr, output, _O_WRONLY | _O_BINARY);
  reset_pages();
  // Negative control proves this fixture observes the console decoding step,
  // not merely the UTF-8 bytes that were sent to it.
  write_console(stdout, output, 0, u8"caf\u00e9");
  require(screen_text(output, 0, 4) != L"caf\u00e9",
          "CP437 negative control unexpectedly decoded UTF-8 correctly");
  ny::use_untranslated_standard_streams();
  require_pages(CP_UTF8, CP_UTF8, "console streams did not select UTF-8");
  write_console(stdout, output, 1, u8"caf\u00e9");
  write_console(stdout, output, 2, u8"\u4e2d");
  write_console(stderr, output, 3, u8"\u6587");
  const auto latin = screen_text(output, 1, 4);
  const auto first_cjk = screen_text(output, 2, 2);
  const auto second_cjk = screen_text(output, 3, 2);
  if (latin != L"caf\u00e9" || first_cjk != L"\u4e2d" || second_cjk != L"\u6587")
    throw std::runtime_error("UTF-8 stdout/stderr rendered incorrect Unicode console cells: latin=" +
        codepoints(latin) + "; first CJK=" + codepoints(first_cjk) +
        "; second CJK=" + codepoints(second_cjk) +
        "; first CJK wide=" + codepoints(screen_text(output, 2, 8)) +
        "; second CJK wide=" + codepoints(screen_text(output, 3, 8)));
  require_console_input(input);
  ny::use_untranslated_standard_streams();
  require_pages(CP_UTF8, CP_UTF8, "console setup is not idempotent");
}

void expect_pipe_bytes(FILE *stream, HANDLE reader,
                       const std::vector<unsigned char> &expected) {
  require(fwrite(expected.data(), 1, expected.size(), stream) == expected.size() &&
              fflush(stream) == 0, "write redirected byte fixture");
  DWORD available = 0;
  require(PeekNamedPipe(reader, nullptr, 0, nullptr, &available, nullptr) &&
              available == expected.size(),
          "redirected output changed the byte count");
  std::vector<unsigned char> actual(expected.size());
  DWORD count = 0;
  require(ReadFile(reader, actual.data(), static_cast<DWORD>(actual.size()), &count, nullptr)
              && count == expected.size() && actual == expected,
          "redirected output changed bytes (including CR, LF, NUL or Ctrl-Z)");
}

void redirected_contract(HANDLE input, HANDLE output) {
  pipe incoming, outgoing, errors;
  std::vector<unsigned char> expected(256);
  for (std::size_t i = 0; i != expected.size(); ++i)
    expected[i] = static_cast<unsigned char>(i);
  DWORD count = 0;
  require(WriteFile(incoming.writer.get(), expected.data(),
                   static_cast<DWORD>(expected.size()), &count, nullptr)
              && count == expected.size(), "populate redirected input bytes");
  incoming.writer.reset();
  bind(stdin, incoming.reader.get(), _O_RDONLY | _O_TEXT);
  bind(stdout, outgoing.writer.get(), _O_WRONLY | _O_TEXT);
  bind(stderr, errors.writer.get(), _O_WRONLY | _O_TEXT);
  // Deliberately leave Win32 standard handles pointing at the private console
  // while CRT stdio is redirected: the helper must inspect the actual streams.
  require(SetStdHandle(STD_INPUT_HANDLE, input) && SetStdHandle(STD_OUTPUT_HANDLE, output)
              && SetStdHandle(STD_ERROR_HANDLE, output), "set divergent Win32 fixture handles");
  reset_pages();
  ny::use_untranslated_standard_streams();
  require_pages(437, 437, "redirected streams changed the attached console code pages");
  std::vector<unsigned char> actual(expected.size());
  require(fread(actual.data(), 1, actual.size(), stdin) == actual.size() && actual == expected,
          "redirected input changed bytes (including CR, LF, NUL or Ctrl-Z)");
  require(fgetc(stdin) == EOF && !ferror(stdin), "redirected input EOF changed");
  expect_pipe_bytes(stdout, outgoing.reader.get(), expected);
  expect_pipe_bytes(stderr, errors.reader.get(), expected);

  bind(stderr, output, _O_WRONLY | _O_BINARY);
  reset_pages();
  ny::use_untranslated_standard_streams();
  require_pages(437, CP_UTF8, "stderr-only console changed the wrong code page");
  write_console(stderr, output, 4, u8"\u4e2d");
  require(screen_text(output, 4, 2) == L"\u4e2d", "stderr-only UTF-8 console decoding");
  expect_pipe_bytes(stdout, outgoing.reader.get(), expected);

  bind(stderr, errors.writer.get(), _O_WRONLY | _O_BINARY);
  bind(stdout, output, _O_WRONLY | _O_BINARY);
  reset_pages();
  ny::use_untranslated_standard_streams();
  require_pages(437, CP_UTF8, "stdout-only console changed the wrong code page");
  write_console(stdout, output, 5, u8"caf\u00e9");
  require(screen_text(output, 5, 4) == L"caf\u00e9", "stdout-only UTF-8 console decoding");
  expect_pipe_bytes(stderr, errors.reader.get(), expected);

  bind(stdout, outgoing.writer.get(), _O_WRONLY | _O_BINARY);
  bind(stdin, input, _O_RDONLY | _O_BINARY);
  reset_pages();
  ny::use_untranslated_standard_streams();
  require_pages(CP_UTF8, 437, "stdin-only console changed the wrong code page");
  require_console_input(input);
  expect_pipe_bytes(stdout, outgoing.reader.get(), expected);
  expect_pipe_bytes(stderr, errors.reader.get(), expected);
}

void null_device_contract() {
  handle null(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
  require(null.get() != INVALID_HANDLE_VALUE && GetFileType(null.get()) == FILE_TYPE_CHAR,
          "create non-console character-device control");
  bind(stdin, null.get(), _O_RDONLY | _O_TEXT);
  bind(stdout, null.get(), _O_WRONLY | _O_TEXT);
  bind(stderr, null.get(), _O_WRONLY | _O_TEXT);
  reset_pages();
  ny::use_untranslated_standard_streams();
  require_pages(437, 437, "NUL character devices were mistaken for console streams");
}

void report(HANDLE destination, const std::string &message) {
  DWORD ignored = 0;
  (void)WriteFile(destination, message.data(), static_cast<DWORD>(message.size()),
                  &ignored, nullptr);
}
} // namespace

int main() {
  // Preserve only the test reporter's pipe/file. Never write console fixtures
  // or change code pages through a parent console handle.
  handle reporter;
  const HANDLE inherited_error = GetStdHandle(STD_ERROR_HANDLE);
  const DWORD kind = GetFileType(inherited_error);
  if (kind == FILE_TYPE_PIPE || kind == FILE_TYPE_DISK) {
    HANDLE saved = nullptr;
    if (DuplicateHandle(GetCurrentProcess(), inherited_error, GetCurrentProcess(),
                        &saved, 0, FALSE, DUPLICATE_SAME_ACCESS))
      reporter.reset(saved);
  }
  try {
    private_console console;
    handle input(CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    handle output(CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    require(input.get() != INVALID_HANDLE_VALUE && output.get() != INVALID_HANDLE_VALUE,
            "open private console fixture handles");
    console_contract(input.get(), output.get());
    redirected_contract(input.get(), output.get());
    null_device_contract();
    report(reporter.get(), "Windows console UTF-8 and redirected byte contracts passed\n");
    return 0;
  } catch (const std::exception &error) {
    report(reporter.get(), std::string("Windows console UTF-8 contract: ") + error.what() + "\n");
    return 1;
  }
}
#else
int main() {
  ny::use_untranslated_standard_streams();
  return 0;
}
#endif
