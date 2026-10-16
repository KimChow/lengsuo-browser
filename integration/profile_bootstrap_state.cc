#include "lengsuo/integration/profile_bootstrap_state.h"
#include <cstdint>
#include <span>
namespace lengsuo {
ProfileBootstrapState::ProfileBootstrapState() = default;
ProfileBootstrapState::~ProfileBootstrapState() = default;
bool ProfileBootstrapState::Initialize(bool requested, bool safe_local_path,
                                       const Reader& read) {
  if (attempted_) return false;
  attempted_ = true;
  if (!requested) return true;
  if (!safe_local_path) return false;
  auto wire = read();
  if (!wire || wire->empty() || wire->size() > 4096) return false;
  auto parsed = DecodeProfile(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(wire->data()), wire->size()));
  if (!parsed) return false;
  profile_ = std::move(parsed);
  return true;
}
}  // namespace lengsuo
