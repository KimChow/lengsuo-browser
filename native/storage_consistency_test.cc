// storage_consistency v1 tests (task F).
//
// Acceptance items from docs/cc-task-brief-l3.md §任务F, plus the reference
// measured triplet (_bee/_f77/_a42 <-> finger-key/finger-uuid1/finger-uuid2)
// as a table-driven fixture. The values are the ones decrypted from the real
// reference cache (brief §共同前提); they are fixed vectors, not recomputed.

#include "storage_consistency.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace lengsuo::fp {
namespace {

constexpr char kDomain[] = ".pinduoduo.com";
constexpr char kOrigin[] = "https://mms.pinduoduo.com";

CookieFact Cookie(const std::string& name, const std::string& value) {
  return CookieFact{kDomain, name, value};
}

StorageFact Storage(const std::string& key, const std::string& value) {
  return StorageFact{kOrigin, key, value};
}

ConsistencyRule Rule(const std::string& cookie_name,
                     const std::string& storage_key) {
  return ConsistencyRule{kDomain, cookie_name, kOrigin, storage_key};
}

// The real reference triple: the cookie name, the storage key it must equal, and
// the pinned value shared by both sides.
struct ReferenceCase {
  const char* cookie_name;
  const char* storage_key;
  const char* value;
};

const ReferenceCase kReference[] = {
    {"_bee", "finger-key_0.1.5", "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5"},
    {"_f77", "finger-uuid1_0.1.5", "9df4fd33-43dc-4432-b6aa-67c80d64545d"},
    {"_a42", "finger-uuid2_0.1.5", "52ca8817-12f0-411d-a7af-8272c47fde50"},
};

TEST(StorageConsistencyTest, EqualValuesProduceNoMismatch) {
  std::vector<ConsistencyRule> rules = {Rule("_bee", "finger-key_0.1.5")};
  std::vector<CookieFact> cookies = {Cookie("_bee", "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5")};
  std::vector<StorageFact> storage = {Storage("finger-key_0.1.5", "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5")};
  EXPECT_TRUE(CheckConsistency(cookies, storage, rules).empty());
}

TEST(StorageConsistencyTest, DifferingValuesReportBoth) {
  std::vector<ConsistencyRule> rules = {Rule("_bee", "finger-key_0.1.5")};
  std::vector<CookieFact> cookies = {Cookie("_bee", "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5")};
  std::vector<StorageFact> storage = {Storage("finger-key_0.1.5", "different")};
  const std::vector<Mismatch> mismatches =
      CheckConsistency(cookies, storage, rules);
  ASSERT_EQ(mismatches.size(), 1u);
  EXPECT_EQ(mismatches[0].cookie_value, "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5");
  EXPECT_EQ(mismatches[0].storage_value, "different");
  EXPECT_EQ(mismatches[0].rule.cookie_name, "_bee");
  EXPECT_EQ(mismatches[0].rule.storage_key, "finger-key_0.1.5");
}

TEST(StorageConsistencyTest, MissingCookieIsMismatch) {
  std::vector<ConsistencyRule> rules = {Rule("_bee", "finger-key_0.1.5")};
  std::vector<StorageFact> storage = {Storage("finger-key_0.1.5", "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5")};
  const std::vector<Mismatch> mismatches = CheckConsistency({}, storage, rules);
  ASSERT_EQ(mismatches.size(), 1u);
  EXPECT_EQ(mismatches[0].cookie_value, "");
  EXPECT_EQ(mismatches[0].storage_value, "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5");
}

TEST(StorageConsistencyTest, MissingStorageIsMismatch) {
  std::vector<ConsistencyRule> rules = {Rule("_bee", "finger-key_0.1.5")};
  std::vector<CookieFact> cookies = {Cookie("_bee", "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5")};
  const std::vector<Mismatch> mismatches = CheckConsistency(cookies, {}, rules);
  ASSERT_EQ(mismatches.size(), 1u);
  EXPECT_EQ(mismatches[0].cookie_value, "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5");
  EXPECT_EQ(mismatches[0].storage_value, "");
}

TEST(StorageConsistencyTest, BothMissingRuleNotApplicable) {
  // A rule for another environment's origin: no facts, no mismatch.
  std::vector<ConsistencyRule> rules = {
      Rule("_bee", "finger-key_0.1.5"),
      ConsistencyRule{".example.com", "_bee", "https://app.example.com",
                      "finger-key_0.1.5"}};
  EXPECT_TRUE(CheckConsistency({}, {}, rules).empty());
}

TEST(StorageConsistencyTest, CookieDomainMatchesSubdomain) {
  // .pinduoduo.com covers mms.pinduoduo.com (and the bare host, RFC 6265
  // 5.1.3); it does not cover non-suffix lookalikes.
  ConsistencyRule rule{".pinduoduo.com", "_bee", "https://mms.pinduoduo.com",
                       "finger-key_0.1.5"};
  const StorageFact storage{"https://mms.pinduoduo.com", "finger-key_0.1.5",
                            "v"};
  EXPECT_TRUE(CheckConsistency({CookieFact{".pinduoduo.com", "_bee", "v"}}, {storage}, {rule}).empty());
  EXPECT_TRUE(CheckConsistency({CookieFact{"mms.pinduoduo.com", "_bee", "v"}}, {storage}, {rule}).empty());
  EXPECT_TRUE(CheckConsistency({CookieFact{"pinduoduo.com", "_bee", "v"}}, {storage}, {rule}).empty());
  EXPECT_TRUE(CheckConsistency({CookieFact{".PINDUODUO.com", "_bee", "v"}}, {storage}, {rule}).empty());
  // evilpinduoduo.com is not a dot-suffix of .pinduoduo.com: the cookie is
  // "missing", so the rule yields a mismatch with an empty cookie value.
  const std::vector<Mismatch> mismatches = CheckConsistency(
      {CookieFact{"evilpinduoduo.com", "_bee", "v"}}, {storage}, {rule});
  ASSERT_EQ(mismatches.size(), 1u);
  EXPECT_EQ(mismatches[0].cookie_value, "");
  EXPECT_EQ(mismatches[0].storage_value, "v");
}

TEST(StorageConsistencyTest, NoRulesReturnsEmpty) {
  const std::vector<CookieFact> cookies = {Cookie("_bee", "x")};
  const std::vector<StorageFact> storage = {Storage("finger-key_0.1.5", "y")};
  EXPECT_TRUE(CheckConsistency(cookies, storage, {}).empty());
  EXPECT_TRUE(CheckConsistency({}, {}, {}).empty());
}

// Item 8: the three measured values, driven through the same table. All three
// agree on one pass; corrupting any single side produces exactly one mismatch
// pointing at that rule.
TEST(StorageConsistencyTest, ReferenceMeasuredTripletTableDriven) {
  std::vector<ConsistencyRule> rules;
  std::vector<CookieFact> cookies;
  std::vector<StorageFact> storage;
  for (const ReferenceCase& entry : kReference) {
    rules.push_back(Rule(entry.cookie_name, entry.storage_key));
    cookies.push_back(Cookie(entry.cookie_name, entry.value));
    storage.push_back(Storage(entry.storage_key, entry.value));
  }
  SCOPED_TRACE("all three pinned pairs agree");
  EXPECT_TRUE(CheckConsistency(cookies, storage, rules).empty());

  for (const ReferenceCase& entry : kReference) {
    SCOPED_TRACE(entry.cookie_name);
    std::vector<StorageFact> corrupted;
    for (const ReferenceCase& other : kReference) {
      const std::string value =
          other.cookie_name == entry.cookie_name ? "drifted" : other.value;
      corrupted.push_back(Storage(other.storage_key, value));
    }
    const std::vector<Mismatch> mismatches =
        CheckConsistency(cookies, corrupted, rules);
    ASSERT_EQ(mismatches.size(), 1u);
    EXPECT_EQ(mismatches[0].rule.cookie_name, entry.cookie_name);
    EXPECT_EQ(mismatches[0].cookie_value, entry.value);
    EXPECT_EQ(mismatches[0].storage_value, "drifted");
  }
}

TEST(StorageConsistencyTest, RulesEvaluatedInOrder) {
  std::vector<ConsistencyRule> rules = {Rule("_f77", "finger-uuid1_0.1.5"),
                                        Rule("_a42", "finger-uuid2_0.1.5")};
  std::vector<Mismatch> mismatches = CheckConsistency({}, {}, rules);
  EXPECT_TRUE(mismatches.empty());

  mismatches = CheckConsistency(
      {Cookie("_a42", kReference[2].value)},
      {Storage("finger-uuid2_0.1.5", "drifted")}, rules);
  ASSERT_EQ(mismatches.size(), 1u);
  EXPECT_EQ(mismatches[0].rule.cookie_name, "_a42");
}

}  // namespace
}  // namespace lengsuo::fp
