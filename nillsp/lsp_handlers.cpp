#include "lsp_handlers.hpp"
#include "matrix_parser.hpp"
#include "nil_version.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ny::lsp {
namespace {

static std::optional<std::string> get_uri_from_text_document(const ny::json::Value& params) {
    const ny::json::Value* td = nullptr;
    if (params.is_object()) td = params.get("textDocument");
    if (!td || !td->is_object()) return std::nullopt;
    auto uri = ny::json::get_string(*td, "uri");
    if (!uri) return std::nullopt;
    return *uri;
}

static std::optional<Position> get_position(const ny::json::Value& params) {
    const ny::json::Value* p = nullptr;
    if (params.is_object()) p = params.get("position");
    if (!p || !p->is_object()) return std::nullopt;
    const ny::json::Value* l = p->get("line");
    const ny::json::Value* c = p->get("character");
    if (!l || !c || !l->is_number() || !c->is_number()) return std::nullopt;
    for (const auto* component : {l, c}) {
        const auto number = component->as_number();
        if (!std::isfinite(number) || number < 0 || number > std::numeric_limits<int>::max() ||
            number != std::floor(number)) return std::nullopt;
    }
    return Position{static_cast<int>(l->as_i64()), static_cast<int>(c->as_i64())};
}

static ny::json::Value to_json_position(const Position& p) {
    return ny::json::object({{"line", ny::json::Value(static_cast<int64_t>(p.line))},
                             {"character", ny::json::Value(static_cast<int64_t>(p.character))}});
}

static ny::json::Value to_json_range(const Range& r) {
    return ny::json::object({{"start", to_json_position(r.start)}, {"end", to_json_position(r.end)}});
}

static ny::json::Value to_json_diagnostic(const Diagnostic& d) {
    ny::json::Value::Object obj;
    obj["range"] = to_json_range(d.range);
    obj["severity"] = ny::json::Value(static_cast<int64_t>(d.severity));
    obj["message"] = ny::json::Value(d.message);
    obj["source"] = ny::json::Value("nillsp");
    return ny::json::Value(std::move(obj));
}

static void publish_diagnostics(HandlerContext& ctx, const std::string& uri, const std::vector<Diagnostic>& diags) {
    ny::json::Value::Array arr;
    arr.reserve(diags.size());
    for (const auto& d : diags) arr.push_back(to_json_diagnostic(d));

    ctx.rpc.send_notification(
        "textDocument/publishDiagnostics",
        ny::json::object({{"uri", ny::json::Value(uri)}, {"diagnostics", ny::json::Value(std::move(arr))}}));
}

static ny::json::Value make_location(const std::string& uri, const Range& range) {
    return ny::json::object({{"uri", ny::json::Value(uri)}, {"range", to_json_range(range)}});
}

static ny::json::Value make_completion_item(const Symbol& s, const Range& edit) {
    ny::json::Value::Object obj;
    obj["label"] = ny::json::Value(s.name);
    // CompletionItemKind and SymbolKind are different protocol enumerations.
    const auto kind = s.kind == symbol_kind::Function ? 3 : s.kind == symbol_kind::Method ? 2 :
                      s.kind == symbol_kind::Field ? 5 : s.kind == symbol_kind::Struct ? 22 :
                      s.kind == symbol_kind::Class ? 7 : s.kind == symbol_kind::Constant ? 21 :
                      s.kind == symbol_kind::Enum ? 13 :
                      s.kind == symbol_kind::Module ? 9 : 6;
    obj["kind"] = ny::json::Value(static_cast<int64_t>(kind));
    if (!s.detail.empty()) obj["detail"] = ny::json::Value(s.detail);
    obj["textEdit"] = ny::json::object({{"range", to_json_range(edit)}, {"newText", s.name}});
    return ny::json::Value(std::move(obj));
}

static ny::json::Value::Array outline(const ParseResult& parsed, int parent) {
    ny::json::Value::Array result;
    for (std::size_t i = 0; i < parsed.symbols.size(); ++i) {
        const auto& symbol = parsed.symbols[i];
        if (symbol.parent != parent) continue;
        ny::json::Value::Object item;
        item["name"] = symbol.name;
        item["kind"] = static_cast<int64_t>(symbol.kind);
        item["range"] = to_json_range(symbol.range);
        item["selectionRange"] = to_json_range(symbol.selectionRange);
        item["detail"] = symbol.detail;
        auto children = outline(parsed, static_cast<int>(i));
        if (!children.empty()) item["children"] = ny::json::Value(std::move(children));
        result.emplace_back(std::move(item));
    }
    return result;
}

}

