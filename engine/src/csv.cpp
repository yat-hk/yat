// RFC 4180 CSV parser — Pack Spec §5.5. All cells are strings, never type-
// inferred. Canonical shape depends on `has_header`:
//   true  -> {"rows": [ {"<col>": "<cell>", ...}, ... ]}  (header row excluded)
//   false -> {"rows": [ ["<cell>", ...], ... ]}
// Capped at 100 data rows x 20 columns; excess truncated, never an error.
#include <yat/engine.h>

#include <string>
#include <vector>

namespace yat {
namespace detail {

namespace {

// Tokenizes comma-separated, double-quote-quoted (""-escaped) records,
// terminated by CRLF or bare LF. Embedded commas/newlines inside a quoted
// field are preserved as data, not treated as separators.
std::vector<std::vector<std::string>> tokenizeCsv(const std::string& body) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string field;
  bool inQuotes = false;
  size_t i = 0, n = body.size();

  while (i < n) {
    char c = body[i];
    if (inQuotes) {
      if (c == '"') {
        if (i + 1 < n && body[i + 1] == '"') { field += '"'; i += 2; continue; }
        inQuotes = false;
        i++;
        continue;
      }
      field += c;
      i++;
      continue;
    }
    if (c == '"') { inQuotes = true; i++; continue; }
    if (c == ',') { row.push_back(field); field.clear(); i++; continue; }
    if (c == '\r') {
      row.push_back(field);
      field.clear();
      rows.push_back(row);
      row.clear();
      i++;
      if (i < n && body[i] == '\n') i++;
      continue;
    }
    if (c == '\n') {
      row.push_back(field);
      field.clear();
      rows.push_back(row);
      row.clear();
      i++;
      continue;
    }
    field += c;
    i++;
  }
  // Trailing record with no terminating newline.
  if (!field.empty() || !row.empty()) {
    row.push_back(field);
    rows.push_back(row);
  }
  return rows;
}

}  // namespace

bool parseCsv(const std::string& body, bool hasHeader, JsonDocument& out, std::string& err) {
  (void)err;  // no parse-failure path — truncation absorbs malformed width
  out.clear();
  JsonObject root = out.to<JsonObject>();
  JsonArray rowsArr = root["rows"].to<JsonArray>();

  std::vector<std::vector<std::string>> rawRows = tokenizeCsv(body);

  const size_t kMaxCols = 20;
  const size_t kMaxRows = 100;

  size_t startIdx = 0;
  std::vector<std::string> header;
  if (hasHeader && !rawRows.empty()) {
    header = rawRows[0];
    if (header.size() > kMaxCols) header.resize(kMaxCols);
    startIdx = 1;
  }

  size_t emitted = 0;
  for (size_t r = startIdx; r < rawRows.size() && emitted < kMaxRows; r++, emitted++) {
    const std::vector<std::string>& row = rawRows[r];
    if (hasHeader) {
      JsonObject o = rowsArr.add<JsonObject>();
      for (size_t c = 0; c < header.size(); c++)
        o[header[c]] = c < row.size() ? row[c] : std::string();
    } else {
      JsonArray a = rowsArr.add<JsonArray>();
      size_t cols = row.size() < kMaxCols ? row.size() : kMaxCols;
      for (size_t c = 0; c < cols; c++) a.add(row[c]);
    }
  }
  return true;
}

}  // namespace detail
}  // namespace yat
