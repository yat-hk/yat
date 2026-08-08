// Thin translation-unit shim so PlatformIO's LDF — which auto-compiles every
// .c/.cpp file it finds directly under this library's srcDir (engine/src,
// per engine/library.json) — picks up the vendored QR encoder for firmware
// builds without duplicating it there. The native tools/preview Makefile
// instead compiles engine/third_party/qrcodegen/qrcodegen.c directly (see
// SRCS there), so this shim is never part of that build's source list and
// the symbols are defined exactly once per target.
//
// See engine/third_party/qrcodegen/ (Project Nayuki's qrcodegen, MIT) for
// the actual implementation and license header.
#include "../third_party/qrcodegen/qrcodegen.c"
