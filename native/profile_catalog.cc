#include "profile_catalog.h"

#include <cstdint>
#include <string_view>
#include <vector>

#include "wire_reader.h"

namespace lengsuo {
namespace {
constexpr std::size_t kMaxContainer = 4096;
constexpr std::size_t kMaxEntries = 16;
constexpr std::size_t kMaxBrands = 8;

bool ReadBrand(WireReader& reader, CatalogBrand& brand) {
  return reader.Text(brand.name, 32) && reader.Text(brand.version, 32);
}

bool ReadBrandList(WireReader& reader, std::vector<CatalogBrand>& out,
                   bool allow_empty) {
  std::uint32_t count;
  if (!reader.Number(count, 1) || count > kMaxBrands ||
      (!allow_empty && count == 0)) {
    return false;
  }
  out.resize(count);
  for (auto& brand : out) {
    if (!ReadBrand(reader, brand)) {
      return false;
    }
  }
  return true;
}
}  // namespace

std::optional<ParsedCatalog> DecodeCatalog(
    std::span<const std::uint8_t> bytes) {
  if (bytes.size() > kMaxContainer) {
    return std::nullopt;
  }
  WireReader reader(bytes);
  std::uint32_t value;
  for (const auto character : std::string_view("LBPC1")) {
    if (!reader.Number(value, 1) ||
        value != static_cast<unsigned char>(character)) {
      return std::nullopt;
    }
  }
  ParsedCatalog catalog;
  if (!reader.Number(catalog.schema_version, 4) ||
      catalog.schema_version != 1 ||
      !reader.Text(catalog.pool_version, 64, true)) {
    return std::nullopt;
  }
  if (!reader.Number(value, 2) || value == 0 || value > kMaxEntries) {
    return std::nullopt;
  }
  catalog.ua_entries.resize(value);
  for (auto& entry : catalog.ua_entries) {
    std::string face;
    if (!reader.Text(face, 12, true) || face != "ua" ||
        !reader.Number(entry.slot, 4) || !InRange(entry.slot, 1, 1048576) ||
        !reader.Text(entry.ua_string, 512) ||
        !ReadBrandList(reader, entry.brands, false) ||
        !ReadBrandList(reader, entry.full_versions, true) ||
        !reader.Text(entry.platform, 32) ||
        !reader.Text(entry.platform_version, 64) ||
        !reader.Text(entry.architecture, 32) ||
        !reader.Text(entry.bitness, 8) ||
        !reader.OptionalText(entry.model, 128)) {
      return std::nullopt;
    }
    std::uint8_t mobile;
    if (!reader.Byte(mobile) || mobile > 1) {
      return std::nullopt;
    }
    entry.mobile = mobile == 1;
  }
  // Slots identify wire pool positions; duplicates make resolution ambiguous.
  for (std::size_t i = 0; i < catalog.ua_entries.size(); ++i) {
    for (std::size_t j = i + 1; j < catalog.ua_entries.size(); ++j) {
      if (catalog.ua_entries[i].slot == catalog.ua_entries[j].slot) {
        return std::nullopt;
      }
    }
  }
  if (!reader.End()) {
    return std::nullopt;
  }
  return catalog;
}

bool CatalogMatchesProfile(const ParsedCatalog& catalog,
                           const ParsedProfile& profile) {
  return !catalog.pool_version.empty() &&
         catalog.pool_version == profile.pool_version;
}
}  // namespace lengsuo
