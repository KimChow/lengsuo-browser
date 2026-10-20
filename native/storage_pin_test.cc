// StoragePin (task E) acceptance tests.
//
// Values marked "Reference sample" are the real decrypted values recorded in
// docs/cc-task-brief-l3.md: localStorage finger-* keys and the matching
// .pinduoduo.com cookie values.

#include "storage_pin.h"

#include <gtest/gtest.h>

#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace lengsuo::fp {
namespace {

constexpr char kOrigin[] = "https://mms.pinduoduo.com";

// Reference sample values (decrypted, see task brief).
constexpr char kFingerKey[] = "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5";
constexpr char kFingerUuid1[] = "9df4fd33-43dc-4432-b6aa-67c80d64545d";
constexpr char kFingerUuid2[] = "52ca8817-12f0-411d-a7af-8272c47fde50";

const std::string kSampleWire =
    std::string(kOrigin) + "|local|finger-key_0.1.5|" + kFingerKey + "\n" +
    std::string(kOrigin) + "|local|finger-uuid1_0.1.5|" + kFingerUuid1 + "\n" +
    std::string(kOrigin) + "|local|finger-uuid2_0.1.5|" + kFingerUuid2 + "\n";

const PinnedEntry* FindEntry(const std::vector<PinnedEntry>& entries,
                             const std::string& key) {
  for (const PinnedEntry& entry : entries) {
    if (entry.key == key) {
      return &entry;
    }
  }
  return nullptr;
}

// Task E acceptance #6: cross-store consistency. The production consistency
// checker belongs to storage_consistency.* (task F), which this task must not
// touch, so the comparison is test-local.
bool CrossStoreMatches(
    const std::vector<PinnedEntry>& cookies,
    const std::vector<PinnedEntry>& local_storage,
    const std::vector<std::pair<std::string, std::string>>& pairs,
    std::ostream& diff) {
  bool ok = true;
  for (const auto& [cookie_key, storage_key] : pairs) {
    const PinnedEntry* cookie = FindEntry(cookies, cookie_key);
    const PinnedEntry* stored = FindEntry(local_storage, storage_key);
    if (cookie == nullptr || stored == nullptr) {
      diff << "missing: cookie " << cookie_key << " or storage "
           << storage_key << '\n';
      ok = false;
      continue;
    }
    if (cookie->value != stored->value) {
      diff << cookie_key << '=' << cookie->value << " != " << storage_key
           << '=' << stored->value << '\n';
      ok = false;
    }
  }
  return ok;
}

}  // namespace

// Acceptance 1: empty, short segment count and bad origin are rejected.
TEST(StoragePinParse, RejectsMalformedWire) {
  StoragePin pin;
  EXPECT_FALSE(pin.Parse(""));
  EXPECT_FALSE(pin.Parse("https://mms.pinduoduo.com"));
  EXPECT_FALSE(pin.Parse("https://mms.pinduoduo.com|local"));
  EXPECT_FALSE(pin.Parse("https://mms.pinduoduo.com|local|finger-key"));
  EXPECT_FALSE(pin.Parse("ftp://mms.pinduoduo.com|local|k|v"));
  EXPECT_FALSE(pin.Parse("mms.pinduoduo.com|local|k|v"));
  EXPECT_FALSE(pin.Parse("https://mms.pinduoduo.com|local|k|v|extra"));
  EXPECT_FALSE(pin.Parse("https://mms.pinduoduo.com|bogus|k|v"));
  EXPECT_FALSE(pin.Parse("https://mms.pinduoduo.com|local||v"));
  EXPECT_FALSE(pin.Parse("\n"));
  EXPECT_FALSE(pin.Parse("https://mms.pinduoduo.com|local|k|v\n\n"));
  EXPECT_EQ(pin.size(), 0u);
}

// Acceptance 2: a failed Parse never half-applies.
TEST(StoragePinParse, FailureKeepsPreviousState) {
  StoragePin pin;
  ASSERT_TRUE(
      pin.Parse(std::string(kOrigin) + "|local|finger-key|original\n"));
  ASSERT_EQ(pin.size(), 1u);

  for (const char* bad :
       {"", "https://mms.pinduoduo.com|local|finger-key",
        "ftp://mms.pinduoduo.com|local|finger-key|changed",
        "https://mms.pinduoduo.com|bogus|finger-key|changed",
        "https://mms.pinduoduo.com|local|finger-key|x|changed"}) {
    EXPECT_FALSE(pin.Parse(bad)) << "wire: " << bad;
    EXPECT_EQ(pin.size(), 1u);
    const DomainPin* domain = pin.Find(kOrigin);
    ASSERT_NE(domain, nullptr);
    ASSERT_EQ(domain->local_storage.size(), 1u);
    EXPECT_EQ(domain->local_storage[0].key, "finger-key");
    EXPECT_EQ(domain->local_storage[0].value, "original");
  }
}

// Acceptance 3: later duplicate wins, no duplicate entry, no error.
TEST(StoragePinParse, DuplicateKeyLastValueWins) {
  StoragePin pin;
  ASSERT_TRUE(pin.Parse(std::string(kOrigin) +
                        "|local|finger-key|first\n" + kOrigin +
                        "|local|finger-key|second\n"));
  EXPECT_EQ(pin.size(), 1u);
  const DomainPin* domain = pin.Find(kOrigin);
  ASSERT_NE(domain, nullptr);
  ASSERT_EQ(domain->local_storage.size(), 1u);
  EXPECT_EQ(domain->local_storage[0].key, "finger-key");
  EXPECT_EQ(domain->local_storage[0].value, "second");
  EXPECT_EQ(domain->session_storage.size(), 0u);
}

