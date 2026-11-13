#include "dap.hpp"
#include "json_rpc.hpp"
#include "../../common/private_directory.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

namespace {
using Json = ny::json::Value;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string request(int seq, const char* command, Json arguments = ny::json::object({})) {
    const auto body = ny::json::stringify(ny::json::object({{"seq", int64_t(seq)},
        {"type", "request"}, {"command", command}, {"arguments", std::move(arguments)}}));
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

// Only the reader consumes this stream. Its next command is released by an
// observed DAP event, so premature EOF cannot pause the VM before the marker.
// Even a missing event returns EOF within five seconds, allowing normal join.
class GatedInput final : public std::streambuf {
public:
    explicit GatedInput(std::vector<std::string> segments) : segments_(std::move(segments)) {
        select(0);
    }
    void release(unsigned stage) {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = std::max(ready_, stage);
        changed_.notify_one();
    }
    bool timed_out() const { return timed_out_; } // read only after run_dap joined its reader
private:
    void select(unsigned stage) {
        auto& segment = segments_.at(stage);
        setg(segment.data(), segment.data(), segment.data() + segment.size());
    }
    int_type underflow() override {
        if (++stage_ >= segments_.size()) return traits_type::eof();
        std::unique_lock<std::mutex> lock(mutex_);
        if (!changed_.wait_for(lock, std::chrono::seconds(5), [&] { return ready_ >= stage_; })) {
            timed_out_ = true;
            return traits_type::eof();
        }
        select(stage_);
        return traits_type::to_int_type(*gptr());
    }
    std::vector<std::string> segments_;
    unsigned stage_ = 0, ready_ = 0;
    bool timed_out_ = false;
    std::mutex mutex_;
    std::condition_variable changed_;
};

// Observe complete framed output emitted by run_dap itself. Adapter writes are
// serialized by its output mutex. Capture the actual writer of each debuggee
// stdout event, not the protocol reader or a recycled operating-system ID.
class ObservedOutput final : public std::streambuf {
public:
    explicit ObservedOutput(GatedInput& input) : input_(input), owner_(std::this_thread::get_id()) {}
    unsigned markers = 0;
    bool all_on_main = true, valid = true;
private:
    int_type overflow(int_type byte) override {
        if (!traits_type::eq_int_type(byte, traits_type::eof())) {
            const char value = traits_type::to_char_type(byte);
            xsputn(&value, 1);
        }
        return traits_type::not_eof(byte);
    }
    std::streamsize xsputn(const char* bytes, std::streamsize count) override {
        buffer_.append(bytes, static_cast<std::size_t>(count));
        if (buffer_.size() > 1024 * 1024) { valid = false; input_.release(2); return count; }
        for (;;) {
            const auto split = buffer_.find("\r\n\r\n");
            if (split == std::string::npos) break;
            if (buffer_.rfind("Content-Length: ", 0) != 0) { valid = false; input_.release(2); break; }
            const auto size = static_cast<std::size_t>(std::stoul(buffer_.substr(16, split - 16)));
            if (size > 1024 * 1024) { valid = false; input_.release(2); break; }
            if (buffer_.size() < split + 4 + size) break;
            auto message = ny::json::parse(buffer_.substr(split + 4, size), nullptr);
            buffer_.erase(0, split + 4 + size);
            if (!message || !message->is_object()) { valid = false; input_.release(2); continue; }
            const auto* event = message->get("event");
            if (!event || !event->is_string()) continue;
            if (event->as_string() == "stopped") input_.release(1);
            if (event->as_string() == "terminated") input_.release(2);
            if (event->as_string() != "output") continue;
            const auto* body = message->get("body");
            const auto* text = body ? body->get("output") : nullptr;
            if (text && text->is_string() &&
                (text->as_string() == "owner-first" || text->as_string() == "owner-second")) {
                ++markers;
                all_on_main = all_on_main && std::this_thread::get_id() == owner_;
            }
        }
        return count;
    }
    GatedInput& input_;
    const std::thread::id owner_;
    std::string buffer_;
};
} // namespace

int main() {
    const auto root = nil::detail::create_private_directory(std::filesystem::temp_directory_path(), "nil-dap-owner-");
    try {
        const auto program = root / "owner.nil", source = root / "owner.ny";
        const auto quoted = ny::json::stringify(Json(source.string()));
        std::ofstream file(program, std::ios::binary);
        file << "module dap_owner\nconst A = \"owner-first\"\nconst B = \"owner-second\"\nfunction main -> i32:\n";
        const std::vector<std::string> instructions = {"PUSH_CONST_STR A", "CALL println 1",
            "PUSH_CONST_STR B", "CALL println 1", "PUSH_CONST 0", "RET"};
        for (unsigned index = 0; index < instructions.size(); ++index)
            file << " # source_debug " << index << ' ' << quoted << " 0\n " << instructions[index]
                 << " ; line " << index / 2 + 1 << '\n';
        file << "end function\n";
        file.close();
        check(static_cast<bool>(file), "could not write owned NIL fixture");
        GatedInput input_buffer({
            request(1, "initialize") + request(2, "launch", ny::json::object({
                {"program", program.string()}, {"stopOnEntry", false}})) +
            request(3, "setBreakpoints", ny::json::object({{"source", ny::json::object({{"path", source.string()}})},
                {"breakpoints", ny::json::array({ny::json::object({{"line", int64_t(2)}})})}})) +
            request(4, "configurationDone"),
            request(5, "continue", ny::json::object({{"threadId", int64_t(1)}})), request(6, "disconnect")});
        ObservedOutput output_buffer(input_buffer);
        std::istream input(&input_buffer);
        std::ostream output(&output_buffer);
        const int status = ny::debug::run_dap(input, output);
        check(status == 0, "adapter failed during owner proof");
        check(!input_buffer.timed_out(), "adapter did not release bounded input gate");
        check(output_buffer.valid && output_buffer.markers == 2, "both real debuggee output markers are required");
        check(output_buffer.all_on_main, "debuggee output was executed off the calling process main thread");
        std::filesystem::remove_all(root);
        std::cout << "DAP owner: both execution segments ran on process main\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code ignored; std::filesystem::remove_all(root, ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
