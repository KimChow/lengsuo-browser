// l3_wire - the managed L3 configuration document (L3W1).
//
// This is the single input that drives the whole storage layer: which
// key/value pairs are pinned per origin, which extensions must travel with the
// environment, and which cookie/localStorage pairs are declared to be mirrors
// of each other. It is decoded in the browser process and never in a renderer
// that could also read arbitrary profile files.
//
// Grammar (ASCII, '\n' separated, bounded to kMaxWireBytes):
//
//   L3W1
//   pins <n>
//   <origin>|<scope>|<key>|<value>          exactly n lines
//   cookies <n>
//   <domain>|<name>|<value>                 exactly n lines
//   ext <n>
//   <32 x [a-p]>                            exactly n lines
//   rules <n>
//   <cookie_domain>|<cookie_name>|<storage_origin>|<storage_key>
//                                           exactly n lines
//   sign <md5hex>_v1_t<unix>
//
// A final "\r\n" or "\n" is accepted; nothing may follow the sign line.
//
// The sign covers the exact bytes of the document *before* the "sign " line,
// so field content can never be re-ordered or re-encoded without detection.
// Freshness uses cache_record::VerifySign semantics: integrity before age.
//
// Decoding is fail-closed and transactional: on any error `*out` is untouched.
// In addition the decoded snapshot must be internally consistent - every
// declared rule linking a cookie to a pinned storage key has to agree with the
// pinned value or the whole document is rejected. This is what stops a
// half-updated environment from being launched.
#ifndef LENGSUO_NATIVE_L3_WIRE_H_
#define LENGSUO_NATIVE_L3_WIRE_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cache_record.h"
#include "extension_pin.h"
#include "storage_consistency.h"
#include "storage_pin.h"

namespace lengsuo::fp {

inline constexpr std::size_t kMaxL3WireBytes = 64 * 1024;

struct L3Config {
  StoragePin pins;
  ExtensionPin extensions;
  std::vector<ConsistencyRule> rules;
  // Cookie facts declared by the document; they are consistency *inputs*
  // (what the environment's cookie jar is expected to contain), not live
  // cookie-jar state.
  std::vector<CookieFact> cookies;
  std::string sign;
  int64_t signed_unix = 0;
};

// True when every rule agrees with the pinned values and declared cookies.
bool L3SnapshotConsistent(const L3Config& config);

// Flattens the pin table into storage facts (consistency check input).
std::vector<StorageFact> CollectStorageFacts(const StoragePin& pins);

// Serializes the document back to the canonical wire form. Field order is
// pins, cookies, ext, rules; counts are re-derived, and the sign is
// recomputed over the produced body. Round-trips: decoding the output yields
// an equal L3Config (extensions sorted, pin/rule order preserved).
std::string EncodeL3Wire(const L3Config& config, int64_t unix_seconds);

// `now_unix` / `max_age_seconds` drive the anti-rollback check on the sign.
// Returns false and sets `*error` (when non-null) on any failure.
//
// Passing now_unix <= 0 skips the freshness window: the signature digest and
// the launch consistency gate still apply, but the timestamp is accepted
// whatever it is. A renderer uses that, because the browser already judged
// freshness against its own clock before forwarding the document and a
// renderer must not re-judge it against a clock it does not control.
bool DecodeL3Wire(std::string_view wire,
                  int64_t now_unix,
                  int64_t max_age_seconds,
                  L3Config* out,
                  std::string* error);

// Normalizes a decoded pin table back to the renderer-facing
// "<origin>|<scope>|<key>|<value>\n" wire. Entries are emitted in parse order,
// so EncodePinWire followed by StoragePin::Parse round-trips byte for byte.
std::string EncodePinWire(const StoragePin& pins);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_L3_WIRE_H_
