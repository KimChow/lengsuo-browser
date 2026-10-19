// fp_overrides - renderer-side fingerprint override state.
//
// One switch carries everything:
//   --lengsuo-managed-fp=<seed-hex>:<canvas>:<audio>:<rects>:<fonts>:<gpu>:
//                        <ua>:<webgl-params>:<vendor>:<renderer>:<hwc>:<mem>
// 0 (or empty, for the two strings) keeps that dimension native. The font
// dimension is the exception: it is not a seed but an explicit family list,
// and it therefore arrives in its own --lengsuo-managed-fonts switch (see
// docs/fp-switch-contract.md). Absent switches => everything disabled and
// upstream behavior stays byte-identical.
#include "fp_overrides.h"

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

// Font availability set. Unlike the numeric slots above this is not a seed:
// the profile hands over an explicit family list (对标端 does not expose a font
// pool, so nothing is ever invented here -- see docs/font-dimension.md). The
// storage is a function-local static so it has no exit-time destructor.
FontFaceSetFilter& FontsStorage() {
  static FontFaceSetFilter fonts;
  return fonts;
}
bool g_fonts_installed = false;

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

// Largest power of two <= value; value must be positive.
std::int64_t PowerOfTwoAtMost(std::int64_t value) {
  std::int64_t result = 1;
  while (result <= value / 2) {
    result *= 2;
  }
  return result;
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

bool InitFontsFromSwitch(std::string_view value) {
  g_fonts_installed = false;
  if (!FontsStorage().Init(value)) {
    return false;
  }
  g_fonts_installed = true;
  return true;
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

const FontFaceSetFilter* Fonts() {
  return g_fonts_installed ? &FontsStorage() : nullptr;
}

std::string FontSubstituteFor(std::string_view family,
                              std::span<const std::string_view> candidates) {
  if (!g_fonts_installed) {
    return std::string();  // no set installed: stay byte-identical to upstream
  }
  // Builds a throwaway substitution view from the installed set. The decision
  // itself lives in native/font_substitution.h so it can be unit-tested
  // without Blink; this wrapper only bridges the renderer-side storage.
  const FontSubstitution substitution = FontSubstitution::From(FontsStorage());
  if (!substitution.NeedsSubstitute(family)) {
    return std::string();
  }
  return substitution.SubstituteFor(family, candidates);
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
  // Report a value a real driver could actually report.
  //
  // Drivers do not hand back arbitrary integers here; the caps people look at
  // (texture size, renderbuffer size, uniform/varying counts) come from a small
  // set of shapes: powers of two, the *_BITS fields stepping by 1, and a handful
  // of architecture constants. 6364 is not one of them — a randomised number in
  // that range is more distinctive than the unmodified driver value, which is
  // the opposite of what this override is for.
  //
  // Never advertise more than the driver supports: a page that trusts a higher
  // cap would allocate a texture/shader that cannot exist and fail at run time.
  // So the candidate set is filtered to <= original, and the step count is
  // measured on that filtered set to keep the distribution uniform.
  //
  // The driver's own value stays a candidate. That is deliberate and it is the
  // honest outcome: on a machine with a real GPU the driver value is already a
  // true measurement, and lowering it only makes sense as "this profile is the
  // next tier down" - which also has to change the renderer string. Until the
  // server sends per-tier parameter tables, most profiles therefore keep the
  // driver value, and this dimension carries almost no entropy. That is the
  // correct trade: a plausible value beats a perturbed one.
  std::int32_t candidates[16];
  std::size_t count = 0;
  const auto add_candidate = [&](std::int64_t value) {
    if (value <= 0 || value > original || count >= 16) {
      return;
    }
    for (std::size_t i = 0; i < count; ++i) {
      if (candidates[i] == value) {
        return;
      }
    }
    candidates[count++] = static_cast<std::int32_t>(value);
  };
  // The driver's own value is always a candidate: leaving a cap untouched is
  // the most plausible outcome of all.
  add_candidate(original);
  // Powers of two at and below the driver value, down to three steps.
  for (int shift = 0; shift <= 3; ++shift) {
    const std::int64_t step = PowerOfTwoAtMost(original) >> shift;
    if (step > 0) {
      add_candidate(step);
    }
  }
  // Real cards commonly report these exact values.
  for (std::int64_t common : {16, 24, 30, 31, 32, 48, 60, 64, 96, 128, 256}) {
    add_candidate(common);
  }
  // Stay within half the driver value: one real tier down at most. A larger
  // drop would contradict the renderer string the profile advertises (a card
  // claiming 32K textures does not report 4K).
  const std::int64_t bound = original > 8 ? original / 2 : 1;
  const std::int64_t floor_value = original - bound;
  if (count == 0) {
    return original;
  }
  const std::int32_t pick =
      candidates[static_cast<std::size_t>(mixed % count)];
  return pick >= floor_value ? pick : original;
}

}  // namespace lengsuo::fp
