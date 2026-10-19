// font_face_set_filter - the font dimension's availability set.
//
// The profile's font slot (slots[3]) has been decoded since schema 1, but
// nothing consumed it: `document.fonts.check()` and `measureText()` answered
// from the host's real font list, so changing the slot changed nothing a page
// could observe. This is the consumer's data structure.
//
// It carries *availability only*. It deliberately does not carry glyph or
// metric data: the pixel layer cannot be faked (docs/gpu-and-detection-plan.md
// three-layer model), so a family that is answerable as "present" is still
// shaped by the machine's real font. Availability and measurement therefore
// stay consistent by construction -- hiding a family hides its widths with it.
//
// Header-only: the filter is three small operations over a caller-owned table
// and gains nothing from a translation unit. It must not pull platform
// headers, because it is compiled both into the standalone native tests and
// into Blink.
//
// Contract: docs/font-dimension.md.
#ifndef LENGSUO_NATIVE_FONT_FACE_SET_FILTER_H_
#define LENGSUO_NATIVE_FONT_FACE_SET_FILTER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lengsuo::fp {

// Tri-state answer. kNotInitialized is a distinct value rather than "empty",
// because "this profile says there are no fonts" and "this profile says
// nothing about fonts" must not collapse into the same behaviour.
enum class FontFaceSetStatus : std::uint8_t {
  kNotInitialized = 0,
  kAbsent = 1,
  kPresent = 2,
  kInit = 3,  // the set applies, independent of this family's membership
};

// Case-sensitive: CSS font family names are case-sensitive, and the browser
// compares the resolved family name, not the authored one.
class FontFaceSetFilter {
 public:
  static constexpr std::size_t kMaxEntries = 64;
  static constexpr std::size_t kMaxName = 128;
  static constexpr std::size_t kMaxPayload = 2048;

  FontFaceSetFilter() = default;

  // Parses the switch payload: a comma-separated list of family names.
  //
  // An empty payload is accepted and means "no family is available" -- an
  // explicit statement, not an absence of one. Malformed input (empty element,
  // oversized payload, non-printable byte, oversized or too many names) is
  // rejected whole: the filter is left uninitialized and the caller must fall
  // back to native behaviour rather than apply half a font list, which would
  // claim the dimension was applied while pages still see deleted families.
  bool Init(std::string_view payload) {
    entries_.clear();
    initialized_ = false;
    if (payload.size() > kMaxPayload) {
      return false;
    }
    for (const char character : payload) {
      const auto byte = static_cast<unsigned char>(character);
      if (byte < 32 || byte > 126) {
        return false;
      }
    }
    std::size_t start = 0;
    while (start < payload.size()) {
      const std::size_t comma = payload.find(',', start);
      const std::size_t end =
          comma == std::string_view::npos ? payload.size() : comma;
      if (!Add(payload.substr(start, end - start))) {
        entries_.clear();
        initialized_ = false;
        return false;
      }
      if (comma == std::string_view::npos) {
        break;
      }
      start = comma + 1;
    }
    initialized_ = true;
    return true;
  }

  // Appends one family. Duplicates are ignored, not stored twice.
  bool Add(std::string_view family) {
    if (family.empty() || family.size() > kMaxName) {
      return false;
    }
    for (const char character : family) {
      const auto byte = static_cast<unsigned char>(character);
      if (byte < 32 || byte > 126) {
        return false;
      }
    }
    if (Contains(family)) {
      return true;
    }
    if (entries_.size() >= kMaxEntries) {
      return false;
    }
    entries_.emplace_back(family);
    return true;
  }

  FontFaceSetStatus status() const {
    return initialized_ ? FontFaceSetStatus::kInit
                        : FontFaceSetStatus::kNotInitialized;
  }

  // Queries are only meaningful once Init() succeeded; the caller is expected
  // to gate on status() exactly as it gates on whether the switch was present.
  FontFaceSetStatus Available(std::string_view family) const {
    if (!initialized_) {
      return FontFaceSetStatus::kNotInitialized;
    }
    return Contains(family) ? FontFaceSetStatus::kPresent
                            : FontFaceSetStatus::kAbsent;
  }

  bool empty() const { return entries_.empty(); }
  std::size_t size() const { return entries_.size(); }

 private:
  bool Contains(std::string_view family) const {
    for (const auto& entry : entries_) {
      if (entry == family) {
        return true;
      }
    }
    return false;
  }

  std::vector<std::string> entries_;
  bool initialized_ = false;
};

}  // namespace lengsuo::fp
#endif  // LENGSUO_NATIVE_FONT_FACE_SET_FILTER_H_
