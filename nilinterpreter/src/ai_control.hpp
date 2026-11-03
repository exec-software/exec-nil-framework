#pragma once

// All VM access takes place at an instruction boundary on the execution thread.
// The transport thread submits bounded requests; it never reads mutable VM data.
#include "vm.hpp"
#include "../../common/strict_json.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

namespace ny_ai {
class Control {
    using Json = nil::detail::json;
    struct Request {
        Json input;
        std::string id, response;
        bool started = false, done = false;
    };
    std::mutex mutex_;
    std::condition_variable condition_;
    std::shared_ptr<Request> pending_;
    std::atomic<bool> pending_flag_{false};
    bool stopped_ = false; // protected by mutex_
    bool paused_, step_pending_ = false; // VM-owner thread only
    const unsigned pid_;
    const std::string version_;
    ny::VM::Checkpoint checkpoint_;
    std::uint64_t checkpoint_id_ = 0;
    using Extension = std::function<Json(const std::string&, const Json&)>;
    Extension extension_;

    static Json number(std::uint64_t value) {
        Json out; out.type = Json::kind::number; out.text = std::to_string(value); return out;
    }
    static std::string field(const Json& request, const std::string& name) {
        const auto& value = request.at(name);
        if (value.type != Json::kind::string || value.text.size() > 256)
            throw std::runtime_error("expected bounded string field: " + name);
        return value.text;
    }
    static std::string response(const std::string& id, const Json& result, const std::string& error = {}) {
        auto out = Json::map();
        out.object["id"] = Json::str(id);
        out.object["result"] = result;
        // Parser/runtime diagnostics can contain arbitrary program-controlled
        // text. Never let reporting a rejected command abort the VM itself.
        std::string safe_error = error.substr(0, 1024);
        for (auto& c : safe_error) if (static_cast<unsigned char>(c) >= 128) c = '?';
        out.object["error"] = error.empty() ? Json{} : Json::str(safe_error);
        auto encoded = nil::detail::dump(out);
        if (encoded.size() > 60 * 1024)
            throw std::runtime_error("control response exceeds 60 KiB limit");
        return encoded;
    }
    // Inspection is deliberately a bounded summary, not the checkpoint storage
    // format. The checkpoint itself retains the complete admitted VM state.
    static Json value_summary(const ny::Value& value) {
        const auto metadata = [](const std::string& text) {
            if (text.size() > 256) throw std::runtime_error("inspection metadata too long");
            nil::detail::valid_utf8(text); return Json::str(text);
        };
        auto out = Json::map();
        if (const auto* v = std::get_if<std::int64_t>(&value)) {
            out.object["kind"] = Json::str("integer");
            out.object["value"] = Json::str(std::to_string(*v)); // no JSON precision loss
        } else if (const auto* v = std::get_if<double>(&value)) {
            out.object["kind"] = Json::str("float");
            std::ostringstream text; text << std::setprecision(17) << *v;
            out.object["value"] = Json::str(text.str());
        } else if (const auto* v = std::get_if<std::string>(&value)) {
            out.object["kind"] = Json::str("text");
            out.object["bytes"] = number(v->size());
            out.object["truncated"] = Json::flag(v->size() > 256);
            // Hex is lossless for arbitrary Ny text bytes, including invalid UTF-8.
            std::string hex; constexpr char digits[] = "0123456789abcdef";
            for (std::size_t i = 0; i < std::min<std::size_t>(v->size(), 256); ++i) {
                auto c = static_cast<unsigned char>((*v)[i]); hex += digits[c >> 4]; hex += digits[c & 15];
            }
            out.object["hex"] = Json::str(std::move(hex));
        } else if (const auto* v = std::get_if<ny::ArrayHandle>(&value)) {
            out.object["kind"] = Json::str("array_handle");
            out.object["address"] = Json::str(std::to_string(v->address));
        } else if (const auto* v = std::get_if<ny::StructValue>(&value)) {
            out.object["kind"] = Json::str("struct"); out.object["type"] = metadata(v->type_name);
        } else if (const auto* v = std::get_if<ny::EnumValue>(&value)) {
            out.object["kind"] = Json::str("enum"); out.object["variant"] = metadata(v->variant_name);
        } else {
            out.object["kind"] = Json::str("aggregate_or_foreign");
        }
        return out;
    }
    static Json bindings(const std::unordered_map<std::string, ny::Value>& values) {
        if (values.size() > 128) throw std::runtime_error("inspection exceeds 128 bindings");
        auto out = Json::map();
        for (const auto& item : values) {
            if (item.first.size() > 256) throw std::runtime_error("inspection binding name too long");
            nil::detail::valid_utf8(item.first);
            out.object[item.first] = value_summary(item.second);
        }
        return out;
    }
    Json inspect(ny::VM& vm) const {
        auto out = Json::map();
        out.object["pc"] = number(vm.getPC()); out.object["paused"] = Json::flag(paused_);
        out.object["globals"] = bindings(vm.getGlobals());
        auto frames = Json::list();
        vm.visitDebugFrames(32, 128, [&](const std::string& function, std::size_t return_pc,
                                       const std::unordered_map<std::string, ny::Value>& locals) {
            auto item = Json::map();
            if (function.size() > 256) throw std::runtime_error("inspection function name too long");
            nil::detail::valid_utf8(function);
            item.object["function"] = Json::str(function);
            item.object["return_pc"] = number(return_pc);
            item.object["locals"] = bindings(locals); frames.array.push_back(std::move(item));
        });
        out.object["frames"] = std::move(frames);
        if (vm.getStack().size() > 128) throw std::runtime_error("inspection exceeds 128 stack values");
        auto stack = Json::list();
        for (const auto& value : vm.getStack()) stack.array.push_back(value_summary(value));
        out.object["stack"] = std::move(stack); return out;
    }
    Json dispatch(ny::VM& vm, const Json& request) {
        const auto method = field(request, "method");
        for (const auto& item : request.object) {
            if (item.first != "id" && item.first != "method" &&
                !(method == "vm.restore" && item.first == "checkpoint") &&
                !(method == "vm.send_msg" && item.first == "json"))
                throw std::runtime_error("unknown request field");
        }
        auto out = Json::map(); out.object["ok"] = Json::flag(true);
        if (method == "vm.status") {
            out.object["running"] = Json::flag(!paused_); out.object["paused"] = Json::flag(paused_);
            out.object["pid"] = number(pid_); out.object["ai_version"] = number(1);
            out.object["nil_version"] = Json::str(version_); out.object["pc"] = number(vm.getPC());
            auto capabilities = Json::list();
            for (const auto* item : {"pause", "resume", "step", "inspect", "checkpoint.same-process.v1"})
                capabilities.array.push_back(Json::str(item));
            out.object["capabilities"] = std::move(capabilities);
        } else if (method == "vm.pause") {
            paused_ = true; step_pending_ = false; vm.setDebugState(ny::VM::DebugState::PAUSED);
        } else if (method == "vm.resume" || method == "vm.step") {
            paused_ = false; step_pending_ = method == "vm.step";
            vm.setDebugState(ny::VM::DebugState::RUNNING);
        } else if (method == "vm.inspect" || method == "vm.stack" || method == "vm.locals" || method == "vm.globals") {
            return inspect(vm);
        } else if (method == "vm.snapshot") {
            if (!paused_) throw std::runtime_error("pause the VM before creating a checkpoint");
            auto candidate = vm.createCheckpoint();
            checkpoint_ = std::move(candidate); ++checkpoint_id_;
            out.object["checkpoint"] = Json::str(std::to_string(checkpoint_id_));
            out.object["size"] = number(checkpoint_.retainedBytes());
            out.object["pc"] = number(vm.getPC());
            out.object["scope"] = Json::str("same-process deterministic VM state; no external resources");
        } else if (method == "vm.restore") {
            if (!paused_) throw std::runtime_error("pause the VM before restoring a checkpoint");
            if (!checkpoint_.valid() || field(request, "checkpoint") != std::to_string(checkpoint_id_))
                throw std::runtime_error("unknown checkpoint; only the latest successful checkpoint is retained");
            vm.restoreCheckpoint(checkpoint_); step_pending_ = false;
            vm.setDebugState(ny::VM::DebugState::PAUSED);
            out.object["checkpoint"] = Json::str(std::to_string(checkpoint_id_)); out.object["pc"] = number(vm.getPC());
        } else if (method == "vm.send_msg" && extension_) {
            return extension_(method, request);
        } else {
            throw std::runtime_error("unsupported control method: " + method);
        }
        return out;
    }
public:
    Control(unsigned pid, std::string version, bool initially_paused, Extension extension = {})
        : paused_(initially_paused), pid_(pid), version_(std::move(version)), extension_(std::move(extension)) {}
    ~Control() { stop(); }
    void stop() noexcept {
        std::lock_guard<std::mutex> guard(mutex_); stopped_ = true; condition_.notify_all();
    }
    std::string request(const std::string& line) {
        auto work = std::make_shared<Request>();
        try {
            if (line.size() > 65536) throw std::runtime_error("request too large");
            work->input = nil::detail::json_parser(line).parse();
            work->id = field(work->input, "id"); (void)field(work->input, "method");
        } catch (const std::exception& e) { return response(work->id, Json{}, e.what()); }
        std::unique_lock<std::mutex> lock(mutex_);
        if (stopped_) return response(work->id, Json{}, "VM execution has ended");
        if (pending_) return response(work->id, Json{}, "control request already pending");
        pending_ = work; pending_flag_.store(true, std::memory_order_release); condition_.notify_all();
        if (!condition_.wait_for(lock, std::chrono::seconds(2), [&] { return work->done || stopped_; })) {
            if (!work->started && pending_ == work) {
                pending_.reset(); pending_flag_.store(false, std::memory_order_release);
                return response(work->id, Json{}, "VM did not reach a safe point; request cancelled");
            }
            return response(work->id, Json{}, "control operation is still completing; query state before retrying");
        }
        return work->done ? work->response : response(work->id, Json{}, "VM execution has ended");
    }
    void safepoint(ny::VM& vm) {
        if (step_pending_) { step_pending_ = false; paused_ = true; vm.setDebugState(ny::VM::DebugState::PAUSED); }
        if (paused_) vm.setDebugState(ny::VM::DebugState::PAUSED);
        if (!paused_ && !pending_flag_.load(std::memory_order_acquire)) return;
        for (;;) {
            std::shared_ptr<Request> work;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (paused_) condition_.wait(lock, [&] { return pending_ || stopped_; });
                if (stopped_) throw std::runtime_error("AI control stopped during execution");
                if (!pending_) return;
                work = std::move(pending_); work->started = true;
                pending_flag_.store(false, std::memory_order_release);
            }
            std::string reply;
            try { reply = response(work->id, dispatch(vm, work->input)); }
            catch (const std::exception& e) { reply = response(work->id, Json{}, e.what()); }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                work->response = std::move(reply); work->done = true; condition_.notify_all();
            }
            if (!paused_) return;
        }
    }
};
} // namespace ny_ai
