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
  // The standalone decoder supports schema 2, but browser-side WebRTC policy
  // injection is not accepted yet. Never silently ignore an explicit policy.
  if (!parsed || parsed->schema_version != 1) return false;
  profile_ = std::move(parsed);
  return true;
}
bool ProfileBootstrapState::InitializeCatalog(bool requested,
                                             bool safe_local_path,
                                             const Reader& read) {
  if (catalog_attempted_) return false;
  catalog_attempted_ = true;
  if (!attempted_) return false;
  if (!requested) return true;
  if (!profile_ || !safe_local_path) return false;
  auto wire = read();
  if (!wire || wire->empty() || wire->size() > 4096) return false;
  auto parsed = DecodeCatalog(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(wire->data()), wire->size()));
  if (!parsed || !ResolveCatalogUa(*parsed, *profile_)) return false;
  catalog_ = std::move(parsed);
  return true;
}
const CatalogUaEntry* ProfileBootstrapState::ua() const {
  return catalog_ && profile_ ? ResolveCatalogUa(*catalog_, *profile_) : nullptr;
}
}  // namespace lengsuo
