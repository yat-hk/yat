# Fuzzing harnesses for the hand-rolled parsers that eat network bytes on
# the device: rss.cpp, csv.cpp, path.cpp, text.cpp (substitute/evalWhen).
# See PACK-SPEC.md §12.3(9): "all fetched bytes are hostile input; engines
# enforce size caps before parse and are fuzzed in CI (native target)."
#
# Targets:
#   make fuzz-rss / fuzz-csv / fuzz-path / fuzz-placeholder
#       Build the libFuzzer+ASan+UBSan binary for one target (bin/fuzz_<name>).
#       Requires a clang whose compiler-rt ships the fuzzer runtime.
#   make run-fuzz-<name>
#       Build (if needed) and run that target for a bounded time/run-count
#       against its seed corpus.
#   make asan-smoke
#       Verification entry point, in descending order of how much it can
#       actually catch:
#         1. libFuzzer + ASan + UBSan, bounded run per target (best)
#         2. plain ASan+UBSan smoke_driver over the seed corpus
#         3. UBSan-only smoke_driver (ASan's runtime hangs at process start
#            on macOS hosts with Developer Mode disabled — `DevToolsSecurity
#            -status` — a host config issue, not a bug in the code under
#            test; this tier still catches real UB)
#         4. unsanitized smoke_driver (last resort: crash/hang only)
#       Each tier is detected with a bounded probe (no GNU `timeout` assumed;
#       macOS ships without it), so this never hangs even when a tier is
#       broken. Prints which tier actually ran.
#   make clean

ROOT := ../..
ENGINE := $(ROOT)/engine

CXX ?= clang++
INCLUDES := -I$(ENGINE)/include -I$(ENGINE)/third_party -I$(ENGINE)/third_party/efont \
            -I$(ENGINE)/third_party/qrcodegen -I.
CXXFLAGS := -std=c++17 -g -O1 -Wall $(INCLUDES)
SAN_ASAN_UBSAN := -fsanitize=address,undefined -fno-sanitize-recover=all
SAN_UBSAN      := -fsanitize=undefined -fno-sanitize-recover=all
FUZZ_SAN       := -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all

CORPUS := corpus
OUT := corpus_out
ARTIFACTS := artifacts

RSS_SRCS         := fuzz_rss.cpp $(ENGINE)/src/rss.cpp
CSV_SRCS         := fuzz_csv.cpp $(ENGINE)/src/csv.cpp
PATH_SRCS        := fuzz_path.cpp $(ENGINE)/src/path.cpp
PLACEHOLDER_SRCS := fuzz_placeholder.cpp $(ENGINE)/src/text.cpp
SMOKE_SRCS       := smoke_driver.cpp $(ENGINE)/src/rss.cpp $(ENGINE)/src/csv.cpp \
                    $(ENGINE)/src/path.cpp $(ENGINE)/src/text.cpp

TIME_BUDGET ?= 60
RUNS_BUDGET ?= 200000

.PHONY: all clean fuzz-rss fuzz-csv fuzz-path fuzz-placeholder \
        run-fuzz-rss run-fuzz-csv run-fuzz-path run-fuzz-placeholder \
        asan-smoke check-fuzzer-support check-asan-support \
        smoke-all smoke-all-ubsan smoke-all-plain

all: fuzz-rss fuzz-csv fuzz-path fuzz-placeholder

bin:
	mkdir -p bin

fuzz-rss: bin/fuzz_rss
fuzz-csv: bin/fuzz_csv
fuzz-path: bin/fuzz_path
fuzz-placeholder: bin/fuzz_placeholder

bin/fuzz_rss: $(RSS_SRCS) targets.h | bin
	$(CXX) $(CXXFLAGS) $(FUZZ_SAN) -o $@ $(RSS_SRCS)

bin/fuzz_csv: $(CSV_SRCS) targets.h | bin
	$(CXX) $(CXXFLAGS) $(FUZZ_SAN) -o $@ $(CSV_SRCS)

