// Standalone unit test for fp_overrides (plain g++, not a GN target).
#include "lengsuo/native/fp_overrides.h"

#include <cstdio>
#include <string>

namespace {
int g_passes = 0;
int g_failures = 0;

void Check(bool ok, const char* expr, int line) {
  if (ok) {
    ++g_passes;
  } else {
    std::printf("FAIL line %d: %s\n", line, expr);
    ++g_failures;
  }
}
}  // namespace

#define CHECK(cond) Check((cond), #cond, __LINE__)

int main() {
  // 1. Empty / malformed payloads leave everything native.
  lengsuo::fp::InitFromSwitch(std::string_view(""));
  CHECK(!lengsuo::fp::Enabled());
  CHECK(lengsuo::fp::HardwareConcurrency() == 0);
  CHECK(lengsuo::fp::DeviceMemoryGB() == 0);
  CHECK(lengsuo::fp::AudioSeed() == 0);
  CHECK(lengsuo::fp::RectsJitterPx() == 0.0);
  CHECK(lengsuo::fp::WebGLVendor().empty());
  CHECK(lengsuo::fp::WebGLRenderer().empty());

  lengsuo::fp::InitFromSwitch(std::string_view("zzzz:1:1:1:1:1:1:1"));
  CHECK(!lengsuo::fp::Enabled());
  lengsuo::fp::InitFromSwitch(std::string_view("0000000000000000:1:1:1:1:1:1:1"));
  CHECK(!lengsuo::fp::Enabled());  // zero seed rejected

  // 2. Full payload round-trips every slot.
  // seed:canvas:audio:rects:fonts:gpu:ua:webgl:vendor:renderer:hwc:mem
  lengsuo::fp::InitFromSwitch(std::string_view(
      "0123456789abcdef:1:1:1:2:1:1:1:VendorX:RendererY:12:8"));
  CHECK(lengsuo::fp::Enabled());
  CHECK(lengsuo::fp::HardwareConcurrency() == 12);
  CHECK(lengsuo::fp::DeviceMemoryGB() == 8);
  CHECK(lengsuo::fp::WebGLVendor() == "VendorX");
  CHECK(lengsuo::fp::WebGLRenderer() == "RendererY");
  CHECK(lengsuo::fp::AudioSeed() != 0);
  // Sub-pixel but above LayoutUnit quantization (1/64 = 0.015625).
  const double jitter = lengsuo::fp::RectsJitterPx();
  CHECK(jitter > 0.015625 && jitter < 0.5);

  // 3. Zero slots keep the dimension native even inside a valid payload.
  lengsuo::fp::InitFromSwitch(std::string_view(
      "0123456789abcdef:0:0:0:0:0:0:0:VendorX:RendererY:0:0"));
  CHECK(lengsuo::fp::Enabled());
  CHECK(lengsuo::fp::AudioSeed() == 0);
  CHECK(lengsuo::fp::RectsJitterPx() == 0.0);
  CHECK(lengsuo::fp::WebGLVendor().empty());   // gpu slot off
  CHECK(lengsuo::fp::HardwareConcurrency() == 0);

  // 4. Determinism: same payload twice gives identical outputs.
  const char* payload = "abcdef0123456789:1:1:1:1:1:1:1:V:R:16:4";
  lengsuo::fp::InitFromSwitch(std::string_view(payload));
  const double j1 = lengsuo::fp::RectsJitterPx();
  const std::uint64_t a1 = lengsuo::fp::AudioSeed();
  const std::uint64_t w1 = lengsuo::fp::WebGLParamsSeed();
  lengsuo::fp::InitFromSwitch(std::string_view(payload));
  CHECK(lengsuo::fp::RectsJitterPx() == j1);
  CHECK(lengsuo::fp::AudioSeed() == a1);
  CHECK(lengsuo::fp::WebGLParamsSeed() == w1);
  lengsuo::fp::InitFromSwitch(std::string_view(
      "1234567890abcdef:1:1:1:1:1:1:1:V:R:16:4"));
  CHECK(lengsuo::fp::RectsJitterPx() != j1);

  // 5. WebGL param bias: allow-list perturbs, state params pass through.
  lengsuo::fp::InitFromSwitch(std::string_view(
      "abcdef0123456789:0:0:0:0:0:0:1:V:R:0:0"));
  const std::uint32_t caps[] = {0x0D33, 0x8073, 0x84E8, 0x8517, 0x8869,
                                0x8872, 0x88FF, 0x8B4C, 0x8B4D, 0x8D57,
                                0x8D6B, 0x8DFB, 0x8DFC, 0x8DFD};
  for (const std::uint32_t cap : caps) {
    for (const std::int32_t v : {16384, 2048, 1024, 256, 16, 8, 1}) {
      const std::int32_t biased = lengsuo::fp::WebGLParamBias(cap, v);
      const std::int64_t bound = v > 8 ? v / 4 : 1;
      CHECK(biased >= v - bound && biased <= v + bound);
      CHECK(biased >= 1);
      CHECK(biased == lengsuo::fp::WebGLParamBias(cap, v));  // stable
    }
  }
  // State params must be untouched.
  for (const std::uint32_t state : {0x84E0, 0x0BA2, 0x0C10, 0x0B70, 0x8894}) {
    CHECK(lengsuo::fp::WebGLParamBias(state, 7) == 7);
    CHECK(lengsuo::fp::WebGLParamBias(state, 0) == 0);
  }
  // Slot off -> pure passthrough.
  lengsuo::fp::InitFromSwitch(std::string_view(
      "abcdef0123456789:0:0:0:0:0:0:0:V:R:0:0"));
  CHECK(lengsuo::fp::WebGLParamBias(0x0D33, 16384) == 16384);

  std::printf("fp_overrides_unitTests: %d passes, %d failures\n", g_passes,
              g_failures);
  return g_failures == 0 ? 0 : 1;
}
