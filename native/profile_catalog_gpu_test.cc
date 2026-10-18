// Isolation test for the catalog's optional trailing GPU section.
//
// The GPU section is a backward-compatible extension of LBPC1: a catalog
// without it must decode exactly as before (see profile_catalog_test.cc for
// the frozen golden), and one with it must resolve faces by the profile's gpu
// slot (slots[4]) with an exact pool match. Split from the main catalog test to
// keep both files under the 400-line soft target.
#include "profile_catalog.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
int checks = 0;

void CheckAt(bool ok, int line) {
  ++checks;
  if (!ok) {
    std::printf("FAIL at line %d (check %d)\n", line, checks);
    std::exit(1);
  }
}

#define CHECK(cond) CheckAt((cond), __LINE__)

void Append(Bytes& out, const Bytes& tail) {
  out.insert(out.end(), tail.begin(), tail.end());
}

Bytes Text(std::string_view value) {
  Bytes out;
  out.push_back(static_cast<std::uint8_t>((value.size() >> 8) & 255));
  out.push_back(static_cast<std::uint8_t>(value.size() & 255));
  for (const char c : value) out.push_back(static_cast<std::uint8_t>(c));
  return out;
}

Bytes Fixed(std::uint32_t value, std::size_t width) {
  Bytes out;
  for (std::size_t i = width; i > 0; --i)
    out.push_back(static_cast<std::uint8_t>((value >> ((i - 1) * 8)) & 255));
  return out;
}

struct GpuSpec {
  std::uint32_t slot = 3;
  std::string vendor = "Google Inc. (NVIDIA)";
  std::string renderer = "ANGLE (NVIDIA, NVIDIA GeForce RTX 3060, D3D11)";
};

// Minimal but complete schema-1 catalog with one UA entry (slot 7, pool p1).
Bytes MinimalCatalog(const std::vector<GpuSpec>& gpus) {
  Bytes out;
  for (const char c : std::string("LBPC1")) out.push_back((std::uint8_t)c);
  Append(out, Fixed(1, 4));
  Append(out, Text("p1"));
  Append(out, Fixed(1, 2));
  Append(out, Text("ua"));
  Append(out, Fixed(7, 4));
  Append(out, Text("Synth/1.0"));
  out.push_back(1);
  Append(out, Text("Alpha"));
  Append(out, Text("1.2"));
  out.push_back(0);
  Append(out, Text("TestOS"));
  Append(out, Text("10.0.0"));
  Append(out, Text("x86"));
  Append(out, Text("64"));
  Append(out, Text(""));
  out.push_back(0);
  if (!gpus.empty()) {
    Append(out, Text("gpu"));
    Append(out, Fixed(static_cast<std::uint32_t>(gpus.size()), 2));
    for (const auto& g : gpus) {
      Append(out, Fixed(g.slot, 4));
      Append(out, Text(g.vendor));
      Append(out, Text(g.renderer));
    }
  }
  return out;
}

lengsuo::ParsedProfile Profile(std::uint32_t gpu_slot,
                               std::string pool = "p1") {
  lengsuo::ParsedProfile p;
  p.pool_version = pool;
  p.slots[4] = gpu_slot;
  p.slots[5] = 7;
  return p;
}

}  // namespace

int main() {
  const Bytes base = MinimalCatalog({});
  const auto no_gpu = lengsuo::DecodeCatalog(base);
  CHECK(no_gpu.has_value());
  CHECK(no_gpu->gpu_entries.empty());
  CHECK(no_gpu->ua_entries.size() == 1);
  CHECK(lengsuo::ResolveCatalogGpu(*no_gpu, Profile(3)) == nullptr);

  const Bytes with = MinimalCatalog({GpuSpec{},
                                     GpuSpec{4, "AMD", "ANGLE (AMD RX 6600)"}});
  const auto parsed = lengsuo::DecodeCatalog(with);
  CHECK(parsed.has_value());
  CHECK(parsed->gpu_entries.size() == 2);
  CHECK(parsed->gpu_entries[0].slot == 3);
  CHECK(parsed->gpu_entries[1].renderer == "ANGLE (AMD RX 6600)");
  CHECK(parsed->ua_entries.size() == 1);

  CHECK(lengsuo::ResolveCatalogGpu(*parsed, Profile(4)) ==
        &parsed->gpu_entries[1]);
  CHECK(lengsuo::ResolveCatalogGpu(*parsed, Profile(3)) ==
        &parsed->gpu_entries[0]);
  CHECK(lengsuo::ResolveCatalogGpu(*parsed, Profile(99)) == nullptr);
  CHECK(lengsuo::ResolveCatalogGpu(*parsed, Profile(3, "p2")) == nullptr);

  auto ambiguous = *parsed;
  ambiguous.gpu_entries.push_back(ambiguous.gpu_entries[0]);
  CHECK(lengsuo::ResolveCatalogGpu(ambiguous, Profile(3)) == nullptr);

  // Truncations strictly inside the GPU section must be rejected; cutting the
  // section off entirely (n == base.size()) is the legacy schema-1 payload and
  // must keep decoding.
  int truncated_rejected = 0;
  for (std::size_t n = base.size() + 1; n < with.size(); ++n) {
    const Bytes cut(with.begin(),
                    with.begin() + static_cast<Bytes::difference_type>(n));
    if (!lengsuo::DecodeCatalog(cut)) ++truncated_rejected;
  }
  CHECK(truncated_rejected == static_cast<int>(with.size() - base.size() - 1));
  CHECK(lengsuo::DecodeCatalog(base).has_value());

  Bytes trailing = with;
  trailing.push_back(0);
  CHECK(!lengsuo::DecodeCatalog(trailing));

  std::printf("PASS %d assertions\n", checks);
  return 0;
}
