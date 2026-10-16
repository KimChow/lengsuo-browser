#include "lengsuo/integration/profile_bootstrap_state.h"
#include <cassert>
#include <string>
namespace {
#include "lengsuo/native/golden.inc"
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
}
