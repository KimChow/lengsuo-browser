#include "profile_bundle.h"

#include <algorithm>
#include <utility>

#include "kernel_pipe_json.h"

namespace lengsuo::fp {
namespace {

// The patterns no collection may ever contain.
//
// The brief's rule is that Cookies and Network Cache are excluded "under
// any circumstances". The exclude list in the configuration says the same
// thing, but the configuration is remote input: a platform that listed
// "*Cookies*" in the include list would otherwise get the cookies. So
// these two patterns are a floor, not a preference - the collection cannot
// contain them even if the configuration asks for them.
//
// This is the only place in this file that names a file. The white list
// stays in the configuration: what is collected is the platform's decision.
constexpr char kForbiddenCookies[] = "*Cookies*";
constexpr char kForbiddenNetwork[] = "*Network*";

// Compares two paths byte-wise, as unsigned bytes.
//
// std::string's own operator< would do the same on every platform we
// build for, but the two repos that agree on the manifest order have to
// agree for a reason that is written down rather than inherited. The
// comparison is therefore spelled out: unsigned byte by unsigned byte, and
// a prefix sorts before the longer path that starts with it.
int ComparePaths(const std::string& left, const std::string& right) {
  const std::size_t shared = std::min(left.size(), right.size());
  for (std::size_t i = 0; i < shared; ++i) {
    const auto a = static_cast<unsigned char>(left[i]);
    const auto b = static_cast<unsigned char>(right[i]);
    if (a != b) {
      return a < b ? -1 : 1;
    }
  }
  if (left.size() == right.size()) {
    return 0;
  }
  return left.size() < right.size() ? -1 : 1;
}

// Reads a JSON string literal, resolving the escapes.
//
// The reader in kernel_pipe_json.h keeps arrays as raw text, so the array
// of patterns has to be scanned here. The escape set is the one JSON
// defines; a \u escape is accepted for a BMP code point and a lone
// surrogate is refused, because a pattern that names a half a character
// cannot match a path.
bool ScanJsonString(std::string_view text, std::size_t* pos, std::string* out) {
  if (*pos >= text.size() || text[*pos] != '"') {
    return false;
  }
  ++(*pos);
  out->clear();
  while (*pos < text.size()) {
    const char c = text[*pos];
    if (c == '"') {
      ++(*pos);
      return true;
    }
    if (c == '\\') {
      ++(*pos);
      if (*pos >= text.size()) {
        return false;
      }
      const char escape = text[*pos];
      ++(*pos);
      switch (escape) {
        case '"': out->push_back('"'); break;
        case '\\': out->push_back('\\'); break;
        case '/': out->push_back('/'); break;
        case 'b': out->push_back('\b'); break;
        case 'f': out->push_back('\f'); break;
        case 'n': out->push_back('\n'); break;
        case 'r': out->push_back('\r'); break;
        case 't': out->push_back('\t'); break;
        case 'u': {
          if (*pos + 4 > text.size()) {
            return false;
          }
          std::uint32_t code = 0;
          for (int i = 0; i < 4; ++i) {
            const char digit = text[*pos + static_cast<std::size_t>(i)];
            std::uint32_t value = 0;
            if (digit >= '0' && digit <= '9') {
              value = static_cast<std::uint32_t>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
              value = static_cast<std::uint32_t>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
              value = static_cast<std::uint32_t>(digit - 'A' + 10);
            } else {
              return false;
            }
            code = (code << 4) | value;
          }
          *pos += 4;
          if (code >= 0xD800 && code <= 0xDFFF) {
            // A surrogate on its own is not a character.
            return false;
          }
          if (code <= 0x7F) {
            out->push_back(static_cast<char>(code));
          } else if (code <= 0x7FF) {
            out->push_back(static_cast<char>(0xC0 | (code >> 6)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            out->push_back(static_cast<char>(0xE0 | (code >> 12)));
            out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          break;
        }
        default:
          return false;
      }
      continue;
    }
    if (static_cast<unsigned char>(c) < 0x20) {
      return false;
    }
    out->push_back(c);
    ++(*pos);
  }
  return false;
}

// Reads a JSON array of strings. The array text is the raw text the
// reader kept, so it starts with '[' and ends with ']'.
bool ParseStringArray(std::string_view text, std::vector<std::string>* out) {
  std::size_t pos = 0;
  auto skip_space = [&text, &pos]() {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
                                 text[pos] == '\n' || text[pos] == '\r')) {
      ++pos;
    }
  };
  skip_space();
  if (pos >= text.size() || text[pos] != '[') {
    return false;
  }
  ++pos;
  skip_space();
  if (pos < text.size() && text[pos] == ']') {
    ++pos;
    skip_space();
    return pos == text.size();
  }
  while (true) {
    skip_space();
    std::string item;
    if (!ScanJsonString(text, &pos, &item)) {
      return false;
    }
    out->push_back(std::move(item));
    skip_space();
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
  skip_space();
  return pos == text.size();
}

// True when any of `patterns` matches `text`.
bool MatchesAny(const std::vector<std::string>& patterns,
                 std::string_view text) {
  for (const std::string& pattern : patterns) {
    if (MatchGlob(pattern, text)) {
      return true;
    }
  }
  return false;
}

}  // namespace

ProfileSyncConfig DefaultProfileSyncConfig() {
  ProfileSyncConfig config;
  // The white list. History is in it because the ruling is that the
  // history is uploaded by default; its scope is still the pattern's, so
  // a path that does not look like a history file is not collected.
  config.include = {"*Local Storage*", "*Extension State*", "*History*"};
  // The black list. The exclude always wins.
  config.exclude = {"*Cache*", "*Cookies*", "*Network*"};
  config.max_package_bytes = kProfileBundleMaxBytes;
  return config;
}

bool MatchGlob(std::string_view pattern, std::string_view text) {
  std::size_t p = 0;
  std::size_t t = 0;
  std::size_t star = std::string_view::npos;
  std::size_t star_end = 0;
  while (t < text.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
      ++p;
      ++t;
    } else if (p < pattern.size() && pattern[p] == '*') {
      // Remember where the star was and try the shortest match first.
      star = p++;
      star_end = t;
    } else if (star != std::string_view::npos) {
      // Let the star swallow one more byte and try again.
      p = star + 1;
      t = ++star_end;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') {
    ++p;
  }
  return p == pattern.size();
}

bool ParseProfileSyncConfig(std::string_view bundle_json,
                            ProfileSyncConfig* out,
                            std::string* error) {
  auto fail = [error](const char* message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  if (out == nullptr) {
    return fail("profile_sync: no output");
  }
  JsonObject bundle;
  if (!JsonObject::Parse(bundle_json, &bundle)) {
    return fail("profile_sync: malformed bundle");
  }
  const JsonValue* section = bundle.Find(kProfileSyncConfigKey);
  if (section == nullptr) {
    // No section means the platform did not ask for a profile upload.
    return fail("profile_sync: the bundle carries no profile_sync section");
  }
  if (section->kind != JsonKind::kObject) {
    return fail("profile_sync: the section is not an object");
  }
  // The reader keeps a nested object as raw text, so it is parsed again
  // with the same reader.
  JsonObject rules;
  if (!JsonObject::Parse(section->text, &rules)) {
    return fail("profile_sync: malformed section");
  }

  ProfileSyncConfig config;
  const JsonValue* include = rules.Find("include");
  if (include == nullptr || include->kind != JsonKind::kArray) {
    return fail("profile_sync: the section has no include list");
  }
  if (!ParseStringArray(include->text, &config.include)) {
    return fail("profile_sync: the include list is malformed");
  }
  const JsonValue* exclude = rules.Find("exclude");
  if (exclude != nullptr) {
    if (exclude->kind != JsonKind::kArray) {
      return fail("profile_sync: the exclude list is not an array");
    }
    if (!ParseStringArray(exclude->text, &config.exclude)) {
      return fail("profile_sync: the exclude list is malformed");
    }
  }
  const JsonValue* ceiling = rules.Find("max_package_bytes");
  if (ceiling != nullptr) {
    std::int64_t value = 0;
    if (!rules.GetInt64("max_package_bytes", &value) || value < 0) {
      return fail("profile_sync: the package ceiling is malformed");
    }
    if (value > 0) {
      // The configuration may lower the ceiling; it may never raise it.
      config.max_package_bytes = std::min<std::uint64_t>(
          static_cast<std::uint64_t>(value), kProfileBundleMaxBytes);
    }
  }
  if (config.max_package_bytes == 0) {
    config.max_package_bytes = kProfileBundleMaxBytes;
  }
  *out = std::move(config);
  return true;
}

void CollectProfileBundle(const ProfileSyncConfig& config,
                          const std::vector<ProfileFile>& files,
                          ProfileBundle* out) {
  if (out == nullptr) {
    return;
  }
  *out = ProfileBundle();

  // The order is decided before anything is collected: the manifest is
  // sorted by path, so two repos that hold the same profile produce the
  // same list without exchanging anything.
  std::vector<ProfileFile> ordered = files;
  std::sort(ordered.begin(), ordered.end(),
            [](const ProfileFile& left, const ProfileFile& right) {
              return ComparePaths(left.relative_path, right.relative_path) < 0;
            });

  const std::uint64_t ceiling =
      config.max_package_bytes == 0 ? kProfileBundleMaxBytes
                                    : config.max_package_bytes;

  for (const ProfileFile& file : ordered) {
    if (!file.readable) {
      // A file that cannot be read is skipped and counted. It never fails
      // the collection: one locked file must not stop a container from
      // closing.
      ++out->skipped_unreadable;
      continue;
    }
    if (MatchesAny({kForbiddenCookies, kForbiddenNetwork},
                   file.relative_path)) {
      // The floor. These never go out, whatever the configuration says.
      ++out->excluded;
      continue;
    }
    if (!MatchesAny(config.include, file.relative_path)) {
      ++out->excluded;
      continue;
    }
    if (MatchesAny(config.exclude, file.relative_path)) {
      // The exclude wins over the include.
      ++out->excluded;
      continue;
    }
    if (out->size_capped) {
      // The ceiling was already reached: this file and the ones after it
      // are cut off.
      ++out->truncated;
      continue;
    }
    // The subtraction cannot underflow: total_bytes never exceeds the
    // ceiling, and the addition that would overflow is never done.
    if (file.size > ceiling - out->total_bytes) {
      out->size_capped = true;
      ++out->truncated;
      continue;
    }
    out->entries.push_back(ProfileBundleEntry{file.relative_path, file.size});
    out->total_bytes += file.size;
  }
}

}  // namespace lengsuo::fp
