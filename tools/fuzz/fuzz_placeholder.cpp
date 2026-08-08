// libFuzzer harness for yat::detail::substitute / evalWhen (engine/src/text.cpp).
//
// Placeholder templates and `when` expressions are pack-authored text, but
// both can embed {{data.*}} references, so their *evaluated* shape is
// influenced by fetched bytes even though the template grammar itself comes
// from the pack file. See targets.h's runPlaceholder() for the byte-0 mode
// selector this harness relies on (substitute plain/urlEncode, evalWhen
// string/JSON forms).
#include "targets.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size > 8192) return 0;  // well above the spec's 1024-char string cap
  yat_fuzz::runPlaceholder(std::string(reinterpret_cast<const char*>(data), size));
  return 0;
}
