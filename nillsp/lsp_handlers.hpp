#pragma once

#include "json_rpc.hpp"
#include "ny_parser.hpp"

#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace ny::lsp {

struct Document {
    int version = 0;
    std::string language_id;
    std::string text;
    std::vector<std::size_t> line_offsets;
    ParseResult parse;
};

struct HandlerContext {
    ny::rpc::JsonRpcConnection& rpc;
    NyParser& parser;
    std::mutex& docs_mutex;
    std::map<std::string, Document>& docs;
    bool& shutdown_requested;
    bool& exit_requested;
};

namespace handlers {

ny::json::Value initialize(HandlerContext& ctx, const ny::json::Value& params);
ny::json::Value shutdown(HandlerContext& ctx);
void exit(HandlerContext& ctx);

void did_open(HandlerContext& ctx, const ny::json::Value& params);
void did_change(HandlerContext& ctx, const ny::json::Value& params);
void did_close(HandlerContext& ctx, const ny::json::Value& params);

ny::json::Value completion(HandlerContext& ctx, const ny::json::Value& params);
ny::json::Value document_symbols(HandlerContext& ctx, const ny::json::Value& params);
ny::json::Value definition(HandlerContext& ctx, const ny::json::Value& params);
ny::json::Value hover(HandlerContext& ctx, const ny::json::Value& params);

}

}
