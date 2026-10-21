#include "l3_envelope.h"

#include <openssl/aead.h>
#include <openssl/evp.h>
#include <openssl/mem.h>
#include <openssl/rand.h>

#include <cstring>
#include <utility>

namespace lengsuo::fp {
namespace {

constexpr char kMagic[4] = {'L', '3', 'E', '1'};
constexpr std::uint8_t kFormatVersion = 1;
constexpr std::uint8_t kKdfPbkdf2HmacSha256 = 1;
constexpr std::uint8_t kCipherAes256Gcm = 1;

// Little-endian 32-bit load/store. The envelope is a byte-exact on-disk
// format, so the encoding must not depend on the host's endianness.
void StoreU32(char* out, std::uint32_t value) {
  out[0] = static_cast<char>(value & 0xff);
  out[1] = static_cast<char>((value >> 8) & 0xff);
  out[2] = static_cast<char>((value >> 16) & 0xff);
  out[3] = static_cast<char>((value >> 24) & 0xff);
}

std::uint32_t LoadU32(const char* in) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(in[0])) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(in[1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(in[2])) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(in[3])) << 24);
}

// Derives the 32-byte AEAD key from the master key, the salt and the
// iteration count.  Returns false when the KDF fails (e.g. an iteration
// count of 0, which BoringSSL rejects).
bool DeriveKey(const L3EnvelopeKey& master,
               const std::uint8_t* salt,
               std::size_t salt_len,
               std::uint32_t iterations,
               std::uint8_t* out) {
  if (iterations == 0) return false;
  return PKCS5_PBKDF2_HMAC(master.bytes().data(), master.bytes().size(), salt,
                            salt_len, iterations, EVP_sha256(),
                            kL3EnvelopeKeyBytes, out) == 1;
}

// Wipes a buffer that held key material.
void Wipe(void* buffer, std::size_t size) {
  if (buffer != nullptr && size > 0) {
    OPENSSL_cleanse(buffer, size);
  }
}

}  // namespace

L3EnvelopeKey::L3EnvelopeKey(const std::string& bytes) {
  // A wrong-length key is rejected outright.  Padding a short key up would
  // silently weaken the KDF, and truncating a long one would ignore bits the
  // caller believed were protecting the document.
  if (bytes.size() != kL3EnvelopeKeyBytes) {
    return;
  }
  bytes_ = bytes;
  valid_ = true;
}

L3EnvelopeKey::~L3EnvelopeKey() {
  Wipe(bytes_.data(), bytes_.size());
}

L3EnvelopeKey::L3EnvelopeKey(L3EnvelopeKey&& other) noexcept
    : bytes_(std::move(other.bytes_)), valid_(other.valid_) {
  other.bytes_.clear();
  other.valid_ = false;
}

L3EnvelopeKey& L3EnvelopeKey::operator=(L3EnvelopeKey&& other) noexcept {
  if (this != &other) {
    Wipe(bytes_.data(), bytes_.size());
    bytes_ = std::move(other.bytes_);
    valid_ = other.valid_;
    other.bytes_.clear();
    other.valid_ = false;
  }
  return *this;
}

