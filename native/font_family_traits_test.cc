// font_family_traits_test - red-first tests for the fonts dimension shape.
//
// docs/cc-task-rulings.md §C fixes the shape: 对标端 stores every overridable
// dimension as {switcher, value, default_value}, and for fonts the value is a
// semicolon-separated family string ("Arial;Arial Black"). Our switch carries
// the profile's own encoding, so the parser must:
//   * derive the online switcher value from the profile's font slot,
//   * treat an empty value as "use the family_mask to decide what changed",
//     which is exactly how the existing --lengsuo-managed-fp switch reads an
//     empty hardware-concurrency field (fp_overrides.cc),
//   * reject the modes that must not perturb anything (noise off, no families).
#include "font_family_traits.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Expect(bool condition, const char* what) {
  if (!condition) {
    ++failures;
    std::fprintf(stderr, "FAIL %s\n", what);
  }
}

#define CHECK(expr) Expect((expr), #expr)

using lengsuo::fonts::FamilyTraits;
using lengsuo::fonts::ParseFamilyTraits;
using lengsuo::fonts::ParseSemicolonFamilies;

// ---------------------------------------------------------------------------
// Semicolon splitting. The value is 对标端's wire form, not a structured list.
// ---------------------------------------------------------------------------
void SplitsOnSemicolons() {
  const auto families = ParseSemicolonFamilies("Arial;Arial Black");
  CHECK(families.size() == 2);
  CHECK(families[0] == "Arial");
  CHECK(families[1] == "Arial Black");
}

void SingleFamilyHasNoSeparator() {
  const auto families = ParseSemicolonFamilies("Arial");
  CHECK(families.size() == 1);
  CHECK(families[0] == "Arial");
}

void EmptyOrBlankValueYieldsNoFamilies() {
  CHECK(ParseSemicolonFamilies("").empty());
  // Whitespace-only elements are dropped, not kept as bogus families.
  CHECK(ParseSemicolonFamilies("  ;  ").empty());
  CHECK(ParseSemicolonFamilies("Arial; ").size() == 1);
}

void DuplicatesCollapse() {
  const auto families = ParseSemicolonFamilies("Arial;Arial;Arial Black");
  CHECK(families.size() == 2);
}

// ---------------------------------------------------------------------------
// Switcher: online value derived from the profile slot. 0 == the profile said
// nothing (or said "private"), so nothing is perturbed -- the same rule the
// GPU dimension uses for slot 0.
// ---------------------------------------------------------------------------
void SlotZeroMeansNative() {
  const auto traits = ParseFamilyTraits(/*slot=*/0, "", 0);
  CHECK(!traits.installed);
  CHECK(!traits.perturbs_metrics);
  CHECK(traits.families.empty());
}

void SwitcherIsRecordedForNonZeroSlot() {
  const auto traits = ParseFamilyTraits(/*slot=*/3, "Arial", 1);
  CHECK(traits.installed);
  CHECK(traits.slot == 3);
}

// ---------------------------------------------------------------------------
// The empty-value convention (mirrors fp_overrides.cc reading an empty
// hardware-concurrency field): when no family list is given, the family_mask
// alone decides whether the metric perturbation is on. mask 0 => off.
// ---------------------------------------------------------------------------
void EmptyValueUsesMask() {
  const auto off = ParseFamilyTraits(/*slot=*/3, "", /*mask=*/0);
  CHECK(off.installed);
  CHECK(!off.perturbs_metrics);  // nothing declares a change

  const auto on = ParseFamilyTraits(/*slot=*/3, "", /*mask=*/1);
  CHECK(on.installed);
  CHECK(on.perturbs_metrics);  // mask says "perturb the metrics"
  CHECK(on.families.empty());  // but no named families -- not a fabricated list
}

void NonEmptyValueEnablesMetrics() {
  const auto traits = ParseFamilyTraits(/*slot=*/3, "Arial;Arial Black", 0);
  CHECK(traits.installed);
  CHECK(traits.perturbs_metrics);  // a named list is itself the statement
  CHECK(traits.families.size() == 2);
}

void MaskOffWithFamiliesStillPerturbs() {
  // switch=replace + a family list is an explicit statement; mask 0 (the
  // service default) must not veto it. This is the case the smoke test drives.
  const auto traits = ParseFamilyTraits(/*slot=*/3, "Arial", 0);
  CHECK(traits.perturbs_metrics);
}

// ---------------------------------------------------------------------------
// Malformed values never install a half-parsed state.
// ---------------------------------------------------------------------------
void ControlBytesRejected() {
  std::string hostile = "Arial";
  hostile.push_back('\n');
  const auto traits = ParseFamilyTraits(/*slot=*/3, hostile, 1);
  CHECK(!traits.installed);
  CHECK(!traits.perturbs_metrics);
}

void OversizedValueRejected() {
  const std::string huge(4097, 'A');
  const auto traits = ParseFamilyTraits(/*slot=*/3, huge, 1);
  CHECK(!traits.installed);
}

void TooManyFamiliesRejected() {
  std::string many;
  for (int i = 0; i < 200; ++i) {
    if (i != 0) {
      many.push_back(';');
    }
    many += "F";
    many += std::to_string(i);
  }
  const auto traits = ParseFamilyTraits(/*slot=*/3, many, 1);
  CHECK(!traits.installed);
}

}  // namespace

int main() {
  SplitsOnSemicolons();
  SingleFamilyHasNoSeparator();
  EmptyOrBlankValueYieldsNoFamilies();
  DuplicatesCollapse();
  SlotZeroMeansNative();
  SwitcherIsRecordedForNonZeroSlot();
  EmptyValueUsesMask();
  NonEmptyValueEnablesMetrics();
  MaskOffWithFamiliesStillPerturbs();
  ControlBytesRejected();
  OversizedValueRejected();
  TooManyFamiliesRejected();
  if (failures != 0) {
    std::fprintf(stderr, "%d assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("font_family_traits_test PASS\n");
  return 0;
}
