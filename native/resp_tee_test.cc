// resp_tee_test - the fan-out judge of the response monitor (task 0017).
//
// The card's requirement is that the page receives its body bytes completely
// unchanged, and this file is where that is proved without a browser: one
// body is fed into the tee, and what came out of the page's side is compared
// against what went in - as bytes, and as a digest.
//
// The other half of the promise is the capture. The monitor reads at most the
// rule's limit; a tee that handed the page only the part it kept would pass a
// naive "the capture is correct" test and still break the page, so the two
// halves are pinned in the same test rather than in two.
//
// What a real browser adds on top - that the data-pipe plumbing in
// integration/resp_throttle.cc moves these bytes and no others - is the
// dispatcher's end-to-end comparison after the merge, not this file's.
#include "resp_tee.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "resp_monitor_body.h"

namespace lengsuo::fp {
namespace {

// A body long enough to be split and to overflow a small limit. It carries a
// NUL and a non-ASCII byte on purpose: a copy that went through anything
// text-aware would show it here, and both are built with explicit values so
// that no literal length has to be counted by hand.
std::string Body() {
  std::string body = "HTTP/1.1 200 OK\r\n\r\n{\"id\":7,\"name\":\"caf";
  body.push_back(static_cast<char>(0xc3));
  body.push_back(static_cast<char>(0xa9));
  body += "\",\"note\":\"a";
  body.push_back('\0');
  body += "b\"}";
  return body;
}

TEST(RespTeeTest, ThePageSideDigestEqualsTheDigestOfWhatWentIn) {
  const std::string body = Body();
  RespTee tee;
  RespTeeReset(&tee, 4096);

  // Awkward pieces on purpose: one byte, then seven, then the rest.
  std::string page;
  std::string page_all;
  std::size_t at = 0;
  for (const std::size_t want : {std::size_t{1}, std::size_t{7}, body.size()}) {
    const std::size_t take = std::min(want, body.size() - at);
    if (take == 0) {
      continue;
    }
    const std::string_view chunk(body.data() + at, take);
    at += take;
    RespTeeFan(&tee, chunk, &page);
    page_all += page;
  }

  // The page's bytes are the bytes that arrived, and so is their digest.
  EXPECT_EQ(page_all, body);
  EXPECT_EQ(RespTeePageDigest(tee), RespMonitorBodyDigest(body));
  EXPECT_EQ(tee.page_bytes, static_cast<std::uint64_t>(body.size()));
}

TEST(RespTeeTest, TheCaptureStopsAtTheLimitAndThePageDoesNot) {
  const std::string body = Body();
  RespTee tee;
  const std::size_t limit = 8;
  RespTeeReset(&tee, limit);

  std::string page;
  RespTeeFan(&tee, body, &page);

  // The monitor kept its limit and says the body was longer.
  EXPECT_EQ(tee.capture.bytes.size(), limit);
  EXPECT_EQ(tee.capture.bytes, body.substr(0, limit));
  EXPECT_TRUE(tee.capture.truncated);
  // It still counted the whole body.
  EXPECT_EQ(tee.capture.total_bytes, static_cast<std::uint64_t>(body.size()));
  // And the page got all of it - this is the regression the card names: a tee
  // that forwarded only the captured prefix would fail right here.
  EXPECT_EQ(page, body);
  EXPECT_EQ(RespTeePageDigest(tee), RespMonitorBodyDigest(body));
}

TEST(RespTeeTest, ALimitOfZeroKeepsNothingAndStillCountsTheBody) {
  const std::string body = Body();
  RespTee tee;
  RespTeeReset(&tee, 0);

  std::string page;
  RespTeeFan(&tee, body, &page);

  EXPECT_TRUE(tee.capture.bytes.empty());
  EXPECT_TRUE(tee.capture.truncated);
  EXPECT_EQ(tee.capture.total_bytes, static_cast<std::uint64_t>(body.size()));
  EXPECT_EQ(page, body);
}

TEST(RespTeeTest, ThePageDigestStartsAtTheDigestOfNothing) {
  // The seed and the capture's digest have to be the same function, or the
  // two "this is what arrived" claims would disagree. That is asserted here,
  // once, rather than trusted.
  RespTee tee;
  RespTeeReset(&tee, 16);
  EXPECT_EQ(RespTeePageDigest(tee), RespMonitorBodyDigest(std::string_view()));
}

TEST(RespTeeTest, AnEmptyChunkChangesNothing) {
  RespTee tee;
  RespTeeReset(&tee, 16);

  std::string page = "leftover";
  RespTeeFan(&tee, std::string_view(), &page);

  // Replaced, not appended: the caller reuses this buffer per chunk.
  EXPECT_TRUE(page.empty());
  EXPECT_EQ(tee.page_bytes, 0u);
  EXPECT_EQ(tee.capture.total_bytes, 0u);
}

TEST(RespTeeTest, TheSourceIsNeverWrittenThrough) {
  // The evidence that the tee copies rather than taps: the buffer the bytes
  // were read from is the same afterwards.
  const std::string body = Body();
  std::string source = body;
  const std::string before = source;

  RespTee tee;
  RespTeeReset(&tee, 4);
  std::string page;
  RespTeeFan(&tee, source, &page);

  EXPECT_EQ(source, before);
  EXPECT_EQ(page, body);
}

TEST(RespTeeTest, ThePageSideTakesOneChunkAtATime) {
  RespTee tee;
  RespTeeReset(&tee, 16);

  // Nothing in flight: the reader may read.
  EXPECT_TRUE(RespTeeWantsRead(tee));
  RespTeeWriteStarted(&tee);
  // A write to the page is outstanding: the reader waits. This is what keeps
  // a slow page from becoming an unbounded buffer.
  EXPECT_FALSE(RespTeeWantsRead(tee));
  RespTeeWriteFinished(&tee);
  EXPECT_TRUE(RespTeeWantsRead(tee));
}

TEST(RespTeeTest, ResetForgetsTheBodyThatCameBefore) {
  RespTee tee;
  RespTeeReset(&tee, 32);

  std::string page;
  RespTeeFan(&tee, Body(), &page);
  EXPECT_NE(tee.page_bytes, 0u);

  RespTeeReset(&tee, 32);
  EXPECT_EQ(tee.page_bytes, 0u);
  EXPECT_TRUE(tee.capture.bytes.empty());
  EXPECT_EQ(tee.capture.total_bytes, 0u);
  EXPECT_FALSE(tee.capture.truncated);
  EXPECT_EQ(RespTeePageDigest(tee), RespMonitorBodyDigest(std::string_view()));
  EXPECT_TRUE(RespTeeWantsRead(tee));
}

}  // namespace
}  // namespace lengsuo::fp