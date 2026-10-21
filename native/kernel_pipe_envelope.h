// kernel_pipe_envelope - the KeyEnvelope of the runner <-> kernel pipe
// (task Q).
//
// The runner seals the session key into a KeyEnvelope; the kernel opens it.
// The payload layout is pinned by docs/kernel-wire-contract.md section 3.1,
// field by field, and the contract publishes a fixed vector for it. The
// client repo (task P) hardcodes the same bytes.
//
// Fail-closed is the rule: an envelope that does not authenticate yields no
// plaintext at all, not a partial one.
//
// The machine-code derivation the envelope is keyed with lives in
// kernel_pipe_keys.{h,cc}.
#ifndef LENGSUO_NATIVE_KERNEL_PIPE_ENVELOPE_H_
#define LENGSUO_NATIVE_KERNEL_PIPE_ENVELOPE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "kernel_pipe_frame.h"
#include "kernel_pipe_keys.h"

namespace lengsuo::fp {

// The GCM nonce and tag sizes. Both are the GCM standard sizes; an 8-byte
// nonce is a legal GCM form but is not what the contract pins.
inline constexpr std::size_t kKernelPipeGcmNonceBytes = 12;
inline constexpr std::size_t kKernelPipeGcmTagBytes = 16;

// The size of the AAD that binds the envelope to the handshake that asked
// for it (frame header + hello nonce).
inline constexpr std::size_t kKernelPipeAadBytes =
    kKernelPipeHeaderBytes + kKernelPipeHelloNonceBytes;

// The container id carried by the envelope, bounded so a hostile peer
// cannot make the kernel allocate without limit.
inline constexpr std::size_t kKernelPipeMaxContainerIdBytes = 256;

// The contents of a KeyEnvelope once it has been opened.
struct KeyEnvelope {
  std::string session_key;  // exactly 32 bytes
  std::string container_id;
  std::uint64_t expires_unix = 0;
};

// Opens a KeyEnvelope payload (contract section 3.1).
//
// `aad` must be the 16 bytes the contract pins: the envelope frame's own
// 8-byte header followed by the 8-byte Hello nonce. Opening with any other
// AAD fails, which is what makes a captured envelope useless in another
// handshake.
//
// Returns false and sets `*error` (when non-null) on any failure. `*out` is
// left untouched, so a wrong key or a tampered envelope yields no partial
// plaintext. The error text never carries payload bytes.
bool OpenKeyEnvelope(std::string_view payload,
                     std::string_view machine_key,
                     std::string_view aad,
                     KeyEnvelope* out,
                     std::string* error);

// Builds the KeyEnvelope payload for a session key (contract section 3.1).
//
// The kernel never sends an envelope - the runner does - but the builder is
// here so the contract's fixed vector can be produced and checked by the
// same code that reads it, and so the client side has a reference to mirror.
//
// The AAD is the envelope frame's own 8-byte header (whose length field is
// this payload's length) followed by the 8-byte Hello nonce; the caller
// passes the hello nonce and the builder derives the header.
//
// Returns false and sets `*error` (when non-null) on any failure.
bool BuildKeyEnvelope(std::string_view session_key,
                      std::string_view container_id,
                      std::uint64_t expires_unix,
                      std::string_view machine_key,
                      std::string_view gcm_nonce,
                      std::string_view hello_nonce,
                      std::string* out,
                      std::string* error);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_KERNEL_PIPE_ENVELOPE_H_
