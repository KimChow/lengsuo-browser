#include "cache_record.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace lengsuo::fp {
namespace {

// ---------------------------------------------------------------------------
// MD5 (RFC 1321), little-endian, used for the sign digest segment.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kMd5K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
    0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
    0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
    0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
    0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

constexpr std::uint32_t kMd5S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

std::uint32_t Rotl32(std::uint32_t x, std::uint32_t c) {
  return (x << c) | (x >> (32 - c));
}

class Md5 {
 public:
  void Update(const std::uint8_t* data, std::size_t len) {
    total_ += len;
    while (len > 0) {
      const std::size_t take = std::min(len, sizeof(buf_) - used_);
      std::memcpy(buf_ + used_, data, take);
      used_ += take;
      data += take;
      len -= take;
      if (used_ == sizeof(buf_)) {
        Transform(buf_);
        used_ = 0;
      }
    }
  }

  std::array<std::uint8_t, 16> Final() {
    const std::uint64_t bits = total_ * 8;
    std::uint8_t pad[72] = {};
    pad[0] = 0x80;
    const std::size_t pad_len = used_ < 56 ? 56 - used_ : 120 - used_;
    Update(pad, pad_len);
    std::uint8_t length[8];
    for (int i = 0; i < 8; ++i) {
      length[i] = static_cast<std::uint8_t>(bits >> (8 * i));
    }
    Update(length, sizeof(length));
    std::array<std::uint8_t, 16> out{};
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        out[static_cast<std::size_t>(i) * 4 + static_cast<std::size_t>(j)] =
            static_cast<std::uint8_t>(state_[i] >> (8 * j));
      }
    }
    return out;
  }

 private:
  void Transform(const std::uint8_t block[64]) {
    std::uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
      m[i] = static_cast<std::uint32_t>(block[4 * i]) |
             static_cast<std::uint32_t>(block[4 * i + 1]) << 8 |
             static_cast<std::uint32_t>(block[4 * i + 2]) << 16 |
             static_cast<std::uint32_t>(block[4 * i + 3]) << 24;
    }
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    for (int i = 0; i < 64; ++i) {
      std::uint32_t f;
      std::uint32_t g;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = static_cast<std::uint32_t>(i);
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      const std::uint32_t tmp = d;
      d = c;
      c = b;
      b = b + Rotl32(a + f + kMd5K[i] + m[g], kMd5S[i]);
      a = tmp;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
  }

  std::uint32_t state_[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  std::uint64_t total_ = 0;
  std::uint8_t buf_[64] = {};
  std::size_t used_ = 0;
};

std::string HexLower(const std::uint8_t* bytes, std::size_t count) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(count * 2);
  for (std::size_t i = 0; i < count; ++i) {
    out.push_back(kHex[bytes[i] >> 4]);
    out.push_back(kHex[bytes[i] & 0x0f]);
  }
  return out;
}

std::string Md5Hex(const std::string& input) {
  Md5 md5;
  md5.Update(reinterpret_cast<const std::uint8_t*>(input.data()),
             input.size());
  const auto digest = md5.Final();
  return HexLower(digest.data(), digest.size());
}

// ---------------------------------------------------------------------------
// Minimal JSON scanning. Only what ParseRecord needs: one top-level object
// whose "name"/"sign" values are strings and whose "data" value is kept as raw
// text. Never builds a DOM; composite values are only bracket-balanced, their
// interior is not validated (the record format does not parse them either).
// ---------------------------------------------------------------------------

