// kernel_pipe_json - the JSON reader of the runner <-> kernel pipe (task Q).
//
// Every payload on this pipe is JSON text (contract section 5) except the
// KeyEnvelope, which is binary. The reader below is deliberately shallow: it
// understands the top level of an object, which is all the action schemas
// and the config bundle need. Nested values are captured as raw text.
//
// The reader is a separate layer from the crypto on purpose: the envelope
// tests must not have to pull in BoringSSL to check that a payload parses,
// and the JSON tests must not have to know anything about keys.
#ifndef LENGSUO_NATIVE_KERNEL_PIPE_JSON_H_
#define LENGSUO_NATIVE_KERNEL_PIPE_JSON_H_

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lengsuo::fp {

enum class JsonKind {
  kString,
  kNumber,
  kBoolean,
  kNull,
  kObject,
  kArray,
};

// One member of a JSON object.
struct JsonValue {
  JsonKind kind = JsonKind::kNull;
  // For a string: the decoded value (escapes resolved). For a number: the
  // raw token. For a boolean: "true" or "false". For an object or an
  // array: the raw slice, including the brackets.
  std::string text;
};

// A shallow view over the members of a JSON object.
//
// The special members are declared here and defined in the .cc: the class
// owns a vector, so the constructor and destructor are not trivial and
// Chromium's style checker wants them out of line.
class JsonObject {
 public:
  JsonObject();
  ~JsonObject();
  JsonObject(const JsonObject& other);
  JsonObject& operator=(const JsonObject& other);
  JsonObject(JsonObject&& other) noexcept;
  JsonObject& operator=(JsonObject&& other) noexcept;

  // Parses a top-level JSON object. Returns false on malformed input; the
  // duplicate-key case is rejected too, because two members with the same
  // name make the payload's meaning depend on the reader.
  static bool Parse(std::string_view text, JsonObject* out);

  // The accessors are defined here rather than in the .cc: they are one-line
  // lookups over the member list, and keeping them in the header is what
  // lets the reader's implementation stay small enough to read. Only the
  // special members have to live in the .cc (see the class comment).
  bool Has(std::string_view key) const {
    return Find(key) != nullptr;
  }

  const JsonValue* Find(std::string_view key) const {
    for (const auto& member : members_) {
      if (member.first == key) {
        return &member.second;
      }
    }
    return nullptr;
  }

  // Typed accessors. Each returns false when the key is absent or holds a
  // different kind.
  bool GetString(std::string_view key, std::string* out) const {
    const JsonValue* value = Find(key);
    if (value == nullptr || value->kind != JsonKind::kString) {
      return false;
    }
    if (out != nullptr) {
      *out = value->text;
    }
    return true;
  }

  bool GetBool(std::string_view key, bool* out) const {
    const JsonValue* value = Find(key);
    if (value == nullptr || value->kind != JsonKind::kBoolean) {
      return false;
    }
    if (out != nullptr) {
      *out = (value->text == "true");
    }
    return true;
  }

  bool GetInt64(std::string_view key, std::int64_t* out) const {
    const JsonValue* value = Find(key);
    if (value == nullptr || value->kind != JsonKind::kNumber) {
      return false;
    }
    const std::string& token = value->text;
    if (token.empty()) {
      return false;
    }
    std::size_t index = 0;
    bool negative = false;
    if (token[0] == '-') {
      negative = true;
      index = 1;
    }
    if (index >= token.size()) {
      return false;
    }
    std::uint64_t magnitude = 0;
    for (; index < token.size(); ++index) {
      const char c = token[index];
      if (c < '0' || c > '9') {
        // A fractional or exponent form is not an integer.
        return false;
      }
      const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
      if (magnitude > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
        return false;
      }
      magnitude = magnitude * 10 + digit;
    }
    if (negative) {
      if (magnitude > static_cast<std::uint64_t>(
                          std::numeric_limits<std::int64_t>::max()) + 1) {
        return false;
      }
      if (out != nullptr) {
        *out = (magnitude == static_cast<std::uint64_t>(
                             std::numeric_limits<std::int64_t>::max()) + 1)
                   ? std::numeric_limits<std::int64_t>::min()
                   : -static_cast<std::int64_t>(magnitude);
      }
      return true;
    }
    if (magnitude > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
      return false;
    }
    if (out != nullptr) {
      *out = static_cast<std::int64_t>(magnitude);
    }
    return true;
  }

 private:
  std::vector<std::pair<std::string, JsonValue>> members_;
};

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_KERNEL_PIPE_JSON_H_
