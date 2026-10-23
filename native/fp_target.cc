#include "fp_target.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "kernel_pipe_json.h"

namespace lengsuo::fp {
namespace {

// The biggest side a sample may have. A side of four gigapixels is far
// past anything a browser allocates, and the ceiling keeps a fat-fingered
// configuration from asking for one.
constexpr std::int64_t kMaxSide = 1 << 20;

// Skips the whitespace of a JSON text.
void SkipWhitespace(std::string_view text, std::size_t* pos) {
  while (*pos < text.size()) {
    const char c = text[*pos];
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
      break;
    }
    ++(*pos);
  }
}

// Finds the end of the JSON value that starts at `*pos`.
//
// Returns false when the text is malformed. The value may be an object, an
// array, a string or a scalar; the caller only needs to know where it
// ends.
bool ScanValue(std::string_view text, std::size_t* pos) {
  if (*pos >= text.size()) {
    return false;
  }
  const char c = text[*pos];
  if (c == '"') {
    ++(*pos);
    while (*pos < text.size()) {
      const char d = text[*pos];
      if (d == '\\') {
        ++(*pos);
        if (*pos >= text.size()) {
          return false;
        }
        ++(*pos);
        continue;
      }
      ++(*pos);
      if (d == '"') {
        return true;
      }
    }
    return false;
  }
  if (c == '{' || c == '[') {
    int depth = 0;
    while (*pos < text.size()) {
      const char d = text[*pos];
      if (d == '"') {
        if (!ScanValue(text, pos)) {
          return false;
        }
        continue;
      }
      ++(*pos);
      if (d == '{' || d == '[') {
        ++depth;
      } else if (d == '}' || d == ']') {
        --depth;
        if (depth == 0) {
          return true;
        }
      }
    }
    return false;
  }
  // A scalar ends at a delimiter.
  while (*pos < text.size()) {
    const char d = text[*pos];
    if (d == ',' || d == ']' || d == '}' || d == ' ' || d == '\t' ||
        d == '\n' || d == '\r') {
      break;
    }
    ++(*pos);
  }
  return true;
}

// Splits a JSON array into its elements.
//
// The elements are the raw slices of the array, so the caller can parse
// each of them on its own. Returns false when the text is not an array.
bool SplitJsonArray(std::string_view text, std::vector<std::string_view>* out) {
  std::size_t pos = 0;
  SkipWhitespace(text, &pos);
  if (pos >= text.size() || text[pos] != '[') {
    return false;
  }
  ++pos;
  SkipWhitespace(text, &pos);
  if (pos < text.size() && text[pos] == ']') {
    return true;
  }
  while (true) {
    SkipWhitespace(text, &pos);
    const std::size_t start = pos;
    if (!ScanValue(text, &pos)) {
      return false;
    }
    out->push_back(text.substr(start, pos - start));
    SkipWhitespace(text, &pos);
    if (pos >= text.size()) {
      return false;
    }
    if (text[pos] == ',') {
      ++pos;
      continue;
    }
    return text[pos] == ']';
  }
}

// How one entry ended up.
enum class EntryStatus {
  kOk,
  // The entry says where it applies but not what to apply.
  kMissingHash,
  // The entry does not describe a sample at all.
  kInvalid,
};

// Reads one entry of the table.
EntryStatus ParseTargetEntry(std::string_view text, FpTarget* out) {
  JsonObject entry;
  if (!JsonObject::Parse(text, &entry)) {
    return EntryStatus::kInvalid;
  }

  std::string dimension;
  if (!entry.GetString("dimension", &dimension)) {
    return EntryStatus::kInvalid;
  }
  if (dimension == "canvas") {
    out->dimension = FpDimension::kCanvas;
  } else if (dimension == "webgl") {
    out->dimension = FpDimension::kWebgl;
  } else {
    return EntryStatus::kInvalid;
  }

  std::int64_t width = 0;
  std::int64_t length = 0;
  if (!entry.GetInt64("width", &width) || !entry.GetInt64("length", &length)) {
    return EntryStatus::kInvalid;
  }
  if (width <= 0 || length <= 0 || width > kMaxSide || length > kMaxSide) {
    return EntryStatus::kInvalid;
  }
  out->width = static_cast<std::uint32_t>(width);
  out->length = static_cast<std::uint32_t>(length);
  // A canvas sample is a square of a fixed side length. A webgl sample
  // has no such rule, so its two sides may differ.
  if (out->dimension == FpDimension::kCanvas && out->width != out->length) {
    return EntryStatus::kInvalid;
  }

  if (entry.Has("url_regex") && !entry.GetString("url_regex", &out->url_regex)) {
    return EntryStatus::kInvalid;
  }
  if (entry.Has("url_glob") && !entry.GetString("url_glob", &out->url_glob)) {
    return EntryStatus::kInvalid;
  }

  bool drew_text = false;
  if (entry.GetBool("drew_text", &drew_text)) {
    out->has_drew_text = true;
    out->drew_text = drew_text;
  } else if (entry.Has("drew_text")) {
    return EntryStatus::kInvalid;
  }

  bool skip = false;
  if (entry.GetBool("skip", &skip)) {
    out->skip = skip;
  } else if (entry.Has("skip")) {
    return EntryStatus::kInvalid;
  }

  if (!entry.Has("target_hash")) {
    return EntryStatus::kMissingHash;
  }
  if (!entry.GetString("target_hash", &out->target_hash)) {
    return EntryStatus::kInvalid;
  }
  if (out->target_hash.empty()) {
    return EntryStatus::kMissingHash;
  }
  return EntryStatus::kOk;
}

}  // namespace

