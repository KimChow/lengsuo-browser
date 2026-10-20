#include "extension_pin.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace lengsuo::fp {
namespace {

// Minimal JSON reader for the one shape PinedFile uses: an array of strings.
// It rejects everything else rather than skipping unknowns, so a malformed or
// hostile document cannot silently narrow the whitelist.
class JsonIdArrayReader {
 public:
  // The text is held by value. Chromium's chromium-rawref and
  // chromium-rawptr checks reject both native references and native pointers
  // as members; raw_ref<> would fix that but drags in a //base dependency,
  // and this directory is deliberately buildable with a plain C++20
  // toolchain. The document is a short array of 32-character ids, so one
  // copy costs nothing.
  explicit JsonIdArrayReader(const std::string& text) : text_(text) {}

  // Returns true only when the entire input is `[ "id", ... ]` with every
  // element a valid extension ID. On false, `ids` may be partially filled;
  // Parse() discards it.
  bool Read(std::set<std::string>* ids) {
    SkipWhitespace();
    if (!Consume('[')) return false;
    SkipWhitespace();
    if (Consume(']')) return AtEnd();
    while (true) {
      std::string id;
      if (!ReadString(&id)) return false;
      if (!ExtensionPin::IsValidId(id)) return false;
      ids->insert(std::move(id));
      SkipWhitespace();
      if (Consume(']')) return AtEnd();
      if (!Consume(',')) return false;
      SkipWhitespace();
      // A trailing comma leaves ']' where a string must begin, so the next
      // ReadString() rejects it.
    }
  }

 private:
  bool AtEnd() {
    SkipWhitespace();
    return pos_ == text_.size();
  }

  bool Consume(char expected) {
    if (pos_ >= text_.size() || text_[pos_] != expected) return false;
    ++pos_;
    return true;
  }

  void SkipWhitespace() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return;
      ++pos_;
    }
  }

  bool ReadString(std::string* out) {
    if (!Consume('"')) return false;
    while (pos_ < text_.size()) {
      const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
      if (c == '"') return true;
      if (c < 0x20) return false;  // JSON forbids raw control characters.
      if (c != '\\') {
        out->push_back(static_cast<char>(c));
        continue;
      }
      if (pos_ >= text_.size()) return false;
      const char escape = text_[pos_++];
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
          std::uint32_t code_point = 0;
          if (!ReadHex4(&code_point)) return false;
          if (code_point >= 0xD800 && code_point <= 0xDBFF) {
            std::uint32_t low = 0;
            if (!Consume('\\') || !Consume('u') || !ReadHex4(&low)) {
              return false;
            }
            if (low < 0xDC00 || low > 0xDFFF) return false;
            code_point =
                0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
          } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
            return false;  // Lone low surrogate.
          }
          AppendUtf8(code_point, out);
          break;
        }
        default: return false;
      }
    }
    return false;  // Unterminated string.
  }

  bool ReadHex4(std::uint32_t* out) {
    if (text_.size() - pos_ < 4) return false;
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_++];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        return false;
      }
    }
    *out = value;
    return true;
  }

  static void AppendUtf8(std::uint32_t code_point, std::string* out) {
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

  std::string text_;
  std::size_t pos_ = 0;
};

}  // namespace

bool ExtensionPin::Parse(const std::string& json) {
  std::set<std::string> parsed;
  JsonIdArrayReader reader(json);
  if (!reader.Read(&parsed)) return false;
  ids_ = std::move(parsed);
  return true;
}

std::string ExtensionPin::Serialize() const {
  std::string out = "[";
  for (auto it = ids_.begin(); it != ids_.end(); ++it) {
    if (it != ids_.begin()) out.push_back(',');
    out.push_back('"');
    out += *it;  // Valid IDs are [a-p]{32}: no JSON escaping needed.
    out.push_back('"');
  }
  out.push_back(']');
  return out;
}

bool ExtensionPin::Contains(const std::string& id) const {
  return ids_.find(id) != ids_.end();
}

bool ExtensionPin::Add(const std::string& id) {
  if (!IsValidId(id)) return false;
  return ids_.insert(id).second;
}

// static
bool ExtensionPin::IsValidId(const std::string& id) {
  if (id.size() != 32) return false;
  for (const char c : id) {
    if (c < 'a' || c > 'p') return false;
  }
  return true;
}

}  // namespace lengsuo::fp
