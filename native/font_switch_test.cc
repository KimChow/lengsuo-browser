// font_switch v1 tests -- the renderer-side font switch, end to end.
//
// This suite exists because the previous frozen WIP put the font switch in
// native/fp_overrides.cc but never compiled that file: a `Fonts()` accessor
// returned `&g_fonts` (the numeric slot) instead of the availability set, and
// nothing caught it because no target built the translation unit. Every path a
// caller actually takes is exercised here against the real switch parser.
//
// It drives the same entry point the renderer uses:
//   InitFontsFromSwitch("Arial,Noto Sans")  ->  FontSubstituteFor(...)
// and pins the two directions the ruling called out for the numeric switch:
// the payload the browser sends and the answer the renderer gives must agree.
#include "fp_overrides.h"

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

std::vector<std::string_view> Candidates() {
  static const std::vector<std::string> names = {"Noto Sans", "Noto Serif"};
  std::vector<std::string_view> views;
  for (const auto& name : names) {
    views.emplace_back(name);
  }
  return views;
}

// Before any switch is seen the accessor is null and nothing substitutes: the
// default state must be indistinguishable from an unmodified renderer.
void NoSwitchMeansNative() {
  // A fresh process would have no install; emulate by only reading the accessor
  // before any Init call in this sequence.
  const auto candidates = Candidates();
  const std::string picked =
      lengsuo::fp::FontSubstituteFor("__LengsuoDeclaredAlpha__", candidates);
  CHECK(picked.empty());
}

// The switch names the declared families; the renderer must answer exactly the
// membership the payload encoded. This is the "双向" check: the switch value
// the browser writes and the decision the renderer reads are one table.
void SwitchValueRoundTripsMembership() {
  CHECK(lengsuo::fp::InitFontsFromSwitch("Arial,Noto Sans"));
  const auto* fonts = lengsuo::fp::Fonts();
  CHECK(fonts != nullptr);
  CHECK(fonts->Available("Arial") ==
        lengsuo::fp::FontFaceSetStatus::kPresent);
  CHECK(fonts->Available("Noto Sans") ==
        lengsuo::fp::FontFaceSetStatus::kPresent);
  CHECK(fonts->Available("Helvetica") ==
        lengsuo::fp::FontFaceSetStatus::kAbsent);
}

// A declared family the host lacks gets a real candidate; an undeclared family
// gets nothing. This is the pair the browser smoke test observes as a
// measureText difference.
void DeclaredGetsSubstituteUndeclaredDoesNot() {
  CHECK(lengsuo::fp::InitFontsFromSwitch("__LengsuoDeclaredAlpha__,Arial"));
  const auto candidates = Candidates();
  const std::string declared =
      lengsuo::fp::FontSubstituteFor("__LengsuoDeclaredAlpha__", candidates);
  CHECK(!declared.empty());
  CHECK(declared == "Noto Sans" || declared == "Noto Serif");
  CHECK(lengsuo::fp::FontSubstituteFor("__LengsuoUndeclared__", candidates)
            .empty());
}

// A malformed payload installs nothing: the accessor stays null and no family
// is substituted, so a broken switch degrades to native instead of to a
// half-applied font list.
void MalformedPayloadInstallsNothing() {
  CHECK(!lengsuo::fp::InitFontsFromSwitch("Arial,,Noto Sans"));
  CHECK(lengsuo::fp::Fonts() == nullptr);
  CHECK(lengsuo::fp::FontSubstituteFor("Arial", Candidates()).empty());
}

// Re-initialising replaces the set wholesale, including with an empty one: the
// profile's latest statement wins, and "no fonts" is a statement, not silence.
void ReinitialiseReplacesTheSet() {
  CHECK(lengsuo::fp::InitFontsFromSwitch("Arial"));
  CHECK(lengsuo::fp::Fonts()->size() == 1);
  CHECK(lengsuo::fp::InitFontsFromSwitch(""));
  CHECK(lengsuo::fp::Fonts() != nullptr);
  CHECK(lengsuo::fp::Fonts()->empty());
  CHECK(lengsuo::fp::FontSubstituteFor("Arial", Candidates()).empty());
}

// An empty candidate pool (a host that offered no families) yields no
// substitution rather than an invented name.
void EmptyCandidatePoolYieldsNothing() {
  CHECK(lengsuo::fp::InitFontsFromSwitch("Arial"));
  const std::vector<std::string_view> none;
  CHECK(lengsuo::fp::FontSubstituteFor("Arial", none).empty());
}

}  // namespace

int main() {
  NoSwitchMeansNative();
  SwitchValueRoundTripsMembership();
  DeclaredGetsSubstituteUndeclaredDoesNot();
  MalformedPayloadInstallsNothing();
  ReinitialiseReplacesTheSet();
  EmptyCandidatePoolYieldsNothing();
  std::printf("PASS %d assertions\n", checks);
  return 0;
}
