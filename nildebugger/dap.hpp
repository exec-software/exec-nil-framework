#pragma once

#include <istream>
#include <ostream>

namespace ny::debug {
// DAP owns both streams exclusively. No sockets or persistent service.
int run_dap(std::istream& input, std::ostream& output);
}
