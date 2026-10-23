// resp_monitor_body_test - the body capture (task T1).
//
// The capture is a tee: the page's bytes go on to the page exactly as they
// were, and the monitor keeps a copy of the first few of them. The test that
// matters most is the one that says the copy did not change the source.
#include "resp_monitor_body.h"

#include <gtest/gtest.h>

#include <string>

namespace lengsuo::fp {
namespace {

TEST(RespMonitorBody, KeepsWhatItWasGiven) {
  RespMonitorBody body;
  RespMonitorBodyReset(&body, 64);
  RespMonitorBodyAppend(&body, "hello");
  EXPECT_EQ(body.bytes, "hello");
  EXPECT_EQ(RespMonitorBodySize(body), 5u);
  EXPECT_EQ(body.total_bytes, 5u);
  EXPECT_FALSE(body.truncated);
}

TEST(RespMonitorBody, TheLimitTruncatesAndSaysSo) {
  RespMonitorBody body;
  RespMonitorBodyReset(&body, 4);
  RespMonitorBodyAppend(&body, "abcdefgh");
  // Only the first four bytes are kept.
  EXPECT_EQ(body.bytes, "abcd");
  EXPECT_EQ(RespMonitorBodySize(body), 4u);
  // The count is of the whole body, so a truncated body can say how much it
  // did not keep.
  EXPECT_EQ(body.total_bytes, 8u);
  EXPECT_TRUE(body.truncated);
}

TEST(RespMonitorBody, TheLimitIsNeverExceeded) {
  RespMonitorBody body;
  RespMonitorBodyReset(&body, 4);
  RespMonitorBodyAppend(&body, "ab");
  EXPECT_FALSE(body.truncated);
  RespMonitorBodyAppend(&body, "cdef");
  EXPECT_TRUE(body.truncated);
  EXPECT_EQ(body.bytes, "abcd");
  EXPECT_EQ(body.total_bytes, 6u);
  // More bytes after the limit are counted and dropped, not kept.
  RespMonitorBodyAppend(&body, "ghijkl");
  EXPECT_EQ(body.bytes, "abcd");
  EXPECT_EQ(body.total_bytes, 12u);
}

TEST(RespMonitorBody, AZeroLimitCountsAndKeepsNothing) {
  RespMonitorBody body;
  RespMonitorBodyReset(&body, 0);
  RespMonitorBodyAppend(&body, "abc");
  EXPECT_TRUE(body.bytes.empty());
  EXPECT_EQ(body.total_bytes, 3u);
  EXPECT_TRUE(body.truncated);
}

TEST(RespMonitorBody, TheSourceIsNotChanged) {
  // The capture reads the network stack's buffer and never writes through
  // it: the bytes the page receives are the bytes the page received before.
  RespMonitorBody body;
  RespMonitorBodyReset(&body, 64);
  std::string source = "the original response";
  const std::uint64_t before = RespMonitorBodyDigest(source);
  RespMonitorBodyAppend(&body, source);
  EXPECT_EQ(source, "the original response");
  EXPECT_EQ(RespMonitorBodyDigest(source), before);
  EXPECT_EQ(body.bytes, source);
  EXPECT_EQ(RespMonitorBodyDigest(body.bytes), before);
}

TEST(RespMonitorBody, TheCapturedPrefixEqualsTheSourcePrefix) {
  // The same bytes in, the same bytes out: what comes out of the capture is
  // what went in, byte for byte.
  RespMonitorBody body;
  RespMonitorBodyReset(&body, 1024);
  std::string source;
  for (int i = 0; i < 256; ++i) {
    source.push_back(static_cast<char>(i));
  }
  RespMonitorBodyAppend(&body, source);
  EXPECT_EQ(body.bytes.size(), source.size());
  EXPECT_EQ(RespMonitorBodyDigest(body.bytes), RespMonitorBodyDigest(source));
}

TEST(RespMonitorBody, ATruncatedCaptureKeepsThePrefixByteForByte) {
  RespMonitorBody body;
  RespMonitorBodyReset(&body, 10);
  std::string source = "0123456789abcdef";
  RespMonitorBodyAppend(&body, source);
  ASSERT_EQ(body.bytes.size(), 10u);
  EXPECT_EQ(body.bytes, source.substr(0, 10));
  EXPECT_EQ(RespMonitorBodyDigest(body.bytes),
            RespMonitorBodyDigest(source.substr(0, 10)));
}

TEST(RespMonitorBody, AnEmptyAppendChangesNothing) {
  RespMonitorBody body;
  RespMonitorBodyReset(&body, 64);
  RespMonitorBodyAppend(&body, "");
  EXPECT_TRUE(body.bytes.empty());
  EXPECT_EQ(body.total_bytes, 0u);
  EXPECT_FALSE(body.truncated);
}

TEST(RespMonitorBody, AResetClearsEverything) {
  RespMonitorBody body;
  RespMonitorBodyReset(&body, 4);
  RespMonitorBodyAppend(&body, "abcdef");
  ASSERT_TRUE(body.truncated);
  RespMonitorBodyReset(&body, 4);
  EXPECT_TRUE(body.bytes.empty());
  EXPECT_EQ(body.total_bytes, 0u);
  EXPECT_FALSE(body.truncated);
  EXPECT_EQ(body.limit, 4u);
}

TEST(RespMonitorBody, TheDigestTellsTwoByteStringsApart) {
  EXPECT_EQ(RespMonitorBodyDigest("abc"), RespMonitorBodyDigest("abc"));
  EXPECT_NE(RespMonitorBodyDigest("abc"), RespMonitorBodyDigest("abd"));
  EXPECT_NE(RespMonitorBodyDigest("abc"), RespMonitorBodyDigest("ab"));
  // The empty string has a digest of its own.
  EXPECT_NE(RespMonitorBodyDigest(""), RespMonitorBodyDigest("a"));
}

}  // namespace
}  // namespace lengsuo::fp
