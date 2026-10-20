// l3_wire_test - transactional decode, sign gate, launch-gate consistency.
//
// Sample documents follow docs/cc-task-brief-l3.md: the pinduoduo pin set
// with _bee/_f77/_a42 mirror rules is the reference scenario from the
// decrypted Reference cache.
#include "l3_wire.h"

#include <gtest/gtest.h>

#include <string>

namespace lengsuo::fp {
namespace {

// Signs `body` and appends the sign line; keeps tests readable.
std::string FinishDoc(const std::string& body, int64_t ts) {
  return body + "sign " + MakeSign(body, ts) + "\n";
}

std::string ReferenceBody() {
  return "L3W1\n"
         "pins 4\n"
         "https://mms.pinduoduo.com|local|finger-key_0.1.5|"
         "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5\n"
         "https://mms.pinduoduo.com|local|finger-uuid1_0.1.5|"
         "9df4fd33-43dc-4432-b6aa-67c80d64545d\n"
         "https://mms.pinduoduo.com|local|finger-uuid2_0.1.5|"
         "52ca8817-12f0-411d-a7af-8272c47fde50\n"
         "https://mms.pinduoduo.com|session|finger-key_0.1.5|"
         "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5\n"
         "cookies 6\n"
         ".pinduoduo.com|_bee|7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5\n"
         ".pinduoduo.com|_f77|9df4fd33-43dc-4432-b6aa-67c80d64545d\n"
         ".pinduoduo.com|_a42|52ca8817-12f0-411d-a7af-8272c47fde50\n"
         ".pinduoduo.com|rckk|7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5\n"
         ".pinduoduo.com|ru1k|9df4fd33-43dc-4432-b6aa-67c80d64545d\n"
         ".pinduoduo.com|ru2k|52ca8817-12f0-411d-a7af-8272c47fde50\n"
         "ext 1\n"
         "fjchbfkopknlodpfoaeogfbclcpppiog\n"
         "rules 3\n"
         ".pinduoduo.com|_bee|https://mms.pinduoduo.com|finger-key_0.1.5\n"
         ".pinduoduo.com|_f77|https://mms.pinduoduo.com|"
         "finger-uuid1_0.1.5\n"
         ".pinduoduo.com|_a42|https://mms.pinduoduo.com|"
         "finger-uuid2_0.1.5\n";
}

constexpr int64_t kNow = 1700000000;
constexpr int64_t kMaxAge = 86400;

TEST(L3WireTest, ReferenceDocumentDecodes) {
  const std::string doc = FinishDoc(ReferenceBody(), kNow - 60);
  L3Config config;
  std::string error = "keep";
  ASSERT_TRUE(DecodeL3Wire(doc, kNow, kMaxAge, &config, &error)) << error;
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(config.pins.size(), 4u);
  EXPECT_EQ(config.extensions.size(), 1u);
  EXPECT_EQ(config.rules.size(), 3u);
  EXPECT_EQ(config.cookies.size(), 6u);
  EXPECT_EQ(config.signed_unix, kNow - 60);
  EXPECT_TRUE(config.extensions.Contains(
      "fjchbfkopknlodpfoaeogfbclcpppiog"));
  const DomainPin* pin =
      config.pins.Find("https://mms.pinduoduo.com");
  ASSERT_NE(pin, nullptr);
  ASSERT_EQ(pin->local_storage.size(), 3u);
  EXPECT_EQ(pin->local_storage[0].value,
            "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5");
}

TEST(L3WireTest, MissingSignRejected) {
  L3Config config;
  EXPECT_FALSE(DecodeL3Wire(ReferenceBody(), kNow, kMaxAge, &config, nullptr));
}

TEST(L3WireTest, TamperedBodyRejected) {
  std::string doc = FinishDoc(ReferenceBody(), kNow - 60);
  // Flip one byte inside a pinned value after signing.
  const auto pos = doc.find("7FcROX");
  ASSERT_NE(pos, std::string::npos);
  doc[pos] = '7' == doc[pos] ? '8' : '7';
  L3Config config;
  std::string error;
  EXPECT_FALSE(DecodeL3Wire(doc, kNow, kMaxAge, &config, &error));
  EXPECT_EQ(error, "l3: sign verification failed");
  EXPECT_EQ(config.pins.size(), 0u);  // transactional: nothing applied
}

TEST(L3WireTest, ExpiredSignRejected) {
  const std::string doc = FinishDoc(ReferenceBody(), kNow - kMaxAge - 1);
  L3Config config;
  std::string error;
  EXPECT_FALSE(DecodeL3Wire(doc, kNow, kMaxAge, &config, &error));
  EXPECT_EQ(error, "l3: sign verification failed");
}

TEST(L3WireTest, SignInFutureRejected) {
  const std::string doc = FinishDoc(ReferenceBody(), kNow + 1);
  L3Config config;
  EXPECT_FALSE(DecodeL3Wire(doc, kNow, kMaxAge, &config, nullptr));
}

TEST(L3WireTest, InconsistentSnapshotRejected) {
  // The pin disagrees with the cookie mirror: the launch gate must refuse it
  // even though the signature is valid.
  std::string body = ReferenceBody();
  const auto pos = body.find("finger-key_0.1.5|7FcROX");
  ASSERT_NE(pos, std::string::npos);
  body.replace(pos, sizeof("finger-key_0.1.5|7FcROX") - 1,
               "finger-key_0.1.5|7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH4");
  const std::string doc = FinishDoc(body, kNow - 60);
  L3Config config;
  std::string error;
  EXPECT_FALSE(DecodeL3Wire(doc, kNow, kMaxAge, &config, &error));
  EXPECT_EQ(error, "l3: pinned snapshot is internally inconsistent");
}

TEST(L3WireTest, BadMagicRejected) {
  std::string body = ReferenceBody();
  body[0] = 'X';
  L3Config config;
  std::string error;
  EXPECT_FALSE(DecodeL3Wire(FinishDoc(body, kNow), kNow, kMaxAge, &config,
                            &error));
  EXPECT_EQ(error, "l3: bad magic");
}

TEST(L3WireTest, TrailingContentRejected) {
  // Appending a section after the sign line changes the signed body's
  // interpretation; rfind() anchors the sign at the *last* "sign " line, so
  // the tail lands outside the signed bytes and integrity must fail.
  const std::string doc =
      FinishDoc(ReferenceBody(), kNow - 60) + "pins 1\n";
  L3Config config;
  std::string error;
  EXPECT_FALSE(DecodeL3Wire(doc, kNow, kMaxAge, &config, &error));
  EXPECT_EQ(error, "l3: sign verification failed");
}

TEST(L3WireTest, OversizedRejected) {
  std::string body = ReferenceBody();
  body.append(kMaxL3WireBytes, 'x');
  L3Config config;
  EXPECT_FALSE(
      DecodeL3Wire(FinishDoc(body, kNow), kNow, kMaxAge, &config, nullptr));
}

TEST(L3WireTest, ZeroPinsRejected) {
  // Section counts of zero are rejected by the count parser itself: a
  // document must carry pins, so "pins 0" fails at the header rather than
  // producing an empty (and therefore meaningless) snapshot.
  std::string body = ReferenceBody();
  const auto pos = body.find("pins ");
  body.replace(pos, 6, "pins 0");
  L3Config config;
  std::string error;
  EXPECT_FALSE(DecodeL3Wire(FinishDoc(body, kNow), kNow, kMaxAge, &config,
                            &error));
  EXPECT_EQ(error, "l3: bad pins header");
}

TEST(L3WireTest, EncodeDecodeRoundTrip) {
  const std::string doc = FinishDoc(ReferenceBody(), kNow - 60);
  L3Config config;
  ASSERT_TRUE(DecodeL3Wire(doc, kNow, kMaxAge, &config, nullptr));

  const std::string encoded = EncodeL3Wire(config, kNow - 30);
  L3Config decoded;
  ASSERT_TRUE(DecodeL3Wire(encoded, kNow, kMaxAge, &decoded, nullptr));
  EXPECT_EQ(decoded.pins.size(), config.pins.size());
  EXPECT_EQ(decoded.extensions.size(), config.extensions.size());
  EXPECT_EQ(decoded.rules.size(), config.rules.size());
  EXPECT_EQ(decoded.cookies.size(), config.cookies.size());
  EXPECT_EQ(decoded.sign, MakeSign(encoded.substr(0, encoded.size() -
                                                      decoded.sign.size() - 6),
                                   kNow - 30));
  // Encoding is deterministic.
  EXPECT_EQ(encoded, EncodeL3Wire(decoded, kNow - 30));
}

TEST(L3WireTest, EncodePinWireRoundTripsIntoStoragePin) {
  const std::string doc = FinishDoc(ReferenceBody(), kNow - 60);
  L3Config config;
  ASSERT_TRUE(DecodeL3Wire(doc, kNow, kMaxAge, &config, nullptr));
  const std::string pin_wire = EncodePinWire(config.pins);
  StoragePin parsed;
  ASSERT_TRUE(parsed.Parse(pin_wire));
  EXPECT_EQ(parsed.size(), config.pins.size());
  EXPECT_TRUE(
      parsed.IsLocked("https://mms.pinduoduo.com", "finger-key_0.1.5"));
}

TEST(L3WireTest, CollectStorageFactsCoversBothScopes) {
  const std::string doc = FinishDoc(ReferenceBody(), kNow - 60);
  L3Config config;
  ASSERT_TRUE(DecodeL3Wire(doc, kNow, kMaxAge, &config, nullptr));
  const auto facts = CollectStorageFacts(config.pins);
  ASSERT_EQ(facts.size(), 4u);
  bool saw_session = false;
  for (const StorageFact& fact : facts) {
    if (fact.key == "finger-key_0.1.5" &&
        fact.value == "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5") {
      saw_session = saw_session || fact.origin ==
                                      "https://mms.pinduoduo.com";
    }
  }
  EXPECT_TRUE(saw_session);
}

TEST(L3WireTest, NullOutputRejected) {
  EXPECT_FALSE(DecodeL3Wire(FinishDoc(ReferenceBody(), kNow), kNow, kMaxAge,
                            nullptr, nullptr));
}

}  // namespace
}  // namespace lengsuo::fp
