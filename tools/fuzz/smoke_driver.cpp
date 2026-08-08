// Non-libFuzzer fallback runner: feeds each given corpus file through the
// matching parser under plain ASAN/UBSAN (no -fsanitize=fuzzer). Exists for
// toolchains where libFuzzer isn't available; not coverage-guided, just a
// deterministic crash check over the seed corpus (`make asan-smoke` uses
// this automatically when the local compiler lacks fuzzer support).
//
// Usage: smoke_driver <rss|csv|path|placeholder> file [file ...]
#include "targets.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string slurp(const char* path) {
  std::ifstream f(path, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <rss|csv|path|placeholder> file [file ...]\n", argv[0]);
    return 2;
  }
  std::string kind = argv[1];
  size_t count = 0;
  for (int i = 2; i < argc; i++) {
    std::string data = slurp(argv[i]);
    if (kind == "rss") {
      yat_fuzz::runRss(data);
    } else if (kind == "csv") {
      yat_fuzz::runCsv(data);
    } else if (kind == "path") {
      yat_fuzz::runPath(data);
    } else if (kind == "placeholder") {
      yat_fuzz::runPlaceholder(data);
    } else {
      fprintf(stderr, "unknown kind '%s' (want rss|csv|path|placeholder)\n", kind.c_str());
      return 2;
    }
    count++;
  }
  fprintf(stderr, "smoke_driver: %s ok (%zu files, no crash)\n", kind.c_str(), count);
  return 0;
}
