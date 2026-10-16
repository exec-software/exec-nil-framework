#include "dap.hpp"
#include "json_rpc.hpp"
#include "debugger_core.hpp"

#include <atomic>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace ny::debug {
namespace {
using Json = json::Value;

Json safe_text(const std::string& text) {
    // Programs may print byte strings; malformed UTF-8 must never corrupt the
    // adapter's JSON framing. Reuse the debugger's bounded-byte escaping.
    return *json::parse(Debugger::json_str(text), nullptr);
}

std::string string_arg(const Json& value, const std::string& name,
                       const std::string& fallback = {}) {
    const auto* field = value.get(name);
    if (!field) return fallback;
    if (!field->is_string()) throw std::runtime_error(name + " must be a string");
    return field->as_string();
}

int integer_arg(const Json& value, const std::string& name, int fallback = -1) {
    const auto* field = value.get(name);
    if (!field) return fallback;
    if (!field->is_number() || !std::isfinite(field->as_number()) ||
        field->as_number() < 0 || field->as_number() > 2147483647 ||
        std::floor(field->as_number()) != field->as_number())
        throw std::runtime_error(name + " must be a nonnegative integer");
    return static_cast<int>(field->as_number());
}

bool bool_arg(const Json& value, const std::string& name, bool fallback) {
    const auto* field = value.get(name);
    if (!field) return fallback;
    if (!field->is_bool()) throw std::runtime_error(name + " must be a boolean");
    return field->as_bool();
}

// A separate bounded DAP reader avoids treating JSON Lines or an unbounded
// Content-Length as protocol input. DAP JSON is not JSON-RPC.
bool read_request(std::istream& input, Json& request) {
    std::string line;
    size_t header_bytes = 0;
    size_t length = 0;
    bool have_length = false;
    for (;;) {
        line.clear();
        for (;;) {
            const int byte = input.get();
            if (byte == std::char_traits<char>::eof()) {
                if (header_bytes == 0 && line.empty()) return false;
                throw std::runtime_error("truncated DAP header");
            }
            if (++header_bytes > 8192) throw std::runtime_error("DAP header exceeds 8 KiB");
            if (byte == '\n') break;
            line.push_back(static_cast<char>(byte));
        }
        if (line.empty() || line.back() != '\r')
            throw std::runtime_error("DAP headers require CRLF");
        line.pop_back();
        if (line.empty()) break;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) throw std::runtime_error("malformed DAP header");
        std::string name = line.substr(0, colon);
        for (char& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (name != "content-length") continue;
        if (have_length) throw std::runtime_error("duplicate Content-Length");
        have_length = true;
        const size_t begin = line.find_first_not_of(" \t", colon + 1);
        if (begin == std::string::npos) throw std::runtime_error("empty Content-Length");
        const auto parsed = std::from_chars(line.data() + begin, line.data() + line.size(), length);
        if (parsed.ec != std::errc{} || parsed.ptr != line.data() + line.size() ||
            length == 0 || length > 1024 * 1024)
            throw std::runtime_error("Content-Length must be between 1 and 1048576 bytes");
    }
    if (!have_length) throw std::runtime_error("missing Content-Length");
    std::string body(length, '\0');
    if (!input.read(body.data(), static_cast<std::streamsize>(length)))
        throw std::runtime_error("truncated DAP body");
    json::ParseError error;
    auto parsed = json::parse(body, &error);
    if (!parsed || !parsed->is_object()) throw std::runtime_error("invalid DAP JSON object");
    request = std::move(*parsed);
    return true;
}

class OutputBuffer final : public std::streambuf {
public:
    explicit OutputBuffer(std::function<void(std::string)> send) : send_(std::move(send)) {}
private:
    int_type overflow(int_type value) override {
        if (!traits_type::eq_int_type(value, traits_type::eof()))
            send_(std::string(1, traits_type::to_char_type(value)));
        return traits_type::not_eof(value);
    }
    std::streamsize xsputn(const char* data, std::streamsize size) override {
        // Bound each output event without buffering the whole running program.
        std::streamsize offset = 0;
        while (offset < size) {
            const auto count = std::min<std::streamsize>(size - offset, 8192);
            send_(std::string(data + offset, static_cast<size_t>(count)));
            offset += count;
        }
        return size;
    }
    std::function<void(std::string)> send_;
};

class Adapter {
public:
    explicit Adapter(std::ostream& output)
        : output_(output), output_buffer_([this](std::string text) {
              event("output", json::object({{"category", "stdout"}, {"output", safe_text(text)}}));
          }), program_output_(&output_buffer_) {
        debugger_.set_silent_mode(true);
        debugger_.get_vm().setOutputStream(program_output_);
        debugger_.get_vm().setInputStream(program_input_);
    }
    ~Adapter() {
        debugger_.request_pause();
        if (worker_.joinable()) worker_.join();
    }

