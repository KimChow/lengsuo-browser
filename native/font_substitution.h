// font_substitution - make a profile's declared families actually resolve.
//
// The font dimension (profile slots[3]) was decoded but did nothing observable.
// The first proposed fix -- make `document.fonts.check()` answer false for
// families outside the set -- was withdrawn: upstream `FontFaceSet::check()`
// (font_face_set.cc:232) returns true for every family the page did not itself
// register, so that state cannot exist. The observable face of the dimension
// is instead *availability*: which families resolve to a real typeface, and
// the widths that follow (docs/cc-task-rulings.md §C).
//
// The chosen mechanism is gamma2: add, never remove. A family the profile
// declares is made to resolve to a real, installed candidate family instead of
// falling through to the default fallback. We do not invent glyphs, metrics or
// an inventory -- the substitute is one of the host's own font families, so the
// pixel layer stays honest (docs/gpu-and-detection-plan.md three-layer model).
// Families the host already resolves are untouched.
//
// This header is pure: it depends only on the availability set and a caller-
// supplied pool of real candidate names, so it is unit-testable without Blink
// and without a font backend. The Blink call site feeds it the CSS generic
// families -- see the comment on the call site for why the host's family list
// is deliberately not enumerated (native/font_substitution_test.cc,
// font_cache_skia.cc).
//
// Contract: docs/font-dimension.md.
#ifndef LENGSUO_NATIVE_FONT_SUBSTITUTION_H_
#define LENGSUO_NATIVE_FONT_SUBSTITUTION_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "font_face_set_filter.h"

namespace lengsuo::fp {

class FontSubstitution {
 public:
  // Not installed: every query answers "leave it to the platform". The default
  // state, so a build with no font switch behaves exactly like upstream.
  FontSubstitution() = default;

  // Wraps a parsed availability set by value: the substitution owns a copy, so
  // it outlives the temporary a caller naturally builds it from. A default-
  // constructed (uninitialized) filter yields a not-installed substitution,
  // which is the correct answer: "the profile said nothing about fonts" must be
  // a no-op.
  static FontSubstitution From(const FontFaceSetFilter& fonts) {
    FontSubstitution substitution;
    substitution.fonts_ = fonts;
    substitution.installed_ = fonts.status() == FontFaceSetStatus::kInit;
    return substitution;
  }

  bool Installed() const { return installed_; }

  // True when this family is declared by the profile *and* therefore eligible
  // for aliasing. Whether the host actually lacks it is decided by the caller
  // (a family the host resolves is never touched).
  bool NeedsSubstitute(std::string_view family) const {
    return installed_ && fonts_.Available(family) == FontFaceSetStatus::kPresent;
  }

  // Picks a real family from `candidates` for a declared family. Deterministic
  // in `family` and `candidates` only: a stable, platform-independent hash
  // (never std::hash, whose value may vary per process), so two renderer
  // processes given one profile agree. Returns an empty string when there is
  // nothing to alias onto, which the caller must read as "stay native".
  std::string SubstituteFor(std::string_view family,
                            std::span<const std::string_view> candidates) const {
    if (candidates.empty()) {
      return std::string();
    }
    const std::uint64_t hash = StableHash(family);
    return std::string(candidates[hash % candidates.size()]);
  }

 private:
  // FNV-1a. Chosen because it is tiny, has no per-process salt, and is trivial
  // to reproduce in a test or another language (see the Python smoke test).
  static std::uint64_t StableHash(std::string_view value) {
    std::uint64_t hash = 1469598103934665603ULL;  // offset basis
    for (const char character : value) {
      hash ^= static_cast<unsigned char>(character);
      hash *= 1099511628211ULL;  // FNV prime
    }
    return hash;
  }

  FontFaceSetFilter fonts_;
  bool installed_ = false;
};

}  // namespace lengsuo::fp
#endif  // LENGSUO_NATIVE_FONT_SUBSTITUTION_H_
