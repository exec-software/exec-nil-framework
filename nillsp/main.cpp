#include "json_rpc.hpp"
#include "lsp_server.hpp"
#include "nil_version.hpp"

#include <iostream>
#include <string>

int main(int argc, char *argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "nillsp " << NIL_VERSION << '\n';
        return 0;
    }
    if (argc == 2 && (std::string(argv[1]) == "-h" ||
                      std::string(argv[1]) == "--help")) {
        std::cout << "Usage: nillsp [--help|--version]\n"
                  << "Runs the NIL Framework language server for Ny and Matrix over standard input/output.\n";
        return 0;
    }
    if (argc != 1) {
        std::cerr << "nillsp does not accept positional arguments; use --help.\n";
        return 1;
    }

    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    ny::rpc::JsonRpcConnection conn(std::cin, std::cout);
    ny::lsp::LspServer server(conn);

    while (!server.should_exit()) {
        ny::json::Value msg;
        std::string err;
        if (!conn.read_message(msg, err)) break;

        if (!err.empty()) {
            ny::rpc::Id null_id;
            null_id.value = std::monostate{};
            ny::rpc::Error e;
            e.code = -32700;
            e.message = err;
            conn.send_error(null_id, e);
            continue;
        }

        server.handle_incoming(msg);
    }
    return 0;
}
