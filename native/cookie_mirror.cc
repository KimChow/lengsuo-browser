#include "cookie_mirror.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace lengsuo::fp {
namespace {

// Two declared domains describe the same cookie when each one covers the
// other: after the leading dot is dropped and case is folded, that is only
// true for equal hosts (".example.com" and "example.com" are one domain in
// RFC 6265 5.3, while "a.example.com" stays distinct from "example.com").
bool SameDomain(const std::string& left, const std::string& right) {
  return CookieDomainMatches(left, right) &&
         CookieDomainMatches(right, left);
}

// The number of characters of a declared domain, used to rank a match: the
// most specific domain wins, the way the browser resolves two cookies that
// share a name. The leading dot is not counted - it is RFC 6265 5.3 syntax
// for the same domain, not a different one.
std::size_t EffectiveDomainLength(const std::string& domain) {
  return !domain.empty() && domain.front() == '.' ? domain.size() - 1
                                                  : domain.size();
}

// Splits a Chromium cookie line ("name=value; name2=value2") into its
// entries. The browser never emits a name containing '=' or ';', but a
// defensive parse keeps one malformed entry from taking the whole answer
// down.
std::vector<std::string_view> SplitCookieLine(std::string_view line) {
  std::vector<std::string_view> entries;
  std::size_t start = 0;
  while (start <= line.size()) {
    const std::size_t separator = line.find(';', start);
    const std::size_t end =
        separator == std::string_view::npos ? line.size() : separator;
    std::string_view entry = line.substr(start, end - start);
    // Chromium writes "; " between entries.
    while (!entry.empty() && (entry.front() == ' ' || entry.front() == '\t')) {
      entry.remove_prefix(1);
    }
    while (!entry.empty() && (entry.back() == ' ' || entry.back() == '\t')) {
      entry.remove_suffix(1);
    }
    if (!entry.empty()) {
      entries.push_back(entry);
    }
    if (separator == std::string_view::npos) {
      break;
    }
    start = separator + 1;
  }
  return entries;
}

// The name part of a "name=value" entry.
std::string_view EntryName(std::string_view entry) {
  const std::size_t equals = entry.find('=');
  return equals == std::string_view::npos ? entry : entry.substr(0, equals);
}

}  // namespace

void CookieMirror::Init(std::vector<CookieFact> cookies,
                        std::vector<ConsistencyRule> rules) {
  // Collapse repeated (domain, name) pairs the way StoragePin collapses
  // repeated (origin, scope, key): the last value wins, the first position
  // is kept. Without this the most specific domain could be shadowed by
  // whichever entry happened to be declared last.
  std::vector<CookieFact> collapsed;
  for (CookieFact& fact : cookies) {
    const auto existing = std::find_if(
        collapsed.begin(), collapsed.end(),
        [&fact](const CookieFact& other) {
          return other.name == fact.name && SameDomain(other.domain, fact.domain);
        });
    if (existing == collapsed.end()) {
      collapsed.push_back(std::move(fact));
    } else {
      existing->value = std::move(fact.value);
    }
  }
  cookies_ = std::move(collapsed);
  rules_ = std::move(rules);
}

std::optional<std::string> CookieMirror::Lookup(const std::string& host,
                                                const std::string& name) const {
  const CookieFact* best = nullptr;
  for (const CookieFact& fact : cookies_) {
    if (fact.name != name || !CookieDomainMatches(host, fact.domain)) {
      continue;
    }
    // Most specific domain wins; a tie keeps the earlier declaration.
    if (best == nullptr ||
        EffectiveDomainLength(fact.domain) > EffectiveDomainLength(best->domain)) {
      best = &fact;
    }
  }
  if (best == nullptr) {
    return std::nullopt;
  }
  return best->value;
}

bool CookieMirror::IsLocked(const std::string& host,
                            const std::string& name) const {
  // A pinned cookie is read-only, exactly like a pinned localStorage key.
  //
  // A rule on its own does not lock anything: the localStorage side locks a
  // key only when that key is pinned, so locking a cookie that only a rule
  // names would make the two halves disagree. The launch consistency gate
  // (l3_wire) already guarantees that a rule whose storage side is pinned has
  // its cookie side declared, so a valid snapshot never needs the extra rule.
  for (const CookieFact& fact : cookies_) {
    if (fact.name == name && CookieDomainMatches(host, fact.domain)) {
      return true;
    }
  }
  return false;
}

std::optional<ConsistencyRule> CookieMirror::PairFor(
    const std::string& host, const std::string& name) const {
  for (const ConsistencyRule& rule : rules_) {
    if (rule.cookie_name == name &&
        CookieDomainMatches(host, rule.cookie_domain)) {
      return rule;
    }
  }
  return std::nullopt;
}

std::string CookieMirror::MergeCookieLine(const std::string& host,
                                          std::string_view browser_line) const {
  // The pinned cookies of this host, grouped by name. The representative of a
  // name is the most specific domain, the same one Lookup returns, so the
  // merged line and the single-value query never disagree.
  std::vector<const CookieFact*> pinned;
  for (const CookieFact& fact : cookies_) {
    if (!CookieDomainMatches(host, fact.domain)) {
      continue;
    }
    const auto same_name = std::find_if(
        pinned.begin(), pinned.end(),
        [&fact](const CookieFact* other) { return other->name == fact.name; });
    if (same_name == pinned.end()) {
      pinned.push_back(&fact);
    } else if (EffectiveDomainLength(fact.domain) > EffectiveDomainLength((*same_name)->domain)) {
      *same_name = &fact;
    }
  }
  if (pinned.empty()) {
    return std::string(browser_line);
  }

  // The browser line carries no domain and no path, so a pinned cookie can
  // only be matched by name. A matching entry keeps its place in the line but
  // takes the pinned value: the snapshot is authoritative, and a stale value
  // sitting next to the pinned one is exactly the drift the platform sees.
  std::vector<std::string> entries;
  std::vector<bool> pinned_used(pinned.size(), false);
  for (const std::string_view entry : SplitCookieLine(browser_line)) {
    const std::string_view entry_name = EntryName(entry);
    bool replaced = false;
    for (std::size_t i = 0; i < pinned.size(); ++i) {
      if (pinned[i]->name == entry_name) {
        entries.emplace_back(pinned[i]->name + "=" + pinned[i]->value);
        pinned_used[i] = true;
        replaced = true;
      }
    }
    if (!replaced) {
      entries.emplace_back(entry);
    }
  }

  // A pinned cookie the browser does not carry is appended, so the page still
  // sees the value the environment declared.
  for (std::size_t i = 0; i < pinned.size(); ++i) {
    if (!pinned_used[i]) {
      entries.emplace_back(pinned[i]->name + "=" + pinned[i]->value);
    }
  }

  std::string merged;
  for (const std::string& entry : entries) {
    if (!merged.empty()) {
      merged += "; ";
    }
    merged += entry;
  }
  return merged;
}

}  // namespace lengsuo::fp
