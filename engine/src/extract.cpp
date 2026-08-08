// Engine load / fetch / extract / hash — spec §3.3 (defaults), §5, §11.2 (v0.2 subset).
#include <yat/engine.h>

#include <cstdio>
#include <cstring>

namespace yat {

using namespace detail;

namespace {

// Source-side evaluation context (§7.2): no strings, no item/index.
EvalCtx sourceCtx(JsonVariantConst pack, JsonVariantConst params, JsonVariantConst data,
                  time_t now) {
  EvalCtx ctx;
  ctx.params = params;
  ctx.data = data;
  ctx.pack = pack;
  ctx.lang = langValue(pack, params);
  ctx.now = now;
  return ctx;
}

// §9.8a nested lists: exactly one level, legal only as a *direct* element of
// an outer list's row array. Everything else here is load-time static (the
// pack's `data.sources` declarations and the resolved param values are both
// fixed at install), so the outer-bind/inner-bind/prop-membership/product-cap
// rules are all validated here rather than deferred to render.

// The for_each source whose `collect.field` equals `fieldName`, or a null
// variant (JsonVariantConst().isNull() == true) if none declares it.
JsonVariantConst findForEachSourceByField(JsonVariantConst sources, const std::string& fieldName) {
  for (JsonVariantConst src : sources.as<JsonArrayConst>()) {
    if (src["for_each"].isNull()) continue;
    if ((src["collect"]["field"] | std::string("")) == fieldName) return src;
  }
  return JsonVariantConst();
}

// Load-time max_rows resolution — mirrors render.cpp's listPlan (§9.8 param
// form) — using the params already resolved earlier in Engine::load.
int resolveMaxRowsAtLoad(JsonVariantConst mr, JsonVariantConst params) {
  int v;
  if (mr.isNull()) {
    v = 10;
  } else if (mr.is<JsonObjectConst>()) {
    const char* pn = mr["param"] | "";
    JsonVariantConst pv = params[pn];
    v = (pv.is<long>() || pv.is<int>() || pv.is<double>()) ? (int)pv.as<long>() : 10;
  } else {
    v = mr.as<int>();
  }
  if (v < 1) v = 1;
  if (v > 20) v = 20;
  return v;
}

// depth: 0 = not inside any list row; 1 = iterating a list's row array
// directly (a `list` found here is the one legal §9.8a nested list); 2 =
// iterating a nested list's own row array (a `list` found here would be a
// third level — always rejected). `isRowArray` is true only while iterating a
// `row` array itself, not after descending into a column/row container
// inside it — that's what distinguishes "direct element of the row" from
// "nested deeper inside the row template" (also always rejected, §9.8a).
bool scanListNesting(JsonVariantConst widgets, int depth, bool isRowArray,
                      JsonVariantConst sources, JsonVariantConst params, std::string& err) {
  for (JsonVariantConst w : widgets.as<JsonArrayConst>()) {
    const char* t = w["type"] | "";
    if (strcmp(t, "list") == 0) {
      if (depth == 0) {
        // An outer (or ordinary, non-nesting) list. Find a direct nested
        // list in its row, if any, and validate the §9.8a [V] rules before
        // recursing structurally.
        JsonVariantConst outerRow = w["row"];
        JsonVariantConst nested;  // null until a direct nested `list` is found
        for (JsonVariantConst rw : outerRow.as<JsonArrayConst>()) {
          if (strcmp(rw["type"] | "", "list") == 0) { nested = rw; break; }
        }
        if (!nested.isNull()) {
          std::string outerBind = w["bind"] | "";
          static const std::string kDataPrefix = "data.";
          if (outerBind.compare(0, kDataPrefix.size(), kDataPrefix) != 0) {
            err = "pack: §9.8a nested list requires the outer list's bind to be a for_each "
                  "source's collect.field ('" + outerBind + "' is not data.<field>)";
            return false;
          }
          std::string fieldName = outerBind.substr(kDataPrefix.size());
          JsonVariantConst src = findForEachSourceByField(sources, fieldName);
          if (src.isNull()) {
            err = "pack: §9.8a outer list bind 'data." + fieldName +
                  "' does not name a for_each source's collect.field";
            return false;
          }
          std::string innerBind = nested["bind"] | "";
          static const std::string kItemPrefix = "item.";
          if (innerBind.compare(0, kItemPrefix.size(), kItemPrefix) != 0) {
            err = "pack: §9.8a nested list bind must be 'item.<prop>' ('" + innerBind + "')";
            return false;
          }
          std::string prop = innerBind.substr(kItemPrefix.size());
          if (src["collect"]["extract"][prop.c_str()].isNull()) {
            err = "pack: §9.8a nested list bind 'item." + prop +
                  "' is not a collect.extract field of source '" + (src["id"] | "") + "'";
            return false;
          }
          int outerMax = resolveMaxRowsAtLoad(w["max_rows"], params);
          int innerMax = resolveMaxRowsAtLoad(nested["max_rows"], params);
          if ((long)outerMax * (long)innerMax > 40) {
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "pack: §9.8a outer max_rows (%d) x nested max_rows (%d) = %d exceeds the "
                     "cap of 40 [V]", outerMax, innerMax, outerMax * innerMax);
            err = buf;
            return false;
          }
        }
        if (!scanListNesting(outerRow, 1, true, sources, params, err)) return false;
      } else if (depth == 1 && isRowArray) {
        // The one legal nested list (already validated above); its own row
        // may hold ordinary widgets but never a third list level.
        if (!scanListNesting(w["row"], 2, true, sources, params, err)) return false;
      } else {
        err = "pack: §9.8a a `list` may appear only as a direct element of an outer list's row "
              "— never inside a column/row of the row template, and never a third level deep";
        return false;
      }
    } else if (!w["children"].isNull()) {
      if (!scanListNesting(w["children"], depth, false, sources, params, err)) return false;
    }
  }
  return true;
}

// §9.10 [V]: at most 2 `image` widgets per pack. Walks the whole widget
// tree — root `widgets`, every `column`/`row` `children` array, and every
// `list`'s `row` template (incl. the one legal §9.8a nested level) — since
// an `image` could otherwise hide inside any of those.
void countImageWidgets(JsonVariantConst widgets, int& count) {
  for (JsonVariantConst w : widgets.as<JsonArrayConst>()) {
    const char* t = w["type"] | "";
    if (strcmp(t, "image") == 0) count++;
    if (!w["children"].isNull()) countImageWidgets(w["children"], count);
    if (strcmp(t, "list") == 0 && !w["row"].isNull()) countImageWidgets(w["row"], count);
  }
}

// §5.1a [V] (RFC-foreach-partial-results.md option D): a `collect.extract`
// field name may not be `each` or `status` — both are reserved members the
// engine itself writes onto every collected element (rule 3), and a pack
// field of either name would collide with them silently (the pack's value
// clobbered by, or clobbering, the engine's own bookkeeping) rather than by
// a clear load-time error. `each` was already reserved by prose with no [V]
// enforcement before this; `status` is new alongside it, so both are closed
// here together.
bool checkCollectReservedNames(JsonVariantConst sources, std::string& err) {
  for (JsonVariantConst src : sources.as<JsonArrayConst>()) {
    if (src["for_each"].isNull()) continue;
    for (JsonPairConst kv : src["collect"]["extract"].as<JsonObjectConst>()) {
      const char* name = kv.key().c_str();
      if (strcmp(name, "each") == 0 || strcmp(name, "status") == 0) {
        err = std::string("pack: source '") + (src["id"] | "") + "': collect.extract field name '" +
              name + "' is reserved (§5.1a rule 3) [V]";
        return false;
      }
    }
  }
  return true;
}

}  // namespace