bool IsWs(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void SkipWs(const std::string& s, std::size_t& pos) {
  while (pos < s.size() && IsWs(s[pos])) {
    ++pos;
  }
}

bool HexValue(char c, std::uint32_t& out) {
  if (c >= '0' && c <= '9') {
    out = static_cast<std::uint32_t>(c - '0');
  } else if (c >= 'a' && c <= 'f') {
    out = static_cast<std::uint32_t>(c - 'a' + 10);
  } else if (c >= 'A' && c <= 'F') {
    out = static_cast<std::uint32_t>(c - 'A' + 10);
  } else {
    return false;
  }
  return true;
}

void AppendUtf8(std::uint32_t cp, std::string& out) {
  if (cp <= 0x7f) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else if (cp <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  }
}

// Parses a JSON string starting at an opening quote; `pos` ends just past the
// closing quote. Unescaped control characters and lone surrogates fail.
bool ScanJsonString(const std::string& s, std::size_t& pos, std::string& out) {
  if (pos >= s.size() || s[pos] != '"') {
    return false;
  }
  ++pos;
  out.clear();
  while (pos < s.size()) {
    const unsigned char c = static_cast<unsigned char>(s[pos]);
    if (c == '"') {
      ++pos;
      return true;
    }
    if (c < 0x20) {
      return false;
    }
    if (c != '\\') {
      out.push_back(static_cast<char>(c));
      ++pos;
      continue;
    }
    ++pos;
    if (pos >= s.size()) {
      return false;
    }
    const char esc = s[pos++];
    switch (esc) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u': {
        if (pos + 4 > s.size()) {
          return false;
        }
        std::uint32_t cp = 0;
        for (int i = 0; i < 4; ++i) {
          std::uint32_t digit;
          if (!HexValue(s[pos++], digit)) {
            return false;
          }
          cp = (cp << 4) | digit;
        }
        if (cp >= 0xd800 && cp <= 0xdbff) {
          // High surrogate: must pair with an immediately following low one.
          if (pos + 6 > s.size() || s[pos] != '\\' || s[pos + 1] != 'u') {
            return false;
          }
          pos += 2;
          std::uint32_t low = 0;
          for (int i = 0; i < 4; ++i) {
            std::uint32_t digit;
            if (!HexValue(s[pos++], digit)) {
              return false;
            }
            low = (low << 4) | digit;
          }
          if (low < 0xdc00 || low > 0xdfff) {
            return false;
          }
          cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
        } else if (cp >= 0xdc00 && cp <= 0xdfff) {
          return false;
        }
        AppendUtf8(cp, out);
        break;
      }
      default:
        return false;
    }
  }
  return false;
}

bool IsJsonNumber(const std::string& tok) {
  std::size_t i = 0;
  if (i < tok.size() && tok[i] == '-') {
    ++i;
  }
  if (i >= tok.size()) {
    return false;
  }
  if (tok[i] == '0') {
    ++i;
  } else if (tok[i] >= '1' && tok[i] <= '9') {
    while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') {
      ++i;
    }
  } else {
    return false;
  }
  if (i < tok.size() && tok[i] == '.') {
    ++i;
    if (i >= tok.size() || tok[i] < '0' || tok[i] > '9') {
      return false;
    }
    while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') {
      ++i;
    }
  }
  if (i < tok.size() && (tok[i] == 'e' || tok[i] == 'E')) {
    ++i;
    if (i < tok.size() && (tok[i] == '+' || tok[i] == '-')) {
      ++i;
    }
    if (i >= tok.size() || tok[i] < '0' || tok[i] > '9') {
      return false;
    }
    while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') {
      ++i;
    }
  }
  return i == tok.size();
}

// Advances past one JSON value without decoding it. Returns the end position
// semantics via `pos`; strings are fully validated, composites are balanced.
bool SkipJsonValue(const std::string& s, std::size_t& pos);

bool SkipComposite(const std::string& s, std::size_t& pos) {
  std::vector<char> stack;
  std::size_t i = pos;
  while (i < s.size()) {
    const char c = s[i];
    if (c == '"') {
      std::string tmp;
      if (!ScanJsonString(s, i, tmp)) {
        return false;
      }
      continue;
    }
    if (c == '{') {
      stack.push_back('}');
      ++i;
      continue;
    }
    if (c == '[') {
      stack.push_back(']');
      ++i;
      continue;
    }
    if (c == '}' || c == ']') {
      if (stack.empty() || stack.back() != c) {
        return false;
      }
      stack.pop_back();
      ++i;
      if (stack.empty()) {
        pos = i;
        return true;
      }
      continue;
    }
    ++i;
  }
  return false;
}

bool SkipPrimitive(const std::string& s, std::size_t& pos) {
  std::size_t i = pos;
  while (i < s.size() && !IsWs(s[i]) && s[i] != ',' && s[i] != '}' &&
         s[i] != ']') {
    ++i;
  }
  if (i == pos) {
    return false;
  }
  const std::string tok = s.substr(pos, i - pos);
  if (tok != "true" && tok != "false" && tok != "null" &&
      !IsJsonNumber(tok)) {
    return false;
  }
  pos = i;
  return true;
}

bool SkipJsonValue(const std::string& s, std::size_t& pos) {
  if (pos >= s.size()) {
    return false;
  }
  const char c = s[pos];
  if (c == '"') {
    std::string tmp;
    return ScanJsonString(s, pos, tmp);
  }
  if (c == '{' || c == '[') {
    return SkipComposite(s, pos);
  }
  return SkipPrimitive(s, pos);
}

std::string EscapeJsonString(const std::string& in) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(in.size() + 2);
  for (const unsigned char c : in) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          out += "\\u00";
          out.push_back(kHex[c >> 4]);
          out.push_back(kHex[c & 0x0f]);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

