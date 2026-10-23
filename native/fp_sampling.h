// fp_sampling - the sampling geometry, the fingerprint hash and the
// deterministic perturbation (task T3).
//
// Three things live here.
//
// The geometry. A canvas sample is a square of a fixed side length: the
// configuration says how wide the side is and the sample is taken at that
// size, so that two samples of the same page are the same size and their
// fingerprints can be compared.
//
// The fingerprint hash. What the page reads is a fingerprint; how that
// fingerprint is reduced to bytes is a contract between the kernel and
// whoever produces the target value. The definition here is the raw RGBA
// pixels in row-major order, hashed with FNV-1a 64 and printed as sixteen
// lowercase hexadecimal characters. It needs no crypto library and gives
// the same bytes on every machine.
//
// The perturbation. The perturbation is a function of the target value and
// the geometry and of nothing else, so the same target always produces the
// same bytes. The noise itself is the canvas noise the kernel already had;
// only the seed changes.
//
// The rules this file follows come from the task brief and from
// docs/fp-switch-contract.md; see docs/l3-reports/task-T3.md for the
// evidence and for the questions that are still open.
#ifndef LENGSUO_NATIVE_FP_SAMPLING_H_
#define LENGSUO_NATIVE_FP_SAMPLING_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace lengsuo::fp {

// The two dimensions a fingerprint sample can be taken in.
enum class FpDimension : std::uint8_t { kCanvas, kWebgl };

// The geometry a sample was taken at.
struct FpSamplingSpec {
  // The width of the sample in pixels.
  std::uint32_t width = 0;
  // The height of the sample in pixels. A canvas sample is a square, so
  // width == length there.
  std::uint32_t length = 0;
  // Whether the page drew text into the canvas. Empty when the target did
  // not say, which means the target does not constrain it.
  std::optional<bool> drew_text;
};

// The side of the square a canvas sample is taken at.
//
// Zero when the geometry is not a square: a canvas sample is a square of a
// fixed side length, and a non-square geometry has no side.
std::uint32_t SamplingSideLength(const FpSamplingSpec& spec);

// True when two samples were taken at the same geometry.
//
// `drew_text` is compared only when both sides constrain it: a target that
// did not say whether text was drawn does not make a sample incomparable.
bool SameSamplingSpec(const FpSamplingSpec& a, const FpSamplingSpec& b);

// The fingerprint hash of a sampled buffer.
//
// The hash is over the raw RGBA pixels in row-major order; the padding of
// the last row is skipped. The buffer must hold at least width * height *
// 4 bytes; a shorter buffer is hashed up to its end.
std::uint64_t FingerprintHash(std::span<const std::uint8_t> pixels,
                               std::uint32_t width,
                               std::uint32_t height);

// The same hash as sixteen lowercase hexadecimal characters.
std::string FingerprintHashHex(std::span<const std::uint8_t> pixels,
                                std::uint32_t width,
                                std::uint32_t height);

// The base seed of a target.
//
// It is derived from the target value and the geometry and from nothing
// else, so the same target always produces the same perturbation.
std::uint64_t TargetSeed(std::string_view target_hash,
                          const FpSamplingSpec& spec);

// One candidate of the convergence search.
//
// Attempt 0 is the seed used when no convergence is attempted.
std::uint64_t AttemptSeed(std::uint64_t base_seed, std::uint32_t attempt);

// Applies the deterministic perturbation of `seed` to a copy of `src`.
//
// `src` and `dst` must not overlap. Returns false when the geometry does
// not fit the buffers, in which case `dst` is left untouched.
bool ApplySeedNoise(std::uint64_t seed,
                     std::span<const std::uint8_t> src,
                     std::span<std::uint8_t> dst,
                     std::uint32_t width,
                     std::uint32_t height,
                     std::uint64_t stride);

// The perturbation of a target: attempt 0 of the search.
bool ApplyTargetNoise(std::string_view target_hash,
                      const FpSamplingSpec& spec,
                      std::span<const std::uint8_t> src,
                      std::span<std::uint8_t> dst,
                      std::uint32_t width,
                      std::uint32_t height,
                      std::uint64_t stride);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_FP_SAMPLING_H_