bool Engine::load(const char* packJson, const char* paramsJson, std::string& err) {
  DeserializationError e = deserializeJson(pack_, packJson, DeserializationOption::NestingLimit(24));
  if (e) { err = std::string("pack parse: ") + e.c_str(); return false; }

  // Resolve params: defaults first (§3.3.1), then user overlay.
  params_.clear();
  JsonObject vals = params_.to<JsonObject>();
  JsonObjectConst props = pack_["params"]["properties"].as<JsonObjectConst>();
  for (JsonPairConst kv : props) {
    JsonVariantConst def = kv.value()["default"];
    if (!def.isNull()) vals[kv.key()] = def;
  }
  if (paramsJson && paramsJson[0]) {
    JsonDocument user;
    e = deserializeJson(user, paramsJson);
    if (e) { err = std::string("params parse: ") + e.c_str(); return false; }
    for (JsonPairConst kv : user.as<JsonObjectConst>()) vals[kv.key()] = kv.value();
  }

  // §9.13 [V]: a strings table requires render.lang_param, which must name a
  // resolved param (the engine reads its installed value, §9.13(2)).
  if (!pack_["strings"].isNull() && pack_["render"]["lang_param"].isNull()) {
    err = "pack: `strings` requires render.lang_param (§9.13)";
    return false;
  }
  const char* lp = pack_["render"]["lang_param"] | (const char*)nullptr;
  if (lp && vals[lp].isNull()) {
    err = std::string("pack: render.lang_param '") + lp + "' has no installed value (§9.13)";
    return false;
  }

  // §5.1a [V]: collect.extract field names may not be `each` or `status`.
  if (!checkCollectReservedNames(pack_["data"]["sources"], err)) return false;

  // §9.8a [V]: at most one nested list level, direct row element only, outer
  // bind must be a for_each source's collect.field, inner bind's prop must be
  // a field of that source's collect.extract, outer x nested max_rows <= 40.
  if (!scanListNesting(pack_["render"]["widgets"], 0, false, pack()["data"]["sources"], params(),
                       err))
    return false;

  // §9.10 [V]: at most 2 `image` widgets per pack.
  int imageCount = 0;
  countImageWidgets(pack_["render"]["widgets"], imageCount);
  if (imageCount > 2) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "pack: at most 2 `image` widgets allowed per pack (%d found) (§9.10 [V])",
             imageCount);
    err = buf;
    return false;
  }
  return true;
}