bin/fuzz_path: $(PATH_SRCS) targets.h | bin
	$(CXX) $(CXXFLAGS) $(FUZZ_SAN) -o $@ $(PATH_SRCS)

bin/fuzz_placeholder: $(PLACEHOLDER_SRCS) targets.h | bin
	$(CXX) $(CXXFLAGS) $(FUZZ_SAN) -o $@ $(PLACEHOLDER_SRCS)

bin/smoke_driver: $(SMOKE_SRCS) targets.h | bin
	$(CXX) $(CXXFLAGS) $(SAN_ASAN_UBSAN) -o $@ $(SMOKE_SRCS)

bin/smoke_driver_ubsan: $(SMOKE_SRCS) targets.h | bin
	$(CXX) $(CXXFLAGS) $(SAN_UBSAN) -o $@ $(SMOKE_SRCS)

bin/smoke_driver_plain: $(SMOKE_SRCS) targets.h | bin
	$(CXX) $(CXXFLAGS) -o $@ $(SMOKE_SRCS)

# Bounded runs against the seed corpus. New interesting inputs libFuzzer
# discovers are saved to corpus_out/<name>/ (gitignored, scratch) — the
# checked-in corpus/<name>/ is passed read-only as an extra seed directory
# and never mutated.
run-fuzz-rss: bin/fuzz_rss
	mkdir -p $(OUT)/rss $(ARTIFACTS)
	./bin/fuzz_rss -max_total_time=$(TIME_BUDGET) -runs=$(RUNS_BUDGET) \
	  -artifact_prefix=$(ARTIFACTS)/rss- $(OUT)/rss $(CORPUS)/rss

run-fuzz-csv: bin/fuzz_csv
	mkdir -p $(OUT)/csv $(ARTIFACTS)
	./bin/fuzz_csv -max_total_time=$(TIME_BUDGET) -runs=$(RUNS_BUDGET) \
	  -artifact_prefix=$(ARTIFACTS)/csv- $(OUT)/csv $(CORPUS)/csv

run-fuzz-path: bin/fuzz_path
	mkdir -p $(OUT)/path $(ARTIFACTS)
	./bin/fuzz_path -max_total_time=$(TIME_BUDGET) -runs=$(RUNS_BUDGET) \
	  -artifact_prefix=$(ARTIFACTS)/path- $(OUT)/path $(CORPUS)/path

run-fuzz-placeholder: bin/fuzz_placeholder
	mkdir -p $(OUT)/placeholder $(ARTIFACTS)
	./bin/fuzz_placeholder -max_total_time=$(TIME_BUDGET) -runs=$(RUNS_BUDGET) \
	  -artifact_prefix=$(ARTIFACTS)/placeholder- $(OUT)/placeholder $(CORPUS)/placeholder

# Prints "yes"/"no" — whether this $(CXX) can produce a *linkable*
# libFuzzer binary. Some clangs (e.g. some Xcode Command Line Tools builds)
# accept -fsanitize=fuzzer at compile time but are missing the
# libclang_rt.fuzzer archive at link time, so this must actually link, not
# just compile.
check-fuzzer-support:
	@echo 'extern "C" int LLVMFuzzerTestOneInput(const unsigned char*, unsigned long){return 0;}' \
	  > /tmp/yat_fuzzer_check.cpp
	@$(CXX) -fsanitize=fuzzer -std=c++17 /tmp/yat_fuzzer_check.cpp \
	  -o /tmp/yat_fuzzer_check.bin >/tmp/yat_fuzzer_check.log 2>&1 && echo yes || echo no

