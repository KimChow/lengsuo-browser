#include "resp_monitor_config.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "kernel_pipe_json.h"

namespace lengsuo::fp {
namespace {

// Skips the whitespace JSON allows between tokens.
void SkipSpace(std::string_view text, std::size_t* pos) {
  while (*pos < text.size()) {
    const char c = text[*pos];
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
      return;
    }
    ++(*pos);
  }
}

// Finds the end of the object that starts at `pos`, which must point at an
// opening brace. Returns the position just past the closing brace, or
// `std::string_view::npos` when the object is never closed.
//
// The scanner tracks strings so that a brace inside a string value does not
// close the object.
std::size_t FindObjectEnd(std::string_view text, std::size_t pos) {
  if (pos >= text.size() || text[pos] != '{') {
    return std::string_view::npos;
  }
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t i = pos; i < text.size(); ++i) {
    const char c = text[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
    } else if (c == '{') {
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0) {
        return i + 1;
      }
    }
  }
  return std::string_view::npos;
}

// Finds the end of the array that starts at `pos`, which must point at an
// opening bracket.
std::size_t FindArrayEnd(std::string_view text, std::size_t pos) {
  if (pos >= text.size() || text[pos] != '[') {
    return std::string_view::npos;
  }
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t i = pos; i < text.size(); ++i) {
    const char c = text[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
    } else if (c == '[') {
      ++depth;
    } else if (c == ']') {
      --depth;
      if (depth == 0) {
        return i + 1;
      }
    }
  }
  return std::string_view::npos;
}

// Finds the end of the JSON value that starts at `pos`. Returns the position
// just past the value, or `std::string_view::npos` when the value is
// malformed.
std::size_t FindValueEnd(std::string_view text, std::size_t pos) {
  if (pos >= text.size()) {
    return std::string_view::npos;
  }
  const char c = text[pos];
  if (c == '"') {
    bool escaped = false;
    for (std::size_t i = pos + 1; i < text.size(); ++i) {
      const char s = text[i];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (s == '\\') {
        escaped = true;
        continue;
      }
      if (s == '"') {
        return i + 1;
      }
    }
    return std::string_view::npos;
  }
  if (c == '{') {
    return FindObjectEnd(text, pos);
  }
  if (c == '[') {
    return FindArrayEnd(text, pos);
  }
  // A number, a boolean or null: it runs to the next delimiter.
  std::size_t i = pos;
  while (i < text.size() && text[i] != ',' && text[i] != ']' && text[i] != '}' &&
         text[i] != ' ' && text[i] != '\t' && text[i] != '\n' &&
         text[i] != '\r') {
    ++i;
  }
  if (i == pos) {
    return std::string_view::npos;
  }
  return i;
}

// Splits the raw text of a JSON array into its elements.
//
// The reader in kernel_pipe_json.h keeps an array as raw text, so the array
// is walked here and each element's raw text is handed back. The elements
// are not parsed here: the caller parses each one with the same reader and
// counts the ones that do not parse. That is what keeps one broken rule
// from taking the whole configuration down.
//
// Returns false when the array itself is malformed.
bool SplitArrayElements(std::string_view text,
                        std::vector<std::string>* elements) {
  std::size_t pos = 0;
  SkipSpace(text, &pos);
  if (pos >= text.size() || text[pos] != '[') {
    return false;
  }
  ++pos;
  SkipSpace(text, &pos);
  if (pos < text.size() && text[pos] == ']') {
    ++pos;
    SkipSpace(text, &pos);
    return pos == text.size();
  }
  while (true) {
    const std::size_t start = pos;
    const std::size_t end = FindValueEnd(text, pos);
    if (end == std::string_view::npos) {
      return false;
    }
    elements->push_back(std::string(text.substr(start, end - start)));
    pos = end;
    SkipSpace(text, &pos);
    if (pos >= text.size()) {
      return false;
    }
    if (text[pos] == ',') {
      ++pos;
      SkipSpace(text, &pos);
      continue;
    }
    if (text[pos] == ']') {
      ++pos;
      break;
    }
    return false;
  }
  SkipSpace(text, &pos);
  return pos == text.size();
}

