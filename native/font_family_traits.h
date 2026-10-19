// font_family_traits - the fonts dimension's {switcher, value, mask} shape.
//
// docs/cc-task-rulings.md §C: 对标端 gives every overridable dimension the same
// shape -- {switcher, value, default_value} -- and for fonts the value is a
// semicolon-separated family string ("Arial;Arial Black"), not a structured
// list. Our profile encodes the fonts dimension as one slot number, which is
// why nothing consumed it; this header is the missing decoder from that slot
// plus the switch's value/mask fields into the shape the mechanism needs.
//
// It does NOT carry a font inventory. 对标端's full family pool lives on its
// server and was never captured, so no list is invented here: an empty value
// is a legitimate statement ("use the mask"), and the family names are only
// ever the ones the profile actually sent.
//
// Header-only: three small operations over caller-owned strings, compiled both
// into the standalone native tests and into Blink (no platform headers).
//
// Contract doc: docs/font-dimension.md.
#ifndef LENGSUO_NATIVE_FONT_FAMILY_TRAITS_H_
#define LENGSUO_NATIVE_FONT_FAMILY_TRAITS_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lengsuo::fonts {

// Upper bounds are what the profile wire accepts for the same fields
// (profile_wire.cc), so a hostile payload cannot make the switch accept what
// the wire rejects.
inline constexpr std::size_t kMaxFamilyName = 128;
inline constexpr std::size_t kMaxFamilyValue = 4096;
inline constexpr std::size_t kMaxFamilyCount = 64;

struct FamilyTraits {
  // False when the profile said nothing about fonts (slot 0) or the payload was
  // malformed; the caller must then leave measurement byte-identical to native.
  bool installed{false};
  // The profile's raw font slot, kept for diagnostics/tests.
  std::uint32_t slot{};
  // The family names the profile declared, in wire order. May be empty: a mask
  // can ask for metric perturbation without naming families.
  std::vector<std::string> families;
  // Whether the font metric perturbation is active. True iff installed and the
  // profile made a statement: a non-empty value, or a non-zero family mask.
  bool perturbs_metrics{false};
};

// Splits 对标端's semicolon-separated value. Blank elements and duplicates are
// dropped; interior spaces are preserved (family names may contain them). A
// malformed element rejects the whole value (returns empty).
std::vector<std::string> ParseSemicolonFamilies(std::string_view value);

// Decodes (slot, value, mask) into the mechanism's input.
//
//   * slot == 0              -> not installed: "the profile said nothing",
//                               identical to the GPU dimension's slot 0.
//   * value non-empty        -> installed; a non-empty family list is itself
//                               the statement, so metrics are perturbed even
//                               when mask is 0 (the service default).
//   * value empty, mask != 0 -> installed; the mask is the only statement.
//   * value empty, mask == 0 -> installed but dormant: nothing declares a
//                               change, so measurement stays native.
//
// A malformed value (oversize, too many or oversized names, control bytes)
// yields a not-installed result rather than a half-parsed one.
FamilyTraits ParseFamilyTraits(std::uint32_t slot, std::string_view value,
                               std::uint32_t mask);

// ---------------------------------------------------------------------------
// Implementation (header-only).
// ---------------------------------------------------------------------------
namespace internal {

inline bool ValidFamily(std::string_view family) {
  if (family.empty() || family.size() > kMaxFamilyName) {
    return false;
  }
  for (const char character : family) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 32 || byte > 126) {
      return false;
    }
  }
  return true;
}

}  // namespace internal

inline std::vector<std::string> ParseSemicolonFamilies(std::string_view value) {
  std::vector<std::string> families;
  std::size_t start = 0;
  while (start < value.size()) {
    const std::size_t semicolon = value.find(';', start);
    const std::size_t end =
        semicolon == std::string_view::npos ? value.size() : semicolon;
    const std::string_view element = value.substr(start, end - start);
    const bool blank = element.find_first_not_of(' ') == std::string_view::npos;
    if (!blank) {
      if (!internal::ValidFamily(element) ||
          families.size() >= kMaxFamilyCount) {
        return {};  // malformed: reject the whole value, not one element
      }
      bool duplicate = false;
      for (const auto& existing : families) {
        if (existing == element) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        families.emplace_back(element);
      }
    }
    if (semicolon == std::string_view::npos) {
      break;
    }
    start = semicolon + 1;
  }
  return families;
}

inline FamilyTraits ParseFamilyTraits(std::uint32_t slot,
                                      std::string_view value,
                                      std::uint32_t mask) {
  FamilyTraits traits;
  traits.slot = slot;
  if (slot == 0) {
    return traits;  // the profile said nothing about fonts
  }
  if (value.size() > kMaxFamilyValue) {
    return traits;  // too large: not installed, never half-parsed
  }
  for (const char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 32 || byte > 126) {
      return traits;  // control bytes are never a valid family list
    }
  }
  traits.families = ParseSemicolonFamilies(value);
  // ParseSemicolonFamilies returns {} both for "empty" and for "malformed"; a
  // non-empty value that parsed to nothing is the malformed case.
  const bool blank = value.find_first_not_of("; ") == std::string_view::npos;
  if (!blank && traits.families.empty()) {
    return traits;  // malformed (oversize/too many names): stay native
  }
  traits.installed = true;
  traits.perturbs_metrics = !traits.families.empty() || mask != 0;
  return traits;
}

}  // namespace lengsuo::fonts

#endif  // LENGSUO_NATIVE_FONT_FAMILY_TRAITS_H_
