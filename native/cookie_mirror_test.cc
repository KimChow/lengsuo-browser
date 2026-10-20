// cookie_mirror v1 tests (task M).
//
// The cookie half of the L3 cross-store lock. The fixtures use the values
// measured from the reference cache (docs/l3-storage-layer.md §一):
//
//   .pinduoduo.com  _bee = 7FcROX...  <->  mms.pinduoduo.com  finger-key_0.1.5
//
// They are fixed vectors, not recomputed.

#include "cookie_mirror.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "l3_wire.h"
#include "storage_consistency.h"
#include "storage_pin.h"

namespace lengsuo::fp {
namespace {

constexpr char kDomain[] = ".pinduoduo.com";
constexpr char kHost[] = "mms.pinduoduo.com";
constexpr char kOrigin[] = "https://mms.pinduoduo.com";
constexpr char kBeeValue[] = "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5";
constexpr char kF77Value[] = "9df4fd33-43dc-4432-b6aa-67c80d64545d";
constexpr char kA42Value[] = "52ca8817-12f0-411d-a7af-8272c47fde50";

CookieFact Cookie(const std::string& name, const std::string& value) {
  return CookieFact{kDomain, name, value};
}

ConsistencyRule Rule(const std::string& cookie_name,
                     const std::string& storage_key) {
  return ConsistencyRule{kDomain, cookie_name, kOrigin, storage_key};
}

// The three measured reference pairs: cookie name, storage key, shared value.
struct MeasuredCase {
  const char* cookie_name;
  const char* storage_key;
  const char* value;
};

constexpr MeasuredCase kMeasured[] = {
    {"_bee", "finger-key_0.1.5", kBeeValue},
    {"_f77", "finger-uuid1_0.1.5", kF77Value},
    {"_a42", "finger-uuid2_0.1.5", kA42Value},
};

// A mirror holding the three measured pairs, both sides declared.
CookieMirror MeasuredMirror() {
  std::vector<CookieFact> cookies;
  std::vector<ConsistencyRule> rules;
  for (const MeasuredCase& entry : kMeasured) {
    cookies.push_back(Cookie(entry.cookie_name, entry.value));
    rules.push_back(Rule(entry.cookie_name, entry.storage_key));
  }
  CookieMirror mirror;
  mirror.Init(std::move(cookies), std::move(rules));
  return mirror;
}

TEST(CookieMirrorTest, EmptySnapshotHasNothing) {
  const CookieMirror mirror;
  EXPECT_EQ(mirror.size(), 0u);
  EXPECT_EQ(mirror.Lookup(kHost, "_bee"), std::nullopt);
  EXPECT_FALSE(mirror.IsLocked(kHost, "_bee"));
  EXPECT_EQ(mirror.PairFor(kHost, "_bee"), std::nullopt);
  // No pinned cookie for this host: the browser's line is returned unchanged.
  EXPECT_EQ(mirror.MergeCookieLine(kHost, "a=1; b=2"), "a=1; b=2");
}

TEST(CookieMirrorTest, ExactDomainMatch) {
  CookieMirror mirror;
  mirror.Init({CookieFact{"mms.pinduoduo.com", "_bee", kBeeValue}}, {});
  const auto value = mirror.Lookup("mms.pinduoduo.com", "_bee");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, kBeeValue);
}

TEST(CookieMirrorTest, ParentDomainCoversSubdomain) {
  // ".pinduoduo.com" covers "a.example.com"-style subdomains: RFC 6265 5.1.3.
  CookieMirror mirror;
  mirror.Init({CookieFact{".pinduoduo.com", "_bee", kBeeValue}}, {});
  const auto value = mirror.Lookup("a.pinduoduo.com", "_bee");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, kBeeValue);
  // The bare domain is covered too.
  const auto bare = mirror.Lookup("pinduoduo.com", "_bee");
  ASSERT_TRUE(bare.has_value());
  EXPECT_EQ(*bare, kBeeValue);
}

