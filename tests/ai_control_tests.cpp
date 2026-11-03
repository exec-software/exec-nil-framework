#include "../nilinterpreter/src/ai_control.hpp"
#include "nil_loader.hpp"

#include <atomic>
#include <exception>
#include <iostream>
#include <thread>

namespace {
using Json = nil::detail::json;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
std::atomic<unsigned> checks{0};
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void fixture(ny::VM& vm) {
    std::istringstream input(R"(
module ai_control_fixture
function main() -> i64:
  PUSH_CONST 7
  STORE_GLOBAL result
  PUSH_CONST 42
  RET
end function
)");
    std::unordered_map<std::string, ny::Value> constants;
    vm.loadProgram(ny::parseProgram(input, constants), constants);
    vm.resetExecution("main");
    vm.setGlobal("marker", std::int64_t{17});
}

Json command(const std::string& method, const std::string& id = "test") {
    auto out = Json::map();
    out.object["id"] = Json::str(id);
    out.object["method"] = Json::str(method);
    return out;
}
Json checked_response(const std::string& encoded) {
    require(encoded.size() <= 60 * 1024, "control response exceeds wire bound");
    auto response = nil::detail::json_parser(encoded).parse();
    require(response.type == Json::kind::object && response.object.size() == 3 &&
            response.at("id").type == Json::kind::string, "control response schema");
    return response;
}
Json ask(ny_ai::Control& control, const Json& request, bool success = true) {
    auto response = checked_response(control.request(nil::detail::dump(request)));
    require(response.at("id").text == request.at("id").text, "control response ID changed");
    if (success) {
        require(response.at("error").type == Json::kind::null, "expected successful control command");
        return response.at("result");
    }
    const auto& error = response.at("error");
    require(error.type == Json::kind::string && !error.text.empty() && error.text.size() <= 1024 &&
            response.at("result").type == Json::kind::null, "expected bounded control error");
    return response;
}
void error_contains(const Json& response, const std::string& expected) {
    require(response.at("error").text.find(expected) != std::string::npos, "unexpected control diagnostic");
}
Json message(const std::string& value) {
    auto out = command("vm.send_msg");
    out.object["json"] = Json::str(value);
    return out;
}

// Even a regressed paused-owner wait is released by a bounded control stop.
// The watchdog does not read or mutate VM state and always joins.
class Deadline {
public:
    explicit Deadline(ny_ai::Control& control, std::chrono::milliseconds limit = 8s)
        : worker_([this, &control, limit] {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!changed_.wait_for(lock, limit, [&] { return finished_; })) {
                expired_ = true;
                lock.unlock();
                control.stop();
            }
        }) {}
    ~Deadline() {
        { std::lock_guard<std::mutex> lock(mutex_); finished_ = true; }
        changed_.notify_all();
        worker_.join();
    }
    bool expired() const { return expired_; }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool finished_ = false;
    std::atomic<bool> expired_{false};
    std::thread worker_;
};

template<class Script> void paused_session(ny::VM& vm, Script script) {
    const auto owner = std::this_thread::get_id();
    unsigned extensions = 0;
    ny_ai::Control control(123, "test-version", true,
        [&](const std::string&, const Json& request) {
            require(std::this_thread::get_id() == owner, "VM extension left owner thread");
            ++extensions;
            const auto& action = request.at("json").text;
            if (action == "marker99") vm.setGlobal("marker", std::int64_t{99});
            else if (action == "marker123") vm.setGlobal("marker", std::int64_t{123});
            else if (action == "foreign") vm.setGlobal("foreign", ny::BoxedValue{});
            else if (action == "bad-struct") {
                ny::StructValue value; value.type_name = std::string(1, char(0xff));
                vm.setGlobal("metadata", value);
            } else if (action == "long-struct") {
                ny::StructValue value; value.type_name = std::string(257, 'x');
                vm.setGlobal("metadata", value);
            } else if (action == "bad-enum") {
                ny::EnumValue value; value.variant_name = std::string(1, char(0xff));
                vm.setGlobal("metadata", value);
            } else if (action == "long-enum") {
                ny::EnumValue value; value.variant_name = std::string(257, 'x');
                vm.setGlobal("metadata", value);
            } else if (action == "reset-metadata") vm.setGlobal("metadata", std::int64_t{0});
            else if (action == "raw-text") vm.setGlobal("raw", std::string("a\0\xff", 3));
            else throw std::runtime_error("unknown fixture action");
            auto out = Json::map(); out.object["ok"] = Json::flag(true); return out;
        });
    Deadline deadline(control);
    std::exception_ptr requester_failure, owner_failure;
    std::thread requester([&] {
        try {
            script(control);
            (void)ask(control, command("vm.resume"));
        } catch (...) {
            requester_failure = std::current_exception();
            control.stop();
        }
    });
    try { control.safepoint(vm); }
    catch (...) { owner_failure = std::current_exception(); control.stop(); }
    requester.join();
    if (requester_failure) std::rethrow_exception(requester_failure);
    if (owner_failure) std::rethrow_exception(owner_failure);
    require(!deadline.expired() && extensions > 0, "owner session deadline or extension execution missing");
    require(vm.getDebugState() == ny::VM::DebugState::RUNNING, "successful session did not resume VM");
    control.stop();
    error_contains(ask(control, command("vm.status"), false), "execution has ended");
}