// local and session are separate areas and may pin the same key.
TEST(StoragePinParse, LocalAndSessionAreSeparate) {
  StoragePin pin;
  ASSERT_TRUE(pin.Parse(std::string(kOrigin) + "|local|shared|L\n" +
                        kOrigin + "|session|shared|S\n"));
  EXPECT_EQ(pin.size(), 2u);
  const DomainPin* domain = pin.Find(kOrigin);
  ASSERT_NE(domain, nullptr);
  ASSERT_EQ(domain->local_storage.size(), 1u);
  ASSERT_EQ(domain->session_storage.size(), 1u);
  EXPECT_EQ(domain->local_storage[0].value, "L");
  EXPECT_EQ(domain->session_storage[0].value, "S");
}

// A successful Parse replaces the previous table.
TEST(StoragePinParse, SuccessfulParseReplacesTable) {
  StoragePin pin;
  ASSERT_TRUE(pin.Parse(std::string(kOrigin) + "|local|old|1\n"));
  ASSERT_TRUE(pin.Parse(std::string(kOrigin) + "|session|new|2\n"));
  EXPECT_EQ(pin.size(), 1u);
  const DomainPin* domain = pin.Find(kOrigin);
  ASSERT_NE(domain, nullptr);
  EXPECT_EQ(domain->local_storage.size(), 0u);
  ASSERT_EQ(domain->session_storage.size(), 1u);
  EXPECT_EQ(domain->session_storage[0].key, "new");
}

// Acceptance 4: unknown origins are not found.
TEST(StoragePinFind, UnknownOriginReturnsNullptr) {
  StoragePin pin;
  ASSERT_TRUE(pin.Parse(kSampleWire));
  EXPECT_NE(pin.Find(kOrigin), nullptr);
  EXPECT_EQ(pin.Find("https://example.com"), nullptr);
  EXPECT_EQ(pin.Find(""), nullptr);
}

// Acceptance 5: pinned keys are locked, others are not.
TEST(StoragePinLock, OnlyPinnedKeysAreLocked) {
  StoragePin pin;
  ASSERT_TRUE(pin.Parse(kSampleWire + std::string(kOrigin) +
                        "|session|session-only|s\n"));
  EXPECT_TRUE(pin.IsLocked(kOrigin, "finger-key_0.1.5"));
  EXPECT_TRUE(pin.IsLocked(kOrigin, "finger-uuid2_0.1.5"));
  EXPECT_TRUE(pin.IsLocked(kOrigin, "session-only"));
  EXPECT_FALSE(pin.IsLocked(kOrigin, "finger-unknown_0.1.5"));
  EXPECT_FALSE(pin.IsLocked("https://example.com", "finger-key_0.1.5"));
}

// Acceptance 6: cookie values must equal their pinned localStorage values.
TEST(StoragePinCrossStore, PinnedStorageMatchesCookieFacts) {
  StoragePin pin;
  ASSERT_TRUE(pin.Parse(kSampleWire));
  const DomainPin* domain = pin.Find(kOrigin);
  ASSERT_NE(domain, nullptr);

  // .pinduoduo.com cookie names against the localStorage finger-* keys.
  const std::vector<PinnedEntry> cookies = {{"_bee", kFingerKey},
                                            {"_f77", kFingerUuid1},
                                            {"_a42", kFingerUuid2}};
  const std::vector<std::pair<std::string, std::string>> pairs = {
      {"_bee", "finger-key_0.1.5"},
      {"_f77", "finger-uuid1_0.1.5"},
      {"_a42", "finger-uuid2_0.1.5"}};

  std::ostringstream diff;
  EXPECT_TRUE(CrossStoreMatches(cookies, domain->local_storage, pairs, diff));
  EXPECT_TRUE(diff.str().empty());

  std::vector<PinnedEntry> tampered = cookies;
  tampered[0].value = "different-cookie-value";
  std::ostringstream mismatch;
  EXPECT_FALSE(
      CrossStoreMatches(tampered, domain->local_storage, pairs, mismatch));
  EXPECT_NE(mismatch.str().find("_bee"), std::string::npos);
  EXPECT_NE(mismatch.str().find("different-cookie-value"), std::string::npos);
  EXPECT_NE(mismatch.str().find(kFingerKey), std::string::npos);
}

// The values themselves survive parsing unchanged.
TEST(StoragePinParse, SampleValuesArePreserved) {
  StoragePin pin;
  ASSERT_TRUE(pin.Parse(kSampleWire));
  const DomainPin* domain = pin.Find(kOrigin);
  ASSERT_NE(domain, nullptr);
  ASSERT_EQ(domain->local_storage.size(), 3u);
  ASSERT_NE(FindEntry(domain->local_storage, "finger-key_0.1.5"), nullptr);
  EXPECT_EQ(FindEntry(domain->local_storage, "finger-key_0.1.5")->value,
            kFingerKey);
  EXPECT_EQ(FindEntry(domain->local_storage, "finger-uuid1_0.1.5")->value,
            kFingerUuid1);
  EXPECT_EQ(FindEntry(domain->local_storage, "finger-uuid2_0.1.5")->value,
            kFingerUuid2);
}

// A Windows-style CRLF terminator is accepted.
TEST(StoragePinParse, AcceptsCrLf) {
  StoragePin pin;
  ASSERT_TRUE(pin.Parse(std::string(kOrigin) + "|local|k|v\r\n"));
  const DomainPin* domain = pin.Find(kOrigin);
  ASSERT_NE(domain, nullptr);
  ASSERT_EQ(domain->local_storage.size(), 1u);
  EXPECT_EQ(domain->local_storage[0].value, "v");
}

}  // namespace lengsuo::fp