namespace {

// Parses one https-or-inline source's fetched body per its declared
// `format` (§5.3-5.5) into `doc`. Returns false + `perr` on a parse failure
// (json) — rss/csv never fail to parse (tolerant scanners; misses become ""
// / null per their own spec sections), so `ok` is always true for them
// unless the format itself is unrecognized (a hard/grammar-level error,
// signaled via `err` + returning false from this function).
bool parseBody(const std::string& fmt, const std::string& body, JsonVariantConst csvOpts,
               JsonDocument& doc, std::string& err, bool& ok) {
  ok = true;
  if (fmt == "json") {
    DeserializationError de = deserializeJson(doc, body, DeserializationOption::NestingLimit(24));
    if (de) { ok = false; err = de.c_str(); }
    return true;
  }
  if (fmt == "rss") {
    std::string perr;
    if (!detail::parseRss(body, doc, perr)) { ok = false; err = perr; }
    return true;
  }
  if (fmt == "csv") {
    bool hasHeader = csvOpts["has_header"] | false;
    std::string perr;
    detail::parseCsv(body, hasHeader, doc, perr);  // never fails
    return true;
  }
  err = std::string("engine v0.2: format ") + fmt + " not implemented";
  return false;
}

// Fan-out over `params.<array>` for a `for_each` https source (§5.1a rule 5,
// RFC-foreach-partial-results.md option D). Writes into out[collect.field]:
// the full collected array (one element per input element, in input order)
// whenever at least one iteration succeeds — a failed iteration contributes
// an element carrying `each`, `status: "failed"`, and null extract fields
// rather than being dropped, so {{index}} stays stable and the array is
// never mixed with stale values — or JSON null when every iteration failed
// (unchanged pre-D shape, so the caller's existing stale-serve branch still
// fires off that sentinel). `failCount` is the caller's only way to tell
// "some failed" (partial, fresh, must not be snapshotted) apart from "none
// failed" now that a partial array is no longer null. Returns false only on
// a grammar/programming error in the pack itself (bad for_each target,
// malformed placeholder, path-expression syntax error, unrecognized
// format) — a fetch/parse failure for one iteration is a *soft* failure,
// matching how a plain https source's fetch failure is handled, and no
// longer short-circuits the remaining iterations (§D: `continue`, not the
// old `break`).
bool fetchForEach(JsonVariantConst src, const FetchFn& fetch, const EvalCtx& base,
                  JsonObject out, int& failCount, std::string& err) {
  const char* id = src["id"] | "";
  const char* fieldName = src["collect"]["field"] | "";
  std::string feRef = src["for_each"] | "";
  static const std::string kPrefix = "params.";
  failCount = 0;
  if (feRef.compare(0, kPrefix.size(), kPrefix) != 0) {
    err = std::string("source ") + id + ": for_each must be 'params.<name>'";
    return false;
  }
  std::string arrName = feRef.substr(kPrefix.size());
  JsonVariantConst arr = base.params[arrName.c_str()];

  JsonDocument resultDoc;
  JsonArray result = resultDoc.to<JsonArray>();

  if (!arr.is<JsonArrayConst>()) {
    // §3.3(3): an unreferenced/defaultless array param resolves to null.
    // A conformant pack can't actually reach this (for_each's target must be
    // required-or-defaulted per §3.3(2)) — defensively treat it as the
    // empty-array case (§5.1a rule 7) rather than a hard failure.
    out[fieldName] = resultDoc.as<JsonVariantConst>();  // []
    return true;
  }

  std::string ferrSaved;
  const char* fmt = src["format"] | "json";
  size_t total = 0;

  for (JsonVariantConst elem : arr.as<JsonArrayConst>()) {
    total++;
    EvalCtx ec = base;
    ec.each = elem;  // §5.1a: {{each}} in scope for this iteration

    std::string url;
    if (!substitute(src["url"] | "", ec, url, err, /*urlEncode=*/true))
      return false;  // grammar error in the pack — hard fail

    std::string body, ferr;
    bool iterOk = fetch(id, url, body, ferr);
    JsonDocument doc;
    if (iterOk) {
      bool ok;
      if (!parseBody(fmt, body, src["csv"], doc, ferr, ok)) { err = ferr; return false; }
      iterOk = ok;
    }

    JsonObject itemObj = result.add<JsonObject>();
    if (!iterOk) {
      failCount++;
      ferrSaved = ferr;
      itemObj["each"] = elem;
      itemObj["status"] = "failed";
      for (JsonPairConst kv : src["collect"]["extract"].as<JsonObjectConst>())
        itemObj[kv.key()] = nullptr;
      continue;  // §D: a failed iteration no longer drops the rest
    }

    for (JsonPairConst kv : src["collect"]["extract"].as<JsonObjectConst>()) {
      std::string expr;
      if (!substitute(kv.value().as<const char*>(), ec, expr, err)) return false;
      JsonDocument res;
      if (!evalPath(doc.as<JsonVariantConst>(), expr, res, err)) return false;
      itemObj[kv.key()] = res.as<JsonVariantConst>();
    }
    itemObj["each"] = elem;
    itemObj["status"] = "ok";
  }

  if (failCount > 0 && (size_t)failCount == total) {
    // All iterations failed: unchanged pre-D shape — null tells the caller
    // to stale-serve the last-good collected array (§11.3), same as today.
    out[fieldName] = nullptr;
    err = std::string("source ") + id + ": " + ferrSaved;
    return true;  // soft failure, like a plain https source's fetch error
  }
  out[fieldName] = resultDoc.as<JsonVariantConst>();
  if (failCount > 0) {
    // §D: some (not all) iterations failed. The array is full-length and
    // entirely fresh; named E_ITER so it can join E_TYPE/E_BIND in the
    // §11.4 data-warning trigger set. The caller must not snapshot this —
    // that is what `failCount` tells it, off the isNull() sentinel.
    err = std::string("source ") + id + ": E_ITER — " + std::to_string(failCount) + "/" +
          std::to_string(total) + " iteration(s) failed (" + ferrSaved +
          "); serving the rest fresh, no snapshot written (§5.1a)";
  }
  return true;
}

// §11.3/§5.1 snapshot key: "<packid>.<sourceid>" — one entry per source.
std::string snapshotKey(JsonVariantConst pack, const char* sourceId) {
  std::string k = pack["id"] | "";
  k += '.';
  k += sourceId;
  return k;
}

// Loads a source's last-good snapshot ({"t":epoch,"fields":{...}}) from
// `store`. Returns false (leaving `snapOut`/`tsOut` untouched) when the
// store is null, the key is absent, or the stored value doesn't parse as
// the expected shape — all treated as "no snapshot on record" (§11.3
// never-succeeded case), never a hard error.
bool loadSnapshot(StateStore* store, const std::string& key, JsonDocument& snapOut, time_t& tsOut) {
  if (!store) return false;
  std::string s;
  if (!store->get(key.c_str(), s)) return false;
  if (deserializeJson(snapOut, s)) return false;
  JsonVariantConst t = snapOut["t"];
  if (!t.is<long>() && !t.is<int>() && !t.is<double>()) return false;
  tsOut = (time_t)t.as<long>();
  return true;
}

// Persists `fields` (this source's just-extracted field values, keyed by
// field name — or the for_each collect.field -> array) as its §11.3
// stale-serve snapshot. No-op when persistence is disabled (store == null).
void putSnapshot(StateStore* store, const std::string& key, JsonVariantConst fields, time_t now) {
  if (!store) return;
  JsonDocument doc;
  doc["t"] = (long)now;
  doc["fields"].set(fields);
  std::string s;
  serializeJson(doc, s);
  store->put(key.c_str(), s);
}

}  // namespace

