// l3_envelope_session_test - the seam between the pipe's session key and the
// L3E1 document envelope (task N).
//
// Two envelopes live in this tree and they are NOT the same thing:
//
//   native/l3_envelope.{h,cc}          the L3E1 envelope: an L3W1 document on
//                                      disk, sealed under a master key the
//                                      caller supplies. Little-endian,
//                                      PBKDF2, 76 bytes of overhead.
//   native/kernel_pipe_envelope.{h,cc} the KeyEnvelope of the runner <-> kernel
//                                      pipe: the session key handed down at
//                                      handshake time. Big-endian, no KDF, an
//                                      85-byte payload.
//
// This file's subject is the seam between them, and it is the only one: the
// pipe's session key becomes the L3E1 master key, unchanged.
//
// The tests below pin the three properties that matter:
//   * the key comes from the caller and from nowhere else;
//   * no key means "unavailable", never "a weak key";
//   * the seam does not weaken the authentication the envelope already had.
#include "l3_envelope_session.h"

#include <gtest/gtest.h>

#include <string>

#include "kernel_pipe_keys.h"

namespace lengsuo::fp {
namespace {

// The session key length the pipe contract pins (contract section 1): the
// runner sends 32 bytes of session key inside the KeyEnvelope. The L3E1
// master key is exactly that many bytes, so the two agree by construction
// rather than by coincidence.
//
// The check is against the pipe's own constant, not against a number
// written here: if the pipe ever changes the length, this file fails to
// compile instead of silently padding or truncating.
static_assert(kKernelPipeSessionKeyBytes == kL3EnvelopeSessionKeyBytes,
              "the pipe's session key and the envelope's master key must "
              "have the same length");
static_assert(kL3EnvelopeSessionKeyBytes == kL3EnvelopeKeyBytes,
              "the seam's key length and the envelope's key length must "
              "have the same length");

constexpr std::size_t kSessionKeyBytes = kKernelPipeSessionKeyBytes;

// A session key. It is a test fixture, not a secret.
std::string SessionKey(char fill) {
  return std::string(kSessionKeyBytes, fill);
}

// An L3W1 document: the body plus its sign line, the same shape
// l3_envelope_test.cc seals.
std::string ReferenceDocument() {
  return "L3W1\n"
         "pins 1\n"
         "https://mms.pinduoduo.com|local|finger-key_0.1.5|"
         "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5\n"
         "sign 0123456789abcdef0123456789abcdef_v1_t1700000000\n";
}

// --- the mapping -----------------------------------------------------------

TEST(L3EnvelopeSessionTest, TheSessionKeyLengthIsThePipeContractLength) {
  // The pipe's session key (contract section 1) and the L3E1 master key
  // must be the same size, otherwise the seam could not hand one to the
  // other without padding or truncating. The static_asserts above make the
  // compile fail if the two ever drift apart; this test is the runtime
  // half of the same statement.
  EXPECT_EQ(kSessionKeyBytes, kL3EnvelopeSessionKeyBytes);
  EXPECT_EQ(kL3EnvelopeSessionKeyBytes, kL3EnvelopeKeyBytes);
}

TEST(L3EnvelopeSessionTest, ASessionKeyBecomesAValidEnvelopeKey) {
  L3EnvelopeKey key;
  std::string error = "unset";
  ASSERT_TRUE(MakeL3EnvelopeKey(SessionKey('S'), &key, &error)) << error;
  EXPECT_TRUE(error.empty());
  EXPECT_TRUE(key.valid());
  EXPECT_EQ(key.size(), kL3EnvelopeSessionKeyBytes);
  EXPECT_EQ(key.bytes(), SessionKey('S'));
}

TEST(L3EnvelopeSessionTest, ASessionKeyOfTheWrongLengthIsRejected) {
  // Padding a short key up would weaken the KDF; truncating a long one
  // would drop entropy the sender believed was protecting the document.
  // Both are refused.
  for (const std::size_t size : {std::size_t{0}, std::size_t{16},
                                 std::size_t{31}, std::size_t{33},
                                 std::size_t{64}}) {
    L3EnvelopeKey key;
    std::string error;
    EXPECT_FALSE(MakeL3EnvelopeKey(std::string(size, 'S'), &key, &error))
        << "a session key of " << size << " bytes was accepted";
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(key.valid());
  }
}

TEST(L3EnvelopeSessionTest, ARejectedSessionKeyLeavesTheOutputUntouched) {
  L3EnvelopeKey key;
  std::string error;
  ASSERT_TRUE(MakeL3EnvelopeKey(SessionKey('S'), &key, &error)) << error;
  const std::string before = key.bytes();

  EXPECT_FALSE(MakeL3EnvelopeKey("too short", &key, &error));
  EXPECT_EQ(key.bytes(), before);
  EXPECT_TRUE(key.valid());
}

TEST(L3EnvelopeSessionTest, NoSessionKeyMeansUnavailableNotAWeakKey) {
  // There is no default key and no derivation from a constant: a caller
  // without a session key simply has no envelope.
  L3EnvelopeKey key;
  std::string error;
  EXPECT_FALSE(MakeL3EnvelopeKey("", &key, &error));
  EXPECT_FALSE(key.valid());
  EXPECT_EQ(key.size(), 0u);
  EXPECT_TRUE(key.bytes().empty());
}

TEST(L3EnvelopeSessionTest, DifferentSessionKeysGiveDifferentEnvelopeKeys) {
  L3EnvelopeKey first;
  L3EnvelopeKey second;
  std::string error;
  ASSERT_TRUE(MakeL3EnvelopeKey(SessionKey('A'), &first, &error)) << error;
  ASSERT_TRUE(MakeL3EnvelopeKey(SessionKey('B'), &second, &error)) << error;
  EXPECT_NE(first.bytes(), second.bytes());
}

// --- seal and open through the seam ---------------------------------------

TEST(L3EnvelopeSessionTest, ADocumentRoundTripsThroughTheSessionKey) {
  const std::string document = ReferenceDocument();
  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Document(document, SessionKey('S'), 1000, &envelope,
                             &error))
      << error;
  EXPECT_TRUE(error.empty());
  EXPECT_TRUE(IsL3Envelope(envelope));

