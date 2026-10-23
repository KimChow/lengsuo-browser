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
#include "fp_target.h"
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

// Slot 7: deterministic seed for WebGL MAX_* parameter perturbation.
// Returns 0 when the webgl slot is off.
std::uint64_t WebGLParamsSeed();
// Deterministic bias for GLenum-typed int parameters (MAX_*, *_BITS...);
// returns 0 when the webgl slot is off. Applied inside GetIntParameter.
std::int32_t WebGLParamBias(std::uint32_t pname, std::int32_t original);

// --- Per-URL target values (task T3 wiring) ---
//
// What a dimension must report is not the same everywhere: the platform can
// name a target per URL, and the table of those targets arrives in the
// `fp_target` section of the frozen configuration bundle (native/fp_target.h).
// Installing it here is what turns the profile-wide perturbation above into a
// per-URL one. It is the only entry that reads that section, so there is no
// second channel and no second parser.
//
// A bundle without the section installs an empty table, and then every
// accessor below returns exactly what it returned before this entry existed,
// byte for byte: nothing is sampled, compared or reported.
//
// A target that governs a URL takes precedence over the profile's slot for
// that dimension. The slot is the fallback for the URLs no target names, which
// is what "no target value => the existing behaviour" means here.
bool SetFpTargetsFromBundle(std::string_view bundle_json);

// True when the installed table can govern `dimension` at all. Callers use it
// to skip building a page URL when no target can apply.
bool HasFpTargets(FpDimension dimension);

// The target that governs `url` in `dimension`, or nullptr when none does. The
// most specific entry wins, and an entry whose pattern is empty is the default
// of its dimension. An empty `url` never matches: the caller had no page URL,
// so nothing is targeted and the native value stays.
const FpTarget* FpTargetForUrl(std::string_view url, FpDimension dimension);

// The perturbation seed the target governing `url` asks for: attempt 0 of its
// convergence sequence (native/fp_sampling.h), the seed ApplyTargetNoise uses.
// Returns false and leaves `*seed` alone when no target governs the URL, or
// when the governing target says it must not be perturbed at all.
bool FpTargetSeedForUrl(std::string_view url,
                        FpDimension dimension,
                        std::uint64_t* seed);

// Slot 7 again, per URL: the capability-cap bias for the target that governs
// `url`, restricted to the same allow-list. Byte-identical to
// WebGLParamBias(pname, original) when no target governs the URL.
std::int32_t WebGLParamBiasForUrl(std::string_view url,
                                  std::uint32_t pname,
                                  std::int32_t original);

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
