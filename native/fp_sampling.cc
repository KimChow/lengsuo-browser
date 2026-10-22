#include "fp_sampling.h"

#include <cstddef>

#include "canvas_noise.h"

namespace lengsuo::fp {
namespace {

// FNV-1a 64. The offset basis and the prime are the ones the algorithm
// defines; both are constants of the method, not fingerprint values.
constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x00000100000001B3ULL;

// SplitMix64's finalizer. It is used to spread the bits of a seed over
// the whole word, so that two seeds that differ in one bit produce
// perturbations that have nothing in common.
std::uint64_t Mix64(std::uint64_t value) {
  std::uint64_t mixed = value;
  mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ULL;
  mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBULL;
  return mixed ^ (mixed >> 31);
}

std::uint64_t Fnv1a64(std::string_view data) {
  std::uint64_t hash = kFnvOffset;
  for (const char c : data) {
    hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
    hash *= kFnvPrime;
  }
  return hash;
}

}  // namespace

std::uint32_t SamplingSideLength(const FpSamplingSpec& spec) {
  if (spec.width == 0 || spec.width != spec.length) {
    return 0;
  }
  return spec.width;
}

bool SameSamplingSpec(const FpSamplingSpec& a, const FpSamplingSpec& b) {
  if (a.width != b.width || a.length != b.length) {
    return false;
  }
  if (!a.drew_text.has_value() || !b.drew_text.has_value()) {
    // One side did not constrain the question, so it cannot disagree.
    return true;
  }
  return *a.drew_text == *b.drew_text;
}

std::uint64_t FingerprintHash(std::span<const std::uint8_t> pixels,
                               std::uint32_t width,
                               std::uint32_t height) {
  std::uint64_t hash = kFnvOffset;
  std::size_t index = 0;
  for (std::uint32_t row = 0; row < height; ++row) {
    for (std::uint32_t column = 0; column < width; ++column) {
      for (int channel = 0; channel < 4; ++channel) {
        if (index >= pixels.size()) {
          return hash;
        }
        hash ^= static_cast<std::uint64_t>(pixels[index]);
        hash *= kFnvPrime;
        ++index;
      }
    }
  }
  return hash;
}

std::string FingerprintHashHex(std::span<const std::uint8_t> pixels,
                                std::uint32_t width,
                                std::uint32_t height) {
  static const char kDigits[] = "0123456789abcdef";
  std::uint64_t hash = FingerprintHash(pixels, width, height);
  std::string text(16, '0');
  for (int index = 15; index >= 0; --index) {
    text[static_cast<std::size_t>(index)] =
        kDigits[static_cast<std::size_t>(hash & 0xFULL)];
    hash >>= 4;
  }
  return text;
}

std::uint64_t TargetSeed(std::string_view target_hash,
                          const FpSamplingSpec& spec) {
  std::uint64_t hash = Fnv1a64(target_hash);
  hash ^= static_cast<std::uint64_t>(spec.width) * 0x9E3779B97F4A7C15ULL;
  hash ^= static_cast<std::uint64_t>(spec.length) * 0xC2B2AE3D27D4EB4FULL;
  hash ^= spec.drew_text.value_or(false) ? 0xFFFFFFFFFFFFFFFFULL : 0ULL;
  return Mix64(hash);
}

std::uint64_t AttemptSeed(std::uint64_t base_seed, std::uint32_t attempt) {
  const std::uint64_t offset =
      (static_cast<std::uint64_t>(attempt) + 1ULL) * 0x9E3779B97F4A7C15ULL;
  return Mix64(base_seed + offset);
}

bool ApplySeedNoise(std::uint64_t seed,
                     std::span<const std::uint8_t> src,
                     std::span<std::uint8_t> dst,
                     std::uint32_t width,
                     std::uint32_t height,
                     std::uint64_t stride) {
  if (width == 0 || height == 0) {
    return false;
  }
  const std::uint64_t needed = stride * static_cast<std::uint64_t>(height);
  if (needed == 0 || src.size() < needed || dst.size() < needed) {
    return false;
  }
  canvas::CanvasNoiseConfig config;
  config.algorithm_version = 1;
  config.enabled = true;
  config.seed = seed;
  config.slot = 0;
  const canvas::CanvasNoiseResult result = canvas::ApplyCanvasNoiseV1(
      config, src, dst, width, height, stride, needed);
  return result.status == canvas::CanvasNoiseStatus::kOk;
}

bool ApplyTargetNoise(std::string_view target_hash,
                      const FpSamplingSpec& spec,
                      std::span<const std::uint8_t> src,
                      std::span<std::uint8_t> dst,
                      std::uint32_t width,
                      std::uint32_t height,
                      std::uint64_t stride) {
  const std::uint64_t seed = AttemptSeed(TargetSeed(target_hash, spec), 0);
  return ApplySeedNoise(seed, src, dst, width, height, stride);
}

}  // namespace lengsuo::fp
