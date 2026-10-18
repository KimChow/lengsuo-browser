// canvas_noise v1 — deterministic RGBA8 pixel perturbation (pure algorithm).
//
// Contract: docs/canvas-component-contract.md (frozen 2026-10-18, plan A).
// Independent reference: tests/canvas_ref/gen_canvas_ref.py (Python).
// Frozen bytes: tests/canvas_ref/golden_fixture.inc.
//
// This component is a pure algorithm library: no Chromium injection, no IPC,
// no dynamic allocation, no static mutable state. The immutable-source rule
// is the API contract: reads come only from `src`, writes go only to `dst`.

#ifndef LENGSUO_NATIVE_CANVAS_NOISE_H_
#define LENGSUO_NATIVE_CANVAS_NOISE_H_

#include <cstdint>
#include <span>

namespace lengsuo::canvas {

enum class CanvasNoiseStatus : std::uint8_t {
  kOk,
  kUnsupportedVersion,
  kInvalidGeometry,
  kBufferTooSmall,
};

struct CanvasNoiseConfig {
  std::uint32_t algorithm_version{};
  bool enabled{};
  std::uint64_t seed{};
  std::uint32_t slot{};
};

struct CanvasNoiseResult {
  CanvasNoiseStatus status{CanvasNoiseStatus::kOk};
  std::uint64_t changed_pixels{};
  std::uint64_t preserved_pixels{};
};

// Applies canvas_noise v1 to an RGBA8 grid. `dst` may alias `src` exactly
// (in-place, per-pixel read-then-write); any other overlap is undefined
// input. All validation happens before any write; a failed call leaves
// `dst` untouched.
CanvasNoiseResult ApplyCanvasNoiseV1(const CanvasNoiseConfig& config,
                                     std::span<const std::uint8_t> src,
                                     std::span<std::uint8_t> dst,
                                     std::uint32_t width,
                                     std::uint32_t height,
                                     std::uint64_t stride,
                                     std::uint64_t buffer_size);

}  // namespace lengsuo::canvas

#endif  // LENGSUO_NATIVE_CANVAS_NOISE_H_
