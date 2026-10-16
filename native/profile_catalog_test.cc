#include "profile_catalog.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
int checks = 0;

void Check(bool ok) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL " << checks << '\n';
    std::exit(1);
  }
}

void Bad(const Bytes& bytes) { Check(!lengsuo::DecodeCatalog(bytes)); }

// Frozen independently with Python from the documented LBPC1 layout; the
// in-test builders must reproduce it byte for byte (first assertion).
constexpr unsigned char kGoldenCatalog[] = {
    76,  66,  80,  67,  49,  0,   0,   0,   1,   0,   2,   112,
    49,  0,   1,   0,   2,   117, 97,  0,   0,   0,   7,   0,
    19,  83,  121, 110, 116, 104, 47,  49,  46,  48,  32,  84,
    101, 115, 116, 32,  69,  110, 118, 55,  2,   0,   5,   65,
    108, 112, 104, 97,  0,   3,   49,  46,  50,  0,   4,   66,
    101, 116, 97,  0,   3,   51,  46,  52,  2,   0,   5,   65,
    108, 112, 104, 97,  0,   7,   49,  46,  50,  46,  55,  46,
    56,  0,   4,   66,  101, 116, 97,  0,   7,   51,  46,  52,
    46,  57,  46,  48,  0,   6,   84,  101, 115, 116, 79,  83,
    0,   6,   49,  48,  46,  48,  46,  48,  0,   3,   120, 56,
    54,  0,   2,   54,  52,  0,   0,   0};

struct BrandSpec {
  std::string name;
  std::string version;
};

struct EntrySpec {
  std::string face = "ua";
  std::uint32_t slot = 7;
  std::string ua_string = "Synth/1.0 Test Env7";
  std::vector<BrandSpec> brands = {{"Alpha", "1.2"}, {"Beta", "3.4"}};
  int brand_count = -1;  // Negative: brands.size().
  std::vector<BrandSpec> full_versions = {{"Alpha", "1.2.7.8"},
                                          {"Beta", "3.4.9.0"}};
  int full_count = -1;  // Negative: full_versions.size().
  std::string platform = "TestOS";
  std::string platform_version = "10.0.0";
  std::string architecture = "x86";
  std::string bitness = "64";
  std::string model;  // Empty is allowed for model only.
  int mobile = 0;
};

struct CatalogSpec {
  std::string magic = "LBPC1";
  std::uint32_t container_version = 1;
  std::string pool_version = "p1";
  int entry_count = -1;  // Negative: entries.size().
  std::vector<EntrySpec> entries{EntrySpec{}};
};

void Append(Bytes& out, const Bytes& tail) {
  out.insert(out.end(), tail.begin(), tail.end());
}

Bytes Text(std::string_view value) {
  Bytes out;
  out.push_back(static_cast<std::uint8_t>((value.size() >> 8) & 255));
  out.push_back(static_cast<std::uint8_t>(value.size() & 255));
  for (const char c : value) {
    out.push_back(static_cast<std::uint8_t>(c));
  }
  return out;
}

Bytes Fixed(std::uint32_t value, std::size_t width) {
  Bytes out;
  for (std::size_t i = width; i > 0; --i) {
    out.push_back(static_cast<std::uint8_t>((value >> ((i - 1) * 8)) & 255));
  }
  return out;
}

Bytes BrandBytes(const BrandSpec& brand) {
  Bytes out = Text(brand.name);
  Append(out, Text(brand.version));
  return out;
}

int ResolvedCount(int explicit_count, std::size_t size) {
  return explicit_count >= 0 ? explicit_count
                             : static_cast<int>(size);
}

Bytes EntryBytes(const EntrySpec& entry) {
  Bytes out = Text(entry.face);
  Append(out, Fixed(entry.slot, 4));
  Append(out, Text(entry.ua_string));
  const int brand_count =
      ResolvedCount(entry.brand_count, entry.brands.size());
  out.push_back(static_cast<std::uint8_t>(brand_count));
  for (int i = 0; i < brand_count &&
                  i < static_cast<int>(entry.brands.size());
       ++i) {
    Append(out, BrandBytes(entry.brands[static_cast<std::size_t>(i)]));
  }
  const int full_count =
      ResolvedCount(entry.full_count, entry.full_versions.size());
  out.push_back(static_cast<std::uint8_t>(full_count));
  for (int i = 0; i < full_count &&
                  i < static_cast<int>(entry.full_versions.size());
       ++i) {
    Append(out,
           BrandBytes(entry.full_versions[static_cast<std::size_t>(i)]));
  }
  Append(out, Text(entry.platform));
  Append(out, Text(entry.platform_version));
  Append(out, Text(entry.architecture));
  Append(out, Text(entry.bitness));
  Append(out, Text(entry.model));
  out.push_back(static_cast<std::uint8_t>(entry.mobile));
  return out;
}