void owner_dispatch_and_rejected_requests() {
    ny::VM vm;
    fixture(vm);
    paused_session(vm, [](ny_ai::Control& control) {
        const auto initial = ask(control, command("vm.status"));
        require(initial.at("paused").boolean && !initial.at("running").boolean &&
                initial.at("pid").text == "123" && initial.at("nil_version").text == "test-version",
                "owner status does not describe initial pause");
        (void)ask(control, message("marker99"));
        auto inspect = ask(control, command("vm.inspect"));
        require(inspect.at("globals").at("marker").at("value").text == "99" &&
                inspect.at("frames").array.at(0).at("function").text == "main",
                "owner-dispatched mutation or actual frame inspection missing");

        auto huge_key = command("vm.status");
        huge_key.object[std::string(63000, 'k')] = Json{};
        require(nil::detail::dump(huge_key).size() <= 65536, "oversized-key fixture exceeds request bound");
        error_contains(ask(control, huge_key, false), "unknown request field");
        const std::string key(32000, 'k');
        const auto duplicate = "{\"id\":\"duplicate\",\"method\":\"vm.status\",\"" + key +
                               "\":null,\"" + key + "\":null}";
        require(duplicate.size() <= 65536, "duplicate-key fixture exceeds request bound");
        const auto duplicate_reply = checked_response(control.request(duplicate));
        require(duplicate_reply.at("error").type == Json::kind::string &&
                duplicate_reply.at("error").text.size() <= 1024,
                "large parser diagnostic escaped bounded error encoding");
        error_contains(duplicate_reply, "duplicate JSON field");
        error_contains(ask(control, command("vm.nonexistent"), false), "unsupported control method");
        auto bad_pause = command("vm.resume"); bad_pause.object["ignored"] = Json::flag(true);
        (void)ask(control, bad_pause, false);
        require(ask(control, command("vm.status")).at("paused").boolean,
                "invalid mutating command changed pause state");

        for (const auto* action : {"bad-struct", "long-struct", "bad-enum", "long-enum"}) {
            (void)ask(control, message(action));
            (void)ask(control, command("vm.inspect"), false);
            require(ask(control, command("vm.status")).at("paused").boolean,
                    "inspection metadata error aborted or resumed VM");
            (void)ask(control, message("reset-metadata"));
            require(ask(control, command("vm.inspect")).at("globals").at("metadata").at("value").text == "0",
                    "inspection did not recover after malformed metadata");
        }
        (void)ask(control, message("raw-text"));
        require(ask(control, command("vm.inspect")).at("globals").at("raw").at("hex").text == "6100ff",
                "raw text inspection lost bytes or emitted invalid JSON");
    });
}

