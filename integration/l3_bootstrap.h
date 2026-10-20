// l3_bootstrap - browser-side L3 configuration load, frozen at startup.
//
// The browser reads one managed L3W1 document before any renderer exists and
// keeps it immutable afterwards. Three outcomes are deliberately distinct:
//
//   * not requested        -> Initialize returns true, config() is nullptr.
//     No L3 statement, upstream behavior is untouched.
//   * requested but broken -> Initialize returns false. The caller must abort
//     startup: a half-valid snapshot is never downgraded to "no L3", because
//     that would silently unpin values the platform already observed.
//   * requested and valid  -> Initialize returns true, config() is non-null.
//
// Clock injection: the signature carries a unix timestamp and VerifySign
// needs "now", so `now_unix` is an explicit argument rather than a call to
// base::Time inside the decoder. Tests therefore never depend on wall time.
//
// Size limit: the profile wire caps at 4096 bytes, but an L3 document holds
// pin/cookie/rule tables and is legitimately larger, so this reader accepts up
// to fp::kMaxL3WireBytes (64 KiB) and nothing beyond it.
#ifndef LENGSUO_INTEGRATION_L3_BOOTSTRAP_H_
#define LENGSUO_INTEGRATION_L3_BOOTSTRAP_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "lengsuo/native/l3_wire.h"

namespace lengsuo {

// Command-line switch naming the managed L3 document. Transitional: Reference
// carries fingerprint state in a managed file read over IPC, never on the
// command line (see docs/fp-switch-contract.md). The value is treated as a
// secret-bearing path and must not be logged.
inline constexpr char kL3WireSwitch[] = "lengsuo-l3-wire";

// Default anti-rollback window for the document signature.
inline constexpr int64_t kL3MaxAgeSeconds = 7 * 24 * 60 * 60;

class L3BootstrapState {
 public:
  using Reader = std::function<std::optional<std::string>()>;

  L3BootstrapState();
  ~L3BootstrapState();
  L3BootstrapState(const L3BootstrapState&) = delete;
  L3BootstrapState& operator=(const L3BootstrapState&) = delete;

  // Single-shot. `requested` false succeeds without loading. A load or
  // verification failure returns false and leaves config() untouched.
  bool Initialize(bool requested,
                  bool safe_local_path,
                  const Reader& read,
                  int64_t now_unix,
                  int64_t max_age_seconds);

  const fp::L3Config* config() const { return config_ ? &*config_ : nullptr; }

  // True when a document was requested but failed to load; callers use this to
  // distinguish "absent" from "broken" when reporting startup failure.
  bool load_failed() const { return load_failed_; }

 private:
  bool attempted_ = false;
  bool load_failed_ = false;
  std::optional<fp::L3Config> config_;
};

}  // namespace lengsuo

#endif  // LENGSUO_INTEGRATION_L3_BOOTSTRAP_H_
