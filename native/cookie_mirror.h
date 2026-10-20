// cookie_mirror - the cookie half of the L3 cross-store lock (task M).
//
// The L3 document declares two things that have to stay in lockstep:
//   * the "cookies" section: the cookie values this environment is pinned to,
//   * the "rules" section: which cookie mirrors which localStorage key.
//
// The localStorage half is already enforced (Blink's StorageArea reads and
// writes through the frozen snapshot, see patches/0015-l3-storage.patch).
// This class is the cookie half: it answers "what value does this page see
// for this cookie" and "may this page write this cookie", always from the
// snapshot, never from the live cookie jar.
//
// The renderer cannot see the live jar: Blink's CookieJar talks to the
// network service over Mojo, so the jar lives in the browser process. The
// snapshot is the only cookie state a renderer can hold, and it is the very
// document the browser already hands down (--lengsuo-managed-l3). The Blink
// interception points that would consume this class are outside this task's
// file scope; they are described in docs/l3-reports/task-M.md. This task
// delivers the policy, not the patch.
//
// Contract:
//   * Domain matching reuses storage_consistency::CookieDomainMatches
//     (RFC 6265 5.1.3): ASCII case-insensitive, a leading dot is ignored, and
//     a declared domain covers the host itself and its dot-suffixed
//     subdomains. `host` is the concrete page host, `domain` is what the
//     document declared.
//   * Cookie *names* are compared byte-exactly. RFC 6265 5.4 makes names
//     case-sensitive, and a case-insensitive match would let a page write
//     "_BEE" over a pinned "_bee" and drift the environment.
//   * A pinned cookie is read-only. A cookie that only appears as the cookie
//     half of a mirror rule is read-only as well, so that a page cannot
//     create one half of a pair the environment does not carry.
//   * Nothing here logs, hashes or serializes a value: pinned values are
//     device identity, not diagnostics (docs/fp-switch-contract.md).
#ifndef LENGSUO_NATIVE_COOKIE_MIRROR_H_
#define LENGSUO_NATIVE_COOKIE_MIRROR_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "storage_consistency.h"

namespace lengsuo::fp {

// The cookie-side view of a frozen L3 snapshot. No IO, no clock, no browser
// state: the snapshot is the whole world.
class CookieMirror {
 public:
  CookieMirror() = default;

  // Installs the snapshot. The document was already validated by the L3
  // decoder (l3_wire), so this only copies - there is no second error path.
  //
  // Repeated (domain, name) entries collapse to one: the last value wins and
  // the first position is kept, the same rule StoragePin uses.
  void Init(std::vector<CookieFact> cookies,
            std::vector<ConsistencyRule> rules);

  // The value a page on `host` sees for `name`, or no value when the snapshot
  // declares no such cookie. An empty string is a valid value and comes back
  // as a value, not as "absent".
  //
  // When several declared cookies match the host and the name, the one with
  // the longest domain wins - the same precedence the browser applies to
  // cookies of the same name (RFC 6265 5.1.3).
  std::optional<std::string> Lookup(const std::string& host,
                                    const std::string& name) const;

  // True when a write to (host, name) has to be dropped.
  bool IsLocked(const std::string& host, const std::string& name) const;

  // The mirror rule that names this cookie, when there is one. The first
  // matching rule wins; a cookie named by several rules is a document
  // authoring error and is flagged in the task-M report.
  std::optional<ConsistencyRule> PairFor(const std::string& host,
                                          const std::string& name) const;

  // The browser's cookie line with the pinned cookies of `host` applied: a
  // pinned cookie replaces the browser's value in place, a pinned cookie the
  // browser does not carry is appended. Every other cookie and the browser's
  // own ordering are preserved.
  //
  // The browser line is Chromium's "name=value; name2=value2" form
  // (net/cookies/canonical_cookie.cc, AppendCookieLineEntry).
  std::string MergeCookieLine(const std::string& host,
                              std::string_view browser_line) const;

  // The pinned cookies a page on `host` can see, in declaration order.
  std::vector<CookieFact> VisibleCookies(const std::string& host) const;

  // Number of distinct pinned cookies.
  std::size_t size() const { return cookies_.size(); }

 private:
  std::vector<CookieFact> cookies_;
  std::vector<ConsistencyRule> rules_;
};

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_COOKIE_MIRROR_H_
