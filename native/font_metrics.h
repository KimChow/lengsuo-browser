// font_metrics v1 - deterministic perturbation of the measured glyph advance.
//
// This is the accepted mechanism for the font dimension
// (docs/cc-task-rulings.md §C, docs/font-dimension-reference-approach.md). The
// earlier proposals -- filter the availability set, or alias declared families
// -- were both withdrawn: upstream `FontFaceSet::check()` only judges the
// page's own @font-face registrations and returns true for every system family,
// so "make check() answer false for an absent family" is not a state upstream
// has, and faking it would fabricate a font registry the page can enumerate.
//
// What 对标端 does instead is overlay a deterministic mask on the *real* value:
// its font_fingerprint_mask sits alongside canvas/webgl/audio masks, and the
// open-source base it is built on applies it inside font_cache.cc. The
// observable face of the dimension is therefore not availability but the
// measured advance -- and that is exactly what a canvas `measureText()` reads.
//
// Contract:
//   * Off (enabled == false, or seed == 0, or a non-positive advance) returns
//     the native advance unchanged, bit for bit. A build with no font switch
//     therefore measures identically to upstream.
//   * On, the result is a pure function of (seed, native advance): the same
//     profile yields the same widths in every renderer/worker process, and two
//     profiles yield different widths.
//   * The shift is a whole number of 1/64 px steps (the layout quantum) with
//     |shift| < kMaxAdvanceDelta (1/4 px), so text can never move by a whole
//     pixel: the perturbation is measurable but not visible.
//
// Pure: no Blink, no IPC, no allocation, no static mutable state -- so it is
// unit-testable, and reproducible from tests/font_metrics_ref.py.
//
// Contract doc: docs/font-dimension.md.
#ifndef LENGSUO_NATIVE_FONT_METRICS_H_
#define LENGSUO_NATIVE_FONT_METRICS_H_

#include <cstdint>

namespace lengsuo::fonts {

enum class FontNoiseStatus : std::uint8_t {
  kDisabled,   // native advance returned unchanged
  kPerturbed,  // advance moved by a non-zero number of 1/64 px steps
};

struct FontNoiseConfig {
  bool enabled{};
  std::uint64_t seed{};
  float advance{};
};

struct FontNoiseResult {
  FontNoiseStatus status{FontNoiseStatus::kDisabled};
  float advance{};
};

// 1/64 px: the quantum the native measurement itself uses, so the result is
// exactly representable after Blink's own rounding.
//
// The step bound is chosen from measurement (native/font_probe.py, report §3):
// native widths land only on 1/8 px fractions, i.e. the largest native
// fractional jitter is 0.5 units. +/-4 units is 8x that, so a perturbed width
// is unambiguously separable from native jitter, while 4/64 = 1/16 px per glyph
// is far below one device pixel, so no glyph can visibly move.
constexpr float kAdvanceQuantum = 1.0f / 64.0f;
constexpr int kMaxAdvanceSteps = 4;
constexpr float kMaxAdvanceDelta = kAdvanceQuantum * kMaxAdvanceSteps;

// Perturbs one glyph advance. See the contract above for the off conditions.
FontNoiseResult PerturbAdvance(const FontNoiseConfig& config);

// Exposed for the frozen-vector test only (mirrors tests/font_metrics_ref.py).
std::uint64_t MixForTest(std::uint64_t x);

}  // namespace lengsuo::fonts

#endif  // LENGSUO_NATIVE_FONT_METRICS_H_
