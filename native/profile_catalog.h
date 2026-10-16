#ifndef LENGSUO_NATIVE_PROFILE_CATALOG_H_
#define LENGSUO_NATIVE_PROFILE_CATALOG_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "profile_wire.h"

namespace lengsuo {
// Decoding only: no signature/lease verification, runner authorization or
// browser injection is implied by a decoded catalog.
struct CatalogBrand {
  std::string name;
  std::string version;
};

// One resolved "ua" slot: exactly the values the two browser exits need
// (override string and client-hints metadata). Fields the catalog does not
// carry stay upstream; nothing is fabricated here.
struct CatalogUaEntry {
  std::uint32_t slot{};
  std::string ua_string;
  std::vector<CatalogBrand> brands;
  std::vector<CatalogBrand> full_versions;
  std::string platform;
  std::string platform_version;
  std::string architecture;
  std::string bitness;
  std::string model;
  bool mobile{};
};

struct ParsedCatalog {
  std::uint32_t schema_version{};
  std::string pool_version;
  std::vector<CatalogUaEntry> ua_entries;
};

// All malformed input returns nullopt; no partially parsed catalog escapes.
std::optional<ParsedCatalog> DecodeCatalog(std::span<const std::uint8_t> bytes);

// Binding check: a catalog may only serve a profile from the same pool.
bool CatalogMatchesProfile(const ParsedCatalog& catalog,
                           const ParsedProfile& profile);
}  // namespace lengsuo
#endif