    bool dispatch(const Json& request) {
        std::lock_guard<std::mutex> lock(session_mutex_);
        int seq = -1;
        std::string command;
        try {
            seq = integer_arg(request, "seq");
            command = string_arg(request, "command");
            if (seq < 0 || command.empty() || string_arg(request, "type") != "request")
                throw std::runtime_error("expected a DAP request with seq and command");
            const Json empty = json::object({});
            const Json& args = request.get("arguments") ? *request.get("arguments") : empty;
            if (!args.is_object()) throw std::runtime_error("arguments must be an object");
            if (command == "initialize") {
                if (initialized_) throw std::runtime_error("already initialized");
                if (!bool_arg(args, "linesStartAt1", true) || !bool_arg(args, "columnsStartAt1", true) ||
                    string_arg(args, "pathFormat", "path") != "path")
                    throw std::runtime_error("Ny requires one-based positions and native path format");
                initialized_ = true;
                response(seq, command, json::object({
                    {"supportsConfigurationDoneRequest", true},
                    {"supportsConditionalBreakpoints", true},
                    {"supportsEvaluateForHovers", true},
                    {"supportsTerminateRequest", true}}));
                return true;
            }
            if (!initialized_) throw std::runtime_error("initialize is required");
            if (command == "disconnect" || command == "terminate") {
                terminate_requested_.store(true);
                debugger_.request_pause();
                response(seq, command);
                if (!running_.load()) event("terminated");
                return command != "disconnect";
            }
            if (command == "pause") {
                require_thread(args);
                if (!running_.load()) throw std::runtime_error("the program is not running");
                debugger_.request_pause();
                response(seq, command);
                return true;
            }
            if (running_.load()) throw std::runtime_error("pause the program before inspection or mutation");
            if (command == "launch") {
                if (launched_) throw std::runtime_error("one launch is supported per adapter process");
                const auto program = string_arg(args, "program");
                if (program.empty()) throw std::runtime_error("launch.program must name a compiled NIL file");
                std::vector<std::string> program_args;
                if (const auto* items = args.get("args")) {
                    if (!items->is_array()) throw std::runtime_error("args must be an array of strings");
                    for (const auto& item : items->as_array()) {
                        if (!item.is_string()) throw std::runtime_error("args must contain only strings");
                        program_args.push_back(item.as_string());
                    }
                }
                const auto cwd = string_arg(args, "cwd");
                if (!cwd.empty()) std::filesystem::current_path(cwd);
                if (!debugger_.load_program(program)) throw std::runtime_error("could not load NIL program");
                bool has_source = false;
                for (const auto& instruction : debugger_.get_vm().getProgram())
                    has_source = has_source || !instruction.source_file.empty();
                if (!has_source) throw std::runtime_error("source debugging requires NIL compiled with --debug-info");
                const auto memory_mode = string_arg(args, "memoryMode", "safe");
                if (memory_mode != "safe" && memory_mode != "gc" &&
                    memory_mode != "unsafe" && memory_mode != "manual")
                    throw std::runtime_error("memoryMode must be safe/gc or unsafe/manual");
                debugger_.get_vm().setMemoryMode(memory_mode == "safe" || memory_mode == "gc"
                    ? MemoryMode::SAFE_GC : MemoryMode::UNSAFE_MANUAL);
                debugger_.get_vm().setArgs(program_args);
                stop_on_entry_ = bool_arg(args, "stopOnEntry", true);
                launched_ = true;
                response(seq, command);
                event("initialized");
            } else if (command == "setBreakpoints") {
                require_launched();
                const auto* source = args.get("source");
                if (!source || !source->is_object()) throw std::runtime_error("source.path is required");
                auto path = string_arg(*source, "path");
                if (path.empty()) throw std::runtime_error("source.path is required");
                std::error_code path_error;
                const auto normalized = std::filesystem::weakly_canonical(path, path_error);
                if (!path_error) path = normalized.string();
                const auto* list = args.get("breakpoints");
                if (list && (!list->is_array() || list->as_array().size() > 4096))
                    throw std::runtime_error("breakpoints must be an array of at most 4096 entries");
                // Validate shapes before replacing this source's previous set.
                if (list) for (const auto& item : list->as_array()) {
                    if (!item.is_object() || integer_arg(item, "line") <= 0)
                        throw std::runtime_error("breakpoint.line must be positive");
                    (void)string_arg(item, "condition");
                    if (item.get("hitCondition") || item.get("logMessage"))
                        throw std::runtime_error("hit conditions and logpoints are not supported");
                }
                for (int id : source_breakpoints_[path]) debugger_.remove_breakpoint(id);
                source_breakpoints_[path].clear();
                Json::Array result;
                if (list) for (const auto& item : list->as_array()) {
                    const int line = integer_arg(item, "line");
                    std::string error;
                    const auto id = debugger_.set_breakpoint(path, line, string_arg(item, "condition"), &error);
                    Json bp = json::object({{"verified", id.has_value()}, {"line", int64_t(line)},
                                            {"source", json::object({{"path", path}})}});
                    if (id) {
                        source_breakpoints_[path].push_back(*id);
                        bp.mut_object()["id"] = Json(int64_t(*id));
                    } else bp.mut_object()["message"] = Json(error);
                    result.push_back(std::move(bp));
                }
                response(seq, command, json::object({{"breakpoints", std::move(result)}}));
            } else if (command == "configurationDone") {
                require_launched();
                if (configured_) throw std::runtime_error("configurationDone was already received");
                configured_ = true;
                try {
                    start(stop_on_entry_ ? "entry" : "continue", [&] { response(seq, command); });
                } catch (...) {
                    configured_ = false;
                    throw;
                }
            } else if (command == "threads") {
                require_launched();
                response(seq, command, json::object({{"threads", json::array({
                    json::object({{"id", int64_t(1)}, {"name", "Ny main"}})})}}));
            } else if (command == "continue" || command == "next" || command == "stepIn" || command == "stepOut") {
                require_stopped();
                require_thread(args);
                start(command, [&] {
                    response(seq, command, command == "continue"
                        ? json::object({{"allThreadsContinued", true}}) : json::object({}));
                });
            } else if (command == "stackTrace") {
                require_stopped();
                require_thread(args);
                const auto frames = json::parse(debugger_.get_stack_json(), nullptr)->as_array();
                const int start_frame = integer_arg(args, "startFrame", 0);
                const int levels = integer_arg(args, "levels", 0);
                Json::Array result;
                for (size_t index = static_cast<size_t>(start_frame); index < frames.size() &&
                     (levels == 0 || result.size() < static_cast<size_t>(levels)); ++index) {
                    if (index > static_cast<size_t>(std::numeric_limits<int>::max()))
                        throw std::runtime_error("stack depth exceeds adapter identifier range");
                    const int depth = static_cast<int>(index);
                    auto known = depth_frames_.find(depth);
                    if (known == depth_frames_.end()) {
                        if (next_frame_ == std::numeric_limits<int>::max())
                            throw std::runtime_error("frame identifier space exhausted; start a new debug session");
                        const int id = next_frame_++;
                        frame_depths_[id] = depth;
                        known = depth_frames_.emplace(depth, id).first;
                    }
                    const int id = known->second;
                    auto frame = json::object({{"id", int64_t(id)},
                        {"name", string_arg(frames[index], "fn")},
                        {"line", int64_t(0)}, {"column", int64_t(0)}});
                    const int pc = integer_arg(frames[index], "pc");
                    const auto& program = debugger_.get_vm().getProgram();
                    if (pc >= 0 && static_cast<size_t>(pc) < program.size() &&
                        !program[static_cast<size_t>(pc)].source_file.empty()) {
                        frame.mut_object()["source"] = json::object({{"path", program[static_cast<size_t>(pc)].source_file}});
                        frame.mut_object()["line"] = Json(int64_t(std::max(1, integer_arg(frames[index], "line", 1))));
                        frame.mut_object()["column"] = Json(int64_t(1));
                    }
                    result.push_back(std::move(frame));
                }
                response(seq, command, json::object({{"stackFrames", std::move(result)}, {"totalFrames", int64_t(frames.size())}}));
            } else if (command == "scopes") {
                require_stopped();
                const int depth = frame_depth(args);
                if (!frame_is_mapped(depth)) {
                    response(seq, command, json::object({{"scopes", Json::Array{}}}));
                    return true;
                }
                Json::Object values;
                for (const auto& local : debugger_.get_source_locals(depth)) {
                    auto value = json::parse(Debugger::format_value(local.value), nullptr);
                    values[local.name] = value ? *value : Json("<unavailable>");
                }
                const int reference = store_variables(Json(std::move(values)));
                response(seq, command, json::object({{"scopes", json::array({json::object({
                    {"name", "Locals"}, {"presentationHint", "locals"},
                    {"variablesReference", int64_t(reference)}, {"expensive", false}})})}}));
            } else if (command == "variables") {
                require_stopped();
                const int reference = integer_arg(args, "variablesReference");
                const auto found = variables_.find(reference);
                if (found == variables_.end()) throw std::runtime_error("unknown or expired variablesReference");
                const Json value = found->second;
                Json::Array result;
                if (value.is_object()) for (const auto& item : value.as_object())
                    result.push_back(variable(item.first, item.second));
                if (value.is_array()) for (size_t i = 0; i < value.as_array().size(); ++i)
                    result.push_back(variable(std::to_string(i), value.as_array()[i]));
                response(seq, command, json::object({{"variables", std::move(result)}}));
            } else if (command == "evaluate") {
                require_stopped();
                const int depth = args.get("frameId") ? frame_depth(args) : 0;
                if (!frame_is_mapped(depth))
                    throw std::runtime_error("source-local evaluation requires a mapped source frame");
                debugger_.set_frame(depth);
                const auto expression = string_arg(args, "expression");
                const auto value = debugger_.evaluate_expression(expression);
                debugger_.set_frame(-1);
                if (value.rfind("(undefined:", 0) == 0) throw std::runtime_error(value);
                auto parsed = json::parse(value, nullptr);
                response(seq, command, json::object({{"result", value},
                    {"variablesReference", int64_t(parsed ? store_variables(*parsed) : 0)}}));
            } else if (command == "setExceptionBreakpoints") {
                // Runtime errors always stop; no selectable filters are advertised.
                const auto* filters = args.get("filters");
                if (filters && (!filters->is_array() || !filters->as_array().empty()))
                    throw std::runtime_error("exception filters are not supported");
                response(seq, command, json::object({{"breakpoints", Json::Array{}}}));
            } else throw std::runtime_error("unsupported DAP request: " + command);
        } catch (const std::exception& error) {
            response(seq, command, json::object({}), false, error.what());
        }
        return true;
    }

private:
    void send(Json message) {
        std::lock_guard<std::mutex> lock(output_mutex_);
        // JSON numbers are represented exactly only through 2^53 - 1. Keep
        // sequence arithmetic unsigned and within that wire representation.
        if (next_sequence_ > 9007199254740991ULL)
            throw std::runtime_error("DAP sequence space exhausted");
        message.mut_object()["seq"] = Json(int64_t(next_sequence_++));
        const auto body = json::stringify(message);
        output_ << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
    }
    void event(const std::string& name, Json body = json::object({})) {
        send(json::object({{"type", "event"}, {"event", name}, {"body", std::move(body)}}));
    }
    void response(int seq, const std::string& command, Json body = json::object({}),
                  bool success = true, const std::string& message = {}) {
        auto result = json::object({{"type", "response"}, {"request_seq", int64_t(seq)},
            {"command", command}, {"success", success}, {"body", std::move(body)}});
        if (!success) result.mut_object()["message"] = safe_text(message);
        send(std::move(result));
    }
    void require_launched() const {
        if (!launched_) throw std::runtime_error("launch is required");
    }
    void require_stopped() const {
        require_launched();
        if (!configured_ || debugger_.get_state() != ExecutionState::Paused)
            throw std::runtime_error("the program is not stopped");
    }
    void require_thread(const Json& args) const {
        if (integer_arg(args, "threadId", 1) != 1) throw std::runtime_error("unknown threadId");
    }
    int frame_depth(const Json& args) const {
        const auto found = frame_depths_.find(integer_arg(args, "frameId"));
        if (found == frame_depths_.end()) throw std::runtime_error("unknown or expired frameId");
        return found->second;
    }
    bool frame_is_mapped(int depth) {
        const auto frames = debugger_.get_vm().getDebugFrames();
        if (depth < 0 || static_cast<size_t>(depth) >= frames.size()) return false;
        const size_t pc = depth == 0 ? debugger_.get_vm().getPC()
            : frames[frames.size() - static_cast<size_t>(depth)].return_pc;
        const auto& program = debugger_.get_vm().getProgram();
        return pc < program.size() && !program[pc].source_file.empty();
    }
    int store_variables(const Json& value) {
        if (!value.is_object() && !value.is_array()) return 0;
        if (variables_.size() >= 16384) throw std::runtime_error("variable inspection limit reached");
        if (next_variable_ == std::numeric_limits<int>::max())
            throw std::runtime_error("variable identifier space exhausted; start a new debug session");
        const int id = next_variable_++;
        variables_.emplace(id, value);
        return id;
    }
    Json variable(const std::string& name, const Json& value) {
        return json::object({{"name", name}, {"value", json::stringify(value)},
            {"variablesReference", int64_t(store_variables(value))}});
    }
    void start(const std::string& action, const std::function<void()>& acknowledge) {
        if (next_frame_ == std::numeric_limits<int>::max())
            throw std::runtime_error("frame identifier space exhausted; start a new debug session");
        if (worker_.joinable()) worker_.join();
        variables_.clear();
        frame_depths_.clear();
        depth_frames_.clear();
        debugger_.clear_pause_request();
        running_.store(true);
        try {
          worker_ = std::thread([this, action] {
            // dispatch holds this lock until the worker is created and its
            // successful response is sent. No debuggee output precedes that
            // response, and thread-start failure yields only one failure reply.
            { std::lock_guard<std::mutex> starting(session_mutex_); }
            if (action == "entry") debugger_.stop_at_entry();
            else if (action == "next") debugger_.step_over();
            else if (action == "stepIn") debugger_.step_into();
            else if (action == "stepOut") debugger_.step_out();
            else debugger_.continue_cooperatively();
            std::lock_guard<std::mutex> lock(session_mutex_);
            running_.store(false);
            if (terminate_requested_.load()) {
                debugger_.detach();
                event("terminated");
            } else if (debugger_.get_state() == ExecutionState::Terminated) {
                const auto requested = debugger_.get_vm().getRequestedExitCode();
                event("exited", json::object({{"exitCode", int64_t(requested ? *requested : debugger_.get_vm().getProgramExitCode())}}));
                event("terminated");
            } else {
                auto body = json::object({{"reason", debugger_.last_stop_reason()},
                    {"threadId", int64_t(1)}, {"allThreadsStopped", true}});
                if (!debugger_.last_error().empty()) body.mut_object()["text"] = safe_text(debugger_.last_error());
                if (debugger_.last_breakpoint_id() > 0 && debugger_.last_stop_reason() == "breakpoint")
                    body.mut_object()["hitBreakpointIds"] = json::array({Json(int64_t(debugger_.last_breakpoint_id()))});
                event("stopped", std::move(body));
            }
          });
        } catch (...) {
            running_.store(false);
            throw;
        }
        acknowledge();
        event("continued", json::object({{"threadId", int64_t(1)}, {"allThreadsContinued", true}}));
    }

    std::ostream& output_;
    std::mutex output_mutex_;
    std::mutex session_mutex_;
    std::uint64_t next_sequence_ = 1;
    Debugger debugger_;
    OutputBuffer output_buffer_;
    std::ostream program_output_;
    std::istringstream program_input_; // explicit EOF; control stdin is never debuggee input
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> terminate_requested_{false};
    bool initialized_ = false;
    bool launched_ = false;
    bool configured_ = false;
    bool stop_on_entry_ = true;
    int next_frame_ = 1;
    int next_variable_ = 1;
    std::map<int, int> frame_depths_;
    std::map<int, int> depth_frames_;
    std::map<int, Json> variables_;
    std::map<std::string, std::vector<int>> source_breakpoints_;
};
} // namespace

int run_dap(std::istream& input, std::ostream& output) {
    try {
        Adapter adapter(output);
        Json request;
        while (read_request(input, request)) if (!adapter.dispatch(request)) break;
        return 0;
    } catch (const std::exception& error) {
        // stdout is reserved for valid DAP frames even for malformed input.
        std::cerr << "nildebugger DAP: " << error.what() << '\n';
        return 1;
    }
}
} // namespace ny::debug