TEST(CookieMirrorTest, DomainMatchIsCaseInsensitive) {
  CookieMirror mirror;
  mirror.Init({CookieFact{".PINDUODUO.com", "_bee", kBeeValue}}, {});
  const auto value = mirror.Lookup("MMS.Pinduoduo.COM", "_bee");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, kBeeValue);
}

TEST(CookieMirrorTest, NonSuffixLookalikeDoesNotMatch) {
  // "evilpinduoduo.com" is not a dot-suffix of ".pinduoduo.com".
  CookieMirror mirror;
  mirror.Init({CookieFact{".pinduoduo.com", "_bee", kBeeValue}}, {});
  EXPECT_EQ(mirror.Lookup("evilpinduoduo.com", "_bee"), std::nullopt);
  EXPECT_FALSE(mirror.IsLocked("evilpinduoduo.com", "_bee"));
}

TEST(CookieMirrorTest, MissReturnsNullopt) {
  const CookieMirror mirror = MeasuredMirror();
  // Unknown name.
  EXPECT_EQ(mirror.Lookup(kHost, "_nope"), std::nullopt);
  // Unknown host.
  EXPECT_EQ(mirror.Lookup("other.example", "_bee"), std::nullopt);
  // Name comparison is byte-exact: RFC 6265 5.4 makes cookie names
  // case-sensitive, and a case-insensitive match would let a page write
  // "_BEE" over a pinned "_bee".
  EXPECT_EQ(mirror.Lookup(kHost, "_BEE"), std::nullopt);
}

TEST(CookieMirrorTest, PinnedCookieIsLockedUnpinnedIsNot) {
  const CookieMirror mirror = MeasuredMirror();
  EXPECT_TRUE(mirror.IsLocked(kHost, "_bee"));
  EXPECT_TRUE(mirror.IsLocked("other.pinduoduo.com", "_bee"));
  // Not declared by the snapshot: the page may write it.
  EXPECT_FALSE(mirror.IsLocked(kHost, "_free"));
  EXPECT_FALSE(mirror.IsLocked("other.example", "_bee"));
}

TEST(CookieMirrorTest, LockIsPerNameNotPerDomain) {
  const CookieMirror mirror = MeasuredMirror();
  // One declared cookie does not lock every cookie of the domain.
  EXPECT_TRUE(mirror.IsLocked(kHost, "_bee"));
  EXPECT_FALSE(mirror.IsLocked(kHost, "_f77_lookalike"));
}

TEST(CookieMirrorTest, MostSpecificDomainWins) {
  // Two declared cookies share a name; the more specific domain wins, the
  // way the browser resolves them.
  CookieMirror mirror;
  mirror.Init({CookieFact{".pinduoduo.com", "_bee", "generic"},
               CookieFact{"mms.pinduoduo.com", "_bee", "specific"}},
              {});
  const auto value = mirror.Lookup(kHost, "_bee");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "specific");
  // A host the specific domain does not cover still sees the generic one.
  const auto other = mirror.Lookup("a.pinduoduo.com", "_bee");
  ASSERT_TRUE(other.has_value());
  EXPECT_EQ(*other, "generic");
}

TEST(CookieMirrorTest, DuplicateDeclarationKeepsLastValueFirstPosition) {
  // Same collapse rule as StoragePin: last value wins, first position kept.
  CookieMirror mirror;
  mirror.Init({CookieFact{".pinduoduo.com", "_bee", "first"},
               CookieFact{".pinduoduo.com", "_bee", "second"}},
              {});
  EXPECT_EQ(mirror.size(), 1u);
  const auto value = mirror.Lookup(kHost, "_bee");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "second");
}

TEST(CookieMirrorTest, LeadingDotVariantsAreOneDomain) {
  // ".example.com" and "example.com" are the same domain in RFC 6265 5.3.
  CookieMirror mirror;
  mirror.Init({CookieFact{".example.com", "_bee", "dotted"},
               CookieFact{"example.com", "_bee", "bare"}},
              {});
  EXPECT_EQ(mirror.size(), 1u);
  const auto value = mirror.Lookup("a.example.com", "_bee");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "bare");
}

