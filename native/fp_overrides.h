// fp_overrides - renderer-side fingerprint override state.
// One switch carries everything: --lengsuo-managed-fp=<seed-hex>:<a>:<c>:<r>:<f>:<g>:<u>:<w>
// (slots: audio, canvas, rects, fonts, gpu, ua, webgl). Absent => all disabled,
// upstream behavior byte-identical. Face resolution lives here (built-in tables).
#ifndef LENGSUO_NATIVE_FP_OVERRIDES_H_
#define LENGSUO_NATIVE_FP_OVERRIDES_H_
#include <cstdint>
#include <string_view>

#include "font_family_traits.h"
#include "font_metrics.h"
namespace lengsuo::fp {
// Two switches, both optional and both absent by default:
//   --lengsuo-managed-fp=<seed-hex>:<a>:<c>:<r>:<f>:<g>:<u>:<w>:<vendor>:
//                        <renderer>:<hwc>:<mem>
//   --lengsuo-managed-fonts=<slot>:<mask>:<Family;Family;...>
// The fonts dimension is {switcher, value, mask} (docs/cc-task-rulings.md §C):
// its value is a semicolon-separated family string that can be large, so it
// does not ride a colon field of the fp switch (see docs/fp-switch-contract.md).
// Both absent => upstream behavior byte-identical.
void InitFromSwitch(std::string_view value);
// Parses --lengsuo-managed-fonts and installs the font-metric perturbation.
// Returns false on a malformed payload, in which case nothing is installed and
// every measurement keeps its native value.
bool InitFontsFromSwitch(std::string_view value);
bool Enabled();
// WebGL faces resolved from seed+slot (deterministic).
std::string_view WebGLVendor();
std::string_view WebGLRenderer();
std::uint32_t HardwareConcurrency();  // 0 = disabled
std::uint32_t DeviceMemoryGB();       // 0 = disabled
double RectsJitterPx();               // 0 = disabled
std::uint64_t AudioSeed();

// Font dimension: the parsed traits, or a default (not-installed) value until a
// valid payload arrives. Callers must treat !installed as "this profile says
// nothing about fonts" and keep the native measurement. The seed used for the
// perturbation is the global noise seed, so the font dimension shares the
// canvas/audio derivation discipline (docs/font-dimension.md).
const lengsuo::fonts::FamilyTraits& FontTraits();

// Perturbs one measured glyph advance per the installed font dimension. This is
// the browser-side entry point for the accepted mechanism (docs/cc-task-rulings
// §C): the advance is perturbed deterministically, `FontFaceSet::check()` is
// never touched. Returns the native advance unchanged when the dimension is off,
// not installed, or dormant.
float PerturbGlyphAdvance(float advance);
}  // namespace lengsuo::fp
#endif
