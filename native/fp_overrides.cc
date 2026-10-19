// fp_overrides - renderer-side fingerprint override state.
//
// One switch carries everything:
//   --lengsuo-managed-fp=<seed-hex>:<canvas>:<audio>:<rects>:<fonts>:<gpu>:
//                        <ua>:<webgl-params>:<vendor>:<renderer>:<hwc>:<mem>
// 0 (or empty, for the two strings) keeps that dimension native. Absent switch
// => everything disabled and upstream behavior stays byte-identical.
#include "lengsuo/native/fp_overrides.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace lengsuo::fp {
namespace {

// SplitMix64 finalizer: cheap, well distributed, bit-identical everywhere.
std::uint64_t Mix(std::uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

// Field indices of the switch payload.
enum Field : std::size_t {
  kSeed = 0,
  kCanvas,
  kAudio,
  kRects,
  kFonts,
  kGpu,
  kUa,
  kWebglParams,
  kVendor,
  kRenderer,
  kHwc,
  kMem,
  kFieldCount,
};

bool g_enabled = false;
std::uint64_t g_seed = 0;
std::uint64_t g_canvas = 0;
std::uint64_t g_audio = 0;
std::uint32_t g_rects = 0;
std::uint32_t g_fonts = 0;
std::uint32_t g_gpu = 0;
std::uint32_t g_ua = 0;
std::uint32_t g_webgl_params = 0;
// Fixed-capacity owned copies. The switch payload is a temporary in the
// caller, so views into it would dangle; std::string globals are disallowed
// (exit-time destructor), hence plain buffers + std::span for indexing.
constexpr std::size_t kMaxFaceLen = 128;
std::array<char, kMaxFaceLen + 1> g_vendor_storage{};
std::array<char, kMaxFaceLen + 1> g_renderer_storage{};
std::uint32_t g_hw_concurrency = 0;
std::uint32_t g_device_memory = 0;

// Copies at most kMaxFaceLen chars and NUL-terminates. Returns a view of the
// copied prefix (empty when the source was empty).
std::string_view StoreFace(std::string_view text,
                           std::array<char, kMaxFaceLen + 1>& storage) {
  const std::size_t n = text.size() < kMaxFaceLen ? text.size() : kMaxFaceLen;
  std::span<char> dst(storage);
  for (std::size_t i = 0; i < n; ++i) {
    dst[i] = text.at(i);
  }
  dst[n] = '\0';
  return std::string_view(storage.data(), n);
}

std::uint32_t ParseDecimal(std::string_view text) {
  if (text.empty() || text.size() > 9) {
    return 0;
  }
  std::uint32_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return 0;
    }
    value = value * 10u + static_cast<std::uint32_t>(c - '0');
  }
  return value;
}

bool ParseHex(std::string_view text, std::uint64_t* out) {
  if (text.empty() || text.size() > 16) {
    return false;
  }
  std::uint64_t value = 0;
  for (const char c : text) {
    std::uint64_t digit = 0;
    if (c >= '0' && c <= '9') {
      digit = static_cast<std::uint64_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = static_cast<std::uint64_t>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      digit = static_cast<std::uint64_t>(c - 'A' + 10);
    } else {
      return false;
    }
    value = (value << 4) | digit;
  }
  *out = value;
  return true;
}

// A non-zero slot enables the dimension; the slot byte and the process seed
// both feed the mixer so two profiles never collide on one dimension.
std::uint64_t DeriveSlotSeed(std::uint32_t slot, std::uint64_t salt) {
  return slot != 0 ? Mix(g_seed ^ salt ^ static_cast<std::uint64_t>(slot)) : 0;
}

}  // namespace

void InitFromSwitch(std::string_view value) {
  // Reset first so a malformed payload can never leave stale state behind.
  g_enabled = false;
  g_seed = 0;
  g_canvas = 0;
  g_audio = 0;
  g_rects = 0;
  g_fonts = 0;
  g_gpu = 0;
  g_ua = 0;
  g_webgl_params = 0;
  g_vendor_storage.fill(0);
  g_renderer_storage.fill(0);
  g_hw_concurrency = 0;
  g_device_memory = 0;

  std::array<std::string_view, kFieldCount> fields{};
  std::size_t count = 0;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= value.size() && count < kFieldCount; ++i) {
    if (i == value.size() || value[i] == ':') {
      fields.at(count) = value.substr(start, i - start);
      ++count;
      start = i + 1;
    }
  }
  // The two face strings may legitimately be empty, so only the numeric prefix
  // is mandatory.
  if (count < kFieldCount - 2 || fields.at(kSeed).empty()) {
    return;
  }

  std::uint64_t seed = 0;
  if (!ParseHex(fields.at(kSeed), &seed) || seed == 0) {
    return;
  }
  g_seed = seed;

  const std::uint32_t canvas_slot = ParseDecimal(fields.at(kCanvas));
  const std::uint32_t audio_slot = ParseDecimal(fields.at(kAudio));
  g_rects = ParseDecimal(fields.at(kRects));
  g_fonts = ParseDecimal(fields.at(kFonts));
  g_gpu = ParseDecimal(fields.at(kGpu));
  g_ua = ParseDecimal(fields.at(kUa));
  g_webgl_params = ParseDecimal(fields.at(kWebglParams));
  const std::string_view vendor = StoreFace(fields.at(kVendor),
                                            g_vendor_storage);
  const std::string_view renderer = StoreFace(fields.at(kRenderer),
                                              g_renderer_storage);
  g_hw_concurrency = ParseDecimal(fields.at(kHwc));
  g_device_memory = ParseDecimal(fields.at(kMem));

  g_canvas = DeriveSlotSeed(canvas_slot, 0xC4C0FFEEULL);
  g_audio = DeriveSlotSeed(audio_slot, 0x0DEADBEEFULL);
  g_enabled = true;
  // Keep the stored views reachable for the accessors below.
  (void)vendor;
  (void)renderer;
}

