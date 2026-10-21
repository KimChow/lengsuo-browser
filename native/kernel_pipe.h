// kernel_pipe - the kernel side of the runner <-> kernel pipe (task Q).
//
// docs/kernel-wire-contract.md is the single source of truth for the bytes
// that travel on this pipe. This file is its kernel-side implementation; the
// client repo (task P) implements the mirror image. The two sides align on
// the fixed test vectors the contract pins, not on a verbal convention.
//
// The pipe is built from four layers, each in its own file:
//
//   kernel_pipe_frame.{h,cc}      frames, the 8-byte header, the byte helpers
//   kernel_pipe_json.{h,cc}       the shallow JSON reader
//   kernel_pipe_keys.{h,cc}       the machine-code derivation
//   kernel_pipe_envelope.{h,cc}   the KeyEnvelope
//   kernel_pipe.{h,cc}            this file: the handshake and the config
//
// The split is by dependency, not by convenience: the JSON reader must be
// testable without a crypto library, and the frame codec without either.
// The layers below are pulled in through the headers.
//
// Everything here is pure: no Chromium, no base/, no threads, no sockets.
// The transport that carries these bytes lives in the browser-side wiring
// (patches/0016-browser-wiring.patch) and is deliberately kept out of this
// translation unit, so the whole file still compiles standalone for the
// gtest target.
//
// Fail-closed is the rule everywhere: a frame that does not decode, an
// envelope that does not authenticate, a config that does not parse - all of
// them are errors. Nothing here ever degrades into "no configuration".
#ifndef LENGSUO_NATIVE_KERNEL_PIPE_H_
#define LENGSUO_NATIVE_KERNEL_PIPE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "kernel_pipe_envelope.h"
#include "kernel_pipe_frame.h"
#include "kernel_pipe_json.h"
#include "kernel_pipe_keys.h"

namespace lengsuo::fp {

// ---------------------------------------------------------------------------
// Handshake (contract section 1)
// ---------------------------------------------------------------------------

enum class KernelPipeState : std::uint8_t {
  kWaitHello,   // nothing sent yet
  kHandshake,   // Hello sent, waiting for the KeyEnvelope
  kReady,       // the session key is in hand
  kFailed,      // fail-closed: the session is dead
};

// The kernel's half of the handshake.
//
// The kernel sends the Hello, receives the KeyEnvelope, opens it and only
// then reaches kReady. Every rejection ends in kFailed: the session is never
// downgraded to "no session key".
class KernelPipeHandshake {
 public:
  KernelPipeHandshake();
  ~KernelPipeHandshake();
  KernelPipeHandshake(const KernelPipeHandshake&) = delete;
  KernelPipeHandshake& operator=(const KernelPipeHandshake&) = delete;

  KernelPipeState state() const { return state_; }
  bool ready() const { return state_ == KernelPipeState::kReady; }
  bool failed() const { return state_ == KernelPipeState::kFailed; }

  // Builds the Hello frame and moves kWaitHello -> kHandshake. `hello_nonce`
  // must be exactly 8 bytes. Returns false and moves to kFailed otherwise.
  bool SendHello(std::string_view hello_nonce, std::string* frame_out);

  // Consumes the KeyEnvelope frame and moves kHandshake -> kReady.
  //
  // `machine_key` is k_machine; `now_unix` is the current Unix time in
  // seconds. An envelope that does not authenticate, or one whose expiry has
  // passed, fails the session. `*error` never carries payload bytes.
  bool AcceptKeyEnvelope(const Frame& frame,
                          std::string_view machine_key,
                          std::int64_t now_unix,
                          std::string* error);

  // The session key. Only meaningful once ready() is true.
  const std::string& session_key() const { return session_key_; }
  const std::string& container_id() const { return container_id_; }
  std::uint64_t expires_unix() const { return expires_unix_; }

  // True when the session key has outlived its expiry. A caller that uses
  // the key after this point is using an expired key.
  bool Expired(std::int64_t now_unix) const;

  // Wipes the session key and returns the session to kWaitHello.
  void Reset();

 private:
  KernelPipeState state_ = KernelPipeState::kWaitHello;
  std::string hello_nonce_;
  std::string session_key_;
  std::string container_id_;
  std::uint64_t expires_unix_ = 0;
};

// ---------------------------------------------------------------------------
// Config bundle (contract section 3, type 0x03)
// ---------------------------------------------------------------------------

// The configuration bundle the runner sends after the handshake.
//
// The bundle is JSON. The kernel parses it once, freezes it, and never reads
// it again - the same one-shot rule the L3 document follows.
class KernelPipeConfig {
 public:
  KernelPipeConfig() = default;
  ~KernelPipeConfig() = default;
  KernelPipeConfig(const KernelPipeConfig&) = delete;
  KernelPipeConfig& operator=(const KernelPipeConfig&) = delete;

  // Parses and freezes the bundle. A second call fails: the configuration
  // cannot change after the browser has started. Returns false and sets
  // `*error` (when non-null) on a malformed bundle.
  bool Freeze(std::string_view json, std::string* error);

  bool frozen() const { return frozen_; }
  // The frozen bundle's text. Empty when nothing was frozen.
  const std::string& json() const { return json_; }

 private:
  bool frozen_ = false;
  std::string json_;
};

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_KERNEL_PIPE_H_
