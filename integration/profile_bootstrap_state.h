#ifndef LENGSUO_INTEGRATION_PROFILE_BOOTSTRAP_STATE_H_
#define LENGSUO_INTEGRATION_PROFILE_BOOTSTRAP_STATE_H_
#include <functional>
#include <optional>
#include <string>
#include "lengsuo/native/profile_wire.h"
namespace lengsuo {
// Browser startup only. No credentials, entitlement or network decisions.
class ProfileBootstrapState {
 public:
  using Reader = std::function<std::optional<std::string>()>;
  bool Initialize(bool requested, bool safe_local_path, const Reader& read);
  const ParsedProfile* profile() const { return profile_ ? &*profile_ : nullptr; }
 private:
  bool attempted_ = false;
  std::optional<ParsedProfile> profile_;
};
}  // namespace lengsuo
#endif