TEST(CookieMirrorTest, LeadingDotDoesNotMakeADomainMoreSpecific) {
  // ".example.com" and "example.com" are the same domain, so a later,
  // otherwise-equal declaration must not win on the strength of its dot.
  CookieMirror mirror;
  mirror.Init({CookieFact{"example.com", "_bee", "first"},
               CookieFact{".example.com", "_bee", "second"}},
              {});
  const auto value = mirror.Lookup("a.example.com", "_bee");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "second");
}

TEST(CookieMirrorTest, PairForReturnsTheDeclaringRule) {
  const CookieMirror mirror = MeasuredMirror();
  const auto rule = mirror.PairFor(kHost, "_bee");
  ASSERT_TRUE(rule.has_value());
  EXPECT_EQ(rule->cookie_domain, kDomain);
  EXPECT_EQ(rule->cookie_name, "_bee");
  EXPECT_EQ(rule->storage_origin, kOrigin);
  EXPECT_EQ(rule->storage_key, "finger-key_0.1.5");
  EXPECT_EQ(mirror.PairFor(kHost, "_free"), std::nullopt);
}

TEST(CookieMirrorTest, MirrorPairKeepsBothSidesWhenStorageWriteIsRefused) {
  // The point of the mirror: the localStorage side refuses a write to a
  // pinned key, so the cookie side must keep its value too. The cookie side
  // is the one that answers here.
  const CookieMirror mirror = MeasuredMirror();
  // The storage side of the pair is pinned, so the cookie side is locked and
  // its value is the frozen one.
  EXPECT_TRUE(mirror.IsLocked(kHost, "_bee"));
  const auto value = mirror.Lookup(kHost, "_bee");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, kBeeValue);
  // The pair is symmetric in the rule: the same rule names the storage key.
  const auto rule = mirror.PairFor(kHost, "_bee");
  ASSERT_TRUE(rule.has_value());
  EXPECT_EQ(rule->storage_key, "finger-key_0.1.5");
}

TEST(CookieMirrorTest, MirrorPairDrivesTheRealCrossStoreCheck) {
  // The mirror is not a second implementation of the consistency rule: it
  // carries the same rule and the same declared cookie, so the real
  // checker agrees with it. The localStorage side here is the real
  // StoragePin, and the storage write that the platform would attempt is
  // the one the pin table refuses.
  const std::string pin_wire =
      "https://mms.pinduoduo.com|local|finger-key_0.1.5|7FcROXwfKGAKuwlRwwlB"
      "CEUPeZXjwrH5\n";
  StoragePin pins;
  ASSERT_TRUE(pins.Parse(pin_wire));

  std::vector<CookieFact> cookies = {Cookie("_bee", kBeeValue)};
  std::vector<ConsistencyRule> rules = {Rule("_bee", "finger-key_0.1.5")};
  CookieMirror mirror;
  mirror.Init(cookies, rules);

  // The storage side is pinned, so a write to it is refused.
  EXPECT_TRUE(pins.IsLocked(kOrigin, "finger-key_0.1.5"));

  // The cookie side of the same pair is locked and still holds the shared
  // value: neither half moved.
  EXPECT_TRUE(mirror.IsLocked(kHost, "_bee"));
  const auto value = mirror.Lookup(kHost, "_bee");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, kBeeValue);

  // The real checker sees no drift between the two sides.
  EXPECT_TRUE(CheckConsistency(cookies, CollectStorageFacts(pins), rules)
                  .empty());
}

TEST(CookieMirrorTest, MergeReplacesBrowserValueInPlace) {
  // A single pinned cookie, so the expected line stays readable.
  CookieMirror mirror;
  mirror.Init({CookieFact{kDomain, "_bee", kBeeValue}}, {});
  // The browser jar holds a stale value and a cookie the snapshot does not
  // care about.
  EXPECT_EQ(mirror.MergeCookieLine(kHost, "_bee=stale; other=keep"),
            std::string("_bee=") + kBeeValue + "; other=keep");
  // Ordering of the untouched cookies is preserved.
  EXPECT_EQ(mirror.MergeCookieLine(kHost, "a=1; _bee=stale; b=2"),
            std::string("a=1; _bee=") + kBeeValue + "; b=2");
}

