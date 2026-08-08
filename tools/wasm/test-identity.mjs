#!/usr/bin/env node
// Identity proof: yat-engine.wasm's render output must be byte-identical
// (RGB triples) to tools/preview's native target for the same
// (pack, fixtures, params, --now). This is the load-bearing check for the
// "one engine, three targets" architecture (see ../../ARCHITECTURE.md) —
// if this fails, the browser preview cannot be trusted to represent what
// the native preview / firmware would show.
//
// Self-contained: builds/uses tools/preview/yat-preview to produce the
// native reference on the fly (via --raw, an additive flag on that tool),
// and tools/wasm/yat-engine.{js,wasm} (build with ./build.sh first) for the
// wasm side. Run with: node test-identity.mjs
import { createRequire } from "node:module";
import { execFileSync } from "node:child_process";
import { readFileSync, existsSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "../..");
const PREVIEW_DIR = path.join(ROOT, "tools/preview");
const NOW = 1785142800; // pinned instant, matches tools/preview/run-tests.sh

let failures = 0;

function ensureNativeBuilt() {
  const bin = path.join(PREVIEW_DIR, "yat-preview");
  if (!existsSync(bin)) {
    console.log("native yat-preview missing, building...");
    execFileSync("make", ["-s"], { cwd: PREVIEW_DIR, stdio: "inherit" });
  }
  return bin;
}

function nativeRawRender(bin, packRelPath, extraArgs, rawOut) {
  const args = [path.join(ROOT, packRelPath), ...extraArgs, "--now", String(NOW), "--out",
                path.join(rawOut + ".png"), "--raw", rawOut];
  execFileSync(bin, args, { cwd: PREVIEW_DIR, stdio: ["ignore", "ignore", "ignore"] });
  return readFileSync(rawOut); // raw RGB, 800*480*3 bytes
}

async function loadWasm() {
  const jsPath = path.join(HERE, "yat-engine.js");
  if (!existsSync(jsPath)) {
    throw new Error(`${jsPath} not found — run ./build.sh first`);
  }
  const YatModuleFactory = require(jsPath);
  const mod = await YatModuleFactory();
  return mod;
}

function wasmRgbaToRgb(mod, w, h) {
  const ptr = mod.ccall("yat_get_buffer", "number", [], []);
  const rgba = mod.HEAPU8.subarray(ptr, ptr + w * h * 4);
  const rgb = Buffer.alloc(w * h * 3);
  for (let i = 0, o = 0; i < w * h; i++, o += 3) {
    rgb[o] = rgba[i * 4];
    rgb[o + 1] = rgba[i * 4 + 1];
    rgb[o + 2] = rgba[i * 4 + 2];
  }
  return rgb;
}

function firstDivergence(a, b, w) {
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; i++) {
    if (a[i] !== b[i]) {
      const pixelIdx = Math.floor(i / 3);
      const x = pixelIdx % w;
      const y = Math.floor(pixelIdx / w);
      const channel = ["R", "G", "B"][i % 3];
      return { byteOffset: i, x, y, channel, native: a[i], wasm: b[i] };
    }
  }
  if (a.length !== b.length) return { lengthMismatch: true, aLen: a.length, bLen: b.length };
  return null;
}

function compare(name, nativeRgb, wasmRgb, w) {
  if (nativeRgb.length !== wasmRgb.length) {
    console.log(`FAIL ${name}: length mismatch (native ${nativeRgb.length} vs wasm ${wasmRgb.length})`);
    failures++;
    return;
  }
  const div = firstDivergence(nativeRgb, wasmRgb, w);
  if (div === null) {
    console.log(`OK   ${name}: ${nativeRgb.length} bytes identical`);
  } else {
    console.log(`FAIL ${name}: first divergence at byte ${div.byteOffset} ` +
                `(pixel x=${div.x} y=${div.y} channel=${div.channel}): ` +
                `native=0x${div.native.toString(16)} wasm=0x${div.wasm.toString(16)}`);
    failures++;
  }
}

