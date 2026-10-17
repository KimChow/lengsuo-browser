#include "profile_wire.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "golden.inc"

using Bytes = std::vector<std::uint8_t>;
int checks = 0;

void Check(bool ok) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL " << checks << '\n';
    std::exit(1);
  }
}

void Bad(const Bytes& bytes) {
  Check(!lengsuo::DecodeProfile(bytes));
}

void Number(Bytes& bytes, std::size_t offset, std::uint32_t value,
            std::size_t width) {
  for (std::size_t i = width; i > 0; --i) {
    bytes[offset + i - 1] = static_cast<std::uint8_t>(value & 255u);
    value >>= 8;
  }
}

// All offsets originate in the fixed 147-byte fixture or its <= 65-byte fields.
Bytes::iterator At(Bytes& bytes, std::size_t offset) {
  return bytes.begin() + static_cast<Bytes::difference_type>(offset);
}

// Schema 2: identical schema 1 framing with schema_version=2 and one webrtc
// policy byte (WebRTCIPHandlingPolicy enum 0..3) inserted after the timezone
// text and before slot_count (W3 layout). Schema 1 offsets [0, 65) are
// unchanged; the slot table shifts one byte later.
Bytes Schema2(const Bytes& golden, std::uint8_t policy) {
  Bytes bytes = golden;
  Number(bytes, 5, 2, 4);
  bytes.insert(At(bytes, 65), policy);
  return bytes;
}

