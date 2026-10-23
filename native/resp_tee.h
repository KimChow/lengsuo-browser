// resp_tee - the fan-out tee of the response-body monitor (task 0017, the T1
// wiring).
//
// The monitor must not change what the page receives. That promise is kept -
// or broken - here, in the one place where a body chunk is turned into two
// copies: one for the page, byte for byte the same as what arrived, and one
// bounded copy for the monitor.
//
// Why not the tee the tree already has. content/browser/preloading/prefetch/
// prefetch_data_pipe_tee.cc is a tee, but it serves a different shape. It
// allows one active target while the source is being read and refuses a
// second clone until the first is done, and past its buffer limit it drops
// the bytes it already read and streams the rest. Both are correct for a
// prefetch that is read once by a prerender and then by a navigation, and
// both are wrong here:
//
//   * the page and the monitor need their copies at the same time, not one
//     after the other, so a tee that refuses the second target would refuse
//     the page;
//   * the page's copy may not lose its head, so a tee that drops the early
//     bytes would corrupt the page.
//
// The brief names that file for exactly this reason.
//
// What this module is. The fan-out is pure: it takes the chunk the network
// handed it and yields the bytes for the page plus the bounded prefix for the
// monitor. It is pure because tools/rebuild/run_l3_gtest.sh compiles it with
// a plain g++, which has no Chromium type; the data-pipe plumbing that moves
// those bytes (two pipes, a watcher, a producer) lives in
// integration/resp_throttle.cc, where mojo is available.
//
// The judge. The test feeds one body in and compares what came out of the
// page's side against what went in: equal bytes, and equal digests, mean the
// page's copy is untouched. The same test pins the capture at the rule's
// limit, which is the other half of the promise - the monitor keeps at most
// `max_body_bytes` of a body it did not ask for.
//
// The gate. `write_in_flight` is the one piece of state that is not a
// number. The page's copy is the copy that must not be delayed, so the
// reader does not read while a write to the page is still in flight; that
// also bounds the tee to one chunk in memory at a time, which is what makes
// a slow page a slow reader rather than a growing buffer.
#ifndef LENGSUO_NATIVE_RESP_TEE_H_
#define LENGSUO_NATIVE_RESP_TEE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "resp_monitor_body.h"

namespace lengsuo::fp {

// The starting state of the page-side digest.
//
// The same FNV-1a as RespMonitorBodyDigest, and this is that function's value
// for an empty input: a digest that starts here and is advanced once per byte
// is the digest of every byte it was advanced over. The test pins the two
// against each other, so the two copies of the constant cannot drift apart.
inline constexpr std::uint64_t kRespTeeDigestSeed = 1469598103934665603ULL;

// The state of one tee.
struct RespTee {
  // What the monitor keeps of the body. Bounded by the rule's limit; the
  // page's copy is not bounded and does not live here.
  RespMonitorBody capture;
  // How many bytes went to the page. This is the whole body: it is the
  // number the report carries, and the monitor's copy may be shorter.
  std::uint64_t page_bytes = 0;
  // A running digest of exactly the bytes that went to the page. It is the
  // evidence that the page received what arrived: the caller never holds the
  // whole body, because the body goes straight down a pipe.
  std::uint64_t page_digest = kRespTeeDigestSeed;
  // True while a piece written to the page has not been acknowledged. The
  // reader must not read while this is set.
  bool write_in_flight = false;
};

// Starts a tee. `capture_limit` is the most bytes the monitor may keep.
void RespTeeReset(RespTee* tee, std::size_t capture_limit);

// Fans one chunk out.
//
// The whole chunk is copied to `page_out` - the buffer that goes on to the
// page, and nothing else may be written there - and the bounded prefix of it
// is copied to the capture. `data` is read and never written: it is the
// network stack's buffer, and the point of the module is that what the page
// receives is what the page received before.
//
// `page_out` is replaced, not appended to: the caller reuses one buffer per
// chunk, and a buffer that grew with the body would be the leak this module
// exists to avoid.
void RespTeeFan(RespTee* tee, std::string_view data, std::string* page_out);

// The digest of everything that has gone to the page so far.
std::uint64_t RespTeePageDigest(const RespTee& tee);

// Whether the reader may read another chunk: it may not while a write to the
// page is still in flight.
bool RespTeeWantsRead(const RespTee& tee);

// Marks a write to the page as started, and as finished.
void RespTeeWriteStarted(RespTee* tee);
void RespTeeWriteFinished(RespTee* tee);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_RESP_TEE_H_