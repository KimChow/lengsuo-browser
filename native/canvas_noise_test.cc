// canvas_noise v1 tests — golden bytes, guard semantics, counters, in-place.
//
// The golden vectors come from tests/canvas_ref/golden_fixture.inc (frozen
// Python-reference bytes). The C++ under test must match them byte for byte;
// it must never recompute expectations at runtime (no Python is invoked).
// Style follows native/profile_wire_test.cc: self-contained assertions.

#include "canvas_noise.h"

#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

#include "golden_fixture.inc"

namespace {

using lengsuo::canvas::ApplyCanvasNoiseV1;
using lengsuo::canvas::CanvasNoiseConfig;
using lengsuo::canvas::CanvasNoiseResult;
using lengsuo::canvas::CanvasNoiseStatus;

int checks = 0;

void Check(bool ok) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL " << checks << '\n';
    std::exit(1);
  }
}

constexpr std::uint8_t kSentinel = 0xA5;

CanvasNoiseConfig V1Config(std::uint64_t seed, std::uint32_t slot) {
  return CanvasNoiseConfig{1, true, seed, slot};
}

CanvasNoiseResult Run(const CanvasGoldenCase& g, std::vector<std::uint8_t>& dst,
                      bool enabled) {
  CanvasNoiseConfig config{1, enabled, g.seed, g.slot};
  return ApplyCanvasNoiseV1(
      config, std::span<const std::uint8_t>(g.src, g.size),
      std::span<std::uint8_t>(dst.data(), dst.size()), g.width, g.height,
      g.stride, static_cast<std::uint64_t>(g.size));
}

// True when the row-tail padding of `dst` still equals `sentinel`.
bool PaddingUntouched(const CanvasGoldenCase& g,
                      const std::vector<std::uint8_t>& dst,
                      std::uint8_t sentinel) {
  for (std::uint32_t row = 0; row < g.height; ++row) {
    const std::size_t base =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(g.stride);
    const std::size_t tail = base + static_cast<std::size_t>(g.width) * 4ULL;
    for (std::size_t i = tail;
         i < static_cast<std::size_t>(row + 1) * g.stride; ++i) {
      if (dst[i] != sentinel) {
        return false;
      }
    }
  }
  return true;
}