TEST(CookieMirrorTest, MergeAppendsPinnedCookieTheBrowserLacks) {
  CookieMirror mirror;
  mirror.Init({CookieFact{kDomain, "_bee", kBeeValue}}, {});
  EXPECT_EQ(mirror.MergeCookieLine(kHost, "other=keep"),
            std::string("other=keep; _bee=") + kBeeValue);
  EXPECT_EQ(mirror.MergeCookieLine(kHost, ""),
            std::string("_bee=") + kBeeValue);
}

TEST(CookieMirrorTest, MergeLeavesOtherHostsAlone) {
  const CookieMirror mirror = MeasuredMirror();
  EXPECT_EQ(mirror.MergeCookieLine("other.example", "_bee=stale"),
            "_bee=stale");
}

TEST(CookieMirrorTest, MergeAppliesEveryPinnedCookie) {
  const CookieMirror mirror = MeasuredMirror();
  const std::string merged = mirror.MergeCookieLine(kHost, "x=1");
  EXPECT_NE(merged.find(std::string("_bee=") + kBeeValue), std::string::npos);
  EXPECT_NE(merged.find(std::string("_f77=") + kF77Value), std::string::npos);
  EXPECT_NE(merged.find(std::string("_a42=") + kA42Value), std::string::npos);
  EXPECT_NE(merged.find("x=1"), std::string::npos);
}

TEST(CookieMirrorTest, MergePreservesEmptyValue) {
  // An empty value is a value, not an absence: the browser writes
  // "name=value" for it, and the merged line must too.
  CookieMirror mirror;
  mirror.Init({CookieFact{".pinduoduo.com", "_empty", ""}}, {});
  EXPECT_EQ(mirror.MergeCookieLine(kHost, "_empty=stale"), "_empty=");
  const auto value = mirror.Lookup(kHost, "_empty");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "");
}

TEST(CookieMirrorTest, MeasuredTripletAgrees) {
  // The three measured pairs, all of them, through the same table.
  const CookieMirror mirror = MeasuredMirror();
  for (const MeasuredCase& entry : kMeasured) {
    SCOPED_TRACE(entry.cookie_name);
    const auto value = mirror.Lookup(kHost, entry.cookie_name);
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, entry.value);
    EXPECT_TRUE(mirror.IsLocked(kHost, entry.cookie_name));
    const auto rule = mirror.PairFor(kHost, entry.cookie_name);
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->storage_key, entry.storage_key);
  }
}

TEST(CookieMirrorTest, EmptyDomainMatchesNothing) {
  // A rule or cookie with an empty domain is fail-closed, not a wildcard.
  CookieMirror mirror;
  mirror.Init({CookieFact{"", "_bee", kBeeValue}}, {});
  EXPECT_EQ(mirror.Lookup("pinduoduo.com", "_bee"), std::nullopt);
  EXPECT_FALSE(mirror.IsLocked("pinduoduo.com", "_bee"));
}

TEST(CookieMirrorTest, DomainMatchingIsSharedWithConsistency) {
  // The mirror must answer domain questions exactly the way the consistency
  // checker does: one implementation, one answer.
  //
  // The relation is directional - `domain` is the declared domain, `host` the
  // concrete page host - so a broad domain does not cover a narrow host.
  EXPECT_TRUE(CookieDomainMatches("mms.pinduoduo.com", ".pinduoduo.com"));
  EXPECT_TRUE(CookieDomainMatches("pinduoduo.com", ".pinduoduo.com"));
  EXPECT_TRUE(CookieDomainMatches("MMS.PINDUODUO.COM", ".pinduoduo.com"));
  EXPECT_FALSE(CookieDomainMatches("evilpinduoduo.com", ".pinduoduo.com"));
  EXPECT_FALSE(CookieDomainMatches(".pinduoduo.com", "mms.pinduoduo.com"));
  EXPECT_FALSE(CookieDomainMatches("pinduoduo.com", ""));
}

}  // namespace
}  // namespace lengsuo::fp