Bytes CatalogBytes(const CatalogSpec& spec) {
  Bytes out;
  for (const char c : spec.magic) {
    out.push_back(static_cast<std::uint8_t>(c));
  }
  Append(out, Fixed(spec.container_version, 4));
  Append(out, Text(spec.pool_version));
  const int entry_count = ResolvedCount(spec.entry_count, spec.entries.size());
  Append(out, Fixed(static_cast<std::uint32_t>(entry_count), 2));
  for (int i = 0; i < entry_count && i < static_cast<int>(spec.entries.size());
       ++i) {
    Append(out, EntryBytes(spec.entries[static_cast<std::size_t>(i)]));
  }
  return out;
}

Bytes Golden() { return CatalogBytes(CatalogSpec{}); }

lengsuo::ParsedProfile ProfileWithPool(std::string pool) {
  lengsuo::ParsedProfile profile;
  profile.pool_version = std::move(pool);
  return profile;
}

}  // namespace

int main() {
  const Bytes golden_bytes = Golden();
  Check(golden_bytes.size() == sizeof(kGoldenCatalog));
  Check(std::equal(golden_bytes.begin(), golden_bytes.end(),
                   std::begin(kGoldenCatalog)));

  const auto golden = lengsuo::DecodeCatalog(golden_bytes);
  Check(golden.has_value());
  Check(golden->schema_version == 1 && golden->pool_version == "p1");
  Check(golden->ua_entries.size() == 1);
  const auto& entry = golden->ua_entries[0];
  Check(entry.slot == 7 && entry.ua_string == "Synth/1.0 Test Env7");
  Check(entry.brands.size() == 2 && entry.brands[0].name == "Alpha" &&
        entry.brands[0].version == "1.2" &&
        entry.brands[1].name == "Beta" && entry.brands[1].version == "3.4");
  Check(entry.full_versions.size() == 2 &&
        entry.full_versions[0].version == "1.2.7.8" &&
        entry.full_versions[1].version == "3.4.9.0");
  Check(entry.platform == "TestOS" && entry.platform_version == "10.0.0");
  Check(entry.architecture == "x86" && entry.bitness == "64");
  Check(entry.model.empty() && !entry.mobile);

  for (std::size_t n = 0; n < golden_bytes.size(); ++n) {
    Bad(Bytes(golden_bytes.begin(),
              golden_bytes.begin() +
                  static_cast<Bytes::difference_type>(n)));
  }
  Bad(Bytes(4097, 0));
  Bytes bytes = golden_bytes;
  bytes.push_back(0);
  Bad(bytes);
  for (std::size_t i = 0; i < 5; ++i) {
    bytes = golden_bytes;
    bytes[i] = 255;
    Bad(bytes);
  }

  CatalogSpec spec;
  spec.container_version = 0;
  Bad(CatalogBytes(spec));
  spec.container_version = 2;
  Bad(CatalogBytes(spec));
  spec.container_version = 1;
  spec.pool_version = "";
  Bad(CatalogBytes(spec));
  spec.pool_version = std::string(65, 'p');
  Bad(CatalogBytes(spec));
  spec.pool_version = "p 1";
  Bad(CatalogBytes(spec));
  spec.pool_version = "p.1";
  Bad(CatalogBytes(spec));
  spec.pool_version = "p1";
  spec.entry_count = 0;
  Bad(CatalogBytes(spec));
  spec.entry_count = 17;
  Bad(CatalogBytes(spec));
  spec.entry_count = -1;

  spec.entries[0].face = "Ua";
  Bad(CatalogBytes(spec));
  spec.entries[0].face = "audio";
  Bad(CatalogBytes(spec));
  spec.entries[0].face = "";
  Bad(CatalogBytes(spec));
  spec.entries[0].face = std::string(13, 'f');
  Bad(CatalogBytes(spec));
  spec.entries[0].face = "ua";
  spec.entries[0].slot = 0;
  Bad(CatalogBytes(spec));
  spec.entries[0].slot = 1048577;
  Bad(CatalogBytes(spec));
  spec.entries[0].slot = 1048576;
  Check(lengsuo::DecodeCatalog(CatalogBytes(spec)).has_value());
  spec.entries[0].slot = 7;

  spec.entries[0].ua_string = "";
  Bad(CatalogBytes(spec));
  spec.entries[0].ua_string = std::string(513, 'u');
  Bad(CatalogBytes(spec));
  spec.entries[0].ua_string = "bad\nvalue";
  Bad(CatalogBytes(spec));
  spec.entries[0].ua_string = "bad\x7f" "value";
  Bad(CatalogBytes(spec));
  spec.entries[0].ua_string = "Synth/1.0 Test Env7";

  spec.entries[0].brands.clear();
  spec.entries[0].brand_count = 0;
  Bad(CatalogBytes(spec));
  spec.entries[0].brand_count = -1;
  spec.entries[0].brands = std::vector<BrandSpec>(9, {"n", "1"});
  Bad(CatalogBytes(spec));
  spec.entries[0].brands = {{"", "1.2"}};
  Bad(CatalogBytes(spec));
  spec.entries[0].brands = {{std::string(33, 'n'), "1.2"}};
  Bad(CatalogBytes(spec));
  spec.entries[0].brands = {{"Al\tpha", "1.2"}};
  Bad(CatalogBytes(spec));
  spec.entries[0].brands = {{"Alpha", ""}};
  Bad(CatalogBytes(spec));
  spec.entries[0].brands = {{"Alpha", std::string(33, '1')}};
  Bad(CatalogBytes(spec));
  spec.entries[0].brands = {{"Alpha", "1.2"}};

  spec.entries[0].full_versions = std::vector<BrandSpec>(9, {"n", "1"});
  Bad(CatalogBytes(spec));
  spec.entries[0].full_versions.clear();
  auto ok = lengsuo::DecodeCatalog(CatalogBytes(spec));
  Check(ok.has_value() && ok->ua_entries[0].full_versions.empty());
  spec.entries[0].full_versions = {{"Alpha", "1.2.7.8"}, {"Beta", "3.4.9.0"}};

  spec.entries[0].platform = "";
  Bad(CatalogBytes(spec));
  spec.entries[0].platform = std::string(33, 'p');
  Bad(CatalogBytes(spec));
  spec.entries[0].platform = "TestOS";
  spec.entries[0].platform_version = "";
  Bad(CatalogBytes(spec));
  spec.entries[0].platform_version = std::string(65, 'v');
  Bad(CatalogBytes(spec));
  spec.entries[0].platform_version = "10.0.0";
  spec.entries[0].architecture = "";
  Bad(CatalogBytes(spec));
  spec.entries[0].architecture = std::string(33, 'a');
  Bad(CatalogBytes(spec));
  spec.entries[0].architecture = "x86";
  spec.entries[0].bitness = "";
  Bad(CatalogBytes(spec));
  spec.entries[0].bitness = std::string(9, '6');
  Bad(CatalogBytes(spec));
  spec.entries[0].bitness = "64";
  spec.entries[0].model = std::string(129, 'm');
  Bad(CatalogBytes(spec));
  spec.entries[0].model = "mo\x7f" "del";
  Bad(CatalogBytes(spec));
  spec.entries[0].model = "Pixel-Test";
  ok = lengsuo::DecodeCatalog(CatalogBytes(spec));
  Check(ok.has_value() && ok->ua_entries[0].model == "Pixel-Test");
  spec.entries[0].model = "";
  spec.entries[0].mobile = 2;
  Bad(CatalogBytes(spec));
  spec.entries[0].mobile = 1;
  ok = lengsuo::DecodeCatalog(CatalogBytes(spec));
  Check(ok.has_value() && ok->ua_entries[0].mobile);
  spec.entries[0].mobile = 0;

  EntrySpec second;
  second.slot = 8;
  second.ua_string = "Synth/1.0 Test Env8";
  second.brands = {{"Gamma", "5.6"}};
  second.full_versions.clear();
  spec.entries.push_back(second);
  ok = lengsuo::DecodeCatalog(CatalogBytes(spec));
  Check(ok.has_value() && ok->ua_entries.size() == 2 &&
        ok->ua_entries[1].slot == 8 &&
        ok->ua_entries[1].ua_string == "Synth/1.0 Test Env8" &&
        ok->ua_entries[1].brands.size() == 1 &&
        ok->ua_entries[1].full_versions.empty());
  spec.entries[1].slot = 7;
  Bad(CatalogBytes(spec));
  spec.entries.pop_back();

  Check(lengsuo::CatalogMatchesProfile(*golden, ProfileWithPool("p1")));
  Check(!lengsuo::CatalogMatchesProfile(*golden, ProfileWithPool("p2")));
  Check(!lengsuo::CatalogMatchesProfile(*golden, ProfileWithPool("")));

  std::cout << "PASS " << checks << " assertions\n";
  return 0;
}
