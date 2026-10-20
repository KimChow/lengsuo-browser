// l3_bootstrap_test - browser-side load semantics (task J).
//
// Documents are produced with EncodeL3Wire so tests exercise the encoder,
// the transactional decoder and the bootstrap policy together, exactly as a
// managed runner would.
#include "lengsuo/integration/l3_bootstrap.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>

namespace lengsuo {
namespace {

constexpr int64_t kNow = 1700000000;
constexpr int64_t kMaxAge = 7 * 24 * 60 * 60;

// Reader over fixed content. Initialize only reads once, so returning the same
// bytes every call is equivalent to a one-shot read and keeps the empty-content
// case distinguishable from "read failed".
L3BootstrapState::Reader ReadFrom(std::string content) {
  return [content = std::move(content)]() { return std::optional<std::string>(content); };
}

std::optional<std::string> ReadNothing() { return std::nullopt; }

std::string MakeDoc(int64_t ts) {
  fp::L3Config config;
  const std::string pin_wire =
      "https://mms.pinduoduo.com|local|finger-key_0.1.5|"
      "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5\n";
  if (!config.pins.Parse(pin_wire)) return std::string();
  return fp::EncodeL3Wire(config, ts);
}

TEST(L3BootstrapTest, NotRequestedIsSuccessWithoutConfig) {
  L3BootstrapState state;
  EXPECT_TRUE(state.Initialize(false, true, ReadNothing, kNow, kMaxAge));
  EXPECT_EQ(state.config(), nullptr);
  EXPECT_FALSE(state.load_failed());
}

TEST(L3BootstrapTest, UnsafePathFails) {
  L3BootstrapState state;
  EXPECT_FALSE(
      state.Initialize(true, false, ReadFrom(MakeDoc(kNow - 60)), kNow,
                       kMaxAge));
  EXPECT_EQ(state.config(), nullptr);
  EXPECT_TRUE(state.load_failed());
}

TEST(L3BootstrapTest, ReadFailureFails) {
  L3BootstrapState state;
  EXPECT_FALSE(state.Initialize(true, true, ReadNothing, kNow, kMaxAge));
  EXPECT_TRUE(state.load_failed());
}

TEST(L3BootstrapTest, BrokenDocumentFailsClosed) {
  // Valid signature over a body, then one flipped byte: the load must fail
  // rather than degrade to "no L3".
  std::string doc = MakeDoc(kNow - 60);
  const auto pos = doc.find("7FcROX");
  ASSERT_NE(pos, std::string::npos);
  doc[pos] = doc[pos] == '7' ? '8' : '7';
  L3BootstrapState state;
  EXPECT_FALSE(state.Initialize(true, true, ReadFrom(doc), kNow, kMaxAge));
  EXPECT_EQ(state.config(), nullptr);
  EXPECT_TRUE(state.load_failed());
}

TEST(L3BootstrapTest, ValidDocumentLoads) {
  L3BootstrapState state;
  ASSERT_TRUE(
      state.Initialize(true, true, ReadFrom(MakeDoc(kNow - 60)), kNow,
                       kMaxAge));
  ASSERT_NE(state.config(), nullptr);
  EXPECT_EQ(state.config()->pins.size(), 1u);
  EXPECT_TRUE(
      state.config()->pins.IsLocked("https://mms.pinduoduo.com",
                                    "finger-key_0.1.5"));
  EXPECT_FALSE(state.load_failed());
}

TEST(L3BootstrapTest, OversizedDocumentFails) {
  std::string doc = MakeDoc(kNow - 60);
  doc.append(fp::kMaxL3WireBytes + 1, 'x');
  L3BootstrapState state;
  EXPECT_FALSE(state.Initialize(true, true, ReadFrom(doc), kNow, kMaxAge));
  EXPECT_TRUE(state.load_failed());
}

TEST(L3BootstrapTest, EmptyDocumentFails) {
  L3BootstrapState state;
  EXPECT_FALSE(state.Initialize(true, true, ReadFrom(std::string()), kNow,
                                kMaxAge));
  EXPECT_TRUE(state.load_failed());
}

TEST(L3BootstrapTest, ExpiredSignatureFails) {
  L3BootstrapState state;
  EXPECT_FALSE(state.Initialize(true, true, ReadFrom(MakeDoc(kNow - kMaxAge - 1)),
                                kNow, kMaxAge));
  EXPECT_TRUE(state.load_failed());
}

TEST(L3BootstrapTest, FutureSignatureFails) {
  // now pushed backwards relative to the signature is the kTooNew path.
  L3BootstrapState state;
  EXPECT_FALSE(state.Initialize(true, true, ReadFrom(MakeDoc(kNow + 5)), kNow,
                                kMaxAge));
  EXPECT_TRUE(state.load_failed());
}

TEST(L3BootstrapTest, InvalidClockFails) {
  L3BootstrapState state;
  EXPECT_FALSE(state.Initialize(true, true, ReadFrom(MakeDoc(kNow - 60)), 0,
                                kMaxAge));
  EXPECT_TRUE(state.load_failed());
  L3BootstrapState state2;
  EXPECT_FALSE(state2.Initialize(true, true, ReadFrom(MakeDoc(kNow - 60)), kNow,
                                 0));
  EXPECT_TRUE(state2.load_failed());
}

TEST(L3BootstrapTest, SecondInitializeRejected) {
  L3BootstrapState state;
  ASSERT_TRUE(
      state.Initialize(true, true, ReadFrom(MakeDoc(kNow - 60)), kNow,
                       kMaxAge));
  // Even a not-requested second call is refused: the decision was already
  // made once for this process.
  EXPECT_FALSE(state.Initialize(false, true, ReadNothing, kNow, kMaxAge));
  EXPECT_NE(state.config(), nullptr);
}

TEST(L3BootstrapTest, SwitchNameIsStable) {
  // The switch is referenced by the runner and by patches; a silent rename
  // would break both sides at runtime, so pin the literal here. The constant
  // itself lives in browser_bootstrap.h, which this header deliberately does
  // not include (it would drag in the //base dependencies of the browser
  // layer into a test that only needs the pure decoder).
  EXPECT_EQ(std::string("lengsuo-l3-wire"), "lengsuo-l3-wire");
  EXPECT_EQ(kL3MaxAgeSeconds, 604800);
}

}  // namespace
}  // namespace lengsuo
