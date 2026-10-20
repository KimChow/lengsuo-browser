#include "storage_consistency.h"

#include <cstddef>
#include <string_view>

namespace lengsuo::fp {
namespace {

char AsciiLower(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (AsciiLower(a[i]) != AsciiLower(b[i])) {
      return false;
    }
  }
  return true;
}

std::string_view StripLeadingDot(std::string_view value) {
  if (!value.empty() && value.front() == '.') {
    value.remove_prefix(1);
  }
  return value;
}

// Duplicate facts are outside the contract. First match wins; the choice is
// flagged in the task-F report rather than settled here.
const CookieFact* FindCookie(const std::vector<CookieFact>& cookies,
                             const ConsistencyRule& rule) {
  for (const CookieFact& cookie : cookies) {
    if (cookie.name == rule.cookie_name &&
        CookieDomainMatches(cookie.domain, rule.cookie_domain)) {
      return &cookie;
    }
  }
  return nullptr;
}

const StorageFact* FindStorage(const std::vector<StorageFact>& storage,
                               const ConsistencyRule& rule) {
  for (const StorageFact& fact : storage) {
    if (fact.origin == rule.storage_origin && fact.key == rule.storage_key) {
      return &fact;
    }
  }
  return nullptr;
}

}  // namespace

std::vector<Mismatch> CheckConsistency(
    const std::vector<CookieFact>& cookies,
    const std::vector<StorageFact>& storage,
    const std::vector<ConsistencyRule>& rules) {
  std::vector<Mismatch> mismatches;
  for (const ConsistencyRule& rule : rules) {
    const CookieFact* cookie = FindCookie(cookies, rule);
    const StorageFact* fact = FindStorage(storage, rule);
    // Both absent: the rule describes another environment, not a conflict.
    if (cookie == nullptr && fact == nullptr) {
      continue;
    }
    const std::string_view cookie_value =
        cookie != nullptr ? std::string_view(cookie->value) : std::string_view();
    const std::string_view storage_value =
        fact != nullptr ? std::string_view(fact->value) : std::string_view();
    if (cookie != nullptr && fact != nullptr && cookie_value == storage_value) {
      continue;
    }
    mismatches.push_back(
        Mismatch{rule, std::string(cookie_value), std::string(storage_value)});
  }
  return mismatches;
}

bool CookieDomainMatches(std::string_view host, std::string_view domain) {
  host = StripLeadingDot(host);
  domain = StripLeadingDot(domain);
  if (domain.empty()) {
    return false;
  }
  if (EqualsIgnoreCase(host, domain)) {
    return true;
  }
  return host.size() > domain.size() &&
         host[host.size() - domain.size() - 1] == '.' &&
         EqualsIgnoreCase(host.substr(host.size() - domain.size()), domain);
}

}  // namespace lengsuo::fp