int main() {
  const Bytes golden(std::begin(kGolden), std::end(kGolden));
  auto profile = lengsuo::DecodeProfile(golden);
  Check(profile.has_value());
  Check(profile->schema_version == 1 && profile->noise_version == 1);
  Check(profile->environment_id == "env-1" && profile->pool_version == "p1");
  Check(profile->platform == lengsuo::Platform::Windows);
  Check(profile->width == 1920 && profile->height == 1080 &&
        profile->color_depth == 24);
  Check(profile->dpr_milli == 1000 && profile->cpu_cores == 8 &&
        profile->memory_mib == 8192);
  Check(profile->max_touch_points == 0 && profile->language == "en-US" &&
        profile->timezone == "UTC");
  Check(profile->slots == std::array<std::uint32_t, 7>{3, 1, 5, 4, 6, 7, 2});
  Check(profile->noise_seed ==
        std::array<std::uint8_t, 8>{0, 1, 2, 3, 4, 5, 6, 7});
  for (std::size_t n = 0; n < golden.size(); ++n) {
    Bad(Bytes(golden.begin(), golden.begin() +
              static_cast<Bytes::difference_type>(n)));
  }
  Bad(Bytes(4097, 0));
  auto bytes = golden;
  bytes.push_back(0);
  Bad(bytes);
  for (std::size_t offset : {0u, 8u, 12u, 32u, 41u, 52u, 69u}) {
    bytes = golden;
    bytes[offset] = 255;
    Bad(bytes);
  }
  for (std::size_t offset : {15u, 22u, 55u, 62u}) {
    for (auto character : {0, 31, 127, 128}) {
      bytes = golden;
      bytes[offset] = static_cast<std::uint8_t>(character);
      Bad(bytes);
    }
  }
  // Replace each variable string while preserving the rest of the framing.
  for (auto field : {std::array<std::size_t, 3>{13, 5, 64},
                     {20, 2, 64}, {53, 5, 35}, {60, 3, 64}}) {
    for (auto length : {field[2], field[2] + 1}) {
      bytes = golden;
      const auto start = field[0];
      bytes.erase(At(bytes, start + 2), At(bytes, start + 2 + field[1]));
      bytes.insert(At(bytes, start + 2), length, 'A');
      Number(bytes, start, static_cast<std::uint32_t>(length), 2);
      Check(lengsuo::DecodeProfile(bytes).has_value() == (length == field[2]));
    }
  }
  bytes = golden;
  bytes[15] = '.';
  Bad(bytes);
  bytes = golden;
  bytes[22] = ' ';
  Bad(bytes);
  for (std::size_t offset : {13u, 20u, 53u, 60u, 67u}) {
    bytes = golden;
    bytes[offset] = 255;
    Bad(bytes);
  }
  for (std::size_t offset : {13u, 20u, 53u, 60u}) {
    bytes = golden;
    bytes[offset + 1] = 0;
    Bad(bytes);
  }
  struct Range {
    std::size_t offset;
    std::size_t width;
    std::uint32_t low;
    std::uint32_t high;
  };
  for (auto range : {Range{33, 4, 1, 16384}, {37, 4, 1, 16384},
                     {42, 4, 1, 10000}, {46, 2, 1, 1024},
                     {48, 4, 128, 1048576}, {74, 4, 1, 1048576}}) {
    for (auto value : {range.low - 1, range.high + 1, 0xffffffffu}) {
      bytes = golden;
      Number(bytes, range.offset, value, range.width);
      Bad(bytes);
    }
    for (auto value : {range.low, range.high}) {
      bytes = golden;
      Number(bytes, range.offset, value, range.width);
      Check(lengsuo::DecodeProfile(bytes).has_value());
    }
  }
  bytes = golden;
  Number(bytes, 33, 16384, 4);
  Number(bytes, 37, 16384, 4);
  Bad(bytes);
  for (auto depth : {8, 24, 30}) {
    bytes = golden;
    bytes[41] = static_cast<std::uint8_t>(depth);
    Check(lengsuo::DecodeProfile(bytes).has_value());
  }
  for (auto depth : {0, 16, 32}) {
    bytes = golden;
    bytes[41] = static_cast<std::uint8_t>(depth);
    Bad(bytes);
  }
  for (auto code : {0, 1, 2, 3, 4}) {
    bytes = golden;
    bytes[32] = static_cast<std::uint8_t>(code);
    bytes[52] = 10;
    Check(lengsuo::DecodeProfile(bytes).has_value());
    bytes[52] = 11;
    Bad(bytes);
  }
  for (auto code : {3, 4}) {
    bytes = golden;
    bytes[32] = static_cast<std::uint8_t>(code);
    Bad(bytes);
    bytes[52] = 1;
    Check(lengsuo::DecodeProfile(bytes).has_value());
  }
  for (std::size_t offset : {74u, 86u, 104u, 115u, 124u, 132u, 143u}) {
    bytes = golden;
    Number(bytes, offset, 0, 4);
    Bad(bytes);
    bytes = golden;
    Number(bytes, offset, 1048577, 4);
    Bad(bytes);
  }
  bytes = golden;
  bytes[66] = 6;
  Bad(bytes);
  bytes = golden;
  bytes[66] = 8;
  Bad(bytes);
  bytes = golden;
  bytes[69] = 'z';
  Bad(bytes);
  // Duplicate gpu dimension in place of fonts, with its own correct length.
  bytes = golden;
  bytes.erase(At(bytes, 108), At(bytes, 119));
  bytes.insert(At(bytes, 108), {0, 3, 'g', 'p', 'u', 0, 0, 0, 4});
  Bad(bytes);
  // Swap first two whole entries: exact names but noncanonical order.
  bytes = golden;
  Bytes entries(At(bytes, 78), At(bytes, 90));
  entries.insert(entries.end(), At(bytes, 67), At(bytes, 78));
  std::copy(entries.begin(), entries.end(), At(bytes, 67));
  Bad(bytes);
  // --- Schema 2: schema 1 framing plus one webrtc policy byte inserted
  // after the timezone text and before slot_count (W3 layout). ---
  for (const std::uint8_t policy : {
               static_cast<std::uint8_t>(0), static_cast<std::uint8_t>(1),
               static_cast<std::uint8_t>(2), static_cast<std::uint8_t>(3)}) {
    auto decoded = lengsuo::DecodeProfile(Schema2(golden, policy));
    Check(decoded.has_value());
    Check(decoded->schema_version == 2);
    Check(decoded->webrtc_policy.has_value() &&
          *decoded->webrtc_policy == policy);
    // Schema 1 payload fields are unchanged under schema 2.
    Check(decoded->environment_id == "env-1" && decoded->language == "en-US" &&
          decoded->timezone == "UTC" && decoded->width == 1920 &&
          decoded->max_touch_points == 0 &&
          decoded->slots == std::array<std::uint32_t, 7>{3, 1, 5, 4, 6, 7, 2});
  }
  // Schema 1 still decodes and carries no webrtc statement.
  {
    auto s1 = lengsuo::DecodeProfile(golden);
    Check(s1.has_value() && s1->schema_version == 1 &&
          !s1->webrtc_policy.has_value());
  }
  for (auto policy : {4, 255}) {
    Bad(Schema2(golden, static_cast<std::uint8_t>(policy)));
  }
  // Schema 2 with the policy byte (offset 65, after timezone) removed:
  // the remaining stream is 147 bytes of schema-1 framing plus the v2
  // version field — it must not decode.
  {
    auto bytes2 = Schema2(golden, 2);
    bytes2.erase(At(bytes2, 65));
    Bad(bytes2);
  }
  // Duplicated policy byte (extra copy after the designated one).
  {
    auto bytes2 = Schema2(golden, 2);
    bytes2.insert(At(bytes2, 66), 2);
    Bad(bytes2);
  }
  // Trailing byte after the slot table of a schema 2 stream.
  {
    auto bytes2 = Schema2(golden, 2);
    bytes2.push_back(0);
    Bad(bytes2);
  }
  // Unknown schema versions.
  for (auto version : {0u, 3u, 4u, 0xffffffffu}) {
    bytes = golden;
    Number(bytes, 5, version, 4);
    Bad(bytes);
    bytes = Schema2(golden, 1);
    Number(bytes, 5, version, 4);
    Bad(bytes);
  }
  std::cout << "PASS " << checks << " assertions\n";
}
