#!/bin/sh
# Builds the YAT engine to WebAssembly (feasibility spike — see README.md).
# Mirrors tools/preview/Makefile's source list and include flags; the only
# new translation unit is wasm_main.cpp (this directory's host shim).
set -e
cd "$(dirname "$0")"

ROOT=../..
ENGINE=$ROOT/engine
EMCC=${EMCC:-emcc}
BUILD=build
mkdir -p "$BUILD"

CXX_SRCS="
  $ENGINE/src/path.cpp
  $ENGINE/src/text.cpp
  $ENGINE/src/extract.cpp
  $ENGINE/src/render.cpp
  $ENGINE/src/rss.cpp
  $ENGINE/src/csv.cpp
  $ENGINE/src/icons.cpp
  $ENGINE/src/fonts.cpp
  $ENGINE/src/fonts_data_16.cpp
  $ENGINE/src/fonts_data_24.cpp
  $ENGINE/src/fonts_data_32.cpp
  $ENGINE/src/fonts_data_48.cpp
  $ENGINE/src/fonts_data_96.cpp
  $ENGINE/src/fonts_data_128.cpp
  wasm_main.cpp
"
C_SRCS="$ENGINE/third_party/qrcodegen/qrcodegen.c"

INCLUDES="
  -I$ENGINE/include
  -I$ENGINE/third_party
  -I$ENGINE/third_party/qrcodegen
  -I../preview
  -I.
"

# STACK_SIZE: emscripten defaults to 64KB, which the engine's recursive layout
# used to overflow — not because the recursion is deep (the spec caps tree depth
# at 8, §12.1) but because drawWidget carried an 8288-byte frame: the two
# qrcodegen scratch buffers, 3918 bytes each, were inline in the recursive
# function, so every level paid ~8KB whether or not it drew a QR. That is fixed
# (drawQr is a non-inlined leaf now), and the whole-render high-water measured
# across the example packs is 12,960 bytes worst case — family-board, the one
# with a QR; layout and text alone are 7,472 there.
#
# 256KB is ~20x the measured worst case: enough slack for wasm-vs-host frame
# differences and for packs that nest to the depth cap, without reserving 4MB of
# linear memory in a module the project website loads. Re-measure (host proxy:
# `c++ -O2 -fstack-usage`, plus a high-water probe in Canvas::fillRect and
# FontProvider::glyph — fillRect is the only engine hook drawQr calls) before
# trusting these numbers after a layout change. Touches no output pixel.
EXPORTED_FUNCTIONS="_yat_render,_yat_get_buffer,_yat_get_width,_yat_get_height,_yat_get_error,_malloc,_free"
EXPORTED_RUNTIME_METHODS="ccall,cwrap,HEAPU8"

OBJS=""
for f in $CXX_SRCS; do
  o="$BUILD/$(basename "$f").o"
  "$EMCC" -O2 -std=c++17 $INCLUDES -c "$f" -o "$o"
  OBJS="$OBJS $o"
done
for f in $C_SRCS; do
  o="$BUILD/$(basename "$f").o"
  "$EMCC" -O2 -std=c11 $INCLUDES -c "$f" -o "$o"
  OBJS="$OBJS $o"
done

time "$EMCC" -O2 $OBJS \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=0 \
  -sEXPORT_NAME=YatModule \
  -sALLOW_MEMORY_GROWTH=1 \
  -sSTACK_SIZE=256KB \
  -sEXPORTED_FUNCTIONS="$EXPORTED_FUNCTIONS" \
  -sEXPORTED_RUNTIME_METHODS="$EXPORTED_RUNTIME_METHODS" \
  -o yat-engine.js

ls -la yat-engine.js yat-engine.wasm
