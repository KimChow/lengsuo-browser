// fp_overrides - renderer-side fingerprint override state.
// One switch carries everything: --lengsuo-managed-fp=<seed-hex>:<a>:<c>:<r>:<f>:<g>:<u>:<w>
// (slots: audio, canvas, rects, fonts, gpu, ua, webgl). Absent => all disabled,
// upstream behavior byte-identical. Face resolution lives here (built-in tables).
#ifndef LENGSUO_NATIVE_FP_OVERRIDES_H_
#define LENGSUO_NATIVE_FP_OVERRIDES_H_
#include <cstdint>
#include <string_view>
namespace lengsuo::fp {
void InitFromSwitch(std::string_view value);
bool Enabled();
// WebGL faces resolved from seed+slot (deterministic).
std::string_view WebGLVendor();
std::string_view WebGLRenderer();
std::uint32_t HardwareConcurrency();  // 0 = disabled
std::uint32_t DeviceMemoryGB();       // 0 = disabled
double RectsJitterPx();               // 0 = disabled
std::uint64_t AudioSeed();

// Slot 7: deterministic seed for WebGL MAX_* parameter perturbation.
std::uint64_t WebGLParamsSeed();            // 0 = disabled
// Deterministic bias for GLenum-typed int parameters (MAX_*, *_BITS...);
// returns 0 when the webgl slot is off. Applied inside GetIntParameter.
std::int32_t WebGLParamBias(std::uint32_t pname, std::int32_t original);
}  // namespace lengsuo::fp
#endif
