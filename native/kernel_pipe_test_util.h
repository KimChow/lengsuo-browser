// kernel_pipe_test_util - the shared helpers and fixed vectors of the pipe
// tests (task Q).
//
// The fixed test vectors below are copied byte for byte out of
// docs/kernel-wire-contract.md (sections 2 and 3.1). They are the cross-repo
// agreement: the client repo (task P) hardcodes the same bytes, and the two
// implementations are compared by diffing their hex output.
//
// They are hardcoded constants on purpose. A test that called a script to
// produce the expected values would agree with a broken implementation, and
// "both sides are wrong" would look like "both sides match".
//
// This is a header rather than a .cc because each test file links on its own:
// the frame tests must not have to link the crypto.
#ifndef LENGSUO_NATIVE_KERNEL_PIPE_TEST_UTIL_H_
#define LENGSUO_NATIVE_KERNEL_PIPE_TEST_UTIL_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "gtest/gtest.h"

namespace lengsuo::fp::test {

// --- the contract's fixed vectors ---------------------------------------
//
// Frame headers for the legal frames (contract section 2). The payload of
// every legal frame is the fill byte 0x5A repeated to the declared length.

inline constexpr std::uint8_t kFill = 0x5A;

inline constexpr char kLegalHeaderV0[] = "00 00 00 00 03 00 00 00";
inline constexpr char kLegalHeaderV1[] = "00 00 00 01 03 00 00 00";
inline constexpr char kLegalHeaderV3[] = "00 00 00 03 03 00 00 00";
inline constexpr char kLegalHeaderV255[] = "00 00 00 ff 03 00 00 00";
inline constexpr char kLegalHeaderV65535[] = "00 00 ff ff 03 00 00 00";
inline constexpr char kLegalHeaderVMax[] = "00 80 00 00 03 00 00 00";

inline constexpr char kIllegalLenOver[] = "00 80 00 01 03 00 00 00";
inline constexpr char kIllegalReserved[] = "00 00 00 01 03 00 00 01 5a";
inline constexpr char kIllegalType[] = "00 00 00 01 07 00 00 00 5a";
inline constexpr char kIllegalTruncHeader[] = "00 00 00 01 03 00 00";
inline constexpr char kIllegalTruncPayload[] = "00 00 00 03 03 00 00 00 5a";

// Key derivation (contract section 2).
inline constexpr char kMachineCode[] =
    "216feaf8875b0e55d32682bef617480d57c842fc3db57018f0715e17cc199033";
inline constexpr char kMachineKeyHex[] =
    "64fa1b15ef6856fb97cfb29aa397500a3003e8c40568c684cb55fa96438c0d7f";
inline constexpr char kSessionKeyHex[] =
    "b102b38300fa770def96bbd421f83c9858c86401d5905c4e6e4fb97900627ff1";

// The KeyEnvelope vector (contract section 3.1).
inline constexpr char kEnvelopeGcmNonce[] = "00 01 02 03 04 05 06 07 08 09 0a 0b";
inline constexpr char kEnvelopeHelloNonce[] = "01 02 03 04 05 06 07 08";
inline constexpr char kEnvelopeContainerId[] = "ct-01";
inline constexpr std::uint64_t kEnvelopeExpires = 1700000000;
inline constexpr char kEnvelopeCiphertext[] =
    "88 3b 15 2f 81 0f c5 d4 "
    "87 0a 13 99 e7 c2 6b 42 "
    "5e 51 20 47 49 37 0b 39 "
    "ee 25 cb 05 d5 f8 59 86 "
    "a4 91 70 c6 c9 b1 bb 32 "
    "72 a4 67 7e dc ";
inline constexpr char kEnvelopeTag[] =
    "c5 2e 71 8c be 62 81 f0 "
    "b8 bb b4 0c 15 37 57 ad ";

// The envelope payload, exactly as the contract's fixed vector describes it:
//   nonce_len(4) | nonce(12) | ciphertext_len(4) | ciphertext(45) |
//   tag_len(4) | tag(16)
// The container id and the expiry are inside the ciphertext's plaintext, not
// in the clear region: the payload length is 40 + the ciphertext length,
// which is the 85 bytes the contract's AAD length field (0x00000055) names.
//
// The contract document's printed payload block used to splice the 8-byte
// expires_unix into the clear region as well, which made the payload 93
// bytes and the AAD's length field wrong. The document was corrected on
// 2026-10-21 (commit "消除第四处矛盾") and now states the six-field layout
// throughout; tests/test_key_envelope_layout.py keeps the field table, the
// byte listing and the AAD length in agreement, so the constant below can
// not silently drift away from the contract again.
inline constexpr char kEnvelopePayload[] =
    "00 00 00 0c 00 01 02 03 "
    "04 05 06 07 08 09 0a 0b "
    "00 00 00 2d 88 3b 15 2f "
    "81 0f c5 d4 87 0a 13 99 "
    "e7 c2 6b 42 5e 51 20 47 "
    "49 37 0b 39 ee 25 cb 05 "
    "d5 f8 59 86 a4 91 70 c6 "
    "c9 b1 bb 32 72 a4 67 7e "
    "dc 00 00 00 10 c5 2e 71 "
    "8c be 62 81 f0 b8 bb b4 "
    "0c 15 37 57 ad ";

// The envelope frame's header, whose length field (0x00000055 = 85) is part
// of the AAD.
inline constexpr char kEnvelopeFrameHeader[] = "00 00 00 55 02 00 00 00";

// Parses a spaced hex string into bytes. Throws on a malformed literal, so
// a typo in a constant fails the test loudly instead of silently comparing
// nothing.
inline std::string HexToBytes(const char* text) {
  std::string out;
  std::uint8_t high = 0;
  bool have_high = false;
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p == ' ') {
      continue;
    }
    std::uint8_t digit = 0;
    if (*p >= '0' && *p <= '9') {
      digit = static_cast<std::uint8_t>(*p - '0');
    } else if (*p >= 'a' && *p <= 'f') {
      digit = static_cast<std::uint8_t>(*p - 'a' + 10);
    } else if (*p >= 'A' && *p <= 'F') {
      digit = static_cast<std::uint8_t>(*p - 'A' + 10);
    } else {
      ADD_FAILURE() << "bad hex literal: " << text;
      return out;
    }
    if (!have_high) {
      high = digit;
      have_high = true;
    } else {
      out.push_back(static_cast<char>((high << 4) | digit));
      have_high = false;
    }
  }
  if (have_high) {
    ADD_FAILURE() << "odd number of hex digits: " << text;
  }
  return out;
}

inline std::string BytesToHex(const std::string& bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (const char c : bytes) {
    const auto byte = static_cast<std::uint8_t>(c);
    out.push_back(kDigits[byte >> 4]);
    out.push_back(kDigits[byte & 0x0F]);
  }
  return out;
}

inline std::string Fill(std::size_t count) {
  return std::string(count, static_cast<char>(kFill));
}

}  // namespace lengsuo::fp::test

#endif  // LENGSUO_NATIVE_KERNEL_PIPE_TEST_UTIL_H_
