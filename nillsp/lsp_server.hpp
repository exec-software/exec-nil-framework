#pragma once

#include "json_rpc.hpp"
#include "lsp_handlers.hpp"
#include "ny_parser.hpp"

#include <map>
#include <mutex>
#include <string>

namespace ny::lsp {

class LspServer {
public:
    explicit LspServer(ny::rpc::JsonRpcConnection& rpc);

    bool handle_incoming(const ny::json::Value& message);
    bool should_exit() const;

private:
    void handle_one(const ny::json::Value& msg);
    void handle_request(const ny::rpc::Id& id, const std::string& method, const ny::json::Value& params);
    void handle_notification(const std::string& method, const ny::json::Value& params);

    void send_invalid_request(const ny::rpc::Id& id);
    void send_method_not_found(const ny::rpc::Id& id, const std::string& method);
    void send_invalid_params(const ny::rpc::Id& id, const std::string& message);

    ny::rpc::JsonRpcConnection& rpc_;
    NyParser parser_;
    std::mutex docs_mutex_;
    std::map<std::string, Document> docs_;
    bool shutdown_requested_ = false;
    bool exit_requested_ = false;
};

}
