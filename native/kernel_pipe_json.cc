#include "kernel_pipe_json.h"

#include <limits>
#include <utility>

namespace lengsuo::fp {
namespace {

// --- JSON scanning ------------------------------------------------------
//
// A shallow scanner: enough to read the top level of an object. Nested
// values are skipped and kept as raw text; the action schemas and the
// config bundle never look inside them.

bool IsJsonSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void SkipJsonSpace(std::string_view text, std::size_t* pos) {
  while (*pos < text.size() && IsJsonSpace(text[*pos])) {
    ++(*pos);
  }
}

void AppendUtf8(std::string* out, std::uint32_t code_point) {
  if (code_point <= 0x7F) {
    out->push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7FF) {
    out->push_back(static_cast<char>(0xC0 | (code_point >> 6)));
    out->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else if (code_point <= 0xFFFF) {
    out->push_back(static_cast<char>(0xE0 | (code_point >> 12)));
    out->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (code_point >> 18)));
    out->push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  }
}

bool HexDigit(char c, std::uint8_t* out) {
  if (c >= '0' && c <= '9') {
    *out = static_cast<std::uint8_t>(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    *out = static_cast<std::uint8_t>(c - 'a' + 10);
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    *out = static_cast<std::uint8_t>(c - 'A' + 10);
    return true;
  }
  return false;
}

bool ScanHex4(std::string_view text, std::size_t* pos, std::uint32_t* out) {
  if (*pos + 4 > text.size()) {
    return false;
  }
  std::uint32_t value = 0;
  for (int i = 0; i < 4; ++i) {
    std::uint8_t digit = 0;
    if (!HexDigit(text[*pos + static_cast<std::size_t>(i)], &digit)) {
      return false;
    }
    value = (value << 4) | digit;
  }
  *pos += 4;
  *out = value;
  return true;
}

// Reads a JSON string literal, resolving the escapes. `pos` must point at the
// opening quote. On success `*pos` sits just past the closing quote.
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
          std::uint32_t code = 0;
          if (!ScanHex4(text, pos, &code)) {
            return false;
          }
          // A surrogate pair is two \u escapes; anything else is written as
          // the code point it names.
          if (code >= 0xD800 && code <= 0xDBFF && *pos + 1 < text.size() &&
              text[*pos] == '\\' && text[*pos + 1] == 'u') {
            const std::size_t saved = *pos;
            *pos += 2;
            std::uint32_t low = 0;
            if (ScanHex4(text, pos, &low) && low >= 0xDC00 && low <= 0xDFFF) {
              code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            } else {
              *pos = saved;
            }
          }
          AppendUtf8(out, code);
          break;
        }
        default:
          return false;
      }
      continue;
    }
    if (static_cast<unsigned char>(c) < 0x20) {
      // A raw control character is not legal inside a JSON string.
      return false;
    }
    out->push_back(c);
    ++(*pos);
  }
  return false;
}

bool IsJsonNumberStart(char c) {
  return c == '-' || (c >= '0' && c <= '9');
}

bool ScanJsonNumber(std::string_view text, std::size_t* pos, std::string* out) {
  const std::size_t start = *pos;
  if (*pos < text.size() && text[*pos] == '-') {
    ++(*pos);
  }
  while (*pos < text.size() && text[*pos] >= '0' && text[*pos] <= '9') {
    ++(*pos);
  }
  if (*pos < text.size() && text[*pos] == '.') {
    ++(*pos);
    while (*pos < text.size() && text[*pos] >= '0' && text[*pos] <= '9') {
      ++(*pos);
    }
  }
  if (*pos < text.size() && (text[*pos] == 'e' || text[*pos] == 'E')) {
    ++(*pos);
    if (*pos < text.size() && (text[*pos] == '+' || text[*pos] == '-')) {
      ++(*pos);
    }
    while (*pos < text.size() && text[*pos] >= '0' && text[*pos] <= '9') {
      ++(*pos);
    }
  }
  if (*pos == start) {
    return false;
  }
  *out = std::string(text.substr(start, *pos - start));
  return true;
}

