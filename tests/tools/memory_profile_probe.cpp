// A small process boundary for exercising collector behavior independently of
// VM/native allocation implementations. The Python contract validates reports.
#include "memory_profile.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <thread>

int main(int argc, char **argv) {
  if (argc != 2) return 64;
  const std::string mode = argv[1];
  ny::memory_profile::Session session;
  if (!session.begin("native")) return 3;
  if (mode == "disabled") return session.enabled() ? 4 : 0;
  if (!session.enabled()) return 5;
  int identities[4] = {};
  session.site("fixture.ny", "fixture.main", 12);
  if (mode == "basic") {
    session.allocate(&identities[0], 8, "array");
    session.allocate(&identities[1], 0, "array");
    session.resize(&identities[0], 32);
    session.resize(&identities[0], 12);
    session.site("other.matrix", "other.allocate", 34);
    session.allocate(&identities[2], 7, "buffer");
    session.release(&identities[0]);
    session.release(&identities[1]);
    session.release(&identities[3]); // Unknown handles must not fabricate frees.
    session.resize(&identities[3], 100);
    session.collection(25, 32, 2);
  } else if (mode == "zero") {
    session.allocate(&identities[0], 0, "zero");
  } else if (mode == "escape") {
    std::string file = u8"/tmp/Δ \"path\"\\\n\t";
    file.push_back('\0');
    file.push_back(static_cast<char>(0xff));
    session.site(file, "function\n\r\t\x01", 42);
    session.allocate(&identities[0], 1, "kind\"\\\n");
  } else if (mode == "duplicate") {
    session.allocate(&identities[0], 8, "buffer");
    session.allocate(&identities[0], 16, "buffer");
  } else if (mode == "overflow") {
    session.allocate(&identities[0], (std::numeric_limits<std::uint64_t>::max)(), "buffer");
    session.allocate(&identities[1], 1, "buffer");
  } else if (mode == "resize-overflow") {
    session.allocate(&identities[0], (std::numeric_limits<std::uint64_t>::max)() - 1, "buffer");
    session.allocate(&identities[1], 1, "buffer");
    session.resize(&identities[1], 2);
  } else if (mode == "spill") {
    for (std::uint64_t i = 1; i <= 4098; ++i) {
      session.site("fixture.ny", "fixture.main", i);
      session.allocate(&identities[0], 1, "array");
      session.release(&identities[0]);
    }
    // Existing sites remain precise after overflow begins.
    session.site("fixture.ny", "fixture.main", 1);
    session.allocate(&identities[0], 1, "array");
    session.release(&identities[0]);
  } else if (mode == "worker") {
    session.allocate(&identities[0], 5, "parent");
    bool worker_enabled = true, worker_began = false;
    std::thread worker([&] {
      ny::memory_profile::Session other;
      worker_began = other.begin("vm");
      worker_enabled = other.enabled();
      other.site("worker.ny", "worker.run", 1);
      other.allocate(&identities[1], 100, "worker");
      (void)other.finish();
    });
    worker.join();
    if (!worker_began || worker_enabled) return 6;
  } else if (mode == "abandoned") {
    session.allocate(&identities[0], 3, "buffer");
    return 0; // Destructor must preserve retained counts and mark incomplete.
  } else if (mode == "failed") {
    session.allocate(&identities[0], 3, "buffer");
    return session.finish("failed", false) ? 0 : 2;
  } else if (mode == "finish-twice") {
    session.allocate(&identities[0], 3, "buffer");
    if (!session.finish()) return 2;
    session.allocate(&identities[1], 100, "after-finish");
    return session.finish("failed", false) ? 0 : 2;
  } else {
    return 64;
  }
  return session.finish() ? 0 : 2;
}
