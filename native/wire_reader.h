#ifndef LENGSUO_NATIVE_WIRE_READER_H_
#define LENGSUO_NATIVE_WIRE_READER_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace lengsuo {
// Bounded big-endian cursor shared by the LBPF1/LBPC1 decoders. Every read
// either advances with validated data or fails; no partial field escapes.
class WireReader {
 public:
  explicit WireReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  bool Number(std::uint32_t& out, std::size_t count) {
    if (count > bytes_.size() - pos_) {
      return false;
    }
    out = 0;
    for (std::size_t i = 0; i < count; ++i) {
      out = (out << 8) | bytes_[pos_++];
    }
    return true;
  }

  // Fixed-length (2-byte count) printable field; empty values fail.
  bool Text(std::string& out, std::size_t max, bool identifier = false) {
    return OptionalText(out, max, identifier) && !out.empty();
  }

  // Like Text() but a zero-length value is accepted and yields an empty
  // string; used only where the format explicitly allows emptiness.
  bool OptionalText(std::string& out, std::size_t max,
                    bool identifier = false) {
    std::uint32_t count;
    if (!Number(count, 2) || count > max ||
        count > bytes_.size() - pos_) {
      return false;
    }
    out.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto c = bytes_[pos_++];
      const bool id = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_';
      if (c < 32 || c > 126 || (identifier && !id)) {
        return false;
      }
      out.push_back(static_cast<char>(c));
    }
    return true;
  }

  bool Byte(std::uint8_t& out) {
    if (pos_ >= bytes_.size()) {
      return false;
    }
    out = bytes_[pos_++];
    return true;
  }

  bool End() const { return pos_ == bytes_.size(); }

 private:
  std::span<const std::uint8_t> bytes_;
  std::size_t pos_ = 0;
};

inline bool InRange(std::uint32_t value, std::uint32_t low,
                    std::uint32_t high) {
  return value >= low && value <= high;
}
}  // namespace lengsuo
#endif
