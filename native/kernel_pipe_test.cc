// kernel_pipe_test - the handshake and the config bundle of the kernel side
// of the runner <-> kernel pipe (task Q).
//
// The fixed test vectors are in kernel_pipe_test_util.h; they are copied
// byte for byte out of docs/kernel-wire-contract.md sections 2 and 3.1.
#include "kernel_pipe.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "kernel_pipe_test_util.h"

namespace lengsuo::fp {
namespace {

using test::BytesToHex;
using test::HexToBytes;
using test::kEnvelopeContainerId;
using test::kEnvelopeExpires;
using test::kEnvelopeGcmNonce;
using test::kEnvelopeHelloNonce;
using test::kMachineCode;
using test::kMachineKeyHex;
using test::kSessionKeyHex;

// Seals the contract's session key into an envelope and returns its frame.
// The caller must check `ok`: a failed seal leaves the frame empty.
std::string EnvelopeFrame(std::string* machine_key, bool* ok) {
  *ok = DeriveMachineKey(kMachineCode, machine_key);
  if (!*ok) {
    return std::string();
  }
  std::string payload;
  std::string error;
  *ok = BuildKeyEnvelope(
      HexToBytes(kSessionKeyHex), kEnvelopeContainerId, kEnvelopeExpires,
      *machine_key, HexToBytes(kEnvelopeGcmNonce),
      HexToBytes(kEnvelopeHelloNonce), &payload, &error);
  if (!*ok) {
    ADD_FAILURE() << error;
    return std::string();
  }
  std::string frame;
  *ok = EncodeFrame(FrameType::kKeyEnvelope, payload, &frame);
  return frame;
}

// --- handshake -----------------------------------------------------------

TEST(KernelPipeHandshake, TheHappyPathReachesReady) {
  std::string machine_key;
  bool ok = false;
  const std::string envelope_frame = EnvelopeFrame(&machine_key, &ok);
  ASSERT_TRUE(ok);

  KernelPipeHandshake handshake;
  EXPECT_EQ(handshake.state(), KernelPipeState::kWaitHello);
  std::string hello;
  ASSERT_TRUE(handshake.SendHello(HexToBytes(kEnvelopeHelloNonce), &hello));
  EXPECT_EQ(handshake.state(), KernelPipeState::kHandshake);

  // The Hello frame carries {version, nonce} as JSON.
  Frame hello_frame;
  ASSERT_TRUE(DecodeFrame(hello, &hello_frame));
  EXPECT_EQ(hello_frame.type, FrameType::kHello);
  JsonObject hello_json;
  ASSERT_TRUE(JsonObject::Parse(hello_frame.payload, &hello_json));
  std::int64_t version = 0;
  ASSERT_TRUE(hello_json.GetInt64("version", &version));
  EXPECT_EQ(version, static_cast<std::int64_t>(kKernelPipeProtocolVersion));
  std::string nonce_hex;
  ASSERT_TRUE(hello_json.GetString("nonce", &nonce_hex));
  EXPECT_EQ(nonce_hex, "0102030405060708");

  // The envelope arrives in a frame whose header is part of the AAD.
  Frame decoded;
  ASSERT_TRUE(DecodeFrame(envelope_frame, &decoded));
  std::string error;
  ASSERT_TRUE(handshake.AcceptKeyEnvelope(decoded, machine_key,
                                          static_cast<std::int64_t>(
                                              kEnvelopeExpires - 1),
                                          &error))
      << error;
  EXPECT_EQ(handshake.state(), KernelPipeState::kReady);
  EXPECT_TRUE(handshake.ready());
  EXPECT_EQ(BytesToHex(handshake.session_key()), std::string(kSessionKeyHex));
  EXPECT_EQ(handshake.container_id(), std::string(kEnvelopeContainerId));
  EXPECT_EQ(handshake.expires_unix(), kEnvelopeExpires);
  EXPECT_FALSE(handshake.Expired(static_cast<std::int64_t>(kEnvelopeExpires)));
}

TEST(KernelPipeHandshake, AnEnvelopeThatDoesNotAuthenticateFailsClosed) {
  std::string machine_key;
  bool ok = false;
  const std::string envelope_frame = EnvelopeFrame(&machine_key, &ok);
  ASSERT_TRUE(ok);
  Frame decoded;
  ASSERT_TRUE(DecodeFrame(envelope_frame, &decoded));

  KernelPipeHandshake handshake;
  std::string hello;
  ASSERT_TRUE(handshake.SendHello(HexToBytes(kEnvelopeHelloNonce), &hello));

  // The right envelope, but the wrong machine key: the handshake dies.
  std::string wrong_key = machine_key;
  wrong_key[0] = static_cast<char>(wrong_key[0] ^ '\1');
  std::string error;
  EXPECT_FALSE(handshake.AcceptKeyEnvelope(decoded, wrong_key, 0, &error));
  EXPECT_EQ(handshake.state(), KernelPipeState::kFailed);
  EXPECT_TRUE(handshake.failed());
  EXPECT_TRUE(handshake.session_key().empty());
  // The error must not carry the payload.
  EXPECT_EQ(error.find("ct-01"), std::string::npos);
  EXPECT_EQ(error.find(BytesToHex(HexToBytes(kSessionKeyHex))),
            std::string::npos);
}

TEST(KernelPipeHandshake, AnExpiredEnvelopeFailsClosed) {
  std::string machine_key;
  bool ok = false;
  const std::string envelope_frame = EnvelopeFrame(&machine_key, &ok);
  ASSERT_TRUE(ok);
  Frame decoded;
  ASSERT_TRUE(DecodeFrame(envelope_frame, &decoded));

  KernelPipeHandshake handshake;
  std::string hello;
  ASSERT_TRUE(handshake.SendHello(HexToBytes(kEnvelopeHelloNonce), &hello));
  // One second past the expiry.
  std::string error;
  EXPECT_FALSE(handshake.AcceptKeyEnvelope(
      decoded, machine_key,
      static_cast<std::int64_t>(kEnvelopeExpires + 1), &error));
  EXPECT_EQ(handshake.state(), KernelPipeState::kFailed);
  EXPECT_TRUE(handshake.session_key().empty());
}

TEST(KernelPipeHandshake, AnEnvelopeForAnotherHandshakeFailsClosed) {
  // The envelope was sealed against one hello nonce; the handshake used a
  // different one. The AAD must reject it.
  std::string machine_key;
  bool ok = false;
  const std::string envelope_frame = EnvelopeFrame(&machine_key, &ok);
  ASSERT_TRUE(ok);
  Frame decoded;
  ASSERT_TRUE(DecodeFrame(envelope_frame, &decoded));

  KernelPipeHandshake handshake;
  std::string hello;
  ASSERT_TRUE(handshake.SendHello(
      HexToBytes("11 22 33 44 55 66 77 88"), &hello));
  std::string error;
  EXPECT_FALSE(handshake.AcceptKeyEnvelope(decoded, machine_key, 0, &error));
  EXPECT_EQ(handshake.state(), KernelPipeState::kFailed);
}

TEST(KernelPipeHandshake, AWrongNonceLengthFails) {
  KernelPipeHandshake handshake;
  std::string hello;
  EXPECT_FALSE(handshake.SendHello("short", &hello));
  EXPECT_EQ(handshake.state(), KernelPipeState::kFailed);
  EXPECT_FALSE(handshake.SendHello(std::string(9, '\1'), &hello));
  EXPECT_EQ(handshake.state(), KernelPipeState::kFailed);
}

TEST(KernelPipeHandshake, AnEnvelopeBeforeHelloFails) {
  std::string machine_key;
  ASSERT_TRUE(DeriveMachineKey(kMachineCode, &machine_key));
  Frame frame;
  frame.type = FrameType::kKeyEnvelope;
  frame.header = HexToBytes("00 00 00 55 02 00 00 00");
  KernelPipeHandshake handshake;
  std::string error;
  EXPECT_FALSE(handshake.AcceptKeyEnvelope(frame, machine_key, 0, &error));
  EXPECT_TRUE(handshake.failed());
}

TEST(KernelPipeHandshake, AWrongFrameTypeFails) {
  std::string machine_key;
  ASSERT_TRUE(DeriveMachineKey(kMachineCode, &machine_key));
  KernelPipeHandshake handshake;
  std::string hello;
  ASSERT_TRUE(handshake.SendHello(HexToBytes(kEnvelopeHelloNonce), &hello));
  Frame frame;
  frame.type = FrameType::kConfigBundle;
  std::string error;
  EXPECT_FALSE(handshake.AcceptKeyEnvelope(frame, machine_key, 0, &error));
  EXPECT_EQ(handshake.state(), KernelPipeState::kFailed);
}

TEST(KernelPipeHandshake, ResetWipesTheSessionKey) {
  std::string machine_key;
  bool ok = false;
  const std::string envelope_frame = EnvelopeFrame(&machine_key, &ok);
  ASSERT_TRUE(ok);
  Frame decoded;
  ASSERT_TRUE(DecodeFrame(envelope_frame, &decoded));

  KernelPipeHandshake handshake;
  std::string hello;
  ASSERT_TRUE(handshake.SendHello(HexToBytes(kEnvelopeHelloNonce), &hello));
  std::string error;
  ASSERT_TRUE(handshake.AcceptKeyEnvelope(decoded, machine_key, 0, &error));
  ASSERT_FALSE(handshake.session_key().empty());
  handshake.Reset();
  EXPECT_TRUE(handshake.session_key().empty());
  EXPECT_TRUE(handshake.container_id().empty());
  EXPECT_EQ(handshake.state(), KernelPipeState::kWaitHello);
}

// --- config bundle -------------------------------------------------------

TEST(KernelPipeConfig, FreezeHoldsTheBundle) {
  KernelPipeConfig config;
  EXPECT_FALSE(config.frozen());
  std::string error;
  ASSERT_TRUE(config.Freeze("{\"profile\":\"p1\"}", &error)) << error;
  EXPECT_TRUE(config.frozen());
  EXPECT_EQ(config.json(), "{\"profile\":\"p1\"}");
}

TEST(KernelPipeConfig, FreezeIsOneShot) {
  KernelPipeConfig config;
  std::string error;
  ASSERT_TRUE(config.Freeze("{\"profile\":\"p1\"}", &error));
  // A second freeze is refused: the configuration cannot change after the
  // browser has started.
  EXPECT_FALSE(config.Freeze("{\"profile\":\"p2\"}", &error));
  EXPECT_EQ(config.json(), "{\"profile\":\"p1\"}");
}

TEST(KernelPipeConfig, AMalformedBundleIsRefused) {
  KernelPipeConfig config;
  std::string error;
  EXPECT_FALSE(config.Freeze("not json", &error));
  EXPECT_FALSE(config.frozen());
  EXPECT_TRUE(config.json().empty());
  // The failure is not a silent "no configuration": it is an error.
  EXPECT_FALSE(error.empty());
}

}  // namespace
}  // namespace lengsuo::fp
