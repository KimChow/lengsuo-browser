#include "profile_wire.h"

#include <string_view>

namespace lengsuo {
namespace {
class Reader {
 public:
  explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

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

  bool Text(std::string& out, std::size_t max, bool identifier = false) {
    std::uint32_t count;
    if (!Number(count, 2) || count == 0 || count > max ||
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

  bool End() const { return pos_ == bytes_.size(); }

 private:
  std::span<const std::uint8_t> bytes_;
  std::size_t pos_ = 0;
};

bool Range(std::uint32_t value, std::uint32_t low, std::uint32_t high) {
  return value >= low && value <= high;
}
}  // namespace

std::optional<ParsedProfile> DecodeProfile(std::span<const std::uint8_t> bytes) {
  if (bytes.size() > 4096) {
    return std::nullopt;
  }
  Reader reader(bytes);
  std::uint32_t value;
  for (const auto character : std::string_view("LBPF1")) {
    if (!reader.Number(value, 1) ||
        value != static_cast<unsigned char>(character)) {
      return std::nullopt;
    }
  }
  ParsedProfile profile;
  if (!reader.Number(profile.schema_version, 4) || profile.schema_version != 1 ||
      !reader.Number(profile.noise_version, 4) || profile.noise_version != 1 ||
      !reader.Text(profile.environment_id, 64, true) ||
      !reader.Text(profile.pool_version, 64, true)) {
    return std::nullopt;
  }
  for (auto& byte : profile.noise_seed) {
    if (!reader.Number(value, 1)) {
      return std::nullopt;
    }
    byte = static_cast<std::uint8_t>(value);
  }
  if (!reader.Number(value, 1) || value > 4) {
    return std::nullopt;
  }
  profile.platform = static_cast<Platform>(value);
  if (!reader.Number(profile.width, 4) || !Range(profile.width, 1, 16384) ||
      !reader.Number(profile.height, 4) || !Range(profile.height, 1, 16384) ||
      static_cast<std::uint64_t>(profile.width) * profile.height >
          64 * 1024 * 1024 ||
      !reader.Number(value, 1) || (value != 8 && value != 24 && value != 30)) {
    return std::nullopt;
  }
  profile.color_depth = static_cast<std::uint8_t>(value);
  if (!reader.Number(profile.dpr_milli, 4) ||
      !Range(profile.dpr_milli, 1, 10000) || !reader.Number(value, 2) ||
      !Range(value, 1, 1024)) {
    return std::nullopt;
  }
  profile.cpu_cores = static_cast<std::uint16_t>(value);
  if (!reader.Number(profile.memory_mib, 4) ||
      !Range(profile.memory_mib, 128, 1048576) || !reader.Number(value, 1) ||
      value > 10 ||
      ((profile.platform == Platform::Android ||
        profile.platform == Platform::Ios) && value == 0)) {
    return std::nullopt;
  }
  profile.max_touch_points = static_cast<std::uint8_t>(value);
  if (!reader.Text(profile.language, 35) || !reader.Text(profile.timezone, 64) ||
      !reader.Number(value, 2) || value != 7) {
    return std::nullopt;
  }
  constexpr std::array<std::string_view, 7> names = {
      "audio", "canvas", "client_rects", "fonts", "gpu", "ua", "webgl"};
  for (std::size_t i = 0; i < names.size(); ++i) {
    std::string name;
    if (!reader.Text(name, 12) || name != names[i] ||
        !reader.Number(profile.slots[i], 4) ||
        !Range(profile.slots[i], 1, 1048576)) {
      return std::nullopt;
    }
  }
  if (!reader.End()) {
    return std::nullopt;
  }
  return profile;
}
}  // namespace lengsuo