# Prints "yes"/"no" — whether an ASan-instrumented binary actually *runs* on
# this host, as opposed to merely compiling. On macOS, ASan's runtime can
# hang indefinitely at process startup when Developer Mode is disabled
# (`DevToolsSecurity -status`); this has been observed directly (a trivial
# `int main(){return 0;}` hangs at 100% CPU before reaching main). No `wait`
# with a timeout exists in POSIX sh/macOS bash, so this polls kill -0 in a
# loop and force-kills after ~6s.
check-asan-support:
	@echo 'int main(){return 0;}' > /tmp/yat_asan_check.cpp; \
	if ! $(CXX) -fsanitize=address -std=c++17 /tmp/yat_asan_check.cpp \
	     -o /tmp/yat_asan_check.bin >/tmp/yat_asan_check.log 2>&1; then \
	  echo no; exit 0; \
	fi; \
	/tmp/yat_asan_check.bin >/tmp/yat_asan_check.run.log 2>&1 & pid=$$!; \
	i=0; \
	while kill -0 $$pid 2>/dev/null; do \
	  i=$$((i+1)); \
	  if [ $$i -ge 30 ]; then kill -9 $$pid 2>/dev/null; echo no; exit 0; fi; \
	  sleep 0.2; \
	done; \
	wait $$pid 2>/dev/null; \
	echo yes

smoke-all: bin/smoke_driver
	./bin/smoke_driver rss $(CORPUS)/rss/*
	./bin/smoke_driver csv $(CORPUS)/csv/*
	./bin/smoke_driver path $(CORPUS)/path/*
	./bin/smoke_driver placeholder $(CORPUS)/placeholder/*

smoke-all-ubsan: bin/smoke_driver_ubsan
	./bin/smoke_driver_ubsan rss $(CORPUS)/rss/*
	./bin/smoke_driver_ubsan csv $(CORPUS)/csv/*
	./bin/smoke_driver_ubsan path $(CORPUS)/path/*
	./bin/smoke_driver_ubsan placeholder $(CORPUS)/placeholder/*

smoke-all-plain: bin/smoke_driver_plain
	./bin/smoke_driver_plain rss $(CORPUS)/rss/*
	./bin/smoke_driver_plain csv $(CORPUS)/csv/*
	./bin/smoke_driver_plain path $(CORPUS)/path/*
	./bin/smoke_driver_plain placeholder $(CORPUS)/placeholder/*

asan-smoke:
	@if [ "$$($(MAKE) -s check-fuzzer-support)" = "yes" ]; then \
	  echo "== tier 1: libFuzzer available ($(CXX)) — bounded fuzz run over seed corpus, $(TIME_BUDGET)s x4 targets =="; \
	  $(MAKE) run-fuzz-rss run-fuzz-csv run-fuzz-path run-fuzz-placeholder; \
	  echo "== asan-smoke: ran tier 1 (libFuzzer + ASan + UBSan) =="; \
	elif [ "$$($(MAKE) -s check-asan-support)" = "yes" ]; then \
	  echo "== tier 2: no libFuzzer runtime, but ASan runs — smoke_driver (ASan+UBSan) over seed corpus =="; \
	  $(MAKE) smoke-all; \
	  echo "== asan-smoke: ran tier 2 (smoke_driver, ASan+UBSan, no libFuzzer) =="; \
	else \
	  echo "== tier 3: ASan's runtime does not run on this host (hangs at startup — see check-asan-support" \
	       "comment; on macOS this usually means Developer Mode is disabled, 'DevToolsSecurity -status')."; \
	  echo "   Falling back to UBSan-only smoke_driver over seed corpus =="; \
	  $(MAKE) smoke-all-ubsan; \
	  echo "== asan-smoke: ran tier 3 (smoke_driver, UBSan only — no ASan, no libFuzzer) =="; \
	fi

clean:
	rm -rf bin $(OUT) $(ARTIFACTS) *.o \
	  /tmp/yat_fuzzer_check.cpp /tmp/yat_fuzzer_check.bin /tmp/yat_fuzzer_check.log \
	  /tmp/yat_asan_check.cpp /tmp/yat_asan_check.bin /tmp/yat_asan_check.log /tmp/yat_asan_check.run.log
