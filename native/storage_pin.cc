#include "storage_pin.h"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <utility>

namespace lengsuo::fp {
namespace {

constexpr char kDelimiter = '|';

bool IsControl(unsigned char character) {
  return character < 0x20 || character == 0x7f;
}

// "http://" or "https://" (lowercase, per the task brief) followed by a
// non-empty host part with no whitespace/control bytes or delimiters.
bool ValidOrigin(const std::string& origin) {
  std::string_view rest;
  if (origin.rfind("https://", 0) == 0) {
    rest = std::string_view(origin).substr(8);
  } else if (origin.rfind("http://", 0) == 0) {
    rest = std::string_view(origin).substr(7);
  } else {
    return false;
  }
  if (rest.empty()) {
    return false;
  }
  for (const unsigned char character : rest) {
    if (IsControl(character) || character == ' ' || character == kDelimiter) {
      return false;
    }
  }
  return true;
}

bool ValidKey(const std::string& key) {
  if (key.empty()) {
    return false;
  }
  for (const unsigned char character : key) {
    if (IsControl(character)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool StoragePin::Parse(const std::string& wire) {
  if (wire.empty()) {
    return false;
  }

  // Build into a scratch object so any failure leaves `*this` untouched.
  StoragePin parsed;
  std::size_t start = 0;
  while (start < wire.size()) {
    const std::size_t newline = wire.find('\n', start);
    const std::size_t end =
        newline == std::string::npos ? wire.size() : newline;
    std::string line = wire.substr(start, end - start);
    start = newline == std::string::npos ? wire.size() : newline + 1;

    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || !parsed.ParseLine(line)) {
      return false;
    }
  }

  domains_ = std::move(parsed.domains_);
  return true;
}

bool StoragePin::ParseLine(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t delimiter = line.find(kDelimiter, start);
    if (delimiter == std::string::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, delimiter - start));
    start = delimiter + 1;
  }
  if (fields.size() != 4) {
    return false;
  }

  const std::string& origin = fields[0];
  const std::string& scope = fields[1];
  const std::string& key = fields[2];
  const std::string& value = fields[3];

  if (!ValidOrigin(origin) || !ValidKey(key)) {
    return false;
  }
  if (scope != "local" && scope != "session") {
    return false;
  }

  auto domain = std::find_if(
      domains_.begin(), domains_.end(),
      [&origin](const DomainPin& entry) { return entry.origin == origin; });
  if (domain == domains_.end()) {
    domains_.push_back(DomainPin{origin, {}, {}});
    domain = std::prev(domains_.end());
  }

  std::vector<PinnedEntry>& entries =
      scope == "local" ? domain->local_storage : domain->session_storage;
  auto entry = std::find_if(
      entries.begin(), entries.end(),
      [&key](const PinnedEntry& pinned) { return pinned.key == key; });
  if (entry != entries.end()) {
    entry->value = value;
  } else {
    entries.push_back(PinnedEntry{key, value});
  }
  return true;
}

const DomainPin* StoragePin::Find(const std::string& origin) const {
  for (const DomainPin& domain : domains_) {
    if (domain.origin == origin) {
      return &domain;
    }
  }
  return nullptr;
}

bool StoragePin::IsLocked(const std::string& origin,
                          const std::string& key) const {
  const DomainPin* domain = Find(origin);
  if (domain == nullptr) {
    return false;
  }
  const auto pinned = [&key](const PinnedEntry& entry) {
    return entry.key == key;
  };
  return std::any_of(domain->local_storage.begin(), domain->local_storage.end(),
                     pinned) ||
         std::any_of(domain->session_storage.begin(),
                     domain->session_storage.end(), pinned);
}

std::size_t StoragePin::size() const {
  std::size_t total = 0;
  for (const DomainPin& domain : domains_) {
    total += domain.local_storage.size() + domain.session_storage.size();
  }
  return total;
}

}  // namespace lengsuo::fp
