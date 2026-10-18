// Renderer-side canvas noise application state and entry point.
//
// The browser passes seed/slot via the --lengsuo-managed-canvas switch
// (hex seed + decimal slot). No profile file or secrets reach the renderer.
// When the switch is absent the state is disabled and ApplyProfileCanvasNoise
// is a no-op — upstream readback stays byte-identical.
#ifndef LENGSUO_NATIVE_CANVAS_APPLY_H_
#define LENGSUO_NATIVE_CANVAS_APPLY_H_
#include <cstdint>
namespace lengsuo {
// Parses --lengsuo-managed-canvas=<hex-seed>:<slot>. Empty/invalid -> disabled.
void InitCanvasNoiseFromSwitch(const char* switch_value);
bool CanvasNoiseEnabled();
// Applies noise in place to an RGBA8 buffer. No-op when disabled or on any
// validation failure (never throws, never allocates).
void ApplyProfileCanvasNoise(void* data, std::uint32_t width,
                             std::uint32_t height, std::uint64_t stride);
}  // namespace lengsuo
#endif