namespace handlers {

ny::json::Value initialize(HandlerContext& ctx, const ny::json::Value& params) {
    (void)ctx;
    (void)params;
    ny::json::Value::Object text_sync;
    text_sync["openClose"] = ny::json::Value(true);
    text_sync["change"] = ny::json::Value(static_cast<int64_t>(1));

    ny::json::Value::Object completion_provider;
    completion_provider["resolveProvider"] = ny::json::Value(false);
    completion_provider["triggerCharacters"] = ny::json::Value(ny::json::Value::Array{ny::json::Value("."),
                                                                                       ny::json::Value(":"),
                                                                                       ny::json::Value("_"),
                                                                                       ny::json::Value("-")});

    ny::json::Value::Object capabilities;
    capabilities["textDocumentSync"] = ny::json::Value(std::move(text_sync));
    capabilities["completionProvider"] = ny::json::Value(std::move(completion_provider));
    capabilities["definitionProvider"] = ny::json::Value(true);
    capabilities["hoverProvider"] = ny::json::Value(true);
    capabilities["documentSymbolProvider"] = ny::json::Value(true);
    capabilities["positionEncoding"] = ny::json::Value("utf-16");

    ny::json::Value::Object server_info;
    server_info["name"] = ny::json::Value("nillsp");
    server_info["version"] = ny::json::Value(NIL_VERSION);

    return ny::json::Value(ny::json::Value::Object{{"capabilities", ny::json::Value(std::move(capabilities))},
                                                   {"serverInfo", ny::json::Value(std::move(server_info))}});
}

ny::json::Value shutdown(HandlerContext& ctx) {
    ctx.shutdown_requested = true;
    return ny::json::Value(nullptr);
}

void exit(HandlerContext& ctx) {
    ctx.exit_requested = true;
}

void did_open(HandlerContext& ctx, const ny::json::Value& params) {
    const ny::json::Value* td = params.is_object() ? params.get("textDocument") : nullptr;
    if (!td || !td->is_object()) return;
    auto uri = ny::json::get_string(*td, "uri");
    auto text = ny::json::get_string(*td, "text");
    if (!uri || !text) return;
    const int version = static_cast<int>(ny::json::get_i64(*td, "version").value_or(0));

    Document doc;
    doc.version = version;
    doc.language_id = ny::json::get_string(*td, "languageId").value_or("");
    doc.text = *text;
    doc.line_offsets = NyParser::compute_line_offsets(doc.text);
    doc.parse = ctx.parser.parse(*uri, doc.text, doc.language_id);

    {
        std::lock_guard<std::mutex> lock(ctx.docs_mutex);
        ctx.docs[*uri] = std::move(doc);
        publish_diagnostics(ctx, *uri, ctx.docs[*uri].parse.diagnostics);
    }
}

void did_change(HandlerContext& ctx, const ny::json::Value& params) {
    const ny::json::Value* td = params.is_object() ? params.get("textDocument") : nullptr;
    if (!td || !td->is_object()) return;
    auto uri = ny::json::get_string(*td, "uri");
    if (!uri) return;
    const int version = static_cast<int>(ny::json::get_i64(*td, "version").value_or(0));

    const ny::json::Value* changes = params.is_object() ? params.get("contentChanges") : nullptr;
    if (!changes || !changes->is_array() || changes->as_array().empty()) return;
    const ny::json::Value& first = changes->as_array().front();
    if (!first.is_object()) return;
    auto text = ny::json::get_string(first, "text");
    if (!text) return;

    std::lock_guard<std::mutex> lock(ctx.docs_mutex);
    auto it = ctx.docs.find(*uri);
    if (it == ctx.docs.end()) return;
    it->second.version = version;
    it->second.text = *text;
    it->second.line_offsets = NyParser::compute_line_offsets(it->second.text);
    it->second.parse = ctx.parser.parse(*uri, it->second.text, it->second.language_id);
    publish_diagnostics(ctx, *uri, it->second.parse.diagnostics);
}

void did_close(HandlerContext& ctx, const ny::json::Value& params) {
    auto uri = get_uri_from_text_document(params);
    if (!uri) return;
    std::lock_guard<std::mutex> lock(ctx.docs_mutex);
    ctx.docs.erase(*uri);
    publish_diagnostics(ctx, *uri, {});
}

ny::json::Value completion(HandlerContext& ctx, const ny::json::Value& params) {
    auto uri = get_uri_from_text_document(params);
    auto pos = get_position(params);
    if (!uri || !pos) return ny::json::Value(ny::json::Value::Object{{"isIncomplete", ny::json::Value(false)},
                                                                     {"items", ny::json::Value(ny::json::Value::Array{})}});

    ny::json::Value::Array items;

    std::lock_guard<std::mutex> lock(ctx.docs_mutex);
    const auto found = ctx.docs.find(*uri);
    if (found != ctx.docs.end()) {
        const auto& doc = found->second;
        for (const auto& result : NyParser::complete(doc.parse, doc.text, doc.line_offsets, *pos))
            items.push_back(make_completion_item(doc.parse.symbols[result.symbol], result.edit));
        if (doc.parse.matrix) {
            for (const auto& word : matrix_keywords(doc.parse, doc.text, doc.line_offsets, *pos)) {
                items.push_back(ny::json::object({{"label", word.label}, {"kind", static_cast<int64_t>(word.kind)},
                    {"detail", "Matrix language"},
                    {"textEdit", ny::json::object({{"range", to_json_range(word.edit)}, {"newText", word.label}})}}));
            }
        }
    }

    return ny::json::Value(ny::json::Value::Object{{"isIncomplete", ny::json::Value(false)},
                                                   {"items", ny::json::Value(std::move(items))}});
}

ny::json::Value document_symbols(HandlerContext& ctx, const ny::json::Value& params) {
    const auto uri = get_uri_from_text_document(params);
    std::lock_guard<std::mutex> lock(ctx.docs_mutex);
    const auto found = uri ? ctx.docs.find(*uri) : ctx.docs.end();
    return ny::json::Value(found == ctx.docs.end() ? ny::json::Value::Array{} : outline(found->second.parse, -1));
}

ny::json::Value definition(HandlerContext& ctx, const ny::json::Value& params) {
    auto uri = get_uri_from_text_document(params);
    auto pos = get_position(params);
    if (!uri || !pos) return ny::json::Value(nullptr);

    std::lock_guard<std::mutex> lock(ctx.docs_mutex);
    const auto found = ctx.docs.find(*uri);
    if (found == ctx.docs.end()) return ny::json::Value(nullptr);
    const auto& doc = found->second;
    const auto index = NyParser::resolve(doc.parse, doc.text, doc.line_offsets, *pos);
    if (!index) return ny::json::Value(nullptr);

    ny::json::Value::Array locs;
    locs.push_back(make_location(*uri, doc.parse.symbols[*index].selectionRange));
    return ny::json::Value(std::move(locs));
}

ny::json::Value hover(HandlerContext& ctx, const ny::json::Value& params) {
    auto uri = get_uri_from_text_document(params);
    auto pos = get_position(params);
    if (!uri || !pos) return ny::json::Value(nullptr);

    std::lock_guard<std::mutex> lock(ctx.docs_mutex);
    const auto found = ctx.docs.find(*uri);
    if (found == ctx.docs.end()) return ny::json::Value(nullptr);
    const auto& doc = found->second;
    const auto index = NyParser::resolve(doc.parse, doc.text, doc.line_offsets, *pos);
    if (!index) return ny::json::Value(nullptr);
    const auto* sym = &doc.parse.symbols[*index];

    std::string md;
    md += "**";
    md += sym->name;
    md += "**";
    if (!sym->detail.empty()) {
        md += "\n\n";
        md += doc.parse.matrix ? "```matrix\n" : "```ny\n";
        md += sym->detail;
        md += "\n```";
    }

    ny::json::Value contents = ny::json::object({{"kind", "markdown"}, {"value", ny::json::Value(md)}});
    ny::json::Value::Object hover_obj;
    hover_obj["contents"] = contents;
    // Omit optional range rather than incorrectly reporting the declaration's
    // location as the range of a different reference under the cursor.
    return ny::json::Value(std::move(hover_obj));
}

}

}