// Skips one JSON value of any kind, leaving `*pos` just past it.
bool SkipJsonValue(std::string_view text, std::size_t* pos) {
  SkipJsonSpace(text, pos);
  if (*pos >= text.size()) {
    return false;
  }
  const char c = text[*pos];
  if (c == '"') {
    std::string ignored;
    return ScanJsonString(text, pos, &ignored);
  }
  if (c == '{' || c == '[') {
    const char closing = (c == '{') ? '}' : ']';
    ++(*pos);
    while (true) {
      SkipJsonSpace(text, pos);
      if (*pos >= text.size()) {
        return false;
      }
      if (text[*pos] == closing) {
        ++(*pos);
        return true;
      }
      if (c == '{') {
        std::string ignored;
        if (!ScanJsonString(text, pos, &ignored)) {
          return false;
        }
        SkipJsonSpace(text, pos);
        if (*pos >= text.size() || text[*pos] != ':') {
          return false;
        }
        ++(*pos);
      }
      if (!SkipJsonValue(text, pos)) {
        return false;
      }
      SkipJsonSpace(text, pos);
      if (*pos < text.size() && text[*pos] == ',') {
        ++(*pos);
        continue;
      }
      SkipJsonSpace(text, pos);
      if (*pos < text.size() && text[*pos] == closing) {
        ++(*pos);
        return true;
      }
      return false;
    }
  }
  if (c == 't') {
    if (text.compare(*pos, 4, "true") != 0) {
      return false;
    }
    *pos += 4;
    return true;
  }
  if (c == 'f') {
    if (text.compare(*pos, 5, "false") != 0) {
      return false;
    }
    *pos += 5;
    return true;
  }
  if (c == 'n') {
    if (text.compare(*pos, 4, "null") != 0) {
      return false;
    }
    *pos += 4;
    return true;
  }
  if (IsJsonNumberStart(c)) {
    std::string ignored;
    return ScanJsonNumber(text, pos, &ignored);
  }
  return false;
}

// Reads one member value into `*value`. Objects and arrays are kept as raw
// text; scalars are decoded.
bool ScanJsonMemberValue(std::string_view text,
                         std::size_t* pos,
                         JsonValue* value) {
  SkipJsonSpace(text, pos);
  if (*pos >= text.size()) {
    return false;
  }
  const char c = text[*pos];
  if (c == '"') {
    value->kind = JsonKind::kString;
    return ScanJsonString(text, pos, &value->text);
  }
  if (c == '{' || c == '[') {
    const std::size_t start = *pos;
    if (!SkipJsonValue(text, pos)) {
      return false;
    }
    value->kind = (c == '{') ? JsonKind::kObject : JsonKind::kArray;
    value->text = std::string(text.substr(start, *pos - start));
    return true;
  }
  if (c == 't' || c == 'f') {
    const std::size_t start = *pos;
    if (!SkipJsonValue(text, pos)) {
      return false;
    }
    value->kind = JsonKind::kBoolean;
    value->text = std::string(text.substr(start, *pos - start));
    return true;
  }
  if (c == 'n') {
    const std::size_t start = *pos;
    if (!SkipJsonValue(text, pos)) {
      return false;
    }
    value->kind = JsonKind::kNull;
    value->text = std::string(text.substr(start, *pos - start));
    return true;
  }
  if (IsJsonNumberStart(c)) {
    value->kind = JsonKind::kNumber;
    return ScanJsonNumber(text, pos, &value->text);
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// JsonObject
// ---------------------------------------------------------------------------

JsonObject::JsonObject() = default;

JsonObject::~JsonObject() = default;

JsonObject::JsonObject(const JsonObject& other) = default;

JsonObject& JsonObject::operator=(const JsonObject& other) = default;

JsonObject::JsonObject(JsonObject&& other) noexcept = default;

JsonObject& JsonObject::operator=(JsonObject&& other) noexcept = default;

bool JsonObject::Parse(std::string_view text, JsonObject* out) {
  if (out == nullptr) {
    return false;
  }
  std::size_t pos = 0;
  SkipJsonSpace(text, &pos);
  if (pos >= text.size() || text[pos] != '{') {
    return false;
  }
  ++pos;
  JsonObject parsed;
  SkipJsonSpace(text, &pos);
  if (pos < text.size() && text[pos] == '}') {
    ++pos;
  } else {
    while (true) {
      SkipJsonSpace(text, &pos);
      std::string key;
      if (!ScanJsonString(text, &pos, &key)) {
        return false;
      }
      SkipJsonSpace(text, &pos);
      if (pos >= text.size() || text[pos] != ':') {
        return false;
      }
      ++pos;
      JsonValue value;
      if (!ScanJsonMemberValue(text, &pos, &value)) {
        return false;
      }
      for (const auto& member : parsed.members_) {
        if (member.first == key) {
          // A duplicate key makes the payload's meaning depend on which
          // member the reader keeps; reject it instead of picking one.
          return false;
        }
      }
      parsed.members_.emplace_back(std::move(key), std::move(value));
      SkipJsonSpace(text, &pos);
      if (pos < text.size() && text[pos] == ',') {
        ++pos;
        continue;
      }
      SkipJsonSpace(text, &pos);
      if (pos >= text.size() || text[pos] != '}') {
        return false;
      }
      ++pos;
      break;
    }
  }
  SkipJsonSpace(text, &pos);
  if (pos != text.size()) {
    // Trailing bytes after the top-level object.
    return false;
  }
  *out = std::move(parsed);
  return true;
}

}  // namespace lengsuo::fp
