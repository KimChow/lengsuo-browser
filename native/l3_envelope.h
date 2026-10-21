// l3_envelope - the encrypted envelope around an L3W1 document (task N).
//
// Why this exists: the managed L3 document used to travel as plaintext
// base64 on the command line. Base64 is an encoding, not encryption, so
// anyone who can read the process command line recovers every pinned
// fingerprint value. The 对标端's own measurement agrees: its `init_bak.json`
// payload is 724,160 B, i.e. 16-byte aligned, which is an AES block size.
//
// This module is the "中期方案" (mid-term plan) half of the fix:
//
//   * the on-disk managed file (`--lengsuo-l3-wire=<path>`) holds an
//     *envelope*, not a plaintext L3W1 document;
//   * the command line carries only a path, never the document.
//
// The switch that used to carry the document as plaintext base64 on the
// command line is gone: the document reaches the renderer over Mojo
// (lengsuo.mojom.L3Provisioning) instead. Nothing on this path encodes the
// document as base64 any more.
//
// Envelope layout (all integers little-endian, all offsets byte counts):
//
//   offset  size  field
//   0       4     magic "L3E1"
//   4       1     format version (1)
//   5       1     KDF id (1 = PBKDF2-HMAC-SHA256)
//   6       1     cipher id (1 = AES-256-GCM)
//   7       1     reserved, must be 0
//   8       4     header length (bytes, from offset 0 to the ciphertext)
//   12      4     KDF iteration count
//   16      4     salt length
//   20      4     nonce length
//   24      4     tag length
//   28      4     ciphertext length
//   32      ...   salt bytes
//   ...     ...   nonce bytes
//   ...     ...   ciphertext bytes
//   ...     16    GCM authentication tag
//
// The header is authenticated as additional data, so flipping a byte in the
// iteration count, the salt or the lengths is a decryption failure and not a
// silent re-interpretation.
//
// Key handling: the master key is NEVER hardcoded and never derived from a
// constant in this file.  It is supplied by the caller (see
// L3EnvelopeKey).  A caller that has no secure key source must fail
// closed rather than fall back to a weak key - see the task-N report.
//
// Every failure mode is fail-closed: a wrong key, a tampered ciphertext, a
// truncated envelope, an empty payload or an oversized payload all produce
// failure with no partial plaintext escaping.
#ifndef LENGSUO_NATIVE_L3_ENVELOPE_H_
#define LENGSUO_NATIVE_L3_ENVELOPE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lengsuo::fp {

// The managed L3 document is bounded by kMaxL3WireBytes (64 KiB). An
// envelope adds a bounded header, a salt, a nonce and a tag, so the envelope
// bound is the document bound plus that overhead.
inline constexpr std::size_t kMaxL3EnvelopeBytes = 64 * 1024 + 1024;

inline constexpr std::size_t kL3EnvelopeSaltBytes = 16;
inline constexpr std::size_t kL3EnvelopeNonceBytes = 12;
inline constexpr std::size_t kL3EnvelopeTagBytes = 16;
inline constexpr std::size_t kL3EnvelopeKeyBytes = 32;
inline constexpr std::size_t kL3EnvelopeHeaderBytes = 32;

// Bytes an envelope adds around the plaintext: the 32-byte header, the
// 16-byte salt, the 12-byte nonce and the 16-byte tag.
inline constexpr std::size_t kL3EnvelopeOverheadBytes =
    kL3EnvelopeHeaderBytes + kL3EnvelopeSaltBytes + kL3EnvelopeNonceBytes +
    kL3EnvelopeTagBytes;

// The largest plaintext that still fits inside kMaxL3EnvelopeBytes.
inline constexpr std::size_t kMaxL3EnvelopePlaintextBytes =
    kMaxL3EnvelopeBytes - kL3EnvelopeOverheadBytes;

// PBKDF2-HMAC-SHA256 iteration count used when the caller does not pick one.
// Chosen so a single derivation costs ~20 ms on the build host: slow enough
// to make offline guessing of a weak master key expensive, fast enough that
// startup is not noticeable.  BoringSSL does ~270k iterations/second here.
inline constexpr std::uint32_t kDefaultL3EnvelopeIterations = 100000;

// The master key for the envelope.  `bytes` must be exactly
// kL3EnvelopeKeyBytes long; a shorter or longer key is rejected rather
// than padded or truncated, because silently accepting a weak key is how a
// "protected" document turns out to be readable.
//
// The key is held by value and wiped on destruction: the plaintext
// fingerprint values this protects are exactly the ones that must not
// linger in memory longer than needed.
class L3EnvelopeKey {
 public:
  L3EnvelopeKey() = default;
  explicit L3EnvelopeKey(const std::string& bytes);
  ~L3EnvelopeKey();
  L3EnvelopeKey(const L3EnvelopeKey&) = delete;
  L3EnvelopeKey& operator=(const L3EnvelopeKey&) = delete;
  L3EnvelopeKey(L3EnvelopeKey&& other) noexcept;
  L3EnvelopeKey& operator=(L3EnvelopeKey&& other) noexcept;

  // True when a key of the right length is held.
  bool valid() const { return valid_; }
  std::size_t size() const { return bytes_.size(); }
  const std::string& bytes() const { return bytes_; }

 private:
  std::string bytes_;
  bool valid_ = false;
};

// Seals `plaintext` (the exact bytes of an L3W1 document) into an
// envelope.  `key` must be valid; an invalid key fails rather than falling
// back to a default.  `iterations` of 0 means kDefaultL3EnvelopeIterations.
//
// Returns false and sets `*error` (when non-null) on failure.  On success
// `*out` holds a complete envelope.
bool SealL3Envelope(std::string_view plaintext,
                    const L3EnvelopeKey& key,
                    std::uint32_t iterations,
                    std::string* out,
                    std::string* error);

// Opens an envelope and returns the plaintext bytes.
//
// `*out` is left untouched on every failure, so a wrong key or a tampered
// envelope can never yield partial plaintext.  Returns false and sets
// `*error` (when non-null) on failure.
bool OpenL3Envelope(std::string_view envelope,
                    const L3EnvelopeKey& key,
                    std::string* out,
                    std::string* error);

// True when `envelope` starts with the L3E1 magic and the format version.
// This is a cheap structural check, not a validation: use OpenL3Envelope
// for the real thing.
bool IsL3Envelope(std::string_view envelope);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_L3_ENVELOPE_H_