bool Enabled() {
  return g_enabled;
}

std::string_view WebGLVendor() {
  if (g_gpu == 0) {
    return std::string_view();
  }
  return std::string_view(g_vendor_storage.data(),
                          std::strlen(g_vendor_storage.data()));
}

std::string_view WebGLRenderer() {
  if (g_gpu == 0) {
    return std::string_view();
  }
  return std::string_view(g_renderer_storage.data(),
                          std::strlen(g_renderer_storage.data()));
}

std::uint32_t HardwareConcurrency() {
  return g_hw_concurrency;
}

std::uint32_t DeviceMemoryGB() {
  return g_device_memory;
}

double RectsJitterPx() {
  // Must exceed LayoutUnit quantization (1/64 px) to be observable, yet stay
  // sub-pixel so rendering is visually unchanged: 0.101..0.400 px.
  return g_rects != 0 ? 0.101 + static_cast<double>(g_seed % 300u) * 0.001
                      : 0.0;
}

std::uint64_t AudioSeed() {
  return g_audio;
}

std::uint64_t WebGLParamsSeed() {
  return DeriveSlotSeed(g_webgl_params, 0xA5A5A5A5ULL);
}

std::int32_t WebGLParamBias(std::uint32_t pname, std::int32_t original) {
  if (g_webgl_params == 0) {
    return original;
  }
  // Allow-list: only informational MAX_* capability caps form the WebGL
  // fingerprint surface. State params (ACTIVE_TEXTURE, DEPTH_FUNC,
  // CURRENT_PROGRAM, VIEWPORT, SCISSOR_BOX, ...) must pass through untouched
  // or pages break. Values are Khronos GL enums; this translation unit does
  // not include the GL headers.
  switch (pname) {
    case 0x0D33:  // GL_MAX_TEXTURE_SIZE
    case 0x8073:  // GL_MAX_3D_TEXTURE_SIZE (WebGL2)
    case 0x84E8:  // GL_MAX_RENDERBUFFER_SIZE
    case 0x851C:  // GL_MAX_CUBE_MAP_TEXTURE_SIZE
    case 0x8869:  // GL_MAX_VERTEX_ATTRIBS
    case 0x8872:  // GL_MAX_TEXTURE_IMAGE_UNITS
    case 0x88FF:  // GL_MAX_ARRAY_TEXTURE_LAYERS (WebGL2)
    case 0x8B4C:  // GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS
    case 0x8B4D:  // GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
    case 0x8D57:  // GL_MAX_SAMPLES
    case 0x8D6B:  // GL_MAX_ELEMENT_INDEX (WebGL2)
    case 0x8DFB:  // GL_MAX_VERTEX_UNIFORM_VECTORS
    case 0x8DFC:  // GL_MAX_VARYING_VECTORS
    case 0x8DFD:  // GL_MAX_FRAGMENT_UNIFORM_VECTORS
      break;
    default:
      return original;
  }
  if (original <= 0) {
    return original;
  }
  // Mix the cap value in so two profiles can share one dimension while still
  // advertising different limits.
  const std::uint64_t mixed =
      Mix(WebGLParamsSeed() ^ (static_cast<std::uint64_t>(pname) << 24) ^
          static_cast<std::uint64_t>(original));
  // Never advertise more than the driver supports: a page that trusts a
  // higher cap would allocate a texture/shader that cannot exist and fail at
  // run time. Only reduce, never raise, so every value inside the reported cap
  // is still guaranteed to work. Small caps reduce by at most 1.
  const std::int64_t bound = original > 8 ? original / 4 : 1;
  const std::uint64_t span = static_cast<std::uint64_t>(bound) + 1u;
  const std::int64_t drop = static_cast<std::int64_t>(mixed % span);
  const std::int64_t biased = static_cast<std::int64_t>(original) - drop;
  return static_cast<std::int32_t>(biased > 0 ? biased : 1);
}

}  // namespace lengsuo::fp
