#ifndef LENGSUO_INTEGRATION_PROFILE_BOOTSTRAP_STATE_H_
#define LENGSUO_INTEGRATION_PROFILE_BOOTSTRAP_STATE_H_
#include <functional>
#include <optional>
#include <string>
#include "lengsuo/native/profile_wire.h"
#include "lengsuo/native/profile_catalog.h"
namespace lengsuo {
// Browser startup only. No credentials, entitlement or network decisions.
class ProfileBootstrapState {
 public:
  using Reader = std::function<std::optional<std::string>()>;
  // Out-of-line: Chromium style requires it for complex members.
  ProfileBootstrapState();
  ~ProfileBootstrapState();
  ProfileBootstrapState(const ProfileBootstrapState&) = delete;
  ProfileBootstrapState& operator=(const ProfileBootstrapState&) = delete;
  bool Initialize(bool requested, bool safe_local_path, const Reader& read);
  bool InitializeCatalog(bool requested, bool safe_local_path, const Reader& read);
  const ParsedProfile* profile() const { return profile_ ? &*profile_ : nullptr; }
  const CatalogUaEntry* ua() const;
 private:
  bool attempted_ = false;
  std::optional<ParsedProfile> profile_;
  bool catalog_attempted_ = false;
  std::optional<ParsedCatalog> catalog_;
};
}  // namespace lengsuo
#endif