bool AllBytes(const std::vector<std::uint8_t>& buf, std::uint8_t v) {
  for (const std::uint8_t b : buf) {
    if (b != v) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  // ---- 1. Frozen golden bytes on the image area; padding never written. ---
  for (const CanvasGoldenCase* g : kGoldenCases) {
    // Pre-seed dst with the source image bytes; poison padding with sentinel.
    std::vector<std::uint8_t> dst(g->src, g->src + g->size);
    for (std::uint32_t row = 0; row < g->height; ++row) {
      const std::size_t base =
          static_cast<std::size_t>(row) * static_cast<std::size_t>(g->stride);
      for (std::size_t i = base + static_cast<std::size_t>(g->width) * 4ULL;
           i < static_cast<std::size_t>(row + 1) * g->stride; ++i) {
        dst[i] = kSentinel;
      }
    }
    const CanvasNoiseResult result = Run(*g, dst, true);
    Check(result.status == CanvasNoiseStatus::kOk);
    for (std::uint32_t row = 0; row < g->height; ++row) {
      const std::size_t base =
          static_cast<std::size_t>(row) * static_cast<std::size_t>(g->stride);
      for (std::uint32_t col = 0; col < g->width; ++col) {
        const std::size_t p = base + static_cast<std::size_t>(col) * 4ULL;
        for (int c = 0; c < 4; ++c) {
          if (dst[p + c] != g->expected[p + c]) {
            std::cerr << "golden mismatch in " << g->name << " row " << row
                      << " col " << col << " byte " << c << ": got "
                      << static_cast<int>(dst[p + c]) << " want "
                      << static_cast<int>(g->expected[p + c]) << '\n';
            std::exit(1);
          }
        }
      }
    }
    Check(PaddingUntouched(*g, dst, kSentinel));
  }

  // ---- 2. enabled == false: validated, but dst is never touched. ----------
  for (const CanvasGoldenCase* g : kGoldenCases) {
    std::vector<std::uint8_t> dst(g->size, kSentinel);
    const CanvasNoiseResult result = Run(*g, dst, false);
    Check(result.status == CanvasNoiseStatus::kOk);
    Check(AllBytes(dst, kSentinel));
  }
  {
    const std::vector<std::uint8_t> src(16, 1);
    std::vector<std::uint8_t> dst(16, kSentinel);
    const CanvasNoiseConfig bad{7, false, 1, 0};
    Check(ApplyCanvasNoiseV1(bad, src, dst, 1, 1, 4, 16).status ==
          CanvasNoiseStatus::kUnsupportedVersion);
  }

  // ---- 3. Validation fails closed without writing. ------------------------
  {
    const std::vector<std::uint8_t> src(64, 7);
    std::vector<std::uint8_t> dst(64, kSentinel);
    const CanvasNoiseConfig good = V1Config(42, 2);
    Check(ApplyCanvasNoiseV1(good, src, dst, 0, 1, 4, 64).status ==
          CanvasNoiseStatus::kInvalidGeometry);
    Check(ApplyCanvasNoiseV1(good, src, dst, 4, 1, 1ULL << 27, 64).status ==
          CanvasNoiseStatus::kInvalidGeometry);
    Check(ApplyCanvasNoiseV1(good, src, dst, 4, 4, 16, 63).status ==
          CanvasNoiseStatus::kBufferTooSmall);
    Check(ApplyCanvasNoiseV1(good, src, dst, 4, 4, 16, 65).status ==
          CanvasNoiseStatus::kBufferTooSmall);
    Check(ApplyCanvasNoiseV1(good, src, dst, 4, 4, 16, 0).status ==
          CanvasNoiseStatus::kBufferTooSmall);
    Check(AllBytes(dst, kSentinel));
  }

  // ---- 4. Counters: changed_pixels / preserved_pixels. --------------------
  {
    const CanvasGoldenCase& g = kCase_v4_4x1_alpha;
    std::vector<std::uint8_t> dst(g.size, kSentinel);
    const CanvasNoiseResult result = Run(g, dst, true);
    Check(result.status == CanvasNoiseStatus::kOk);
    std::uint64_t changed = 0;
    std::uint64_t preserved = 0;
    for (std::uint32_t col = 0; col < g.width; ++col) {
      const std::size_t p = col * 4ULL;
      if (g.src[p + 3] == 0) {
        ++preserved;
        continue;
      }
      bool differs = false;
      for (int c = 0; c < 3; ++c) {
        differs = differs || dst[p + c] != g.src[p + c];
      }
      if (differs) {
        ++changed;
      }
    }
    Check(result.preserved_pixels == preserved);
    Check(result.changed_pixels == changed);
  }

  // ---- 5. In-place equals two-buffer output (image area). -----------------
  // Padding is never written, so it keeps whatever the caller seeded; the
  // equivalence claim covers the image area only.
  for (const CanvasGoldenCase* g : kGoldenCases) {
    std::vector<std::uint8_t> two_buffer(g->src, g->src + g->size);
    Run(*g, two_buffer, true);
    std::vector<std::uint8_t> in_place(g->src, g->src + g->size);
    CanvasNoiseConfig config{1, true, g->seed, g->slot};
    ApplyCanvasNoiseV1(config, in_place, in_place, g->width, g->height,
                       g->stride, static_cast<std::uint64_t>(g->size));
    bool image_equal = true;
    for (std::uint32_t row = 0; row < g->height; ++row) {
      const std::size_t base =
          static_cast<std::size_t>(row) * static_cast<std::size_t>(g->stride);
      for (std::size_t i = base;
           i < base + static_cast<std::size_t>(g->width) * 4ULL; ++i) {
        if (in_place[i] != two_buffer[i]) {
          image_equal = false;
        }
      }
    }
    Check(image_equal);
  }

  // ---- 6. Repeated calls are deterministic. -------------------------------
  {
    const CanvasGoldenCase& g = kCase_v5_16x16;
    std::vector<std::uint8_t> first(g.size, kSentinel);
    std::vector<std::uint8_t> second(g.size, kSentinel);
    Run(g, first, true);
    Run(g, second, true);
    Check(first == second);
  }

  // ---- 7. Contract property: RGB delta <= 13, alpha strictly preserved. ---
  for (const CanvasGoldenCase* g : kGoldenCases) {
    for (std::uint32_t row = 0; row < g->height; ++row) {
      for (std::uint32_t col = 0; col < g->width; ++col) {
        const std::size_t p = static_cast<std::size_t>(row) *
                                  static_cast<std::size_t>(g->stride) +
                              static_cast<std::size_t>(col) * 4ULL;
        if (g->src[p + 3] == 0) {
          continue;
        }
        for (int c = 0; c < 3; ++c) {
          const int diff = std::abs(static_cast<int>(g->expected[p + c]) -
                                    static_cast<int>(g->src[p + c]));
          if (diff > 13) {
            std::cerr << "delta bound violated in " << g->name << " pixel "
                      << col << " channel " << c << ": " << diff << '\n';
            std::exit(1);
          }
        }
        if (g->expected[p + 3] != g->src[p + 3]) {
          std::cerr << "alpha changed in " << g->name << " pixel " << col
                    << '\n';
          std::exit(1);
        }
      }
    }
    ++checks;
  }

  std::cout << "PASS " << checks << " checks\n";
  return 0;
}
