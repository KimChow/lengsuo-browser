#include "kernel_pipe_envelope.h"

#include <openssl/aead.h>
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
// The envelope
// ---------------------------------------------------------------------------

bool OpenKeyEnvelope(std::string_view payload,
                     std::string_view machine_key,
                     std::string_view aad,
                     KeyEnvelope* out,
                     std::string* error) {
  if (out == nullptr) {
    SetError(error, "envelope: no output");
    return false;
  }
  if (machine_key.size() != kKernelPipeMachineKeyBytes) {
    SetError(error, "envelope: bad machine key length");
    return false;
  }
  if (aad.size() != kKernelPipeAadBytes) {
    SetError(error, "envelope: bad aad length");
    return false;
  }
  // Layout (contract section 3.1, as fixed by the contract's own published
  // payload vector and by docs/task-briefs/client-p1-kernel-pipe-core.txt):
  //   nonce_len(4) nonce(12) ciphertext_len(4) ciphertext(M) tag_len(4)
  //   tag(16).
  //
  // The container id and the expiry are NOT carried in the clear: they live
  // only inside the ciphertext's plaintext, so a middleman cannot rewrite
  // them without breaking the authentication. That also fixes the payload
  // length at 40 + M, which is what the vector's frame length (85) and the
  // AAD's length field describe.
  //
  // Every integer is big-endian.
  std::size_t pos = 0;
  auto need = [&](std::size_t count) { return payload.size() - pos >= count; };
  if (!need(4)) {
    SetError(error, "envelope: truncated");
    return false;
  }
  const std::uint32_t nonce_len = wire::ReadU32BE(payload, pos);
  pos += 4;
  if (nonce_len != kKernelPipeGcmNonceBytes || !need(nonce_len)) {
    SetError(error, "envelope: bad nonce");
    return false;
  }
  // The nonce is carried as a view into the payload: no pointer arithmetic
  // and no copy. The AEAD only reads it.
  const std::string_view nonce = payload.substr(pos, nonce_len);
  pos += nonce_len;

  if (!need(4)) {
    SetError(error, "envelope: truncated");
    return false;
  }
  const std::uint32_t ciphertext_len = wire::ReadU32BE(payload, pos);
  pos += 4;
  if (!need(ciphertext_len)) {
    SetError(error, "envelope: truncated");
    return false;
  }
  const std::string ciphertext(payload.substr(pos, ciphertext_len));
  pos += ciphertext_len;

  if (!need(4)) {
    SetError(error, "envelope: truncated");
    return false;
  }
  const std::uint32_t tag_len = wire::ReadU32BE(payload, pos);
  pos += 4;
  if (tag_len != kKernelPipeGcmTagBytes || !need(tag_len)) {
    SetError(error, "envelope: bad tag");
    return false;
  }
  const std::string tag(payload.substr(pos, tag_len));
  pos += tag_len;
  if (pos != payload.size()) {
    SetError(error, "envelope: trailing bytes");
    return false;
  }

  // The plaintext is session_key(32) || container_id || expires_unix(8).
  if (ciphertext_len < kKernelPipeSessionKeyBytes + 8) {
    SetError(error, "envelope: short ciphertext");
    return false;
  }
  std::string plaintext(ciphertext_len, '\0');
  std::size_t plaintext_len = 0;
  const EVP_AEAD* aead = EVP_aead_aes_256_gcm();
  if (aead == nullptr) {
    SetError(error, "envelope: no aead");
    return false;
  }
  EVP_AEAD_CTX* ctx = EVP_AEAD_CTX_new(
      aead, reinterpret_cast<const std::uint8_t*>(machine_key.data()),
      machine_key.size(), kKernelPipeGcmTagBytes);
  if (ctx == nullptr) {
    SetError(error, "envelope: no aead context");
    return false;
  }
  // The wire keeps the tag in its own field, but the AEAD wants the
  // ciphertext and the tag concatenated.
  std::string sealed = ciphertext;
  sealed += tag;
  const int opened = EVP_AEAD_CTX_open(
      ctx, reinterpret_cast<std::uint8_t*>(plaintext.data()), &plaintext_len,
      plaintext.size(),
      reinterpret_cast<const std::uint8_t*>(nonce.data()), nonce.size(),
      reinterpret_cast<const std::uint8_t*>(sealed.data()), sealed.size(),
      reinterpret_cast<const std::uint8_t*>(aad.data()), aad.size());
  EVP_AEAD_CTX_free(ctx);
  if (opened != 1 || plaintext_len != ciphertext_len) {
    // A failed authentication yields nothing: no partial plaintext escapes.
    OPENSSL_cleanse(plaintext.data(), plaintext.size());
    SetError(error, "envelope: authentication failed");
    return false;
  }

  KeyEnvelope envelope;
  envelope.session_key = plaintext.substr(0, kKernelPipeSessionKeyBytes);
  // The rest is container_id || expires_unix(u64 BE). The expiry is the last
  // eight bytes; everything before it is the container id.
  const std::string tail =
      plaintext.substr(kKernelPipeSessionKeyBytes,
                       plaintext_len - kKernelPipeSessionKeyBytes);
  if (tail.size() < 8) {
    OPENSSL_cleanse(plaintext.data(), plaintext.size());
    SetError(error, "envelope: short plaintext");
    return false;
  }
  envelope.container_id = tail.substr(0, tail.size() - 8);
  if (envelope.container_id.size() > kKernelPipeMaxContainerIdBytes) {
    OPENSSL_cleanse(plaintext.data(), plaintext.size());
    SetError(error, "envelope: container id too long");
    return false;
  }
  envelope.expires_unix = wire::ReadU64BE(tail, tail.size() - 8);
  OPENSSL_cleanse(plaintext.data(), plaintext.size());
  *out = std::move(envelope);
  return true;
}