bool SealL3Envelope(std::string_view plaintext,
                    const L3EnvelopeKey& key,
                    std::uint32_t iterations,
                    std::string* out,
                    std::string* error) {
  const auto fail = [error](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (out == nullptr) return fail("l3envelope: null output");
  if (!key.valid()) return fail("l3envelope: invalid key");
  if (plaintext.empty()) return fail("l3envelope: empty document");
  if (plaintext.size() > kMaxL3EnvelopePlaintextBytes) {
    return fail("l3envelope: oversized document");
  }
  if (iterations == 0) iterations = kDefaultL3EnvelopeIterations;

  std::uint8_t salt[kL3EnvelopeSaltBytes] = {};
  std::uint8_t nonce[kL3EnvelopeNonceBytes] = {};
  if (RAND_bytes(salt, sizeof(salt)) != 1 ||
      RAND_bytes(nonce, sizeof(nonce)) != 1) {
    return fail("l3envelope: no randomness");
  }

  std::uint8_t derived[kL3EnvelopeKeyBytes] = {};
  if (!DeriveKey(key, salt, sizeof(salt), iterations, derived)) {
    Wipe(derived, sizeof(derived));
    return fail("l3envelope: key derivation failed");
  }

  EVP_AEAD_CTX* ctx = EVP_AEAD_CTX_new(EVP_aead_aes_256_gcm(), derived,
                                        sizeof(derived),
                                        kL3EnvelopeTagBytes);
  if (ctx == nullptr) {
    Wipe(derived, sizeof(derived));
    return fail("l3envelope: aead init failed");
  }

  // The ciphertext is the plaintext followed by the GCM tag.
  const std::size_t ciphertext_len = plaintext.size() + kL3EnvelopeTagBytes;

  // The header is additional data: it is authenticated but not encrypted, so
  // a flipped length or iteration count fails the open instead of changing
  // how the payload is interpreted.
  char header[kL3EnvelopeHeaderBytes] = {};
  std::memcpy(header, kMagic, sizeof(kMagic));
  header[4] = static_cast<char>(kFormatVersion);
  header[5] = static_cast<char>(kKdfPbkdf2HmacSha256);
  header[6] = static_cast<char>(kCipherAes256Gcm);
  header[7] = 0;
  StoreU32(header + 8, kL3EnvelopeHeaderBytes);
  StoreU32(header + 12, iterations);
  StoreU32(header + 16, static_cast<std::uint32_t>(sizeof(salt)));
  StoreU32(header + 20, static_cast<std::uint32_t>(sizeof(nonce)));
  StoreU32(header + 24, static_cast<std::uint32_t>(kL3EnvelopeTagBytes));
  StoreU32(header + 28, static_cast<std::uint32_t>(ciphertext_len));

  std::string buffer;
  buffer.reserve(kL3EnvelopeHeaderBytes + sizeof(salt) + sizeof(nonce) +
                 ciphertext_len);
  buffer.append(header, sizeof(header));
  buffer.append(reinterpret_cast<const char*>(salt), sizeof(salt));
  buffer.append(reinterpret_cast<const char*>(nonce), sizeof(nonce));
  // The buffer is grown to its final size before sealing, so the AEAD
  // writes into the string's own storage rather than past its end.
  buffer.resize(kL3EnvelopeHeaderBytes + sizeof(salt) + sizeof(nonce) +
                ciphertext_len);

  std::size_t sealed_len = 0;
  const bool sealed = EVP_AEAD_CTX_seal(
      ctx, reinterpret_cast<std::uint8_t*>(&buffer[kL3EnvelopeHeaderBytes +
                                                    sizeof(salt) +
                                                    sizeof(nonce)]),
      &sealed_len, ciphertext_len, nonce, sizeof(nonce),
      reinterpret_cast<const std::uint8_t*>(plaintext.data()),
      plaintext.size(), reinterpret_cast<const std::uint8_t*>(header),
      sizeof(header)) == 1;
  EVP_AEAD_CTX_free(ctx);
  Wipe(derived, sizeof(derived));
  if (!sealed || sealed_len != ciphertext_len) {
    return fail("l3envelope: seal failed");
  }
  buffer.resize(kL3EnvelopeHeaderBytes + sizeof(salt) + sizeof(nonce) +
                sealed_len);

  *out = std::move(buffer);
  if (error) error->clear();
  return true;
}

bool OpenL3Envelope(std::string_view envelope,
                    const L3EnvelopeKey& key,
                    std::string* out,
                    std::string* error) {
  const auto fail = [error](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (out == nullptr) return fail("l3envelope: null output");
  if (!key.valid()) return fail("l3envelope: invalid key");
  if (envelope.empty()) return fail("l3envelope: empty envelope");
  if (envelope.size() > kMaxL3EnvelopeBytes) {
    return fail("l3envelope: oversized envelope");
  }
  // The envelope must carry at least a header, a salt, a nonce and a tag.
  if (envelope.size() < kL3EnvelopeOverheadBytes) {
    return fail("l3envelope: truncated envelope");
  }
  if (envelope.size() < kL3EnvelopeHeaderBytes) {
    return fail("l3envelope: truncated header");
  }
  if (std::memcmp(envelope.data(), kMagic, sizeof(kMagic)) != 0) {
    return fail("l3envelope: bad magic");
  }
  if (static_cast<std::uint8_t>(envelope[4]) != kFormatVersion) {
    return fail("l3envelope: unsupported version");
  }
  if (static_cast<std::uint8_t>(envelope[5]) != kKdfPbkdf2HmacSha256 ||
      static_cast<std::uint8_t>(envelope[6]) != kCipherAes256Gcm) {
    return fail("l3envelope: unsupported algorithm");
  }
  if (static_cast<std::uint8_t>(envelope[7]) != 0) {
    return fail("l3envelope: reserved bits set");
  }

  const std::uint32_t header_len = LoadU32(envelope.data() + 8);
  const std::uint32_t iterations = LoadU32(envelope.data() + 12);
  const std::uint32_t salt_len = LoadU32(envelope.data() + 16);
  const std::uint32_t nonce_len = LoadU32(envelope.data() + 20);
  const std::uint32_t tag_len = LoadU32(envelope.data() + 24);
  const std::uint32_t ciphertext_len = LoadU32(envelope.data() + 28);

  // Every length must be exactly what this format defines. Accepting a
  // different salt or nonce size would mean interpreting bytes the writer
  // never meant, and a header length other than the fixed one would move
  // the payload boundary.
  if (header_len != kL3EnvelopeHeaderBytes ||
      salt_len != kL3EnvelopeSaltBytes ||
      nonce_len != kL3EnvelopeNonceBytes ||
      tag_len != kL3EnvelopeTagBytes) {
    return fail("l3envelope: bad header lengths");
  }
  if (iterations == 0) return fail("l3envelope: bad iteration count");
  if (ciphertext_len < tag_len) return fail("l3envelope: bad ciphertext length");
  const std::size_t plaintext_len = ciphertext_len - tag_len;
  if (plaintext_len == 0) return fail("l3envelope: empty document");

  const std::size_t expected =
      kL3EnvelopeHeaderBytes + salt_len + nonce_len + ciphertext_len;
  if (envelope.size() != expected) {
    return fail("l3envelope: truncated or oversized envelope");
  }

  const char* salt = envelope.data() + kL3EnvelopeHeaderBytes;
  const char* nonce = salt + salt_len;
  const char* ciphertext = nonce + nonce_len;

  std::uint8_t derived[kL3EnvelopeKeyBytes] = {};
  if (!DeriveKey(key, reinterpret_cast<const std::uint8_t*>(salt), salt_len,
                 iterations, derived)) {
    Wipe(derived, sizeof(derived));
    return fail("l3envelope: key derivation failed");
  }

  EVP_AEAD_CTX* ctx = EVP_AEAD_CTX_new(EVP_aead_aes_256_gcm(), derived,
                                        sizeof(derived), tag_len);
  if (ctx == nullptr) {
    Wipe(derived, sizeof(derived));
    return fail("l3envelope: aead init failed");
  }

  std::string plaintext;
  plaintext.resize(plaintext_len);
  std::size_t opened_len = 0;
  // A wrong key, a flipped ciphertext byte or a tampered header all make
  // EVP_AEAD_CTX_open fail, and the plaintext buffer is zeroed by then, so
  // nothing partial escapes.
  const bool opened = EVP_AEAD_CTX_open(
      ctx, reinterpret_cast<std::uint8_t*>(&plaintext[0]), &opened_len,
      plaintext_len, reinterpret_cast<const std::uint8_t*>(nonce), nonce_len,
      reinterpret_cast<const std::uint8_t*>(ciphertext), ciphertext_len,
      reinterpret_cast<const std::uint8_t*>(envelope.data()), header_len) == 1;
  EVP_AEAD_CTX_free(ctx);
  Wipe(derived, sizeof(derived));
  if (!opened || opened_len != plaintext_len) {
    Wipe(plaintext.data(), plaintext.size());
    return fail("l3envelope: authentication failed");
  }

  *out = std::move(plaintext);
  if (error) error->clear();
  return true;
}

bool IsL3Envelope(std::string_view envelope) {
  return envelope.size() >= kL3EnvelopeHeaderBytes &&
         std::memcmp(envelope.data(), kMagic, sizeof(kMagic)) == 0 &&
         static_cast<std::uint8_t>(envelope[4]) == kFormatVersion;
}

}  // namespace lengsuo::fp
