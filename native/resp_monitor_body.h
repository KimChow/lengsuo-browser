// resp_monitor_body - the side channel that reads a response body (task T1).
//
// The monitor must not change what the page receives. It therefore does not
// read the body for the page: it copies the bytes the network stack has
// already read into a buffer of its own, and the page's bytes go on to the
// page untouched. That is a tee, not a tap.
//
// The two rules that shape the module:
//
//   * The copy is bounded. The capture buffer holds at most
//     `max_body_bytes`; the rest of the body is counted but not kept. A
//     monitor that keeps everything is a way to make the browser allocate on
//     a hostile page's schedule.
//   * The copy never changes the source. The source arrives as a
//     `std::string_view`, which cannot be written through, and the module
//     never copies from it destructively. The evidence that the original
//     response is unchanged is in the test: the same bytes are fed in and
//     the captured prefix comes out equal to the source prefix, with the
//     source itself untouched.
//
// The body is never decompressed. A compressed response is not monitored at
// all: the module would have to inflate it to read it, and inflating is a
// different job with its own failure modes.
#ifndef LENGSUO_NATIVE_RESP_MONITOR_BODY_H_
#define LENGSUO_NATIVE_RESP_MONITOR_BODY_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lengsuo::fp {

// The capture of one response body.
struct RespMonitorBody {
  // The captured prefix. Empty when the capture is switched off.
  std::string bytes;
  // How many body bytes went past. This counts the whole body, not only the
  // captured part, so a truncated body can say how much it did not keep.
  std::uint64_t total_bytes = 0;
  // The capture limit. Zero means "count the bytes, keep none of them".
  std::size_t limit = 0;
  // True when the body was longer than the limit.
  bool truncated = false;
};

// Starts a capture. `limit` is the most bytes the capture may keep.
void RespMonitorBodyReset(RespMonitorBody* body, std::size_t limit);

// Feeds body bytes to the capture.
//
// `data` is read and never written: it is the network stack's buffer, and
// the whole point of the module is that the bytes the page receives are the
// bytes the page received before.
//
// The bytes are copied until the limit is reached; after that they are only
// counted.
void RespMonitorBodyAppend(RespMonitorBody* body, std::string_view data);

// How many bytes the capture holds.
std::size_t RespMonitorBodySize(const RespMonitorBody& body);

// A digest of `data`, used by the test to show that what came out of the
// capture is what went in.
//
// FNV-1a, 64 bits. It is not a security primitive and is not one: it is a
// short fingerprint that says "these two byte strings are the same" without
// carrying them.
std::uint64_t RespMonitorBodyDigest(std::string_view data);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_RESP_MONITOR_BODY_H_
