// kernel_pipe_envelope_test - the key layer of the kernel side of the
// runner <-> kernel pipe (task Q).
//
// The KeyEnvelope vectors are copied byte for byte out of
// docs/kernel-wire-contract.md sections 2 and 3.1. The client repo (task P)
// hardcodes the same bytes; that is the cross-repo agreement.
//
// The roundtrip alone is not the cross-repo check: this file opens the
// contract's own bytes with the contract's own key, and separately rebuilds
// them and compares byte for byte. A pair of implementations that are wrong
// in the same way would still pass a roundtrip, and would still fail these.
#include "kernel_pipe_envelope.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "kernel_pipe_test_util.h"

namespace lengsuo::fp {
namespace {

using test::BytesToHex;
using test::HexToBytes;
using test::kEnvelopeContainerId;
using test::kEnvelopeCiphertext;
using test::kEnvelopeExpires;
using test::kEnvelopeFrameHeader;
using test::kEnvelopeGcmNonce;
using test::kEnvelopeHelloNonce;
using test::kEnvelopePayload;
using test::kEnvelopeTag;
using test::kMachineCode;
using test::kMachineKeyHex;
using test::kSessionKeyHex;

// The AAD the contract pins: the envelope frame's own header (whose length
// field is the payload length) followed by the hello nonce.
std::string ContractAad() {
  return HexToBytes(kEnvelopeFrameHeader) + HexToBytes(kEnvelopeHelloNonce);
}

TEST(KernelPipeEnvelope, TheContractVectorOpens) {
  const std::string payload = HexToBytes(kEnvelopePayload);
  // The payload's length is what the contract's AAD names: 85 bytes, so the
  // frame header's length field is 0x00000055.
  ASSERT_EQ(payload.size(), 85u);
  const std::string machine_key = HexToBytes(kMachineKeyHex);
  KeyEnvelope envelope;
  std::string error;
  ASSERT_TRUE(
      OpenKeyEnvelope(payload, machine_key, ContractAad(), &envelope, &error))
      << error;
  EXPECT_EQ(BytesToHex(envelope.session_key), std::string(kSessionKeyHex));
  EXPECT_EQ(envelope.container_id, std::string(kEnvelopeContainerId));
  EXPECT_EQ(envelope.expires_unix, kEnvelopeExpires);
}

TEST(KernelPipeEnvelope, TheContractVectorHasThePinnedFieldLayout) {
  // The payload's six fields, in the contract's order. A layout that drifts
  // here fails the open above anyway, but this names the drift.
  const std::string payload = HexToBytes(kEnvelopePayload);
  ASSERT_EQ(payload.size(), 85u);
  // nonce_len = 12
  EXPECT_EQ(payload[0], '\0');
  EXPECT_EQ(payload[1], '\0');
  EXPECT_EQ(payload[2], '\0');
  EXPECT_EQ(payload[3], '\x0c');
  // the nonce itself
  EXPECT_EQ(BytesToHex(payload.substr(4, 12)),
            BytesToHex(HexToBytes(kEnvelopeGcmNonce)));
  // ciphertext_len = 45
  EXPECT_EQ(BytesToHex(payload.substr(16, 4)), "0000002d");
  // the ciphertext
  EXPECT_EQ(BytesToHex(payload.substr(20, 45)),
            BytesToHex(HexToBytes(kEnvelopeCiphertext)));
  // tag_len = 16
  EXPECT_EQ(BytesToHex(payload.substr(65, 4)), "00000010");
  // the tag
  EXPECT_EQ(BytesToHex(payload.substr(69, 16)),
            BytesToHex(HexToBytes(kEnvelopeTag)));
}

TEST(KernelPipeEnvelope, TheContractVectorIsReproducible) {
  // Building the envelope from the same inputs must reproduce the contract's
  // bytes exactly. This is the check that the reader and the writer agree.
  std::string machine_key;
  ASSERT_TRUE(DeriveMachineKey(kMachineCode, &machine_key));
  std::string payload;
  std::string error;
  ASSERT_TRUE(BuildKeyEnvelope(
      HexToBytes(kSessionKeyHex), kEnvelopeContainerId, kEnvelopeExpires,
      machine_key, HexToBytes(kEnvelopeGcmNonce),
      HexToBytes(kEnvelopeHelloNonce), &payload, &error))
      << error;
  EXPECT_EQ(BytesToHex(payload), BytesToHex(HexToBytes(kEnvelopePayload)));
  EXPECT_EQ(payload.size(), 85u);
}

TEST(KernelPipeEnvelope, AWrongKeyFails) {
  const std::string payload = HexToBytes(kEnvelopePayload);
  KeyEnvelope envelope;
  std::string error;
  // One bit flipped in the key.
  std::string wrong_key = HexToBytes(kMachineKeyHex);
  wrong_key[0] = static_cast<char>(wrong_key[0] ^ '\1');
  EXPECT_FALSE(
      OpenKeyEnvelope(payload, wrong_key, ContractAad(), &envelope, &error));
  // The failure must not leak the plaintext.
  EXPECT_TRUE(envelope.session_key.empty());
  EXPECT_TRUE(envelope.container_id.empty());
}

TEST(KernelPipeEnvelope, ATamperedCiphertextFails) {
  const std::string payload = HexToBytes(kEnvelopePayload);
  const std::string machine_key = HexToBytes(kMachineKeyHex);
  // Flip one bit of the ciphertext.
  std::string tampered = payload;
  const std::size_t ciphertext_start = 4 + 12 + 4;
  tampered[ciphertext_start] =
      static_cast<char>(tampered[ciphertext_start] ^ '\1');
  KeyEnvelope envelope;
  std::string error;
  EXPECT_FALSE(OpenKeyEnvelope(tampered, machine_key, ContractAad(), &envelope,
                               &error));
  EXPECT_TRUE(envelope.session_key.empty());
}

TEST(KernelPipeEnvelope, ATamperedTagFails) {
  const std::string payload = HexToBytes(kEnvelopePayload);
  const std::string machine_key = HexToBytes(kMachineKeyHex);
  std::string tampered = payload;
  tampered[tampered.size() - 1] =
      static_cast<char>(tampered[tampered.size() - 1] ^ '\1');
  KeyEnvelope envelope;
  std::string error;
  EXPECT_FALSE(OpenKeyEnvelope(tampered, machine_key, ContractAad(), &envelope,
                               &error));
}

TEST(KernelPipeEnvelope, ATruncatedPayloadFails) {
  const std::string payload = HexToBytes(kEnvelopePayload);
  const std::string machine_key = HexToBytes(kMachineKeyHex);
  for (std::size_t cut : {std::size_t{0}, std::size_t{1}, std::size_t{4},
                          std::size_t{16}, std::size_t{40}, std::size_t{84}}) {
    KeyEnvelope envelope;
    std::string error;
    EXPECT_FALSE(OpenKeyEnvelope(payload.substr(0, cut), machine_key,
                                 ContractAad(), &envelope, &error))
        << "cut at " << cut;
  }
}

TEST(KernelPipeEnvelope, TrailingBytesAreRejected) {
  const std::string machine_key = HexToBytes(kMachineKeyHex);
  KeyEnvelope envelope;
  std::string error;

  // A byte past the end of the tag: the payload is longer than its own
  // fields describe, so it must not be accepted.
  const std::string payload = HexToBytes(kEnvelopePayload);
  EXPECT_FALSE(OpenKeyEnvelope(payload + std::string(1, '\0'), machine_key,
                               ContractAad(), &envelope, &error));

  // A byte past the end of the ciphertext, with the length field adjusted to
  // match: the extra byte is then part of the ciphertext, so the tag no
  // longer authenticates.
  std::string stretched = payload;
  stretched[19] = static_cast<char>(0x2e);  // ciphertext_len 45 -> 46
  stretched.insert(stretched.begin() + 65, 1, '\x00');
  EXPECT_FALSE(
      OpenKeyEnvelope(stretched, machine_key, ContractAad(), &envelope, &error));
  EXPECT_TRUE(envelope.session_key.empty());
}

TEST(KernelPipeEnvelope, TheWrongAadFails) {
  // The AAD binds the envelope to the handshake that asked for it. An
  // envelope replayed into another handshake must fail to authenticate.
  const std::string payload = HexToBytes(kEnvelopePayload);
  const std::string machine_key = HexToBytes(kMachineKeyHex);
  const std::string other_hello_nonce = HexToBytes("11 22 33 44 55 66 77 88");
  const std::string wrong_aad = HexToBytes(kEnvelopeFrameHeader) +
                                other_hello_nonce;
  KeyEnvelope envelope;
  std::string error;
  EXPECT_FALSE(
      OpenKeyEnvelope(payload, machine_key, wrong_aad, &envelope, &error));
  // And so must a frame-header-only AAD: that is the replay hole the
  // contract closes by adding the hello nonce.
  const std::string header_only = HexToBytes(kEnvelopeFrameHeader);
  EXPECT_FALSE(
      OpenKeyEnvelope(payload, machine_key, header_only, &envelope, &error));
}

TEST(KernelPipeEnvelope, AWrongSizedAadFails) {
  const std::string payload = HexToBytes(kEnvelopePayload);
  const std::string machine_key = HexToBytes(kMachineKeyHex);
  KeyEnvelope envelope;
  std::string error;
  EXPECT_FALSE(
      OpenKeyEnvelope(payload, machine_key, "", &envelope, &error));
  EXPECT_FALSE(OpenKeyEnvelope(payload, machine_key, "short", &envelope,
                               &error));
  EXPECT_FALSE(OpenKeyEnvelope(payload, machine_key,
                               std::string(17, '\0'), &envelope, &error));
}

TEST(KernelPipeEnvelope, ANonTwelveByteNonceIsRejected) {
  const std::string payload = HexToBytes(kEnvelopePayload);
  const std::string machine_key = HexToBytes(kMachineKeyHex);
  // Rewrite the nonce length to 8: the contract pins 12.
  std::string broken = payload;
  broken[3] = '\x08';
  KeyEnvelope envelope;
  std::string error;
  EXPECT_FALSE(
      OpenKeyEnvelope(broken, machine_key, ContractAad(), &envelope, &error));
}

}  // namespace
}  // namespace lengsuo::fp
