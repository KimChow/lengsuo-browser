#ifndef LENGSUO_NATIVE_PROFILE_WIRE_H_
#define LENGSUO_NATIVE_PROFILE_WIRE_H_

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace lengsuo {
enum class Platform : std::uint8_t {
  Windows,
  Linux,
  Macos,
  Android,
  Ios
};

// Wire decoding only: no catalog resolution, signature/lease verification,
// browser injection or fingerprint acceptance is implied by this value.
struct ParsedProfile {
  std::uint32_t schema_version{};
  std::uint32_t noise_version{};
  std::string environment_id;
  std::string pool_version;
  std::array<std::uint8_t, 8> noise_seed{};
  Platform platform{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t dpr_milli{};
  std::uint32_t memory_mib{};
  std::uint16_t cpu_cores{};
  std::uint8_t color_depth{};
  std::uint8_t max_touch_points{};
  std::string language;
  std::string timezone;
  // Schema 2 only: the font dimension, shaped like every other overridable
  // dimension -- {switcher, value, mask} (docs/font-dimension.md §3.1). `value`
  // is the semicolon-separated family string 对标端 ships ("Arial;Arial Black");
  // no structured inventory is fabricated client-side because the server-side
  // pool was never captured. `mask` is the font_fingerprint_mask bit that turns
  // the metric-noise mechanism on independently of a non-empty value. An omitted
  // section means "the profile says nothing about fonts" (std::nullopt) and the
  // host's real metrics are kept. An empty `value` is a legal statement ("decide
  // by mask"), matching the empty-field convention the other dimensions use.
  struct FontDimension {
    std::uint8_t switcher{};
    std::string value;
    std::uint8_t mask{};
  };
  std::optional<FontDimension> fonts;
  // audio, canvas, client_rects, fonts, gpu, ua, webgl, in canonical order.
  std::array<std::uint32_t, 7> slots{};
  // Schema 2 only: blink WebRtcIpHandlingPolicy enum value 0..3. Schema 1
  // profiles carry no statement (nullopt) and leave upstream behavior.
  std::optional<std::uint8_t> webrtc_policy;
};

// All malformed input returns nullopt; no partially parsed profile escapes.
std::optional<ParsedProfile> DecodeProfile(std::span<const std::uint8_t> bytes);
}  // namespace lengsuo
#endif
