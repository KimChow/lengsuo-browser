#include "profile_wire.h"

#include <string_view>

#include "wire_reader.h"

namespace lengsuo {
std::optional<ParsedProfile> DecodeProfile(
    std::span<const std::uint8_t> bytes) {
  if (bytes.size() > 4096) {
    return std::nullopt;
  }
  WireReader reader(bytes);
  std::uint32_t value;
  for (const auto character : std::string_view("LBPF1")) {
    if (!reader.Number(value, 1) ||
        value != static_cast<unsigned char>(character)) {
      return std::nullopt;
    }
  }
  ParsedProfile profile;
  if (!reader.Number(profile.schema_version, 4) ||
      !InRange(profile.schema_version, 1, 2) ||
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
  if (!reader.Number(profile.width, 4) || !InRange(profile.width, 1, 16384) ||
      !reader.Number(profile.height, 4) || !InRange(profile.height, 1, 16384) ||
      static_cast<std::uint64_t>(profile.width) * profile.height >
          64 * 1024 * 1024 ||
      !reader.Number(value, 1) || (value != 8 && value != 24 && value != 30)) {
    return std::nullopt;
  }
  profile.color_depth = static_cast<std::uint8_t>(value);
  if (!reader.Number(profile.dpr_milli, 4) ||
      !InRange(profile.dpr_milli, 1, 10000) || !reader.Number(value, 2) ||
      !InRange(value, 1, 1024)) {
    return std::nullopt;
  }
  profile.cpu_cores = static_cast<std::uint16_t>(value);
  if (!reader.Number(profile.memory_mib, 4) ||
      !InRange(profile.memory_mib, 128, 1048576) || !reader.Number(value, 1) ||
      value > 10 ||
      ((profile.platform == Platform::Android ||
        profile.platform == Platform::Ios) && value == 0)) {
    return std::nullopt;
  }
  profile.max_touch_points = static_cast<std::uint8_t>(value);
  if (!reader.Text(profile.language, 35) || !reader.Text(profile.timezone, 64)) {
    return std::nullopt;
  }
  // Schema 2 carries one webrtc policy byte after timezone and before the
  // slot table (W3 layout, docs/webrtc-schema2-native.md);
  // schema 1 offsets are unchanged and carry no statement.
  if (profile.schema_version == 2) {
    if (!reader.Number(value, 1) || value > 3) {
      return std::nullopt;
    }
    profile.webrtc_policy = static_cast<std::uint8_t>(value);
    // Optional trailing font section, before the slot table. Writers that
    // predate the font dimension (and every schema-1 profile) simply stop
    // here, so every previously valid profile keeps decoding byte-identically
    // -- the same optional-tail rule the catalog's GPU section uses.
    //
    // The section is one `fonts` tag followed by the {switcher, value, mask}
    // triple: a 1-byte switcher (online encoding 1/2/3), the
    // semicolon-separated family string, and a 1-byte mask. The reader accepts
    // exactly one such section followed by the slot table. Two-char and
    // four-char lookaheads cannot both be "fonts", so an accidental
    // `fontsfonts` payload fails instead of being silently skipped.
    std::string probe;
    const auto mark = reader.position();
    if (reader.PeekText(probe) && probe == "fonts") {
      reader.Seek(mark + 2 + probe.size());
      ParsedProfile::FontDimension fonts;
      if (!reader.Number(value, 1) || !InRange(value, 1, 3)) {
        return std::nullopt;
      }
      fonts.switcher = static_cast<std::uint8_t>(value);
      if (!reader.OptionalText(fonts.value, 4096)) {
        return std::nullopt;
      }
      if (!reader.Number(value, 1) || value > 1) {
        return std::nullopt;
      }
      fonts.mask = static_cast<std::uint8_t>(value);
      profile.fonts = std::move(fonts);
    }
  }
  if (!reader.Number(value, 2) || value != 7) {
    return std::nullopt;
  }
  constexpr std::array<std::string_view, 7> names = {
      "audio", "canvas", "client_rects", "fonts", "gpu", "ua", "webgl"};
  for (std::size_t i = 0; i < names.size(); ++i) {
    std::string name;
    if (!reader.Text(name, 12, true) || name != names[i] ||
        !reader.Number(profile.slots[i], 4) ||
        !InRange(profile.slots[i], 1, 1048576)) {
      return std::nullopt;
    }
  }
  if (!reader.End()) {
    return std::nullopt;
  }
  return profile;
}
}  // namespace lengsuo