// "<32hex>_v1_t<digits>" — shared by ParseSignFields and VerifySign.
bool SplitSign(const std::string& sign, std::string& digest_lower,
               int64_t& unix_seconds) {
  if (sign.size() < 33 || sign.compare(32, 5, "_v1_t") != 0) {
    return false;
  }
  digest_lower.clear();
  digest_lower.reserve(32);
  for (std::size_t i = 0; i < 32; ++i) {
    std::uint32_t digit;
    if (!HexValue(sign[i], digit)) {
      return false;
    }
    digest_lower.push_back(sign[i] >= 'A' && sign[i] <= 'F'
                               ? static_cast<char>(sign[i] - 'A' + 'a')
                               : sign[i]);
  }
  std::uint64_t ts = 0;
  const std::size_t digits_start = 37;
  if (digits_start >= sign.size()) {
    return false;
  }
  for (std::size_t i = digits_start; i < sign.size(); ++i) {
    const char c = sign[i];
    if (c < '0' || c > '9') {
      return false;
    }
    const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
    if (ts > (0xffffffffffffffffULL - digit) / 10) {
      return false;
    }
    ts = ts * 10 + digit;
  }
  if (ts > static_cast<std::uint64_t>(INT64_MAX)) {
    return false;
  }
  unix_seconds = static_cast<int64_t>(ts);
  return true;
}

}  // namespace

bool ParseRecord(const std::string& json, CacheRecord* out) {
  if (out == nullptr) {
    return false;
  }
  CacheRecord tmp;
  bool has_name = false;
  bool has_sign = false;
  bool has_data = false;
  std::size_t pos = 0;
  SkipWs(json, pos);
  if (pos >= json.size() || json[pos] != '{') {
    return false;
  }
  ++pos;
  SkipWs(json, pos);
  if (pos < json.size() && json[pos] == '}') {
    ++pos;
  } else {
    while (true) {
      SkipWs(json, pos);
      if (pos >= json.size() || json[pos] != '"') {
        return false;
      }
      std::string key;
      if (!ScanJsonString(json, pos, key)) {
        return false;
      }
      SkipWs(json, pos);
      if (pos >= json.size() || json[pos] != ':') {
        return false;
      }
      ++pos;
      SkipWs(json, pos);
      const std::size_t value_start = pos;
      if (key == "name" || key == "sign") {
        std::string value;
        if (!ScanJsonString(json, pos, value)) {
          return false;
        }
        if (key == "name") {
          tmp.name = std::move(value);
          has_name = true;
        } else {
          tmp.sign = std::move(value);
          has_sign = true;
        }
      } else {
        if (!SkipJsonValue(json, pos)) {
          return false;
        }
        if (key == "data") {
          tmp.data_json = json.substr(value_start, pos - value_start);
          has_data = true;
        }
      }
      SkipWs(json, pos);
      if (pos < json.size() && json[pos] == ',') {
        ++pos;
        continue;
      }
      if (pos < json.size() && json[pos] == '}') {
        ++pos;
        break;
      }
      return false;
    }
  }
  SkipWs(json, pos);
  if (pos != json.size()) {
    return false;
  }
  if (!has_name || !has_sign || !has_data) {
    return false;
  }
  *out = std::move(tmp);
  return true;
}

std::string SerializeRecord(const CacheRecord& rec) {
  std::string out;
  out.reserve(rec.name.size() + rec.sign.size() + rec.data_json.size() + 32);
  out += "{\"name\":\"";
  out += EscapeJsonString(rec.name);
  out += "\",\"sign\":\"";
  out += EscapeJsonString(rec.sign);
  out += "\",\"data\":";
  out += rec.data_json;
  out += "}";
  return out;
}

std::string MakeSign(const std::string& sign_input, int64_t unix_seconds) {
  return Md5Hex(sign_input) + "_v1_t" + std::to_string(unix_seconds);
}

bool ParseSignFields(const std::string& sign, std::string* md5_hex,
                     int64_t* unix_seconds) {
  std::string digest;
  int64_t ts = 0;
  if (!SplitSign(sign, digest, ts)) {
    return false;
  }
  if (md5_hex != nullptr) {
    *md5_hex = std::move(digest);
  }
  if (unix_seconds != nullptr) {
    *unix_seconds = ts;
  }
  return true;
}

SignVerdict VerifySign(const std::string& sign,
                       const std::string& sign_input,
                       int64_t now_unix,
                       int64_t max_age_seconds) {
  std::string digest;
  int64_t ts = 0;
  if (!SplitSign(sign, digest, ts)) {
    return SignVerdict::kMalformed;
  }
  if (digest != Md5Hex(sign_input)) {
    return SignVerdict::kBadDigest;
  }
  if (ts > now_unix) {
    return SignVerdict::kTooNew;
  }
  // `ts` parsed from decimal digits is non-negative, so this cannot underflow.
  if (now_unix - ts > max_age_seconds) {
    return SignVerdict::kTooOld;
  }
  return SignVerdict::kOk;
}

}  // namespace lengsuo::fp
