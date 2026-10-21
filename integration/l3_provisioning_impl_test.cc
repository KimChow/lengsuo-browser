// l3_provisioning_impl_test - what the browser answers on the L3 Mojo channel
// (task O).
//
// The interesting failure is not "serialization runs": it is that the bytes
// handed to a renderer are the document the browser verified (same signature
// timestamp, same pin table), in the raw wire form the renderer accepts - not
// base64, not a re-encoded variant. Documents are built with EncodeL3Wire,
// the same encoder the acceptance run uses.
#include "lengsuo/integration/l3_provisioning_impl.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "lengsuo/native/l3_runtime.h"

namespace lengsuo {
namespace {

constexpr int64_t kSignedAt = 1700000000;
constexpr int64_t kMaxAge = 7 * 24 * 60 * 60;
constexpr char kOrigin[] = "https://mms.pinduoduo.com";
constexpr char kKey[] = "finger-key_0.1.5";
constexpr char kValue[] = "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5";

fp::L3Config MakeConfig(int64_t signed_unix) {
  fp::L3Config config;
  const std::string pin_wire =
      std::string(kOrigin) + "|local|" + kKey + "|" + kValue + "\n";
  EXPECT_TRUE(config.pins.Parse(pin_wire));
  config.signed_unix = signed_unix;
  return config;
}

TEST(L3ProvisioningTest, NoSnapshotAnswersEmpty) {
  EXPECT_TRUE(GetL3Document([] { return nullptr; }).empty());
}

TEST(L3ProvisioningTest, SnapshotIsAnsweredInRawWireForm) {
  const fp::L3Config config = MakeConfig(kSignedAt);

  const std::string document = GetL3Document([&config] { return &config; });
  // Raw L3W1, not base64: a regression to the old transport would start with
  // an alphabet character and carry no newline.
  EXPECT_EQ(document.rfind("L3W1\n", 0), 0u);
  EXPECT_NE(document.find('\n'), std::string::npos);
  EXPECT_EQ(document, fp::EncodeL3Wire(config, kSignedAt));
}

TEST(L3ProvisioningTest, RendererAcceptsTheAnswer) {
  const fp::L3Config config = MakeConfig(kSignedAt);

  // What renderer_main.cc does with a non-empty answer: now_unix = 0 because
  // the browser already judged freshness.
  fp::L3Runtime runtime;
  ASSERT_TRUE(runtime.Init(GetL3Document([&config] { return &config; }),
                           /*now_unix=*/0, /*max_age_seconds=*/0));
  const std::string* pinned = runtime.PinnedValue(kOrigin, kKey);
  ASSERT_NE(pinned, nullptr);
  EXPECT_EQ(*pinned, kValue);
  EXPECT_TRUE(runtime.IsLocked(kOrigin, kKey));
}

TEST(L3ProvisioningTest, SignatureTimestampIsPreserved) {
  const fp::L3Config config = MakeConfig(kSignedAt);
  const std::string document = GetL3Document([&config] { return &config; });

  fp::L3Config decoded;
  std::string error;
  // Fresh at the timestamp the document was signed with ...
  EXPECT_TRUE(fp::DecodeL3Wire(document, kSignedAt, kMaxAge, &decoded, &error))
      << error;
  EXPECT_EQ(decoded.signed_unix, kSignedAt);
  // ... and stale one second past the window. If EncodeL3Document stamped
  // "now" instead, this decode would fail as "too new" rather than "too old".
  EXPECT_FALSE(fp::DecodeL3Wire(document, kSignedAt + kMaxAge + 1, kMaxAge,
                                &decoded, &error));
}

TEST(L3ProvisioningTest, EveryRequestReReadsTheSnapshot) {
  const fp::L3Config config = MakeConfig(kSignedAt);
  int reads = 0;
  const auto reader = [&config, &reads]() -> const fp::L3Config* {
    ++reads;
    return &config;
  };

  const std::string first = GetL3Document(reader);
  const std::string second = GetL3Document(reader);
  EXPECT_EQ(first, second);
  EXPECT_EQ(reads, 2);
}

}  // namespace
}  // namespace lengsuo
