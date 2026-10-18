#include "canvas_apply.h"
#include <span>
#include <string_view>
#include "canvas_noise.h"
namespace lengsuo {
namespace {
bool g_enabled = false;
std::uint64_t g_seed = 0;
std::uint32_t g_slot = 0;
}  // namespace
void InitCanvasNoiseFromSwitch(const char* switch_value) {
  g_enabled = false;
  if (!switch_value) return;
  const std::string_view value(switch_value);
  const auto sep = value.find(':');
  if (sep == std::string_view::npos || sep == 0 || sep + 1 >= value.size()) {
    return;
  }
  const std::string_view seed_text = value.substr(0, sep);
  const std::string_view slot_text = value.substr(sep + 1);
  std::uint64_t seed = 0;
  if (seed_text.empty() || seed_text.size() > 16) {
    return;
  }
  for (const char c : seed_text) {
    const int digit = (c >= '0' && c <= '9')   ? c - '0'
                      : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                      : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                               : -1;
    if (digit < 0) {
      return;
    }
    seed = (seed << 4) | static_cast<std::uint64_t>(digit);
  }
  if (seed == 0) {
    return;
  }
  std::uint32_t slot = 0;
  for (const char c : slot_text) {
    if (c < '0' || c > '9') {
      return;
    }
    slot = slot * 10u + static_cast<std::uint32_t>(c - '0');
    if (slot > 0xFFFFFFFu) {
      return;
    }
  }
  if (slot_text.empty()) {
    return;
  }
  g_seed = seed;
  g_slot = slot;
  g_enabled = true;
}
bool CanvasNoiseEnabled() { return g_enabled; }
void ApplyProfileCanvasNoise(void* data, std::uint32_t width,
                             std::uint32_t height, std::uint64_t stride) {
  if (!g_enabled || !data || !width || !height) return;
  canvas::CanvasNoiseConfig config;
  config.algorithm_version = 1;
  config.enabled = true;
  config.seed = g_seed;
  config.slot = g_slot;
  const std::uint64_t buffer_size =
      static_cast<std::uint64_t>(stride) * height;
  const std::uint8_t* src = static_cast<const std::uint8_t*>(data);
  std::uint8_t* dst = static_cast<std::uint8_t*>(data);
  canvas::ApplyCanvasNoiseV1(
      config, std::span<const std::uint8_t>(src, buffer_size),
      std::span<std::uint8_t>(dst, buffer_size), width, height, stride,
      buffer_size);
}
}  // namespace lengsuo