// Reads one rule out of its JSON object.
//
// Returns false when the rule is not usable. `*skipped` is set when the rule
// has to be counted as skipped, and `*disabled` when it merely said it was
// switched off - the two are counted apart because one is a mistake and the
// other is a decision.
bool ReadRule(const JsonObject& object,
              bool section_include_body,
              RespMonitorRule* out,
              bool* skipped,
              bool* disabled) {
  *skipped = false;
  *disabled = false;

  std::string id;
  if (!object.GetString("id", &id) || id.empty()) {
    *skipped = true;
    return false;
  }
  std::string url_regex;
  if (!object.GetString("url_regex", &url_regex) || url_regex.empty()) {
    *skipped = true;
    return false;
  }
  std::shared_ptr<const std::regex> matcher;
  try {
    matcher = std::make_shared<const std::regex>(url_regex,
                                                 kRespMonitorRegexGrammar);
  } catch (const std::regex_error&) {
    // The pattern does not compile. The rule is dropped and counted; the
    // rest of the configuration is untouched.
    *skipped = true;
    return false;
  }

  bool enabled = true;
  if (object.Has("enabled") && !object.GetBool("enabled", &enabled)) {
    *skipped = true;
    return false;
  }
  if (!enabled) {
    *disabled = true;
    return false;
  }

  RespMonitorRule rule;
  rule.id = std::move(id);
  rule.url_regex = std::move(url_regex);
  rule.matcher = std::move(matcher);

  if (object.Has("content_type")) {
    std::string content_type;
    if (!object.GetString("content_type", &content_type)) {
      *skipped = true;
      return false;
    }
    rule.content_type = NormalizeRespMonitorContentType(content_type);
  }

  if (object.Has("max_count")) {
    std::int64_t max_count = 0;
    if (!object.GetInt64("max_count", &max_count) || max_count < 1) {
      // A ceiling of zero would mean the rule never fires, which is a
      // contradiction rather than a setting; a negative one is nonsense.
      *skipped = true;
      return false;
    }
    rule.max_count = static_cast<std::uint64_t>(max_count);
  }

  if (object.Has("max_body_bytes")) {
    std::int64_t max_body_bytes = 0;
    if (!object.GetInt64("max_body_bytes", &max_body_bytes) ||
        max_body_bytes < 0) {
      *skipped = true;
      return false;
    }
    if (max_body_bytes > 0) {
      rule.max_body_bytes = std::min<std::size_t>(
          static_cast<std::size_t>(max_body_bytes),
          kRespMonitorMaxBodyBytesCeiling);
    }
  }

  bool include_body = section_include_body;
  if (object.Has("include_body") &&
      !object.GetBool("include_body", &include_body)) {
    *skipped = true;
    return false;
  }
  rule.include_body = include_body;

  *out = std::move(rule);
  return true;
}

}  // namespace

std::string NormalizeRespMonitorContentType(std::string_view value) {
  const std::size_t semicolon = value.find(';');
  const std::string_view media =
      semicolon == std::string_view::npos ? value : value.substr(0, semicolon);
  std::size_t begin = 0;
  std::size_t end = media.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(media[begin]))) {
    ++begin;
  }
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(media[end - 1]))) {
    --end;
  }
  std::string out;
  out.reserve(end - begin);
  for (std::size_t i = begin; i < end; ++i) {
    out.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(media[i]))));
  }
  return out;
}

bool IsValidRespMonitorUrlRegex(std::string_view pattern) {
  try {
    const std::regex probe{std::string(pattern), kRespMonitorRegexGrammar};
    (void)probe;
  } catch (const std::regex_error&) {
    return false;
  }
  return true;
}

bool FindRespMonitorSection(std::string_view bundle_json,
                            std::string* section_json,
                            std::string* error) {
  auto fail = [error](const char* message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (section_json == nullptr) {
    return fail("response_monitor: no output");
  }
  section_json->clear();
  JsonObject bundle;
  if (!JsonObject::Parse(bundle_json, &bundle)) {
    return fail("response_monitor: malformed bundle");
  }
  const JsonValue* section = bundle.Find(kRespMonitorConfigKey);
  if (section == nullptr) {
    // No section means the platform did not ask for a monitor. That is the
    // ordinary case, not a failure.
    return true;
  }
  if (section->kind != JsonKind::kObject) {
    return fail("response_monitor: the section is not an object");
  }
  *section_json = section->text;
  return true;
}

bool ParseRespMonitorConfig(std::string_view section_json,
                            RespMonitorConfig* out,
                            std::string* error) {
  auto fail = [error](const char* message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (out == nullptr) {
    return fail("response_monitor: no output");
  }
  JsonObject section;
  if (!JsonObject::Parse(section_json, &section)) {
    return fail("response_monitor: malformed section");
  }

  RespMonitorConfig config;
  // The whole switch defaults to off: a section that does not say it is on
  // is a section that is not on.
  if (section.Has("enabled") && !section.GetBool("enabled", &config.enabled)) {
    return fail("response_monitor: enabled is not a boolean");
  }
  if (section.Has("include_body") &&
      !section.GetBool("include_body", &config.include_body)) {
    return fail("response_monitor: include_body is not a boolean");
  }

  const JsonValue* rules = section.Find("rules");
  if (rules == nullptr) {
    // Enabled with no rules: nothing to register.
    *out = std::move(config);
    return true;
  }
  if (rules->kind != JsonKind::kArray) {
    return fail("response_monitor: rules is not an array");
  }
  std::vector<std::string> elements;
  if (!SplitArrayElements(rules->text, &elements)) {
    return fail("response_monitor: the rule array is malformed");
  }

  for (const std::string& element : elements) {
    JsonObject object;
    if (!JsonObject::Parse(element, &object)) {
      // A rule that is not even an object is dropped and counted.
      ++config.skipped_rules;
      continue;
    }
    RespMonitorRule rule;
    bool skipped = false;
    bool disabled = false;
    if (!ReadRule(object, config.include_body, &rule, &skipped, &disabled)) {
      if (skipped) {
        ++config.skipped_rules;
      }
      if (disabled) {
        ++config.disabled_rules;
      }
      continue;
    }
    config.rules.push_back(std::move(rule));
  }

  *out = std::move(config);
  return true;
}

}  // namespace lengsuo::fp
