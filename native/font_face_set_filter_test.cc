// font_face_set_filter v1 tests -- the font dimension's consumption point.
//
// The font slot (profile slots[3]) has been decoded since schema 1 but had no
// consumer: changing it changed nothing observable. This suite pins the
// behaviour of the availability set that the renderer will consult, in the
// table-driven style of native/profile_catalog_gpu_test.cc.
//
// Contract (docs/font-dimension.md):
//   * no injected set  -> every query answers "native" (no opinion)
//   * set injected     -> the listed families are available, every other
//                         family is unavailable, and the two are never mixed
//                         up in either direction
//   * an empty injected set is a real statement: nothing is available. It must
//     stay distinguishable from "no injection", or a profile that drops the
//     font dimension would silently fall back to the native system list.
#include "font_face_set_filter.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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

// The filter's two whole-set answers, spelled once so the assertions read as
// "this family is available / this family is not" rather than as enum names.
constexpr auto kInit = lengsuo::fp::FontFaceSetStatus::kInit;
constexpr auto kNotInit = lengsuo::fp::FontFaceSetStatus::kNotInitialized;
constexpr auto kPresent = lengsuo::fp::FontFaceSetStatus::kPresent;
constexpr auto kAbsent = lengsuo::fp::FontFaceSetStatus::kAbsent;

// A filter that was never initialized must defer to the platform for every
// family: an absent switch may never change what a page observes.
void NeverInitializedDefersToPlatform() {
  lengsuo::fp::FontFaceSetFilter filter;
  CHECK(filter.status() == kNotInit);
  CHECK(filter.empty());
  for (const char* family :
       {"Arial", "Times New Roman", "Noto Sans CJK SC", "", "  "}) {
    CHECK(filter.Available(family) == kNotInit);
  }
}

// Zero and one entries are both valid statements.
void ParsesEveryCount() {
  lengsuo::fp::FontFaceSetFilter filter;
  filter.Init("");
  CHECK(filter.status() == kInit);
  CHECK(filter.empty());
  CHECK(filter.Available("Arial") == kAbsent);

  CHECK(filter.Add("Arial"));
  CHECK(filter.Available("Arial") == kPresent);
  CHECK(filter.Available("arial") == kAbsent);
  CHECK(filter.Available("Ari") == kAbsent);
  CHECK(filter.Available("Arial Black") ==
        kAbsent);
}

// The switch payload is a comma-separated family list. Order must not matter
// for membership, and duplicates must not create phantom entries.
void IsOrderAndDuplicateInsensitive() {
  lengsuo::fp::FontFaceSetFilter first;
  first.Init("Arial,Times New Roman,DejaVu Sans");
  lengsuo::fp::FontFaceSetFilter second;
  second.Init("DejaVu Sans,Arial,Times New Roman");
  lengsuo::fp::FontFaceSetFilter third;
  third.Init("Arial,Times New Roman,DejaVu Sans,Arial");

  for (const char* family :
       {"Arial", "Times New Roman", "DejaVu Sans", "Helvetica"}) {
    CHECK(first.Available(family) == second.Available(family));
    CHECK(first.Available(family) == third.Available(family));
  }
  CHECK(first.empty() == third.empty());
  CHECK(first.size() == third.size());
}

// Round-trip: whatever was added is available, whatever was not is not. This
// is the property the browser-side page assertions also check, so it is
// asserted here in both directions and over a list that shares prefixes.
void MembershipIsExactInBothDirections() {
  lengsuo::fp::FontFaceSetFilter filter;
  filter.Init("");
  const std::vector<std::string> listed = {
      "Arial", "Arial Narrow", "Arial Unicode MS", "Courier New",
      "DejaVu Sans", "DejaVu Sans Mono", "Liberation Serif", "Noto Sans CJK SC",
      "Symbol", "Wingdings", "Z003", "abc", "Abc"};
  for (const auto& family : listed) {
    CHECK(filter.Add(family));
  }
  CHECK(filter.size() == listed.size());
  for (const auto& family : listed) {
    CHECK(filter.Available(family) == kPresent);
  }
  for (const char* family :
       {"", "a", "Ari", "Arial ", " Arial", "ARIAL", "Wingding", "Symbols",
        "Noto Sans CJK", "Liberation Sans"}) {
    CHECK(filter.Available(family) == kAbsent);
  }
}

