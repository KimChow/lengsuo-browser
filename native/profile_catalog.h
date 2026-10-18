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

// One resolved "gpu" slot: the two WebGL face strings. Kept separate from UA
// entries so a profile can pin GPU and UA independently.
struct CatalogGpuEntry {
  std::uint32_t slot{};
  std::string vendor;
  std::string renderer;
};
struct ParsedCatalog {
  std::uint32_t schema_version{};
  std::string pool_version;
  std::vector<CatalogUaEntry> ua_entries;
  // Absent in catalog schema 1; optional for backward compatibility.
  std::vector<CatalogGpuEntry> gpu_entries;
};

// All malformed input returns nullopt; no partially parsed catalog escapes.
std::optional<ParsedCatalog> DecodeCatalog(std::span<const std::uint8_t> bytes);

// Binding check: a catalog may only serve a profile from the same pool.
bool CatalogMatchesProfile(const ParsedCatalog& catalog,
                           const ParsedProfile& profile);
// Returns a borrowed entry only for an exact pool and unique UA-slot match.
// The caller must keep the catalog alive and immutable while using the entry.
const CatalogUaEntry* ResolveCatalogUa(const ParsedCatalog& catalog,
                                      const ParsedProfile& profile);
// Returns the unique GPU face for profile.slots[4], or nullptr when the
// catalog carries no GPU section or the slot is missing/ambiguous. Caller
// must keep the catalog alive.
const CatalogGpuEntry* ResolveCatalogGpu(const ParsedCatalog& catalog,
                                         const ParsedProfile& profile);
}  // namespace lengsuo
#endif
