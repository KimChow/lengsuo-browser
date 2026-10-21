// l3_envelope_session - the seam between the pipe's session key and the L3E1
// document envelope (task N).
//
// Two envelopes live in this tree and they are two different layers:
//
//   native/l3_envelope.{h,cc}          the L3E1 envelope: the L3W1 document
//                                      in its on-disk form, sealed under a
//                                      32-byte master key. Little-endian
//                                      header, PBKDF2-HMAC-SHA256, 76 bytes
//                                      of overhead.
//   native/kernel_pipe_envelope.{h,cc} the KeyEnvelope of the runner <-> kernel
//                                      pipe: the session key handed down at
//                                      handshake time. Big-endian, no KDF, an
//                                      85-byte payload.
//
// Their only meeting point is this one: **the key is supplied by the caller**.
// This file is that meeting point, and it is deliberately thin.
//
// Where the key comes from (and it is not from here):
//
//   The runner seals the session key into the pipe's KeyEnvelope; the kernel
//   opens it in the handshake (task Q, native/kernel_pipe.{h,cc}). The
//   handshake holds the key in memory only, and hands it out as
//   KernelPipeHandshake::session_key(). **That is the caller**:
//
//       L3EnvelopeKey key;
//       if (!MakeL3EnvelopeKey(handshake.session_key(), &key, &error)) { ... }
//       SealL3Envelope(document, key, iterations, &envelope, &error);
//
// This module never derives a key, never defaults one, never reads one from
// the environment, a file or a constant. A caller with no key has no
// envelope: the failure is "unavailable", not "a weak key".
//
// The layer boundary is deliberate: this file does not include
// kernel_pipe*.h, because a disk-format module must not depend on the
// transport that happens to carry its key today. The one thing both layers
// must agree on - the session key's length - is pinned here and checked
// against the pipe's own constant by the test, not by a header include.
//
// The lifetime of the key is the caller's business too: the pipe's session
// key expires, and the contract says an expired key must be refused. The
// envelope layer imposes no lifetime of its own, because the key's source
// may stop being a per-session key later. See docs/l3-reports/task-N.md
// section 4 for the open questions.
#ifndef LENGSUO_NATIVE_L3_ENVELOPE_SESSION_H_
#define LENGSUO_NATIVE_L3_ENVELOPE_SESSION_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "l3_envelope.h"

namespace lengsuo::fp {

// The length the caller's key must have. It is the envelope's own key length
// (32 bytes) and also the length the pipe contract pins for the session key
// (contract section 1), so the pipe's session key fits the envelope without
// padding or truncation.
inline constexpr std::size_t kL3EnvelopeSessionKeyBytes = kL3EnvelopeKeyBytes;

// Builds the envelope's master key from the caller's key.
//
// `session_key` must be exactly kL3EnvelopeSessionKeyBytes long. A shorter
// or longer key is refused rather than padded or truncated: padding would
// silently weaken the KDF, truncating would ignore bits the caller believed
// were protecting the document.
//
// Returns false and sets `*error` (when non-null) on failure. `*out` is left
// untouched on failure, so a rejected key cannot leave a half-built key
// behind.
bool MakeL3EnvelopeKey(std::string_view session_key,
                       L3EnvelopeKey* out,
                       std::string* error);

// Seals an L3W1 document under the caller's key.
//
// `iterations` of 0 means kDefaultL3EnvelopeIterations. This is
// SealL3Envelope with the key taken from the caller in one step, so a caller
// cannot accidentally seal under a key it never validated.
//
// Returns false and sets `*error` (when non-null) on failure. `*out` is left
// untouched on failure.
bool SealL3Document(std::string_view document,
                    std::string_view session_key,
                    std::uint32_t iterations,
                    std::string* out,
                    std::string* error);

// Opens an envelope with the caller's key.
//
// Returns false and sets `*error` (when non-null) on failure. `*out` is left
// untouched on failure, so no partial plaintext escapes.
bool OpenL3Document(std::string_view envelope,
                    std::string_view session_key,
                    std::string* out,
                    std::string* error);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_L3_ENVELOPE_SESSION_H_
