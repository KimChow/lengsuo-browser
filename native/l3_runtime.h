// l3_runtime - renderer-side L3 snapshot singleton (wiring task I).
//
// The renderer never parses profile files itself; the browser hands it one
// frozen L3W1 document and this object answers every later query:
//
//   * which key/value pairs are pinned for an origin (localStorage and
//     sessionStorage separately),
//   * whether a key is locked (pinned in either scope),
//   * the pinned value for a key (nullptr when absent - empty string is a
//     valid value, so nullptr must stay distinguishable),
//   * whether an extension id is on the environment whitelist.
//
// Init() is transactional and single-shot: the first call wins, later calls
// are rejected. A renderer therefore cannot be re-armed with a different
// snapshot mid-life, matching the browser's freeze-at-startup design.
#ifndef LENGSUO_NATIVE_L3_RUNTIME_H_
#define LENGSUO_NATIVE_L3_RUNTIME_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "storage_pin.h"

namespace lengsuo::fp {

class L3Runtime {
 public:
  L3Runtime() = default;
  ~L3Runtime() = default;
  L3Runtime(const L3Runtime&) = delete;
  L3Runtime& operator=(const L3Runtime&) = delete;

  // Accepts a full L3W1 document. Returns false (leaving the previous state
  // untouched) when the document is malformed, fails its signature gate or
  // violates the launch consistency gate, or when the runtime was already
  // initialized.
  //
  // now_unix <= 0 skips the freshness window (the digest and the consistency
  // gate still apply). A renderer passes 0 because the browser already judged
  // freshness against its own clock before forwarding the document.
  bool Init(std::string_view wire, int64_t now_unix, int64_t max_age_seconds);

  bool ready() const { return ready_; }

  // Pinned localStorage entries for the origin, in parse order; nullptr when
  // the origin has none.
  const std::vector<PinnedEntry>* LocalPins(
      const std::string& origin) const;

  // Pinned sessionStorage entries; nullptr when none.
  const std::vector<PinnedEntry>* SessionPins(
      const std::string& origin) const;

  // True when the key is pinned for the origin in either scope.
  bool IsLocked(const std::string& origin, const std::string& key) const;

  // The pinned value; nullptr when the key is not pinned. An empty string is
  // a valid pinned value and returns a non-null pointer.
  const std::string* PinnedValue(const std::string& origin,
                                 const std::string& key) const;

  // Extension whitelist membership; always false before Init().
  bool ExtensionAllowed(const std::string& id) const;

 private:
  bool ready_ = false;
  StoragePin pins_;
  // Kept as parallel simple containers: ExtensionPin is not copy-visible and
  // the whitelist is tiny; ids_ mirrors config.extensions for lookups.
  std::vector<std::string> extensions_;
};

// Process-wide access point. The renderer is single-threaded for Blink; Mojo
// replies land on the same sequence, so no lock is taken (matches how
// canvas_apply stores its noise seed).
L3Runtime& GetL3Runtime();

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_L3_RUNTIME_H_
