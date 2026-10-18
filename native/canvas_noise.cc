// canvas_noise v1 — implementation. See canvas_noise.h for the contract.
//
// The arithmetic mirrors tests/canvas_ref/gen_canvas_ref.py line for line:
// all state is uint64 with wrapping (unsigned overflow IS the specified
// modular arithmetic), the stream advances exactly two mixes per pixel in
// row-major order regardless of transparency, and padding bytes are never
// read or written.

#include "canvas_noise.h"

namespace lengsuo::canvas {
namespace {

constexpr std::uint64_t kConstA = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t kConstB = 0xC2B2AE3D27D4EB4FULL;
constexpr std::uint64_t kConstMixSeed = 0xA24BAED5ULL;
constexpr std::uint64_t kConstHi = 0x517CC1B727220A95ULL;
constexpr std::uint64_t kConstLo = 0x4CF8E355ULL;

struct MixPair {
  std::uint64_t a;
  std::uint64_t b;
};

std::uint64_t Rotl64(std::uint64_t x, int k) {
  return (x << k) | (x >> (64 - k));
}

MixPair Mix(std::uint64_t a, std::uint64_t b) {
  a += b;
  a = Rotl64(a, 23);
  a += b + kConstA;
  b += a;
  b = Rotl64(b, 45);
  b += a + kConstB;
  a += Rotl64(b, 31);
  return {a, b};
}

bool ValidateGeometry(std::uint32_t width, std::uint32_t height,
                      std::uint64_t stride, std::uint64_t buffer_size,
                      std::size_t src_size, std::size_t dst_size,
                      CanvasNoiseStatus& status) {
  const std::uint64_t pixels =
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  if (width == 0 || height == 0 || width > 65535 || height > 65535 ||
      pixels > (1ULL << 28)) {
    status = CanvasNoiseStatus::kInvalidGeometry;
    return false;
  }
  const std::uint64_t min_stride =
      static_cast<std::uint64_t>(width) * 4ULL;
  if (stride < min_stride || stride > (1ULL << 27) - 4ULL) {
    status = CanvasNoiseStatus::kInvalidGeometry;
    return false;
  }
  if (stride * height > buffer_size || buffer_size > src_size ||
      buffer_size > dst_size) {
    status = CanvasNoiseStatus::kBufferTooSmall;
    return false;
  }
  if (buffer_size == 0 || src_size == 0 || dst_size == 0) {
    status = CanvasNoiseStatus::kBufferTooSmall;
    return false;
  }
  return true;
}

}  // namespace

CanvasNoiseResult ApplyCanvasNoiseV1(const CanvasNoiseConfig& config,
                                     std::span<const std::uint8_t> src,
                                     std::span<std::uint8_t> dst,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     std::uint64_t stride,
                                     std::uint64_t buffer_size) {
  CanvasNoiseResult result;
  if (config.algorithm_version != 1) {
    result.status = CanvasNoiseStatus::kUnsupportedVersion;
    return result;
  }
  if (!ValidateGeometry(width, height, stride, buffer_size, src.size(),
                        dst.size(), result.status)) {
    return result;
  }
  if (!config.enabled) {
    // Explicit off path: validated but never writes; whether to copy is the
    // caller's decision (contract: no hidden copy side effect).
    return result;
  }

  MixPair init = Mix(config.seed,
                     (static_cast<std::uint64_t>(config.slot) << 32) ^
                         kConstMixSeed);
  std::uint64_t hi = init.a ^ config.seed ^ kConstHi ^
                     (static_cast<std::uint64_t>(width) << 16) ^
                     static_cast<std::uint64_t>(height);
  std::uint64_t lo = (init.b + config.seed + (stride << 3)) ^ kConstLo ^
                     (static_cast<std::uint64_t>(config.slot) * 0x9E3779B9ULL);
  // The reference's derive() returns mix(hi, lo) — one extra mix that is part
  // of derivation itself (see gen_canvas_ref.py::derive). The per-pixel loop
  // then consumes two more mixes per pixel, row-major.
  MixPair derived = Mix(hi, lo);
  hi = derived.a;
  lo = derived.b;

  for (std::uint32_t row = 0; row < height; ++row) {
    const std::size_t base = static_cast<std::size_t>(row) *
                             static_cast<std::size_t>(stride);
    for (std::uint32_t col = 0; col < width; ++col) {
      const std::size_t p = base + static_cast<std::size_t>(col) * 4ULL;
      MixPair s = Mix(hi, lo);
      const std::uint64_t v1 = s.a ^ s.b;
      // Second mix advances the stream only; the mild variant never scales
      // the delta by v2 (that was the rejected plan-B formula).
      s = Mix(s.a, s.b);
      hi = s.a;
      lo = s.b;

      const std::uint8_t alpha = src[p + 3];
      if (alpha == 0) {
        // Fully transparent: all four bytes preserved byte-for-byte. When
        // dst aliases src this is naturally a no-op; when it does not, the
        // copy is still required by the contract.
        dst[p + 0] = src[p + 0];
        dst[p + 1] = src[p + 1];
        dst[p + 2] = src[p + 2];
        dst[p + 3] = src[p + 3];
        ++result.preserved_pixels;
        continue;
      }

      const std::int64_t d =
          static_cast<std::int64_t>(v1 % 27ULL) - 13;  // [-13, 13]
      // The reference uses Python floor division (`d * alpha // 255`), which
      // rounds toward negative infinity; C++ `/` truncates toward zero. The
      // difference only shows for negative numerators (e.g. -1152/255 is -5
      // floored but -4 truncated), so division is implemented explicitly.
      const std::int64_t numerator = d * static_cast<std::int64_t>(alpha);
      const std::int64_t delta =
          numerator >= 0 ? numerator / 255
                         : -((-numerator + 254) / 255);
      bool changed = false;
      for (int c = 0; c < 3; ++c) {
        const std::int64_t value =
            static_cast<std::int64_t>(src[p + c]) + delta;
        const std::uint8_t clamped = value < 0   ? 0
                                     : value > 255 ? 255
                                                   : static_cast<std::uint8_t>(value);
        changed = changed || clamped != src[p + c];
        dst[p + c] = clamped;
      }
      dst[p + 3] = alpha;  // alpha strictly preserved
      if (changed) {
        ++result.changed_pixels;
      }
    }
  }
  return result;
}

}  // namespace lengsuo::canvas
