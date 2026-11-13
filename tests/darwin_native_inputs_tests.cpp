#include "../nilbuild/toolchain_inputs.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: nil_darwin_native_inputs_tests DRIVER RUNTIME\n";
        return 2;
    }
    try {
        nilbuild::cancellation_scope cancellation;
        namespace fs = std::filesystem;
        const auto driver = fs::absolute(argv[1]).lexically_normal();
        const auto runtime = fs::absolute(argv[2]).lexically_normal();
        std::string reason;
        const auto files = nilbuild::native_toolchain_files(
            driver, fs::current_path(), runtime, {}, &reason);
        if (!files || files->empty())
            throw std::runtime_error(reason.empty() ? "native dependency collector returned no files or reason" : reason);
        if (std::find(files->begin(), files->end(), runtime) == files->end())
            throw std::runtime_error("native dependency inventory omitted the selected runtime");
        for (const auto& file : *files) {
            std::error_code error;
            if (!file.is_absolute() || !fs::is_regular_file(file, error) || error)
                throw std::runtime_error("native input is not an absolute regular file: " + file.string());
        }
        // This validates actual default-profile collection only. Content
        // hashing and unchanged-build reuse remain the incremental contract.
        std::cout << "PASS: native dependency collection includes runtime and "
                  << files->size() << " regular inputs (no content-hash/reuse claim)\n";
        return 0;
    } catch (const nilbuild::process_cancelled& error) {
        std::cerr << "FAIL: native dependency collection cancelled\n";
        return error.status;
    } catch (const std::exception& error) {
        std::string reason;
        nilbuild::darwin_toolchain_detail::explain(&reason, error.what());
        std::cerr << "FAIL: " << reason << '\n';
        return 1;
    }
}