FpSamplingSpec SamplingSpecOf(const FpTarget& target) {
  FpSamplingSpec spec;
  spec.width = target.width;
  spec.length = target.length;
  if (target.has_drew_text) {
    spec.drew_text = target.drew_text;
  }
  return spec;
}

bool ParseFpTargetConfig(std::string_view bundle_json,
                         FpTargetTable* out,
                         std::string* error) {
  if (out == nullptr) {
    return false;
  }
  *out = FpTargetTable();

  JsonObject bundle;
  if (!JsonObject::Parse(bundle_json, &bundle)) {
    if (error != nullptr) {
      *error = "the configuration bundle is not a JSON object";
    }
    return false;
  }

  const JsonValue* section_value = bundle.Find(kFpTargetConfigKey);
  if (section_value == nullptr) {
    // No section: the platform asked for no target, and the caller keeps
    // the behaviour it had. That is not a malformed configuration.
    return true;
  }
  if (section_value->kind != JsonKind::kObject) {
    if (error != nullptr) {
      *error = "the fp_target section is not a JSON object";
    }
    return false;
  }

  JsonObject section;
  if (!JsonObject::Parse(section_value->text, &section)) {
    if (error != nullptr) {
      *error = "the fp_target section is malformed";
    }
    return false;
  }

  if (section.Has("similarity")) {
    std::int64_t similarity = 0;
    if (!section.GetInt64("similarity", &similarity) || similarity < 0 ||
        similarity > 0xFFFFFFFFLL) {
      if (error != nullptr) {
        *error = "the fp_target similarity is not a threshold";
      }
      return false;
    }
    out->similarity = static_cast<std::uint32_t>(similarity);
  }

  const JsonValue* targets_value = section.Find("targets");
  if (targets_value == nullptr) {
    // The section says the threshold but names no target. Nothing to
    // apply, and nothing wrong with it.
    return true;
  }
  if (targets_value->kind != JsonKind::kArray) {
    if (error != nullptr) {
      *error = "the fp_target targets are not a JSON array";
    }
    return false;
  }

  std::vector<std::string_view> elements;
  if (!SplitJsonArray(targets_value->text, &elements)) {
    if (error != nullptr) {
      *error = "the fp_target targets are malformed";
    }
    return false;
  }

  for (const std::string_view element : elements) {
    FpTarget target;
    const EntryStatus status = ParseTargetEntry(element, &target);
    if (status == EntryStatus::kOk) {
      out->targets.push_back(target);
      continue;
    }
    if (status == EntryStatus::kMissingHash) {
      ++out->skipped_missing_hash;
      continue;
    }
    ++out->skipped_invalid;
  }
  return true;
}

}  // namespace lengsuo::fp
