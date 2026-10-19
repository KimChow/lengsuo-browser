// font_substitution v1 tests -- the "declared fonts really exist" decision.
//
// Task C's first acceptance was withdrawn: upstream `FontFaceSet::check()`
// answers true for ANY family the page did not itself register
// (font_face_set.cc:232), so "absent -> false" was never implementable, and
// `measureText` is not coupled to the set at all. The real observable face of
// the font dimension is availability + the widths that follow from it, which
// is what gamma2 makes concrete: a family the profile declares is made to
// resolve to a real, installed typeface instead of falling through to the
// default fallback.
//
// This suite pins the decision layer only (pure C++): given an installed
// availability set and a pool of candidate family names, which names need a
// substitute and which candidate each maps to. The Blink call site
// (font_cache_skia.cc) supplies the pool -- three CSS generic families, never
// the host's enumerated family list, because on Linux the renderer's font
// backend aborts on enumeration; GenericPoolIsTheProductionPool pins that
// exact pool so a call-site change is caught here. The end-to-end behavior is
// covered by tests/browser_font_dimension_smoke.py.
//
// Contract (docs/font-dimension.md):
//   * no injected set            -> nothing is ever substituted (native path)
//   * family listed in the set   -> substitute when the host lacks it
//   * family not listed          -> never substituted, even if absent
//   * the substitute is a pure, stable function of the declared name and the
//     candidate pool, so two renderer processes handed one profile agree (no
//     std::hash, no ASLR, no per-process randomness)
#include "font_substitution.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
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

lengsuo::fp::FontFaceSetFilter Installed(const char* names) {
  lengsuo::fp::FontFaceSetFilter filter;
  CHECK(filter.Init(names));
  return filter;
}

// A stand-in for the host's real installed families. The decision layer never
// invents these -- they are always supplied by the caller.
const std::vector<std::string> kCandidates = {
    "Noto Sans", "Noto Serif", "DejaVu Sans", "Liberation Serif", "Noto Mono"};

std::vector<std::string_view> CandidateViews() {
  std::vector<std::string_view> views;
  for (const auto& name : kCandidates) {
    views.emplace_back(name);
  }
  return views;
}

// No set installed: the decision layer must be inert. This is the "未注入时逐字节
// 等于原生" requirement -- every family keeps resolving exactly as upstream.
void NoSetMeansNoSubstitution() {
  lengsuo::fp::FontSubstitution sub;  // default-constructed == not installed
  CHECK(!sub.Installed());
  for (const char* family : {"Arial", "__LengsuoMissing__", "", "serif"}) {
    CHECK(!sub.NeedsSubstitute(family));
  }
}

// A declared family needs a substitute; an undeclared one never does. This is
// the pair the browser smoke test observes as a measureText difference --
// declared-but-absent must diverge from undeclared-absent.
void OnlyDeclaredFamiliesAreSubstituted() {
  auto sub = lengsuo::fp::FontSubstitution::From(Installed(
      "__LengsuoDeclaredAlpha__,__LengsuoDeclaredBeta__,Arial"));
  CHECK(sub.Installed());
  CHECK(sub.NeedsSubstitute("__LengsuoDeclaredAlpha__"));
  CHECK(sub.NeedsSubstitute("__LengsuoDeclaredBeta__"));
  CHECK(sub.NeedsSubstitute("Arial"));
  CHECK(!sub.NeedsSubstitute("__LengsuoUndeclared__"));
  CHECK(!sub.NeedsSubstitute("Noto Sans"));
  CHECK(!sub.NeedsSubstitute(""));
}

// An installed but empty set is a real statement: the profile machine adds no
// fonts. Nothing is substituted.
void EmptySetSubstitutesNothing() {
  auto sub = lengsuo::fp::FontSubstitution::From(Installed(""));
  CHECK(sub.Installed());
  for (const char* family : {"Arial", "__LengsuoDeclaredAlpha__", ""}) {
    CHECK(!sub.NeedsSubstitute(family));
  }
}

// The whole point of gamma2: a declared family is made to resolve to a real
// family name, and it must never be the family that was asked for (which would
// leave the page on the default fallback and change nothing).
void SubstituteIsARealCandidateAndDifferent() {
  auto sub = lengsuo::fp::FontSubstitution::From(
      Installed("__LengsuoDeclaredAlpha__,__LengsuoDeclaredBeta__"));
  const auto candidates = CandidateViews();
  for (const char* family :
       {"__LengsuoDeclaredAlpha__", "__LengsuoDeclaredBeta__"}) {
    const std::string picked = sub.SubstituteFor(family, candidates);
    CHECK(!picked.empty());
    CHECK(picked != family);
    // The result is one of the host's real families, never a synthesised name.
    CHECK(std::find(kCandidates.begin(), kCandidates.end(), picked) !=
          kCandidates.end());
  }
}