// Payloads the format does not allow are dropped whole rather than partially
// applied: half a font list is worse than none, because it would claim the
// profile was applied while pages still see families that were deleted.
void MalformedPayloadIsRejectedWhole() {
  lengsuo::fp::FontFaceSetFilter filter;
  filter.Init("Arial,Times New Roman");
  CHECK(filter.Available("Arial") == kPresent);

  CHECK(!filter.Init("Arial,,Times New Roman"));  // empty element
  CHECK(filter.status() == kNotInit);
  CHECK(filter.empty());
  CHECK(filter.size() == 0);
  CHECK(filter.Available("Arial") == kNotInit);

  CHECK(filter.Init(""));
  CHECK(!filter.Add(""));
  CHECK(!filter.Add(std::string(1, '\x1f')));
  CHECK(!filter.Add(std::string(1, '\x7f')));
  CHECK(filter.Add("Arial,"));  // a plain name: only Init() splits on commas
  CHECK(!filter.Add("Arial\n"));
  CHECK(filter.size() == 1);
  CHECK(filter.Available("Arial") == kAbsent);
  CHECK(filter.Available("Arial,") == kPresent);
}

// Entries are bounded so a hostile profile cannot grow renderer memory.
void BoundsAreEnforced() {
  lengsuo::fp::FontFaceSetFilter filter;
  filter.Init("");
  for (std::size_t i = 0; i < lengsuo::fp::FontFaceSetFilter::kMaxEntries;
       ++i) {
    CHECK(filter.Add("Family" + std::to_string(i)));
  }
  CHECK(filter.size() == lengsuo::fp::FontFaceSetFilter::kMaxEntries);
  CHECK(!filter.Add("OneTooMany"));
  CHECK(filter.size() == lengsuo::fp::FontFaceSetFilter::kMaxEntries);

  lengsuo::fp::FontFaceSetFilter long_name;
  long_name.Init("");
  CHECK(!long_name.Add(std::string(lengsuo::fp::FontFaceSetFilter::kMaxName +
                                       1,
                                   'a')));
  CHECK(long_name.Add(std::string(lengsuo::fp::FontFaceSetFilter::kMaxName,
                                  'a')));
  // The payload itself is bounded, so a huge switch cannot be parsed partially.
  lengsuo::fp::FontFaceSetFilter huge;
  CHECK(!huge.Init(std::string(
      lengsuo::fp::FontFaceSetFilter::kMaxPayload + 1, 'a')));
  CHECK(huge.status() == kNotInit);
}

// "No injection" and "empty set" are different answers and must not collapse:
// kNotInitialized keeps the native list, kAbsent hides the family.
void EmptySetIsNotTheSameAsNoInjection() {
  lengsuo::fp::FontFaceSetFilter injected_empty;
  injected_empty.Init("");
  lengsuo::fp::FontFaceSetFilter uninjected;
  CHECK(injected_empty.status() == kInit);
  CHECK(uninjected.status() == kNotInit);
  CHECK(injected_empty.Available("Arial") ==
        kAbsent);
  CHECK(uninjected.Available("Arial") == kNotInit);
  CHECK(injected_empty.Available("Arial") != uninjected.Available("Arial"));
}

// Same payload, same answers: the filter must be a pure function of its input
// so that two renderer processes given one profile agree.
void ParsingIsDeterministic() {
  const std::string payload = "Arial,Courier New,Noto Sans CJK SC";
  lengsuo::fp::FontFaceSetFilter first;
  lengsuo::fp::FontFaceSetFilter second;
  first.Init(payload);
  second.Init(payload);
  CHECK(first.size() == second.size());
  for (const char* family :
       {"Arial", "Courier New", "Noto Sans CJK SC", "Courier"}) {
    CHECK(first.Available(family) == second.Available(family));
  }
}

}  // namespace

int main() {
  NeverInitializedDefersToPlatform();
  ParsesEveryCount();
  IsOrderAndDuplicateInsensitive();
  MembershipIsExactInBothDirections();
  MalformedPayloadIsRejectedWhole();
  BoundsAreEnforced();
  EmptySetIsNotTheSameAsNoInjection();
  ParsingIsDeterministic();
  std::printf("PASS %d assertions\n", checks);
  return 0;
}
