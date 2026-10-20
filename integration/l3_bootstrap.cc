#include "lengsuo/integration/l3_bootstrap.h"

#include <string_view>

namespace lengsuo {
namespace {

bool ExceedsLimit(const std::string& wire) {
  return wire.empty() || wire.size() > fp::kMaxL3WireBytes;
}

}  // namespace

L3BootstrapState::L3BootstrapState() = default;
L3BootstrapState::~L3BootstrapState() = default;

bool L3BootstrapState::Initialize(bool requested,
                                 bool safe_local_path,
                                 const Reader& read,
                                 int64_t now_unix,
                                 int64_t max_age_seconds) {
  // Single-shot: a second call never re-reads, so a swapped file on disk
  // cannot change the snapshot after the browser has started.
  if (attempted_) {
    return false;
  }
  attempted_ = true;
  if (!requested) {
    return true;
  }
  // An unusable path or clock window is a configuration error, not "absent".
  if (!safe_local_path || now_unix <= 0 || max_age_seconds <= 0) {
    load_failed_ = true;
    return false;
  }
  const std::optional<std::string> wire = read();
  if (!wire || ExceedsLimit(*wire)) {
    load_failed_ = true;
    return false;
  }

  fp::L3Config decoded;
  const bool ok = fp::DecodeL3Wire(std::string_view(*wire), now_unix,
                                   max_age_seconds, &decoded, nullptr);
  // The local copy of the wire is dropped here: the decoded snapshot is what
  // lives on, so the path and raw bytes (which carry pinned secrets) do not
  // stay resident.
  if (!ok) {
    load_failed_ = true;
    return false;
  }
  config_ = std::move(decoded);
  return true;
}

}  // namespace lengsuo
