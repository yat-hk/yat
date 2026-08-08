// libFuzzer harness for yat::detail::parseCsv (engine/src/csv.cpp).
//
// Same threat model as fuzz_rss.cpp: the CSV body is a fetched network
// response, fully attacker-controlled. parseCsv never returns an error (it
// truncates instead per PACK-SPEC.md §5.5/§12.1) — the property under test
// is purely "never crashes, never UB", not "rejects malformed input".
#include "targets.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 1024 * 1024) return 0;
  yat_fuzz::runCsv(std::string(reinterpret_cast<const char*>(data), size));
  return 0;
}
