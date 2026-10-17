#include "lengsuo/integration/profile_bootstrap_state.h"
#include <cassert>
#include <string>
namespace {
#include "lengsuo/native/golden.inc"
constexpr unsigned char kGoldenCatalog[] = {
    76,  66,  80,  67,  49,  0,   0,   0,   1,   0,   2,   112,
    49,  0,   1,   0,   2,   117, 97,  0,   0,   0,   7,   0,
    19,  83,  121, 110, 116, 104, 47,  49,  46,  48,  32,  84,
    101, 115, 116, 32,  69,  110, 118, 55,  2,   0,   5,   65,
    108, 112, 104, 97,  0,   3,   49,  46,  50,  0,   4,   66,
    101, 116, 97,  0,   3,   51,  46,  52,  2,   0,   5,   65,
    108, 112, 104, 97,  0,   7,   49,  46,  50,  46,  55,  46,
    56,  0,   4,   66,  101, 116, 97,  0,   7,   51,  46,  52,
    46,  57,  46,  48,  0,   6,   84,  101, 115, 116, 79,  83,
    0,   6,   49,  48,  46,  48,  46,  48,  0,   3,   120, 56,
    54,  0,   2,   54,  52,  0,   0,   0};
std::string Golden() { return std::string(reinterpret_cast<const char*>(kGolden), sizeof(kGolden)); }
}  // namespace
int main() {
  using lengsuo::ProfileBootstrapState;
  int reads = 0;
  auto valid = [&]() -> std::optional<std::string> { ++reads; return Golden(); };
  ProfileBootstrapState absent;
  assert(absent.Initialize(false, false, valid));
  assert(!absent.profile() && reads == 0);
  ProfileBootstrapState unsafe;
  assert(!unsafe.Initialize(true, false, valid));
  assert(!unsafe.profile() && reads == 0);
  ProfileBootstrapState missing;
  assert(!missing.Initialize(true, true, [] { return std::optional<std::string>(); }));
  for (auto wire : {std::string(), std::string(4097, 'x'), std::string("bad")}) {
    ProfileBootstrapState state;
    assert(!state.Initialize(true, true, [&] { return std::optional(wire); }));
    assert(!state.profile());
    assert(!state.Initialize(true, true, valid));
    assert(!state.profile());
  }
  ProfileBootstrapState state;
  assert(state.Initialize(true, true, valid));
  assert(reads == 1 && state.profile());
  const auto* frozen = state.profile();
  const auto environment = frozen->environment_id;
  assert(!state.Initialize(true, true, valid));
  assert(!state.Initialize(false, false, valid));
  assert(reads == 1 && state.profile() == frozen);
  assert(state.profile()->environment_id == environment);
  for (int scenario : {0, 1, 2, 3}) {
    ProfileBootstrapState candidate;
    assert(candidate.Initialize(true, true, valid));
    std::string catalog(reinterpret_cast<const char*>(kGoldenCatalog),
                        sizeof(kGoldenCatalog));
    if (scenario == 1) catalog[12] = '2';
    if (scenario == 2) catalog[22] = 8;
    if (scenario == 3) catalog.clear();
    const bool ok = candidate.InitializeCatalog(true, true,
        [&]() -> std::optional<std::string> { return catalog; });
    assert(ok == (scenario == 0));
    assert((candidate.ua() != nullptr) == ok);
    if (ok) assert(candidate.ua()->ua_string == "Synth/1.0 Test Env7");
    assert(!candidate.InitializeCatalog(false, false, valid));
  }
  assert(!state.InitializeCatalog(true, false, valid));
  assert(!state.ua());
  assert(!state.InitializeCatalog(false, false, valid));
}