void checkpoint_failure_preservation() {
    ny::VM vm;
    fixture(vm);
    paused_session(vm, [](ny_ai::Control& control) {
        (void)ask(control, message("marker99"));
        const auto saved = ask(control, command("vm.snapshot"));
        require(saved.at("checkpoint").text == "1", "first checkpoint identity");
        (void)ask(control, message("foreign"));
        error_contains(ask(control, command("vm.snapshot"), false), "boxed values");
        (void)ask(control, message("marker123"));
        auto restore = command("vm.restore");
        restore.object["checkpoint"] = Json::str("not-retained");
        (void)ask(control, restore, false);
        auto current = ask(control, command("vm.inspect"));
        require(current.at("globals").at("marker").at("value").text == "123" &&
                current.at("pc").text == saved.at("pc").text && current.at("paused").boolean,
                "invalid restore mutated live execution state");
        restore.object["checkpoint"] = saved.at("checkpoint");
        (void)ask(control, restore);
        current = ask(control, command("vm.inspect"));
        require(current.at("globals").at("marker").at("value").text == "99" &&
                current.at("globals").object.count("foreign") == 0,
                "failed capture displaced previous good checkpoint");
        require(ask(control, command("vm.snapshot")).at("checkpoint").text == "2",
                "failed capture advanced checkpoint identity");
        (void)ask(control, restore, false); // A newer successful checkpoint retires the old ID.
        require(ask(control, command("vm.inspect")).at("globals").at("marker").at("value").text == "99",
                "retired checkpoint rejection mutated globals");
    });
}

void cancelled_request_does_not_execute_later() {
    ny::VM vm;
    fixture(vm);
    vm.setDebugState(ny::VM::DebugState::RUNNING);
    ny_ai::Control control(123, "test-version", false);
    Deadline deadline(control, 6s);
    std::string reply;
    std::exception_ptr failure;
    const auto start = Clock::now();
    std::thread requester([&] {
        try { reply = control.request(nil::detail::dump(command("vm.pause"))); }
        catch (...) { failure = std::current_exception(); }
    });
    // Deliberately provide no owner safepoint until the bounded request returns.
    requester.join();
    if (failure) std::rethrow_exception(failure);
    require(Clock::now() - start >= 1500ms && Clock::now() - start < 4s,
            "idle VM request cancellation deadline");
    error_contains(checked_response(reply), "request cancelled");
    control.safepoint(vm);
    require(!deadline.expired() && vm.getDebugState() == ny::VM::DebugState::RUNNING,
            "cancelled pause executed at a later safepoint");
    std::atomic<bool> finished{false};
    std::thread status([&] {
        try { reply = control.request(nil::detail::dump(command("vm.status"))); }
        catch (...) { failure = std::current_exception(); }
        finished = true;
    });
    const auto status_deadline = Clock::now() + 3s;
    while (!finished && Clock::now() < status_deadline) {
        control.safepoint(vm);
        std::this_thread::sleep_for(1ms);
    }
    if (!finished) control.stop();
    status.join();
    if (failure) std::rethrow_exception(failure);
    const auto response = checked_response(reply);
    require(response.at("error").type == Json::kind::null &&
            response.at("result").at("running").boolean &&
            !response.at("result").at("paused").boolean,
            "cancelled request remained queued or changed later status");
}

void stop_wakes_waiting_request() {
    ny_ai::Control control(123, "test-version", true);
    Deadline deadline(control, 3s);
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, finished = false;
    std::string reply;
    std::exception_ptr failure;
    std::thread requester([&] {
        { std::lock_guard<std::mutex> lock(mutex); entered = true; }
        changed.notify_all();
        try { reply = control.request(nil::detail::dump(command("vm.pause"))); }
        catch (...) { failure = std::current_exception(); }
        { std::lock_guard<std::mutex> lock(mutex); finished = true; }
        changed.notify_all();
    });
    bool started = false, ended_without_stop = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        started = changed.wait_for(lock, 1s, [&] { return entered; });
        if (started) ended_without_stop = changed.wait_for(lock, 30ms, [&] { return finished; });
    }
    const auto start = Clock::now();
    control.stop();
    requester.join();
    if (failure) std::rethrow_exception(failure);
    require(started && !ended_without_stop && Clock::now() - start < 1s && !deadline.expired(),
            "stop failed to promptly wake a waiting request");
    error_contains(checked_response(reply), "execution has ended");
}
} // namespace

int main() try {
    owner_dispatch_and_rejected_requests();
    checkpoint_failure_preservation();
    cancelled_request_does_not_execute_later();
    stop_wakes_waiting_request();
    std::cout << "AI owner control: " << checks.load() << " checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "AI owner control test failure: " << error.what() << '\n';
    return 1;
}
