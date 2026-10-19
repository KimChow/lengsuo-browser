// font_metrics v1 - implementation. See font_metrics.h for the contract.
//
// The arithmetic mirrors tests/font_metrics_ref.py line for line: a single
// SplitMix64 finalizer (the same Mix() fp_overrides.cc uses for canvas/audio,
// so every dimension shares one seed-derivation discipline) over the seed
// salted with the quantized native advance. The salt makes two glyphs in one
// string independent while keeping a given glyph fixed for a given profile.

#include "font_metrics.h"

#include <cmath>

namespace lengsuo::fonts {
namespace {

constexpr std::uint64_t kConstMixAdd = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t kConstMixMul1 = 0xBF58476D1CE4E5B9ULL;
constexpr std::uint64_t kConstMixMul2 = 0x94D049BB133111EBULL;
constexpr std::uint64_t kConstSalt = 0x9E3779B97F4A7C15ULL;

}  // namespace

std::uint64_t MixForTest(std::uint64_t x) {
  x += kConstMixAdd;
  x = (x ^ (x >> 30)) * kConstMixMul1;
  x = (x ^ (x >> 27)) * kConstMixMul2;
  return x ^ (x >> 31);
}

FontNoiseResult PerturbAdvance(const FontNoiseConfig& config) {
  FontNoiseResult result;
  result.advance = config.advance;
  // Off conditions: no dimension, no stream, or nothing to perturb.
  if (!config.enabled || config.seed == 0 || !(config.advance > 0.0f)) {
    result.status = FontNoiseStatus::kDisabled;
    return result;
  }
  // Quantize the native advance to the layout quantum, then salt the seed with
  // it. round() (not truncation) so 7.99999 and 8.00001 land on one bucket.
  const std::int64_t quantized =
      static_cast<std::int64_t>(std::llround(config.advance / kAdvanceQuantum));
  const std::uint64_t stream =
      MixForTest(config.seed ^ (static_cast<std::uint64_t>(quantized) *
                                kConstSalt));
  // steps in [-kMaxAdvanceSteps, kMaxAdvanceSteps].
  const int steps =
      static_cast<int>(stream % (2 * kMaxAdvanceSteps + 1)) - kMaxAdvanceSteps;
  if (steps == 0) {
    // A zero draw leaves the advance native. Reporting kDisabled here would be
    // wrong -- the dimension is on -- so it stays kPerturbed with a zero shift.
    result.status = FontNoiseStatus::kPerturbed;
    return result;
  }
  result.status = FontNoiseStatus::kPerturbed;
  result.advance =
      config.advance + static_cast<float>(steps) * kAdvanceQuantum;
  return result;
}

}  // namespace lengsuo::fonts
