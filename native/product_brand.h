// product_brand - the product brand label this build advertises.
//
// Chromium only emits a product brand into the UA-CH brand list when it is
// built from the branded tree (CHROMIUM_BRANDING=0). An unbranded build
// advertises `Chromium` plus the GREASE brand and nothing else, which is a
// single-field giveaway in any client-hint check: real Chrome always lists
// `Google Chrome` alongside `Chromium`.
//
// The engine underneath is still Chromium; the product identity is what we
// ship, and the brand list has to describe the shipped product rather than the
// source tree it was compiled from. Versions are not hardcoded here on
// purpose - they come from chrome/VERSION so the pair cannot drift.
#ifndef LENGSUO_NATIVE_PRODUCT_BRAND_H_
#define LENGSUO_NATIVE_PRODUCT_BRAND_H_

#include <string_view>

namespace lengsuo::native {

// Must stay consistent with the UA string's product token (`Chrome/<version>`).
inline constexpr std::string_view kProductBrand = "Google Chrome";

}  // namespace lengsuo::native

#endif  // LENGSUO_NATIVE_PRODUCT_BRAND_H_
