// kernel_pipe_frame - the frame layer of the runner <-> kernel pipe (task Q).
//
// docs/kernel-wire-contract.md section 2 is the single source of truth for
// these bytes: [u32 len BE][u8 type][u8 reserved x3][payload]. The client
// repo (task P) implements the mirror image, and the two sides align on the
// fixed test vectors the contract pins - not on a verbal convention.
//
// This layer is the bottom of the stack. It depends on nothing: not on the
// JSON reader, not on the crypto, not on Chromium. The layers above it (the
// JSON reader, the key envelope, the handshake) all sit on top of it, which
// is why the byte helpers live here rather than in each of them.
#ifndef LENGSUO_NATIVE_KERNEL_PIPE_FRAME_H_
#define LENGSUO_NATIVE_KERNEL_PIPE_FRAME_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lengsuo::fp {

// ---------------------------------------------------------------------------
// Byte helpers
//
// Every integer on this wire is big-endian (contract section 5). Writing the
// bytes by hand keeps the encoding independent of the host's byte order and
// independent of any struct packing.
// ---------------------------------------------------------------------------

namespace wire {

// Appends `value` to `out` as four big-endian bytes.
inline void PutU32BE(std::string* out, std::uint32_t value) {
  out->push_back(static_cast<char>((value >> 24) & 0xFF));
  out->push_back(static_cast<char>((value >> 16) & 0xFF));
  out->push_back(static_cast<char>((value >> 8) & 0xFF));
  out->push_back(static_cast<char>(value & 0xFF));
}

// Appends `value` to `out` as eight big-endian bytes.
inline void PutU64BE(std::string* out, std::uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    out->push_back(static_cast<char>((value >> shift) & 0xFF));
  }
}

// Reads the four big-endian bytes at `offset`. The caller must have checked
// the bounds.
inline std::uint32_t ReadU32BE(std::string_view text, std::size_t offset) {
  return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[offset])) <<
          24) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[offset + 1]))
          << 16) |
         (static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[offset + 2]))
          << 8) |
         static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[offset + 3]));
}

// Reads the eight big-endian bytes at `offset`.
inline std::uint64_t ReadU64BE(std::string_view text, std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    value = (value << 8) |
            static_cast<std::uint64_t>(static_cast<std::uint8_t>(text[offset + i]));
  }
  return value;
}

// Appends the lowercase hex of `bytes` to `out`.
//
// The bytes are taken as a string_view so the caller never has to build a
// pointer + length pair, and so the compiler can see the bounds. Lowercase
// is what the contract pins for the Hello nonce and the machine code.
inline void AppendHex(std::string* out, std::string_view bytes) {
  static constexpr std::string_view kDigits = "0123456789abcdef";
  for (const char byte : bytes) {
    const auto value = static_cast<std::uint8_t>(byte);
    out->push_back(kDigits[value >> 4]);
    out->push_back(kDigits[value & 0x0F]);
  }
}

}  // namespace wire

// ---------------------------------------------------------------------------
// Frames (contract section 2)
// ---------------------------------------------------------------------------

// Frame types (contract section 3). The values are the wire values.
enum class FrameType : std::uint8_t {
  kHello = 0x01,
  kKeyEnvelope = 0x02,
  kConfigBundle = 0x03,
  kActionRequest = 0x04,
  kActionResult = 0x05,
  kEvent = 0x81,
  kEventAck = 0x82,
};

// The 8-byte frame header: a big-endian payload length, a type byte and
// three reserved bytes that must be zero.
inline constexpr std::size_t kKernelPipeHeaderBytes = 8;

// The payload length ceiling. A frame that declares more is rejected; the
// decoder never waits for the extra bytes and never truncates.
inline constexpr std::size_t kKernelPipeMaxPayloadBytes = 8 * 1024 * 1024;

// Protocol version carried by the Hello frame.
inline constexpr std::uint32_t kKernelPipeProtocolVersion = 1;

// True when `raw` is a frame type this build knows. Unknown types are
// rejected by the decoder.
bool IsKnownFrameType(std::uint8_t raw);

// A decoded frame. `header` holds the 8 raw header bytes because the
// KeyEnvelope's AAD is built from the frame header (contract section 3.1).
struct Frame {
  FrameType type = FrameType::kHello;
  std::string header;
  std::string payload;
};

// Encodes one frame. The payload must not exceed kKernelPipeMaxPayloadBytes.
// Returns false and leaves `*out` untouched on failure.
bool EncodeFrame(FrameType type,
                 std::string_view payload,
                 std::string* out);

// Decodes the 8-byte header. Returns false when the declared length exceeds
// the ceiling, the reserved bytes are not zero, or the type is unknown.
bool DecodeFrameHeader(std::string_view header,
                       std::uint32_t* payload_len,
                       FrameType* type);

// Decodes a complete frame (header plus payload). Returns false on any
// violation; `*out` is left untouched so no partial plaintext escapes.
bool DecodeFrame(std::string_view frame, Frame* out);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_KERNEL_PIPE_FRAME_H_
