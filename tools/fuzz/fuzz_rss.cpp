// libFuzzer harness for yat::detail::parseRss (engine/src/rss.cpp).
//
// rss.cpp is a hand-written tolerant tag scanner (deliberately not a general
// XML parser) that runs directly on RSS/Atom bytes fetched from a pack's
// `https` source — i.e. on network response bodies an attacker who controls
// (or MITMs, or compromises) the source host fully controls. PACK-SPEC.md
// §12.3(9): "all fetched bytes are hostile input; engines enforce size caps
// before parse and are fuzzed in CI (native target)." This is that fuzzing.
#include "targets.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // §12.1: declared max_bytes tops out at 524288; stay a bit above that so
  // the fuzzer can still explore near the boundary without every run
  // wasting time on multi-megabyte inputs.
  if (size > 1024 * 1024) return 0;
  yat_fuzz::runRss(std::string(reinterpret_cast<const char*>(data), size));
  return 0;
}
