#include "policy_rules.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "kernel_pipe_json.h"
#include "policy_match.h"
#include "ptr_exclusion.h"

namespace lengsuo::fp {
namespace {

// --- reading a list of strings out of the raw array text ------------------
//
// The JSON reader keeps an array as raw text and offers no accessor for its
// elements, so the elements are scanned here and handed back to the reader
// one at a time: a raw literal is valid JSON on its own, so wrapping it in
// a one-member object and parsing that resolves the escapes with the
// decoder that is already tested. Writing a second escape decoder here
// would mean two decoders to keep in step.

bool IsJsonSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void SkipJsonSpace(std::string_view text, std::size_t* pos) {
  while (*pos < text.size() && IsJsonSpace(text[*pos])) {
    ++(*pos);
  }
}

// Copies the raw literal (quotes included) that starts at `*pos` into `*raw`
// and leaves `*pos` just past it.
bool ScanRawLiteral(std::string_view text, std::size_t* pos, std::string* raw) {
  if (*pos >= text.size() || text[*pos] != '"') {
    return false;
  }
  const std::size_t start = *pos;
  ++(*pos);
  while (*pos < text.size()) {
    const char c = text[*pos];
    if (c == '\\') {
      // The escaped byte cannot be the closing quote, whatever it is.
      *pos += 2;
      continue;
    }
    ++(*pos);
    if (c == '"') {
      raw->assign(text.substr(start, *pos - start));
      return true;
    }
  }
  return false;
}

// Resolves the escapes of one raw literal.
bool DecodeLiteral(const std::string& raw, std::string* out) {
  const std::string wrapped = "{\"v\":" + raw + "}";
  JsonObject object;
  if (!JsonObject::Parse(wrapped, &object)) {
    return false;
  }
  return object.GetString("v", out);
}

// Reads a JSON array of strings. The text is the raw text the reader kept,
// so it starts with '[' and ends with ']'.
bool ParseStringArray(const std::string& text, std::vector<std::string>* out) {
  std::size_t pos = 0;
  SkipJsonSpace(text, &pos);
  if (pos >= text.size() || text[pos] != '[') {
    return false;
  }
  ++pos;
  SkipJsonSpace(text, &pos);
  if (pos < text.size() && text[pos] == ']') {
    ++pos;
    SkipJsonSpace(text, &pos);
    return pos == text.size();
  }
  while (true) {
    SkipJsonSpace(text, &pos);
    std::string raw;
    if (!ScanRawLiteral(text, &pos, &raw)) {
      return false;
    }
    std::string item;
    if (!DecodeLiteral(raw, &item)) {
      return false;
    }
    out->push_back(std::move(item));
    SkipJsonSpace(text, &pos);
    if (pos >= text.size()) {
      return false;
    }
    if (text[pos] == ',') {
      ++pos;
      continue;
    }
    if (text[pos] == ']') {
      ++pos;
      break;
    }
    return false;
  }
  SkipJsonSpace(text, &pos);
  return pos == text.size();
}

// --- reading one list out of the section ----------------------------------

// Reads a list of URL patterns.
//
// A list that is not there is an empty list. A list that cannot be read is
// dropped and counted: one unreadable list must not take the whole policy
// with it. A pattern the matcher cannot evaluate is dropped and counted for
// the same reason - a rule that cannot fire must not fail the request that
// trips over it.
void ReadUrlList(const JsonObject& section,
                 const char* key,
                 std::uint64_t* skipped,
                 std::vector<std::string>* out) {
  const JsonValue* value = section.Find(key);
  if (value == nullptr) {
    return;
  }
  if (value->kind != JsonKind::kArray) {
    ++*skipped;
    return;
  }
  std::vector<std::string> items;
  if (!ParseStringArray(value->text, &items)) {
    ++*skipped;
    return;
  }
  for (std::string& item : items) {
    if (!IsValidUrlPattern(item)) {
      ++*skipped;
      continue;
    }
    out->push_back(std::move(item));
  }
}

// Reads a list of host patterns.
//
// The host patterns are globs, and every byte sequence is a valid glob, so
// there is nothing to validate here: a list that cannot be read is dropped
// and counted and nothing else.
void ReadGlobList(const JsonObject& section,
                  const char* key,
                  std::uint64_t* skipped,
                  std::vector<std::string>* out) {
  const JsonValue* value = section.Find(key);
  if (value == nullptr) {
    return;
  }
  if (value->kind != JsonKind::kArray) {
    ++*skipped;
    return;
  }
  std::vector<std::string> items;
  if (!ParseStringArray(value->text, &items)) {
    ++*skipped;
    return;
  }
  for (std::string& item : items) {
    out->push_back(std::move(item));
  }
}

// Reads the black_schemes list. The names are canonicalised here so that
// the comparison later is a plain string comparison: a scheme is
// case-insensitive and is written with or without its colon.
void ReadSchemeList(const JsonObject& section,
                    std::uint64_t* skipped,
                    std::vector<std::string>* out) {
  const JsonValue* value = section.Find("black_schemes");
  if (value == nullptr) {
    return;
  }
  if (value->kind != JsonKind::kArray) {
    ++*skipped;
    return;
  }
  std::vector<std::string> items;
  if (!ParseStringArray(value->text, &items)) {
    ++*skipped;
    return;
  }
  for (std::string& item : items) {
    std::string scheme = item;
    if (!scheme.empty() && scheme.back() == ':') {
      scheme.pop_back();
    }
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    if (scheme.empty()) {
      ++*skipped;
      continue;
    }
    out->push_back(std::move(scheme));
  }
}

// Reads the frame_black_white_list section: a black and a white list per
// frame kind.
void ReadFrameRules(const JsonObject& section,
                    std::uint64_t* skipped,
                    FramePolicyLists* main_frame,
                    FramePolicyLists* sub_frame) {
  const JsonValue* value = section.Find("frame_black_white_list");
  if (value == nullptr) {
    return;
  }
  if (value->kind != JsonKind::kObject) {
    ++*skipped;
    return;
  }
  JsonObject frames;
  if (!JsonObject::Parse(value->text, &frames)) {
    ++*skipped;
    return;
  }
  struct Entry {
    const char* key;
    // A view into the caller's parsed lists, not an owning field.
    LENGSUO_RAW_PTR_EXCLUSION FramePolicyLists* lists;
  };
  const Entry entries[] = {{"main_frame", main_frame}, {"sub_frame", sub_frame}};
  for (const Entry& entry : entries) {
    const JsonValue* frame = frames.Find(entry.key);
    if (frame == nullptr) {
      continue;
    }
    if (frame->kind != JsonKind::kObject) {
      ++*skipped;
      continue;
    }
    JsonObject lists;
    if (!JsonObject::Parse(frame->text, &lists)) {
      ++*skipped;
      continue;
    }
    ReadUrlList(lists, "black", skipped, &entry.lists->black);
    ReadUrlList(lists, "white", skipped, &entry.lists->white);
  }
}

}  // namespace

bool ParseAccessPolicy(std::string_view bundle_json,
                        AccessPolicy* out,
                        std::string* error) {
  auto fail = [error](const char* message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (out == nullptr) {
    return fail("access_policy: no output");
  }
  // An unreadable bundle is the only failure that reaches the caller. A
  // bundle without the section is an empty policy, and an empty policy
  // refuses nothing: the platform did not ask for one.
  JsonObject bundle;
  if (!JsonObject::Parse(bundle_json, &bundle)) {
    *out = AccessPolicy();
    return fail("access_policy: malformed bundle");
  }
  const JsonValue* section = bundle.Find(kAccessPolicyConfigKey);
  if (section == nullptr) {
    *out = AccessPolicy();
    return true;
  }
  if (section->kind != JsonKind::kObject) {
    *out = AccessPolicy();
    return fail("access_policy: the section is not an object");
  }
  // The reader keeps a nested object as raw text, so it is parsed again
  // with the same reader.
  JsonObject rules;
  if (!JsonObject::Parse(section->text, &rules)) {
    *out = AccessPolicy();
    return fail("access_policy: malformed section");
  }

  AccessPolicy policy;
  ReadUrlList(rules, "black_list", &policy.skipped_rules, &policy.black_list);
  ReadUrlList(rules, "white_list", &policy.skipped_rules, &policy.white_list);
  ReadUrlList(rules, "exception_list", &policy.skipped_rules,
              &policy.exception_list);
  ReadSchemeList(rules, &policy.skipped_rules, &policy.black_schemes);
  ReadUrlList(rules, "main_frame_list", &policy.skipped_rules,
              &policy.main_frame_list);
  ReadUrlList(rules, "sub_frame_black_list", &policy.skipped_rules,
              &policy.sub_frame_black_list);
  ReadFrameRules(rules, &policy.skipped_rules, &policy.main_frame_rules,
                 &policy.sub_frame_rules);
  ReadGlobList(rules, "domain_black_list", &policy.skipped_rules,
               &policy.domain_black_list);
  ReadGlobList(rules, "domain_check_list", &policy.skipped_rules,
               &policy.domain_check_list);
  *out = std::move(policy);
  return true;
}

}  // namespace lengsuo::fp
