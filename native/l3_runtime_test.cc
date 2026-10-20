// l3_runtime_test - renderer-side snapshot queries (task I).
//
// The document used here is built through EncodeL3Wire so the tests exercise
// the same path the browser uses, rather than hand-writing signatures.
#include "l3_runtime.h"

#include <gtest/gtest.h>

#include <string>

#include "l3_wire.h"

namespace lengsuo::fp {
namespace {

constexpr int64_t kNow = 1700000000;
constexpr int64_t kMaxAge = 86400;

// Builds a document with one origin pinned in both scopes, one empty-valued
// pin, one extension and no rules (so the launch gate is trivially satisfied).
// Returns an empty string when the fixture itself is malformed, which makes
// every dependent assertion fail loudly instead of silently passing.
std::string MakeDoc(int64_t ts) {
  L3Config config;
  const std::string pin_wire =
      "https://mms.pinduoduo.com|local|finger-key_0.1.5|"
      "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5\n"
      "https://mms.pinduoduo.com|local|finger-uuid1_0.1.5|"
      "9df4fd33-43dc-4432-b6aa-67c80d64545d\n"
      "https://mms.pinduoduo.com|local|finger-empty|\n"
      "https://mms.pinduoduo.com|session|finger-session|sess-value\n"
      "https://other.example|local|k|v\n";
  if (!config.pins.Parse(pin_wire)) return std::string();
  if (!config.extensions.Add("fjchbfkopknlodpfoaeogfbclcpppiog")) {
    return std::string();
  }
  return EncodeL3Wire(config, ts);
}

TEST(L3RuntimeTest, NotReadyBeforeInit) {
  L3Runtime runtime;
  EXPECT_FALSE(runtime.ready());
  EXPECT_EQ(runtime.LocalPins("https://mms.pinduoduo.com"), nullptr);
  EXPECT_EQ(runtime.SessionPins("https://mms.pinduoduo.com"), nullptr);
  EXPECT_FALSE(runtime.IsLocked("https://mms.pinduoduo.com", "finger-key_0.1.5"));
  EXPECT_EQ(runtime.PinnedValue("https://mms.pinduoduo.com", "finger-key_0.1.5"),
            nullptr);
  EXPECT_FALSE(runtime.ExtensionAllowed("fjchbfkopknlodpfoaeogfbclcpppiog"));
}

TEST(L3RuntimeTest, InitAcceptsValidDocument) {
  L3Runtime runtime;
  ASSERT_TRUE(runtime.Init(MakeDoc(kNow - 60), kNow, kMaxAge));
  EXPECT_TRUE(runtime.ready());
}

TEST(L3RuntimeTest, LocalPinsExactOriginMatch) {
  L3Runtime runtime;
  ASSERT_TRUE(runtime.Init(MakeDoc(kNow - 60), kNow, kMaxAge));
  const auto* pins = runtime.LocalPins("https://mms.pinduoduo.com");
  ASSERT_NE(pins, nullptr);
  EXPECT_EQ(pins->size(), 3u);
  EXPECT_EQ((*pins)[0].key, "finger-key_0.1.5");
  EXPECT_EQ((*pins)[0].value, "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5");
  // Unknown origin and a subdomain-looking suffix do not match.
  EXPECT_EQ(runtime.LocalPins("https://nope.example"), nullptr);
  EXPECT_EQ(runtime.LocalPins("mms.pinduoduo.com"), nullptr);
}

TEST(L3RuntimeTest, SessionScopeIsSeparate) {
  L3Runtime runtime;
  ASSERT_TRUE(runtime.Init(MakeDoc(kNow - 60), kNow, kMaxAge));
  const auto* session = runtime.SessionPins("https://mms.pinduoduo.com");
  ASSERT_NE(session, nullptr);
  ASSERT_EQ(session->size(), 1u);
  EXPECT_EQ((*session)[0].key, "finger-session");
  EXPECT_EQ((*session)[0].value, "sess-value");
  // The same origin's local list must not contain the session key.
  const auto* local = runtime.LocalPins("https://mms.pinduoduo.com");
  ASSERT_NE(local, nullptr);
  for (const PinnedEntry& entry : *local) {
    EXPECT_NE(entry.key, "finger-session");
  }
}

TEST(L3RuntimeTest, IsLockedCoversBothScopes) {
  L3Runtime runtime;
  ASSERT_TRUE(runtime.Init(MakeDoc(kNow - 60), kNow, kMaxAge));
  EXPECT_TRUE(runtime.IsLocked("https://mms.pinduoduo.com", "finger-key_0.1.5"));
  EXPECT_TRUE(runtime.IsLocked("https://mms.pinduoduo.com", "finger-session"));
  EXPECT_FALSE(runtime.IsLocked("https://mms.pinduoduo.com", "not-pinned"));
  EXPECT_FALSE(runtime.IsLocked("https://nope.example", "finger-key_0.1.5"));
}

TEST(L3RuntimeTest, PinnedValueReturnsExactValue) {
  L3Runtime runtime;
  ASSERT_TRUE(runtime.Init(MakeDoc(kNow - 60), kNow, kMaxAge));
  const std::string* value =
      runtime.PinnedValue("https://mms.pinduoduo.com", "finger-uuid1_0.1.5");
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(*value, "9df4fd33-43dc-4432-b6aa-67c80d64545d");
  // Session-scope value is reachable through PinnedValue too.
  const std::string* session =
      runtime.PinnedValue("https://mms.pinduoduo.com", "finger-session");
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(*session, "sess-value");
  EXPECT_EQ(runtime.PinnedValue("https://mms.pinduoduo.com", "not-pinned"),
            nullptr);
}

TEST(L3RuntimeTest, EmptyValueIsDistinguishableFromAbsent) {
  L3Runtime runtime;
  ASSERT_TRUE(runtime.Init(MakeDoc(kNow - 60), kNow, kMaxAge));
  const std::string* empty =
      runtime.PinnedValue("https://mms.pinduoduo.com", "finger-empty");
  // A pinned empty string must return a non-null pointer to an empty string;
  // conflating it with "absent" would let a page overwrite a pinned blank.
  ASSERT_NE(empty, nullptr);
  EXPECT_TRUE(empty->empty());
  EXPECT_EQ(runtime.PinnedValue("https://mms.pinduoduo.com", "finger-empty2"),
            nullptr);
}

TEST(L3RuntimeTest, InvalidDocumentLeavesStateUntouched) {
  L3Runtime runtime;
  // Tampering breaks the signature, so the decode (and Init) must fail.
  std::string doc = MakeDoc(kNow - 60);
  const auto pos = doc.find("7FcROX");
  ASSERT_NE(pos, std::string::npos);
  doc[pos] = doc[pos] == '7' ? '8' : '7';
  EXPECT_FALSE(runtime.Init(doc, kNow, kMaxAge));
  EXPECT_FALSE(runtime.ready());

  // A malformed document after a good Init must not disturb the snapshot.
  ASSERT_TRUE(runtime.Init(MakeDoc(kNow - 60), kNow, kMaxAge));
  const std::string* before =
      runtime.PinnedValue("https://mms.pinduoduo.com", "finger-key_0.1.5");
  ASSERT_NE(before, nullptr);
  const std::string snapshot = *before;
  EXPECT_FALSE(runtime.Init("L3W1\ngarbage\n", kNow, kMaxAge));
  const std::string* after =
      runtime.PinnedValue("https://mms.pinduoduo.com", "finger-key_0.1.5");
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(*after, snapshot);
}

TEST(L3RuntimeTest, SecondInitIsRejected) {
  L3Runtime runtime;
  ASSERT_TRUE(runtime.Init(MakeDoc(kNow - 60), kNow, kMaxAge));
  // Even a perfectly valid second document is refused: the snapshot is frozen
  // for the lifetime of the renderer.
  EXPECT_FALSE(runtime.Init(MakeDoc(kNow - 30), kNow, kMaxAge));
  EXPECT_TRUE(runtime.ready());
}

TEST(L3RuntimeTest, ExpiredDocumentRejected) {
  L3Runtime runtime;
  EXPECT_FALSE(runtime.Init(MakeDoc(kNow - kMaxAge - 1), kNow, kMaxAge));
  EXPECT_FALSE(runtime.ready());
}

TEST(L3RuntimeTest, ExtensionWhitelist) {
  L3Runtime runtime;
  ASSERT_TRUE(runtime.Init(MakeDoc(kNow - 60), kNow, kMaxAge));
  EXPECT_TRUE(runtime.ExtensionAllowed("fjchbfkopknlodpfoaeogfbclcpppiog"));
  EXPECT_FALSE(runtime.ExtensionAllowed("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}

TEST(L3RuntimeTest, GlobalRuntimeIsStable) {
  // The process-wide accessor must hand back the same object every time.
  EXPECT_EQ(&GetL3Runtime(), &GetL3Runtime());
}

}  // namespace
}  // namespace lengsuo::fp