bool Engine::fetchAndExtract(const FetchFn& fetch, time_t now, std::string& err) {
  fetch_ = fetch;  // stashed for render()'s `image` widgets (§9.10) — see engine.h
  data_.clear();
  JsonObject out = data_.to<JsonObject>();
  anyStale_ = false;
  oldestStale_ = 0;
  srcWithData_ = 0;
  srcEmpty_ = 0;
  iterWarn_ = false;
  // Per-source outcome tally behind allSourcesEmpty() (§11.3). Every source
  // ends this call in exactly one of three states, and only two of them are
  // counted: it produced content (fresh fetch, min_refresh serve, or a
  // stale-served snapshot) or it failed with nothing on record. A `when`-
  // skipped source is neither — the §11.3 taxonomy says a skip is not a
  // failure, and a page whose sources all skipped is rendering exactly as its
  // author asked, so it must not trip the empty-state card.
  auto sourceHasData = [&] { srcWithData_++; };
  auto sourceEmpty = [&] { srcEmpty_++; };
  // Records a source as stale-serving its `snapshotTs` snapshot, tracking
  // the oldest such timestamp across this call (§11.3 "stale since HH:MM").
  auto markStale = [&](time_t snapshotTs) {
    if (!anyStale_ || snapshotTs < oldestStale_) oldestStale_ = snapshotTs;
    anyStale_ = true;
  };

  // §5.7: id -> {field: value} for every plain/inline source processed so
  // far, in declaration order. A `for_each` source is never added (its
  // collect.field can never be referenced, rule 2) — so referencing one
  // falls through to the same "not an earlier source" error as a forward or
  // self reference.
  JsonDocument sourcesDoc;
  JsonObject sourcesObj = sourcesDoc.to<JsonObject>();

  for (JsonVariantConst src : pack_["data"]["sources"].as<JsonArrayConst>()) {
    const char* id = src["id"] | "";
    const char* type = src["type"] | "";
    bool isForEach = !src["for_each"].isNull();

    // Rebuilt at every use: data_ grows as fields are extracted. `{{sources.
    // *}}` is deliberately excluded here — source-level `when` (§5.0) may
    // not reference it (§5.7 rule 1) — and included only where the spec
    // allows it (url substitution, extract filter literals) via ctxSrc().
    auto ctx = [&] { return sourceCtx(pack(), params(), data(), now); };
    auto ctxSrc = [&] {
      EvalCtx c = ctx();
      c.sources = sourcesDoc.as<JsonVariantConst>();
      return c;
    };
    // Copy this source's just-written `out` fields into sourcesObj[id] so
    // later sources can reference them. Called from every non-for_each exit
    // path (when-skip, fetch/parse failure, E_REF_NULL, and success).
    auto mirrorToSources = [&] {
      JsonObject sf = sourcesObj[id].to<JsonObject>();
      for (JsonPairConst kv : src["extract"].as<JsonObjectConst>()) sf[kv.key()] = out[kv.key()];
    };

    // source-level when (§5.0)
    bool run = true;
    if (!evalWhen(src["when"], ctx(), run, err)) return false;
    if (!run) {
      if (isForEach) {
        const char* fieldName = src["collect"]["field"] | "";
        out[fieldName] = nullptr;  // fresh null (§5.0), not collect.field = []
      } else {
        for (JsonPairConst kv : src["extract"].as<JsonObjectConst>())
          out[kv.key()] = nullptr;  // fresh nulls
        mirrorToSources();
      }
      continue;
    }

    // §11.3/§5.1: one snapshot key per source (both for_each and plain).
    std::string snapKey = snapshotKey(pack(), id);
    int minRefresh = src["min_refresh_min"] | 0;

    if (isForEach) {
      const char* fieldName = src["collect"]["field"] | "";
      if (minRefresh > 0 && store_) {
        JsonDocument snap;
        time_t ts;
        if (loadSnapshot(store_, snapKey, snap, ts) &&
            (long)(now - ts) < (long)minRefresh * 60) {
          out[fieldName] = snap["fields"][fieldName];
          sourceHasData();
          err = std::string("[fetch] ") + id + " served from snapshot (min_refresh_min)";
          continue;
        }
      }
      int iterFailCount = 0;
      if (!fetchForEach(src, fetch, ctx(), out, iterFailCount, err)) return false;
      if (out[fieldName].isNull()) {
        // Soft failure, ALL iterations failed (§5.1a rule 5) -> stale-serve
        // the last-good collected array, unchanged from pre-D.
        JsonDocument snap;
        time_t ts;
        if (loadSnapshot(store_, snapKey, snap, ts)) {
          out[fieldName] = snap["fields"][fieldName];
          markStale(ts);
          sourceHasData();
          err += " (stale-serve)";
        } else {
          sourceEmpty();
        }
      } else if (iterFailCount > 0) {
        // §D: some (not all) iterations failed. The array is full-length and
        // entirely fresh (failed elements carry status:"failed" + null
        // fields), so this counts as data — but the source's last-good
        // snapshot is left exactly as it was: a partial array must never
        // become tomorrow's stale-serve, and must never itself be built by
        // mixing in a value from that snapshot (§5.1a: "not mixed").
        sourceHasData();
        // §11.4: E_ITER is one of the three data warnings the footer glyph is
        // for, and it is the only one that happens out here rather than during
        // render() — so the flag has to survive the trip.
        iterWarn_ = true;
      } else {
        sourceHasData();
        JsonDocument fieldsDoc;
        JsonObject fo = fieldsDoc.to<JsonObject>();
        fo[fieldName] = out[fieldName];
        putSnapshot(store_, snapKey, fieldsDoc.as<JsonVariantConst>(), now);
      }
      continue;
    }

    // Serves this source's last-good snapshot into `out` and marks it stale
    // (§11.3); returns false ("never succeeded") when none is on record —
    // the caller's nulls stand. Called only from a failure path, so it is
    // also the single place that decides which of the two tallied outcomes
    // this failed source lands in.
    auto tryStaleServe = [&] {
      JsonDocument snap;
      time_t ts;
      if (!loadSnapshot(store_, snapKey, snap, ts)) { sourceEmpty(); return false; }
      for (JsonPairConst kv : src["extract"].as<JsonObjectConst>())
        out[kv.key()] = snap["fields"][kv.key()];
      markStale(ts);
      sourceHasData();
      return true;
    };

    if (minRefresh > 0 && store_) {
      JsonDocument snap;
      time_t ts;
      if (loadSnapshot(store_, snapKey, snap, ts) && (long)(now - ts) < (long)minRefresh * 60) {
        for (JsonPairConst kv : src["extract"].as<JsonObjectConst>())
          out[kv.key()] = snap["fields"][kv.key()];
        sourceHasData();
        mirrorToSources();
        err = std::string("[fetch] ") + id + " served from snapshot (min_refresh_min)";
        continue;
      }
    }

    JsonDocument doc;
    if (strcmp(type, "inline") == 0) {
      doc.set(src["data"]);
    } else if (strcmp(type, "https") == 0) {
      std::string url;
      bool refNull = false;
      if (!substitute(src["url"] | "", ctxSrc(), url, err, /*urlEncode=*/true, nullptr, &refNull))
        return false;
      if (refNull) {
        // §5.7 rule 3: a null {{sources.*}} referent means this source is
        // not fetched and counts as failed (E_REF_NULL) -> stale-serve.
        for (JsonPairConst kv : src["extract"].as<JsonObjectConst>()) out[kv.key()] = nullptr;
        err = std::string("source ") + id +
              ": E_REF_NULL — a {{sources.*}} reference resolved to null (§5.7)";
        if (tryStaleServe()) err += " (stale-serve)";
        mirrorToSources();
        continue;
      }
      std::string body, ferr;
      if (!fetch(id, url, body, ferr)) {
        for (JsonPairConst kv : src["extract"].as<JsonObjectConst>()) out[kv.key()] = nullptr;
        err = std::string("source ") + id + ": " + ferr;
        if (tryStaleServe()) err += " (stale-serve)";
        mirrorToSources();
        continue;  // keep going; caller decides severity
      }
      const char* fmt = src["format"] | "json";
      bool ok;
      std::string perr;
      if (!parseBody(fmt, body, src["csv"], doc, perr, ok)) { err = perr; return false; }
      if (!ok) {
        for (JsonPairConst kv : src["extract"].as<JsonObjectConst>()) out[kv.key()] = nullptr;
        err = std::string("source ") + id + " parse: " + perr;
        if (tryStaleServe()) err += " (stale-serve)";
        mirrorToSources();
        continue;
      }
    } else {
      err = std::string("unknown source type ") + type;
      return false;
    }

    // §5.7: extract filter literals may also embed {{sources.<id>.<field>}}.
    // Check every field for a null referent *before* writing any of them —
    // a null anywhere fails the whole source (rule 3), same as a fetch
    // failure above.
    bool refNull = false;
    for (JsonPairConst kv : src["extract"].as<JsonObjectConst>()) {
      std::string expr;
      bool nr = false;
      if (!substitute(kv.value().as<const char*>(), ctxSrc(), expr, err, false, nullptr, &nr))
        return false;
      if (nr) { refNull = true; break; }
    }
    if (refNull) {
      for (JsonPairConst kv : src["extract"].as<JsonObjectConst>()) out[kv.key()] = nullptr;
      err = std::string("source ") + id +
            ": E_REF_NULL — a {{sources.*}} reference resolved to null (§5.7)";
      if (tryStaleServe()) err += " (stale-serve)";
    } else {
      for (JsonPairConst kv : src["extract"].as<JsonObjectConst>()) {
        std::string expr;
        if (!substitute(kv.value().as<const char*>(), ctxSrc(), expr, err)) return false;
        JsonDocument res;
        if (!evalPath(doc.as<JsonVariantConst>(), expr, res, err)) return false;
        out[kv.key()] = res.as<JsonVariantConst>();
      }
      // Success: persist this source's snapshot (§11.3). An `inline` source
      // lands here too — it cannot fail, so it always counts as data.
      sourceHasData();
      JsonDocument fieldsDoc;
      JsonObject fo = fieldsDoc.to<JsonObject>();
      for (JsonPairConst kv : src["extract"].as<JsonObjectConst>()) fo[kv.key()] = out[kv.key()];
      putSnapshot(store_, snapKey, fieldsDoc.as<JsonVariantConst>(), now);
    }
    mirrorToSources();
  }

  // compute (§7.4): evaluated after all extraction, in declaration order;
  // results join data.* before hash-skip (dataHash() below just serializes
  // data_, which now includes them).
  for (JsonPairConst kv : pack_["compute"].as<JsonObjectConst>()) {
    JsonDocument res;
    if (!evalCompute(kv.value().as<const char*>(), sourceCtx(pack(), params(), data(), now), res,
                     err))
      return false;
    out[kv.key()] = res.as<JsonVariantConst>();
  }

  return true;
}

uint32_t Engine::dataHash(time_t now) const {
  std::string s, part;
  serializeJson(data_, part);
  s += part;
  part.clear();
  serializeJson(params_, part);
  s += part;
  struct tm lt = detail::hkLocal(now);
  char date[16];
  snprintf(date, sizeof(date), "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
  s += date;
  s += pack_["id"] | "";
  s += pack_["version"] | "";
  // §11.4a: what the chrome will say about the device, folded in so a notice
  // appearing or clearing cannot be hash-skipped into invisibility. Both are
  // sticky by nature — an Action code holds until a person fixes something,
  // the Degraded flag until a fetch succeeds — so this costs a refresh at each
  // edge and nothing in between, which is exactly the two moments worth 30
  // seconds of panel. The stale timestamp itself is deliberately NOT hashed:
  // it moves on every wake and would defeat the skip entirely.
  // Appended only when there IS one, so a healthy page hashes to exactly what
  // it hashed to before notices existed and no fleet gets a free refresh out
  // of a firmware update.
  if (notice_ != NoticeCode::None) { s += 'N'; s += (char)('a' + (int)notice_); }
  if (staleDegraded(now)) s += 'D';
  return detail::fnv1a(s.data(), s.size());
}

}  // namespace yat
