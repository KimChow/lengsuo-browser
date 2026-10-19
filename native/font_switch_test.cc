// font_switch v1 tests -- the renderer-side font switch, end to end.
//
// This suite exists because the previous frozen WIP put the font switch in
// native/fp_overrides.cc but never compiled that file: an accessor returned the
// numeric slot instead of the consumed state, and nothing caught it because no
// target built the translation unit. Every path a caller actually takes is
// exercised here against the real switch parser.
//
// It drives the same two entry points the renderer uses:
//   InitFromSwitch("seed:...:f:...")   -> the global seed
//   InitFontsFromSwitch("<slot>:<mask>:<F;F>")  -> PerturbGlyphAdvance()
// and pins the property the ruling cares about: with the font dimension off
// (no switch, slot 0, or dormant), every advance is byte-identical to native.
#include "fp_overrides.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int checks = 0;

void CheckAt(bool ok, int line) {
  ++checks;
  if (!ok) {
    std::printf("FAIL at line %d (check %d)\n", line, checks);
    std::exit(1);
  }
}

#define CHECK(cond) CheckAt((cond), __LINE__)

// The fp switch's canonical 12-field payload, with the fonts slot set.
void InstallSeed(const char* seed, unsigned fonts_slot) {
  std::string payload = std::string(seed) + ":1:1:1:" +
                        std::to_string(fonts_slot) + ":1:1:1:::0:0";
  lengsuo::fp::InitFromSwitch(payload);
}

// Before any switch is seen, nothing perturbs: the default state must be
// indistinguishable from an unmodified renderer.
void NoSwitchMeansNative() {
  for (float advance : {2.0f, 8.0f, 8.5f, 100.25f}) {
    CHECK(lengsuo::fp::PerturbGlyphAdvance(advance) == advance);
  }
}

// The shaping path asks for the advance once per glyph; with the switch off the
// answer is the native value, bit for bit, so measureText is unchanged.
void DormantDimensionIsNative() {
  InstallSeed("0123456789ABCDEF", /*fonts_slot=*/3);
  // No InitFontsFromSwitch yet: the traits are not installed.
  CHECK(lengsuo::fp::PerturbGlyphAdvance(8.5f) == 8.5f);

  // Slot 0: the profile said nothing about fonts.
  CHECK(lengsuo::fp::InitFontsFromSwitch("0:0:"));
  CHECK(lengsuo::fp::PerturbGlyphAdvance(8.5f) == 8.5f);

  // Slot 3 but empty value and mask 0: installed yet dormant.
  CHECK(lengsuo::fp::InitFontsFromSwitch("3:0:"));
  CHECK(lengsuo::fp::PerturbGlyphAdvance(8.5f) == 8.5f);
}

// A named family list is the statement; the advance must move, and the same
// profile must give the same answer twice (determinism across calls).
void NamedFamiliesPerturbDeterministically() {
  InstallSeed("0123456789ABCDEF", /*fonts_slot=*/3);
  CHECK(lengsuo::fp::InitFontsFromSwitch("3:0:Arial;Arial Black"));
  const float first = lengsuo::fp::PerturbGlyphAdvance(8.5f);
  const float second = lengsuo::fp::PerturbGlyphAdvance(8.5f);
  CHECK(first == second);       // pure: no hidden state between calls
  CHECK(first != 8.5f);         // a statement actually moved the metric
  CHECK(first > 8.5f - 0.25f);  // and stayed inside the published band
  CHECK(first < 8.5f + 0.25f);
}

// Two profiles must diverge. Different seeds over the same advance are the
// proxy for "same profile twice, different profiles apart".
void DifferentProfilesDiffer() {
  InstallSeed("0123456789ABCDEF", /*fonts_slot=*/3);
  CHECK(lengsuo::fp::InitFontsFromSwitch("3:0:Arial"));
  const float a = lengsuo::fp::PerturbGlyphAdvance(6.0f);
  InstallSeed("FEDCBA9876543210", /*fonts_slot=*/3);
  CHECK(lengsuo::fp::InitFontsFromSwitch("3:0:Arial"));
  const float b = lengsuo::fp::PerturbGlyphAdvance(6.0f);
  CHECK(a != b);
}

// The mask alone can turn the dimension on (the "use the mask" convention).
void MaskAloneEnablesTheDimension() {
  InstallSeed("0123456789ABCDEF", /*fonts_slot=*/3);
  CHECK(lengsuo::fp::InitFontsFromSwitch("3:1:"));
  const float perturbed = lengsuo::fp::PerturbGlyphAdvance(7.25f);
  CHECK(perturbed != 7.25f);
}

// A malformed payload installs nothing: measurement stays native instead of
// degrading to a half-applied font dimension.
void MalformedPayloadInstallsNothing() {
  InstallSeed("0123456789ABCDEF", /*fonts_slot=*/3);
  CHECK(!lengsuo::fp::InitFontsFromSwitch("no-colons"));
  CHECK(lengsuo::fp::PerturbGlyphAdvance(8.5f) == 8.5f);
  CHECK(!lengsuo::fp::InitFontsFromSwitch("x:0:Arial"));  // non-numeric slot
  CHECK(lengsuo::fp::PerturbGlyphAdvance(8.5f) == 8.5f);
}

// Re-initialising replaces the traits wholesale.
void ReinitialiseReplacesTheTraits() {
  InstallSeed("0123456789ABCDEF", /*fonts_slot=*/3);
  CHECK(lengsuo::fp::InitFontsFromSwitch("3:0:Arial"));
  CHECK(lengsuo::fp::FontTraits().installed);
  CHECK(lengsuo::fp::FontTraits().families.size() == 1);
  // Back to dormant: the latest statement wins.
  CHECK(lengsuo::fp::InitFontsFromSwitch("3:0:"));
  CHECK(lengsuo::fp::FontTraits().installed);
  CHECK(!lengsuo::fp::FontTraits().perturbs_metrics);
  CHECK(lengsuo::fp::PerturbGlyphAdvance(8.5f) == 8.5f);
}

// The sw/sv/sf triple must survive the switch intact: the families the browser
// wrote are the families the renderer sees (the "双向" pin for this dimension).
void SwitchValueRoundTripsFamilies() {
  InstallSeed("0123456789ABCDEF", /*fonts_slot=*/3);
  CHECK(lengsuo::fp::InitFontsFromSwitch("3:0:Arial;Arial Black"));
  const auto& families = lengsuo::fp::FontTraits().families;
  CHECK(families.size() == 2);
  CHECK(families[0] == "Arial");
  CHECK(families[1] == "Arial Black");
}

}  // namespace

int main() {
  NoSwitchMeansNative();
  DormantDimensionIsNative();
  NamedFamiliesPerturbDeterministically();
  DifferentProfilesDiffer();
  MaskAloneEnablesTheDimension();
  MalformedPayloadInstallsNothing();
  ReinitialiseReplacesTheTraits();
  SwitchValueRoundTripsFamilies();
  std::printf("PASS %d assertions\n", checks);
  return 0;
}
