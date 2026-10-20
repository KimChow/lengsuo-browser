#include "l3_runtime.h"

#include <algorithm>

#include "l3_wire.h"

namespace lengsuo::fp {
namespace {

// ExtensionPin keeps a std::set, but L3Config is move-only through Parse();
// copying the ids out keeps L3Runtime trivially queryable afterwards.
std::vector<std::string> IdsOf(const ExtensionPin& extensions) {
  return std::vector<std::string>(extensions.Ids().begin(),
                                  extensions.Ids().end());
}

const PinnedEntry* FindIn(const std::vector<PinnedEntry>& entries,
                          const std::string& key) {
  const auto found = std::find_if(
      entries.begin(), entries.end(),
      [&key](const PinnedEntry& entry) { return entry.key == key; });
  return found == entries.end() ? nullptr : &*found;
}

}  // namespace

bool L3Runtime::Init(std::string_view wire, int64_t now_unix,
                     int64_t max_age_seconds) {
  // Single-shot: a renderer keeps the snapshot it started with so that a
  // later document can never silently change what the page already read.
  if (ready_) {
    return false;
  }
  L3Config config;
  if (!DecodeL3Wire(wire, now_unix, max_age_seconds, &config, nullptr)) {
    return false;
  }
  pins_ = std::move(config.pins);
  extensions_ = IdsOf(config.extensions);
  ready_ = true;
  return true;
}

const std::vector<PinnedEntry>* L3Runtime::LocalPins(
    const std::string& origin) const {
  if (!ready_) {
    return nullptr;
  }
  const DomainPin* domain = pins_.Find(origin);
  if (domain == nullptr || domain->local_storage.empty()) {
    return nullptr;
  }
  return &domain->local_storage;
}

const std::vector<PinnedEntry>* L3Runtime::SessionPins(
    const std::string& origin) const {
  if (!ready_) {
    return nullptr;
  }
  const DomainPin* domain = pins_.Find(origin);
  if (domain == nullptr || domain->session_storage.empty()) {
    return nullptr;
  }
  return &domain->session_storage;
}

bool L3Runtime::IsLocked(const std::string& origin,
                         const std::string& key) const {
  return ready_ && pins_.IsLocked(origin, key);
}

const std::string* L3Runtime::PinnedValue(const std::string& origin,
                                          const std::string& key) const {
  if (!ready_) {
    return nullptr;
  }
  const DomainPin* domain = pins_.Find(origin);
  if (domain == nullptr) {
    return nullptr;
  }
  // Local scope wins when a key is pinned in both: local storage is the scope
  // platforms read first, and the browser writes both from the same document.
  if (const PinnedEntry* entry = FindIn(domain->local_storage, key)) {
    return &entry->value;
  }
  if (const PinnedEntry* entry = FindIn(domain->session_storage, key)) {
    return &entry->value;
  }
  return nullptr;
}

bool L3Runtime::ExtensionAllowed(const std::string& id) const {
  return ready_ && std::find(extensions_.begin(), extensions_.end(), id) !=
                       extensions_.end();
}

L3Runtime& GetL3Runtime() {
  static L3Runtime runtime;
  return runtime;
}

}  // namespace lengsuo::fp
