#include "lsp_server.hpp"

namespace ny::lsp {
namespace {

static ny::rpc::Error make_error(int code, std::string message) {
    ny::rpc::Error e;
    e.code = code;
    e.message = std::move(message);
    return e;
}

static bool is_request_object(const ny::json::Value& msg) {
    if (!msg.is_object()) return false;
    const auto* m = msg.get("method");
    const auto* id = msg.get("id");
    return m && m->is_string() && id;
}

static bool is_notification_object(const ny::json::Value& msg) {
    if (!msg.is_object()) return false;
    const auto* m = msg.get("method");
    const auto* id = msg.get("id");
    return m && m->is_string() && !id;
}

}

LspServer::LspServer(ny::rpc::JsonRpcConnection& rpc) : rpc_(rpc) {}

bool LspServer::handle_incoming(const ny::json::Value& message) {
    if (message.is_array()) {
        for (const auto& it : message.as_array()) handle_one(it);
        return true;
    }
    handle_one(message);
    return true;
}

bool LspServer::should_exit() const {
    return exit_requested_;
}

void LspServer::handle_one(const ny::json::Value& msg) {
    if (!msg.is_object()) return;

    if (is_request_object(msg)) {
        const auto* method_v = msg.get("method");
        const auto* id_v = msg.get("id");
        if (!method_v || !id_v) return;

        auto id = ny::rpc::Id::from_json(*id_v);
        if (!id) return;

        const auto* params_v = msg.get("params");
        ny::json::Value params = params_v ? *params_v : ny::json::Value(nullptr);
        handle_request(*id, method_v->as_string(), params);
        return;
    }

    if (is_notification_object(msg)) {
        const auto* method_v = msg.get("method");
        if (!method_v) return;
        const auto* params_v = msg.get("params");
        ny::json::Value params = params_v ? *params_v : ny::json::Value(nullptr);
        handle_notification(method_v->as_string(), params);
        return;
    }
}

void LspServer::handle_request(const ny::rpc::Id& id, const std::string& method, const ny::json::Value& params) {
    HandlerContext ctx{rpc_, parser_, docs_mutex_, docs_, shutdown_requested_, exit_requested_};

    if (method == "initialize") {
        rpc_.send_response(id, handlers::initialize(ctx, params));
        return;
    }
    if (method == "shutdown") {
        rpc_.send_response(id, handlers::shutdown(ctx));
        return;
    }
    if (method == "textDocument/completion") {
        rpc_.send_response(id, handlers::completion(ctx, params));
        return;
    }
    if (method == "textDocument/documentSymbol") {
        rpc_.send_response(id, handlers::document_symbols(ctx, params));
        return;
    }
    if (method == "textDocument/definition") {
        rpc_.send_response(id, handlers::definition(ctx, params));
        return;
    }
    if (method == "textDocument/hover") {
        rpc_.send_response(id, handlers::hover(ctx, params));
        return;
    }

    send_method_not_found(id, method);
}

void LspServer::handle_notification(const std::string& method, const ny::json::Value& params) {
    HandlerContext ctx{rpc_, parser_, docs_mutex_, docs_, shutdown_requested_, exit_requested_};

    if (method == "exit") {
        handlers::exit(ctx);
        return;
    }
    if (method == "textDocument/didOpen") {
        handlers::did_open(ctx, params);
        return;
    }
    if (method == "textDocument/didChange") {
        handlers::did_change(ctx, params);
        return;
    }
    if (method == "textDocument/didClose") {
        handlers::did_close(ctx, params);
        return;
    }
}

void LspServer::send_invalid_request(const ny::rpc::Id& id) {
    rpc_.send_error(id, make_error(-32600, "Invalid Request"));
}

void LspServer::send_method_not_found(const ny::rpc::Id& id, const std::string& method) {
    ny::rpc::Error e = make_error(-32601, "Method not found");
    e.data = ny::json::object({{"method", ny::json::Value(method)}});
    rpc_.send_error(id, e);
}

void LspServer::send_invalid_params(const ny::rpc::Id& id, const std::string& message) {
    rpc_.send_error(id, make_error(-32602, message));
}

}
