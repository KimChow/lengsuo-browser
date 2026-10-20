// font_metrics_test - red-first tests for the font-metric noise component.
//
// The accepted font dimension (docs/cc-task-rulings.md §C) is a *deterministic
// perturbation of the measured glyph advance*: same profile -> same value,
// different profile -> different value, jitter off -> byte-identical to native.
// This suite pins those three properties before the implementation exists, and
// pins the measured magnitude so a future "tune" cannot silently make the
// perturbation large enough to move glyphs visibly.
//
// The tests are deliberately independent of the browser: the decision function
// is pure, so the same arithmetic can be reproduced in the Python probe and the
// end-to-end smoke test. Build: see CMakeLists.txt (font_metrics_test).
#include "font_metrics.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>

namespace {

int failures = 0;

void Expect(bool condition, const char* what) {
  if (!condition) {
    ++failures;
    std::fprintf(stderr, "FAIL %s\n", what);
  }
}

#define CHECK(expr) Expect((expr), #expr)

using lengsuo::fonts::FontNoiseConfig;
using lengsuo::fonts::FontNoiseStatus;

constexpr std::uint64_t kSeed = 0x0123456789ABCDEFULL;

FontNoiseConfig Config(std::uint64_t seed, float advance, bool enabled = true) {
  FontNoiseConfig config;
  config.enabled = enabled;
  config.seed = seed;
  config.advance = advance;
  return config;
}

// ---------------------------------------------------------------------------
// Red baseline: disabled means byte-identical, always. This is the reference
// the report calls "slots[3]==0 -> identical to native".
// ---------------------------------------------------------------------------
void DisabledIsIdentical() {
  for (float advance : {0.0f, 1.0f, 7.5f, 8.0f, 12.34f, 9.399993f, 123.456f}) {
    const auto result = lengsuo::fonts::PerturbAdvance(Config(kSeed, advance,
                                                              /*enabled=*/false));
    CHECK(result.status == FontNoiseStatus::kDisabled);
    CHECK(result.advance == advance);
  }
}

void ZeroSeedIsDisabled() {
  // A zero seed cannot derive a stream; treat it as "off" rather than
  // perturbing with a predictable constant.
  const auto result = lengsuo::fonts::PerturbAdvance(Config(0, 8.0f));
  CHECK(result.status == FontNoiseStatus::kDisabled);
  CHECK(result.advance == 8.0f);
}

void NonPositiveAdvanceUntouched() {
  for (float advance : {0.0f, -1.0f, -0.25f}) {
    const auto result = lengsuo::fonts::PerturbAdvance(Config(kSeed, advance));
    CHECK(result.status == FontNoiseStatus::kDisabled);
    CHECK(result.advance == advance);
  }
}

// ---------------------------------------------------------------------------
// Determinism: same inputs -> same output, and the function is pure (calling
// it twice must not depend on hidden state, or two renderer processes handed
// one profile would disagree).
// ---------------------------------------------------------------------------
void SameInputIsStable() {
  for (float advance : {0.5f, 2.0f, 8.0f, 15.5f, 42.25f, 199.75f}) {
    const auto first = lengsuo::fonts::PerturbAdvance(Config(kSeed, advance));
    const auto second = lengsuo::fonts::PerturbAdvance(Config(kSeed, advance));
    CHECK(first.status == FontNoiseStatus::kPerturbed);
    CHECK(first.advance == second.advance);
  }
}

void DifferentProfileDiffers() {
  // Two profiles (different seeds) must be distinguishable on at least one of
  // the advances a realistic text produces. Checking a single advance would be
  // brittle: collisions are allowed, systematic equality is not.
  const float advances[] = {2.0f, 4.5f, 7.0f, 8.0f, 11.25f, 13.5f, 16.0f};
  bool any_difference = false;
  for (float advance : advances) {
    const auto a = lengsuo::fonts::PerturbAdvance(Config(0xAAAAAAAAAAAAAAAAULL,
                                                         advance));
    const auto b = lengsuo::fonts::PerturbAdvance(Config(0x5555555555555555ULL,
                                                         advance));
    if (a.advance != b.advance) {
      any_difference = true;
    }
  }
  CHECK(any_difference);
}

// ---------------------------------------------------------------------------
// Magnitude: the perturbation must stay inside the band the report justifies
// from measured native data -- strictly below 1/4 px, so a glyph can never move
// by a whole pixel, yet large enough to survive the 1/64 px layout quantization
// (a whole row must be measurable).
// ---------------------------------------------------------------------------
void MagnitudeBandIsRespected() {
  std::set<float> distinct;
  int shifted = 0;
  const int total = 5000;
  for (std::uint64_t probe = 0; probe < total; ++probe) {
    const float advance = 1.0f + static_cast<float>(probe % 400) * 0.5f;
    const std::uint64_t seed = kSeed ^ (probe * 0x9E3779B97F4A7C15ULL);
    const auto result = lengsuo::fonts::PerturbAdvance(Config(seed, advance));
    CHECK(result.status == FontNoiseStatus::kPerturbed);
    // Every result -- shifted or not -- stays in the band and stays positive.
    // The bound is inclusive: a full 8-step draw lands exactly on the limit.
    const float delta = result.advance - advance;
    CHECK(delta <= lengsuo::fonts::kMaxAdvanceDelta);
    CHECK(delta >= -lengsuo::fonts::kMaxAdvanceDelta);
    CHECK(result.advance >= 0.0f);
    // A non-zero shift is always a whole number of 1/64 steps.
    const float steps = delta / lengsuo::fonts::kAdvanceQuantum;
    CHECK(std::fabs(steps - std::round(steps)) < 1e-4f);
    if (delta != 0.0f) {
      ++shifted;
      distinct.insert(result.advance);
    }
  }
  // A zero draw (the stream lands on 0) is an allowed "leave it native"
  // outcome, but it must be the minority: the dimension exists to move widths.
  CHECK(shifted > total / 2);
  // The perturbation must actually vary: a constant bias would be a fingerprint
  // of its own and would be caught here.
  CHECK(distinct.size() > 100);
}

// ---------------------------------------------------------------------------
// The published reference values. These bytes are what the Python probe and the
// e2e smoke test reproduce; freezing them here means an accidental change to
// the derivation shows up as a fast unit failure, not a mysterious width drift
// in a browser run.
// ---------------------------------------------------------------------------
void ReferenceVectorIsFrozen() {
  const auto result = lengsuo::fonts::PerturbAdvance(Config(kSeed, 8.0f));
  CHECK(result.status == FontNoiseStatus::kPerturbed);
  // 7.9375 == 8.0 - 4/64; the same vector is printed by
  // `python3 -I tests/font_metrics_ref.py` (probe/measurement section).
  CHECK(std::fabs(result.advance - 7.9375f) < 1e-6f);
}

// ---------------------------------------------------------------------------
// Round-trip through the published Python reference. The probe derives the
// stream with the same Mix() as fp_overrides.cc; this test asserts the C++
// side agrees with a literal produced by the reference implementation.
// ---------------------------------------------------------------------------
void MixMatchesReference() {
  // SplitMix64 finalizer over a fixed input. The value is also produced by
  // tests/font_metrics_ref.py::mix, so the two implementations are pinned to
  // one another without either importing the other.
  CHECK(lengsuo::fonts::MixForTest(0) == 0xE220A8397B1DCDAFULL);
  CHECK(lengsuo::fonts::MixForTest(1) == 0x910A2DEC89025CC1ULL);
}

}  // namespace

int main() {
  DisabledIsIdentical();
  ZeroSeedIsDisabled();
  NonPositiveAdvanceUntouched();
  SameInputIsStable();
  DifferentProfileDiffers();
  MagnitudeBandIsRespected();
  ReferenceVectorIsFrozen();
  MixMatchesReference();
  if (failures != 0) {
    std::fprintf(stderr, "%d assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("font_metrics_test PASS\n");
  return 0;
}