bool BuildKeyEnvelope(std::string_view session_key,
                      std::string_view container_id,
                      std::uint64_t expires_unix,
                      std::string_view machine_key,
                      std::string_view gcm_nonce,
                      std::string_view hello_nonce,
                      std::string* out,
                      std::string* error) {
  if (out == nullptr) {
    SetError(error, "envelope: no output");
    return false;
  }
  if (session_key.size() != kKernelPipeSessionKeyBytes) {
    SetError(error, "envelope: bad session key length");
    return false;
  }
  if (machine_key.size() != kKernelPipeMachineKeyBytes) {
    SetError(error, "envelope: bad machine key length");
    return false;
  }
  if (gcm_nonce.size() != kKernelPipeGcmNonceBytes) {
    SetError(error, "envelope: bad nonce length");
    return false;
  }
  if (hello_nonce.size() != kKernelPipeHelloNonceBytes) {
    SetError(error, "envelope: bad hello nonce length");
    return false;
  }
  if (container_id.size() > kKernelPipeMaxContainerIdBytes) {
    SetError(error, "envelope: container id too long");
    return false;
  }

  // The plaintext: session_key || container_id || expires_unix(u64 BE).
  std::string plaintext(session_key);
  plaintext.append(container_id);
  wire::PutU64BE(&plaintext, expires_unix);

  // The wire keeps the tag in its own field, so the ciphertext field holds
  // exactly the plaintext's length and the tag field holds the 16 bytes the
  // AEAD appends. The payload length is 40 + the ciphertext length, and the
  // frame header's length field is part of the AAD, so it has to be known
  // before sealing.
  const std::size_t ciphertext_len = plaintext.size();
  const std::size_t payload_len = 4 + kKernelPipeGcmNonceBytes + 4 +
                                  ciphertext_len + 4 + kKernelPipeGcmTagBytes;

  std::string header;
  wire::PutU32BE(&header, static_cast<std::uint32_t>(payload_len));
  header.push_back(static_cast<char>(static_cast<std::uint8_t>(
      FrameType::kKeyEnvelope)));
  header.push_back('\0');
  header.push_back('\0');
  header.push_back('\0');
  std::string aad = header;
  aad.append(hello_nonce);

  std::string sealed(ciphertext_len + kKernelPipeGcmTagBytes, '\0');
  std::size_t sealed_len = 0;
  const EVP_AEAD* aead = EVP_aead_aes_256_gcm();
  if (aead == nullptr) {
    SetError(error, "envelope: no aead");
    return false;
  }
  EVP_AEAD_CTX* ctx = EVP_AEAD_CTX_new(
      aead, reinterpret_cast<const std::uint8_t*>(machine_key.data()),
      machine_key.size(), kKernelPipeGcmTagBytes);
  if (ctx == nullptr) {
    SetError(error, "envelope: no aead context");
    return false;
  }
  const int sealed_ok = EVP_AEAD_CTX_seal(
      ctx, reinterpret_cast<std::uint8_t*>(sealed.data()), &sealed_len,
      sealed.size(), reinterpret_cast<const std::uint8_t*>(gcm_nonce.data()),
      gcm_nonce.size(),
      reinterpret_cast<const std::uint8_t*>(plaintext.data()), plaintext.size(),
      reinterpret_cast<const std::uint8_t*>(aad.data()), aad.size());
  EVP_AEAD_CTX_free(ctx);
  if (sealed_ok != 1 || sealed_len != sealed.size()) {
    SetError(error, "envelope: seal failed");
    return false;
  }

  // The payload: nonce_len | nonce | ciphertext_len | ciphertext | tag_len |
  // tag. The tag is the last 16 bytes of the sealed output.
  std::string payload;
  payload.reserve(payload_len);
  wire::PutU32BE(&payload, kKernelPipeGcmNonceBytes);
  payload.append(gcm_nonce);
  wire::PutU32BE(&payload, static_cast<std::uint32_t>(ciphertext_len));
  payload.append(sealed.substr(0, ciphertext_len));
  wire::PutU32BE(&payload, kKernelPipeGcmTagBytes);
  payload.append(sealed.substr(ciphertext_len));
  if (payload.size() != payload_len) {
    SetError(error, "envelope: bad payload length");
    return false;
  }
  *out = std::move(payload);
  return true;
}

}  // namespace lengsuo::fp