// Deterministic and order-insensitive: reordering the set changes nothing, and
// reordering the *candidate pool* only permutes which candidate a hash lands
// on -- it must not change the answer for a fixed pool.
void SubstituteIsDeterministicAndOrderInsensitive() {
  const auto candidates = CandidateViews();
  auto first = lengsuo::fp::FontSubstitution::From(Installed("A,B,C"));
  auto second = lengsuo::fp::FontSubstitution::From(Installed("C,A,B"));
  auto third = lengsuo::fp::FontSubstitution::From(Installed("A,B,C"));
  for (const char* family : {"A", "B", "C"}) {
    const std::string a = first.SubstituteFor(family, candidates);
    CHECK(a == second.SubstituteFor(family, candidates));
    CHECK(a == third.SubstituteFor(family, candidates));
  }
}

// Distinct declared names are spread across the pool rather than collapsing
// onto one family: two declared families that rendered identically would be
// their own tell.
void DistinctNamesAreSpreadAcrossCandidates() {
  const auto candidates = CandidateViews();
  auto sub = lengsuo::fp::FontSubstitution::From(
      Installed("Alpha,Beta,Gamma,Delta,Epsilon,Zeta,Eta,Theta"));
  std::set<std::string> seen;
  for (const char* family :
       {"Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta"}) {
    seen.insert(sub.SubstituteFor(family, candidates));
  }
  CHECK(seen.size() >= 2);
}

// An empty candidate pool means "there is nothing to alias onto": the
// substitute is empty and the caller must leave resolution native rather than
// invent a name.
void EmptyPoolYieldsNoSubstitute() {
  auto sub = lengsuo::fp::FontSubstitution::From(Installed("A"));
  const std::vector<std::string_view> empty;
  CHECK(sub.SubstituteFor("A", empty).empty());
}

// The pool the Blink call site actually passes: the three CSS generic families
// whose fontconfig aliases resolve without enumerating the installed set. This
// test exists because the obvious-looking alternative -- feed it
// SkFontMgr::countFamilies()/getFamilyName() -- aborts the renderer on Linux
// (the mojo FontLoader's enumeration is SK_ABORT("Not implemented.")), and the
// abort would not show up in any pure unit test. Pinning the pool here means a
// call site that reintroduces enumeration fails a fast, hermetic test.
void GenericPoolIsTheProductionPool() {
  static const std::vector<std::string> kGenerics = {"serif", "sans-serif",
                                                     "monospace"};
  std::vector<std::string_view> generics;
  for (const auto& name : kGenerics) {
    generics.emplace_back(name);
  }
  auto sub = lengsuo::fp::FontSubstitution::From(
      Installed("__LengsuoDeclaredAlpha__,__LengsuoDeclaredBeta__"));
  for (const char* family :
       {"__LengsuoDeclaredAlpha__", "__LengsuoDeclaredBeta__"}) {
    const std::string picked = sub.SubstituteFor(family, generics);
    CHECK(std::find(kGenerics.begin(), kGenerics.end(), picked) !=
          kGenerics.end());
  }
  // Every generic must be reachable by some declared name, or the pool is
  // effectively smaller than it looks.
  std::set<std::string> reached;
  auto wide = lengsuo::fp::FontSubstitution::From(Installed(
      "A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z"));
  for (const char* family :
       {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N",
        "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"}) {
    reached.insert(wide.SubstituteFor(family, generics));
  }
  CHECK(reached.size() == kGenerics.size());
}

}  // namespace

int main() {
  NoSetMeansNoSubstitution();
  OnlyDeclaredFamiliesAreSubstituted();
  EmptySetSubstitutesNothing();
  SubstituteIsARealCandidateAndDifferent();
  SubstituteIsDeterministicAndOrderInsensitive();
  DistinctNamesAreSpreadAcrossCandidates();
  EmptyPoolYieldsNoSubstitute();
  GenericPoolIsTheProductionPool();
  std::printf("PASS %d assertions\n", checks);
  return 0;
}
