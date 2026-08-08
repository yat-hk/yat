// libFuzzer harness for yat::detail::evalPath (engine/src/path.cpp).
//
// Path expressions (Pack Spec §6.1-§6.4) are authored inside a pack's
// `extract` map, so PACK-SPEC.md §12.1 caps them at load time (<=256 chars,
// <=1 filter, <=8 steps, <=2 pipe stages) — but evalPath() itself enforces
// none of that; it just parses whatever string it's handed. A pack file is
// untrusted the moment it's shared/installed from a gallery, and the
// extracted JSON document a path runs against is itself derived from
// fetched bytes, so both the expression grammar and its interaction with
// attacker-shaped data need to survive garbage.
#include "targets.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 8192) return 0;  // well above the spec's 256-char cap
  yat_fuzz::runPath(std::string(reinterpret_cast<const char*>(data), size));
  return 0;
}
