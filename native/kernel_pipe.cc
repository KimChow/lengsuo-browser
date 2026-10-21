#include "kernel_pipe.h"

#include <openssl/mem.h>

#include <utility>

namespace lengsuo::fp {
namespace {

void SetError(std::string* error, const char* message) {
  if (error != nullptr) {
    *error = message;
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Handshake
// ---------------------------------------------------------------------------

KernelPipeHandshake::KernelPipeHandshake() = default;

KernelPipeHandshake::~KernelPipeHandshake() {
  // The session key never outlives the object: it is wiped here rather than
  // left for the allocator to recycle.
  OPENSSL_cleanse(session_key_.data(), session_key_.size());
}

bool KernelPipeHandshake::SendHello(std::string_view hello_nonce,
                                    std::string* frame_out) {
  if (state_ != KernelPipeState::kWaitHello) {
    return false;
  }
  if (hello_nonce.size() != kKernelPipeHelloNonceBytes) {
    state_ = KernelPipeState::kFailed;
    return false;
  }
  std::string hex;
  hex.reserve(kKernelPipeHelloNonceBytes * 2);
  wire::AppendHex(&hex, hello_nonce);
  // The Hello payload is JSON {version, nonce}; the nonce is the 8 random
  // bytes in hex, which is what the contract's Hello frame carries.
  std::string payload = "{\"version\":";
  payload += std::to_string(kKernelPipeProtocolVersion);
  payload += ",\"nonce\":\"";
  payload += hex;
  payload += "\"}";
  std::string frame;
  if (!EncodeFrame(FrameType::kHello, payload, &frame)) {
    state_ = KernelPipeState::kFailed;
    return false;
  }
  hello_nonce_ = std::string(hello_nonce);
  state_ = KernelPipeState::kHandshake;
  if (frame_out != nullptr) {
    *frame_out = std::move(frame);
  }
  return true;
}

bool KernelPipeHandshake::AcceptKeyEnvelope(const Frame& frame,
                                            std::string_view machine_key,
                                            std::int64_t now_unix,
                                            std::string* error) {
  if (state_ != KernelPipeState::kHandshake) {
    // An envelope that arrives before the Hello, or a second one after the
    // session is up, means the peer is not speaking the protocol. Fail
    // closed: the session is dead rather than half-alive.
    SetError(error, "handshake: unexpected envelope");
    state_ = KernelPipeState::kFailed;
    return false;
  }
  if (frame.type != FrameType::kKeyEnvelope) {
    SetError(error, "handshake: wrong frame type");
    state_ = KernelPipeState::kFailed;
    return false;
  }
  // The AAD is the envelope frame's own header followed by the Hello nonce.
  std::string aad = frame.header;
  aad += hello_nonce_;
  KeyEnvelope envelope;
  if (!OpenKeyEnvelope(frame.payload, machine_key, aad, &envelope, error)) {
    state_ = KernelPipeState::kFailed;
    return false;
  }
  if (envelope.session_key.size() != kKernelPipeSessionKeyBytes) {
    SetError(error, "handshake: bad session key length");
    state_ = KernelPipeState::kFailed;
    return false;
  }
  if (now_unix >= 0 &&
      static_cast<std::uint64_t>(now_unix) > envelope.expires_unix) {
    SetError(error, "handshake: envelope already expired");
    state_ = KernelPipeState::kFailed;
    return false;
  }
  OPENSSL_cleanse(session_key_.data(), session_key_.size());
  session_key_ = std::move(envelope.session_key);
  container_id_ = std::move(envelope.container_id);
  expires_unix_ = envelope.expires_unix;
  state_ = KernelPipeState::kReady;
  return true;
}

bool KernelPipeHandshake::Expired(std::int64_t now_unix) const {
  if (state_ != KernelPipeState::kReady) {
    return true;
  }
  if (now_unix < 0) {
    return false;
  }
  return static_cast<std::uint64_t>(now_unix) > expires_unix_;
}

void KernelPipeHandshake::Reset() {
  OPENSSL_cleanse(session_key_.data(), session_key_.size());
  session_key_.clear();
  container_id_.clear();
  hello_nonce_.clear();
  expires_unix_ = 0;
  state_ = KernelPipeState::kWaitHello;
}

// ---------------------------------------------------------------------------
// Config bundle
// ---------------------------------------------------------------------------

bool KernelPipeConfig::Freeze(std::string_view json, std::string* error) {
  if (frozen_) {
    // One-shot: the configuration cannot change after the browser started.
    SetError(error, "config: already frozen");
    return false;
  }
  JsonObject parsed;
  if (!JsonObject::Parse(json, &parsed)) {
    SetError(error, "config: malformed bundle");
    return false;
  }
  json_ = std::string(json);
  frozen_ = true;
  return true;
}

}  // namespace lengsuo::fp