  std::string opened;
  ASSERT_TRUE(OpenL3Document(envelope, SessionKey('S'), &opened, &error))
      << error;
  EXPECT_EQ(opened, document);
}

TEST(L3EnvelopeSessionTest, TheWrongSessionKeyCannotOpenTheDocument) {
  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Document(ReferenceDocument(), SessionKey('S'), 1000,
                             &envelope, &error))
      << error;

  std::string opened = "sentinel";
  EXPECT_FALSE(OpenL3Document(envelope, SessionKey('T'), &opened, &error));
  EXPECT_EQ(opened, "sentinel");
}

TEST(L3EnvelopeSessionTest, TheSessionKeyIsNotInTheEnvelope) {
  // The session key must not survive in the envelope as readable bytes.
  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Document(ReferenceDocument(), SessionKey('S'), 1000,
                             &envelope, &error))
      << error;
  EXPECT_EQ(envelope.find(SessionKey('S')), std::string::npos);
  EXPECT_EQ(envelope.find("L3W1"), std::string::npos);
  EXPECT_EQ(envelope.find("finger-key_0.1.5"), std::string::npos);
}

TEST(L3EnvelopeSessionTest, SealingWithoutASessionKeyFails) {
  std::string envelope = "sentinel";
  std::string error;
  EXPECT_FALSE(SealL3Document(ReferenceDocument(), "", 1000, &envelope,
                              &error));
  EXPECT_EQ(envelope, "sentinel");
  EXPECT_FALSE(error.empty());
}

TEST(L3EnvelopeSessionTest, OpeningWithoutASessionKeyFails) {
  // An envelope sealed under one session key must not open under no key at
  // all: there is no "empty key" to fall back to.
  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Document(ReferenceDocument(), SessionKey('S'), 1000,
                             &envelope, &error))
      << error;

  std::string opened = "sentinel";
  EXPECT_FALSE(OpenL3Document(envelope, "", &opened, &error));
  EXPECT_EQ(opened, "sentinel");
}

TEST(L3EnvelopeSessionTest, TheSeamDoesNotWeakenAuthentication) {
  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Document(ReferenceDocument(), SessionKey('S'), 1000,
                             &envelope, &error))
      << error;

  // One flipped byte in the ciphertext body still fails to open.
  std::string tampered = envelope;
  tampered[kL3EnvelopeHeaderBytes + kL3EnvelopeSaltBytes +
            kL3EnvelopeNonceBytes + 3] ^= 0x01;

  std::string opened = "sentinel";
  EXPECT_FALSE(OpenL3Document(tampered, SessionKey('S'), &opened, &error));
  EXPECT_EQ(opened, "sentinel");
}

}  // namespace
}  // namespace lengsuo::fp
