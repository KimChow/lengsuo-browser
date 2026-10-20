#ifndef LENGSUO_NATIVE_STORAGE_PIN_H_
#define LENGSUO_NATIVE_STORAGE_PIN_H_

#include <cstddef>
#include <string>
#include <vector>

namespace lengsuo::fp {

struct PinnedEntry {
  std::string key;
  std::string value;
};

// A single origin's pin table. origin looks like "https://mms.pinduoduo.com".
struct DomainPin {
  std::string origin;
  std::vector<PinnedEntry> local_storage;
  std::vector<PinnedEntry> session_storage;
};

// Per-origin DOM storage pins: values written before page script runs and
// treated as read-only thereafter (Reference semantics: pinned means locked).
//
// Wire format: one pin per line, four '|'-separated segments:
//
//   <origin>|<scope>|<key>|<value>
//
//   origin  "http://..." or "https://..." (lowercase scheme, non-empty rest)
//   scope   "local" (localStorage) or "session" (sessionStorage)
//   key     non-empty, no control characters
//   value   may be empty; '|' and newlines are not representable
//
// A single trailing "\n" or "\r\n" on the last line is accepted. Empty input,
// blank lines, unknown scopes, malformed origins and any segment count other
// than four are rejected. Values containing '|' therefore fail closed instead
// of being silently truncated.
//
// Duplicates: a repeated (origin, scope, key) keeps one entry whose value is
// the last one parsed; position is that of the first occurrence.
//
// The wire encoding is not fixed by the L3 task brief; this grammar is the
// contract recorded in the header so callers and tests share one definition.
class StoragePin {
 public:
  // Replaces the current table only after the whole input validates. On
  // failure returns false and leaves the previous table unchanged.
  bool Parse(const std::string& wire);

  // Exact origin match. Returns nullptr when the origin has no pinned entries.
  const DomainPin* Find(const std::string& origin) const;

  // True when `key` is pinned for `origin` in either storage area. The
  // contract has no scope argument, so either area locks the key.
  bool IsLocked(const std::string& origin, const std::string& key) const;

  // Total number of pinned key/value pairs across both storage areas and all
  // origins (duplicates count once).
  std::size_t size() const;

  // Read-only view in parse order; callers must not mutate through it.
  const std::vector<DomainPin>& Domains() const { return domains_; }

 private:
  bool ParseLine(const std::string& line);

  std::vector<DomainPin> domains_;
};

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_STORAGE_PIN_H_
