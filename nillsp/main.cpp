#include "json_rpc.hpp"
#include "lsp_server.hpp"
#include "nil_version.hpp"
#include "../common/standard_streams.hpp"

#include <iostream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char *argv[]) {
    ny::use_untranslated_standard_streams();
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

#ifdef _WIN32
    // LSP framing is byte-exact: headers end in a literal CRLF and the body is
    // exactly Content-Length bytes. Windows' default text mode breaks both ends
    // of that contract - it expands each '\n' we write into "\r\n" (emitting
    // "\r\r\n"), and collapses "\r\n" on the way in, desynchronising the body
    // byte count. Both streams must be binary.
    if (_setmode(_fileno(stdin), _O_BINARY) == -1) {
        std::cerr << "nillsp: cannot set stdin to binary mode\n";
        return 1;
    }
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        std::cerr << "nillsp: cannot set stdout to binary mode\n";
        return 1;
    }
#endif

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