async function main() {
  const bin = ensureNativeBuilt();
  const tmp = mkdtempSync(path.join(tmpdir(), "yat-wasm-identity-"));
  const mod = await loadWasm();
  const w = mod.ccall("yat_get_width", "number", [], []);
  const h = mod.ccall("yat_get_height", "number", [], []);
  console.log(`canvas: ${w}x${h}`);

  // --- hko-now: real https source via fixture, exercises path expressions,
  // ---   placeholders, bignum/text/row/column widgets, standard chrome.
  // ---   Reads the frozen copy under tools/preview/testpacks/ — the pack
  // ---   itself moved to yat-hk/yat-packs, and this proof may only depend on
  // ---   packs that ship in this repo (testpacks/README.md).
  {
    const packRel = "tools/preview/testpacks/hko-now.yat-pack.json";
    const current = readFileSync(path.join(PREVIEW_DIR, "fixtures/hko-now.current.json"), "utf8");
    const warnsum = readFileSync(path.join(PREVIEW_DIR, "fixtures/hko-now.warnsum.json"), "utf8");
    const nativeRgb = nativeRawRender(
      bin, packRel,
      ["--doc", `current=${path.join(PREVIEW_DIR, "fixtures/hko-now.current.json")}`,
       "--doc", `warnsum=${path.join(PREVIEW_DIR, "fixtures/hko-now.warnsum.json")}`],
      path.join(tmp, "hko-now.raw"));

    const docsJson = JSON.stringify({ current, warnsum });
    const packJson = readFileSync(path.join(ROOT, packRel), "utf8");
    const rc = mod.ccall("yat_render", "number",
      ["string", "string", "string", "number"], [packJson, "", docsJson, NOW]);
    const wasmErr = mod.ccall("yat_get_error", "string", [], []);
    if (rc !== 0) console.log(`  wasm yat_render rc=${rc} error="${wasmErr}"`);
    else if (wasmErr) console.log(`  wasm warn: ${wasmErr}`);
    const wasmRgb = wasmRgbaToRgb(mod, w, h);
    compare("hko-now", nativeRgb, wasmRgb, w);
  }

  // --- test-ink-roles: inline source only (no fixtures/network at all), and
  // ---   CJK-heavy — exercises efont glyph rendering and icon widgets in
  // ---   wasm, proving the vendored font/icon data round-trips through emcc.
  // ---   It also draws all six §9.1a roles, which makes it the pack that
  // ---   catches the two display palettes (tools/preview/main.cpp and
  // ---   wasm_main.cpp) drifting apart: they are two hand-copied tables, and
  // ---   §9.1a-a's calibrated yellow lives in both.
  // ---   (No QR coverage since family-board moved out with the other packs —
  // ---   no pack in this repo draws one.)
  {
    const packRel = "tools/preview/fixtures/packs/test-ink-roles.yat-pack.json";
    const nativeRgb = nativeRawRender(bin, packRel, [], path.join(tmp, "test-ink-roles.raw"));

    const packJson = readFileSync(path.join(ROOT, packRel), "utf8");
    const rc = mod.ccall("yat_render", "number",
      ["string", "string", "string", "number"], [packJson, "", "", NOW]);
    const wasmErr = mod.ccall("yat_get_error", "string", [], []);
    if (rc !== 0) console.log(`  wasm yat_render rc=${rc} error="${wasmErr}"`);
    else if (wasmErr) console.log(`  wasm warn: ${wasmErr}`);
    const wasmRgb = wasmRgbaToRgb(mod, w, h);
    compare("test-ink-roles (CJK/icon/palette)", nativeRgb, wasmRgb, w);
  }

  if (failures > 0) {
    console.log(`\n${failures} pack(s) FAILED identity check`);
    process.exit(1);
  }
  console.log("\nALL PACKS IDENTICAL (native RGB == wasm RGB)");
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
