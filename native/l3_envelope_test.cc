// l3_envelope_test - the encrypted envelope around an L3W1 document.
//
// The scenario is the one the task brief describes: a managed L3 document
// (pins / cookie facts / extension whitelist / mirror rules) that used to
// travel as plaintext base64 on the command line, and that must now be
// sealed into an envelope whose only on-disk form is ciphertext.
//
// The reference document is the pinduoduo pin set from the 对标端 cache,
// the same fixture l3_wire_test.cc uses, so a sealed envelope and a parsed
// L3Config describe the same environment.
#include "l3_envelope.h"

#include <gtest/gtest.h>

#include <string>

namespace lengsuo::fp {
namespace {

// A real L3W1 document: the body plus its sign line. The bytes matter -
// the envelope must recover them exactly, not an equivalent document.
std::string ReferenceDocument() {
  const std::string body =
      "L3W1\n"
      "pins 2\n"
      "https://mms.pinduoduo.com|local|finger-key_0.1.5|"
      "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5\n"
      "https://mms.pinduoduo.com|local|finger-uuid1_0.1.5|"
      "9df4fd33-43dc-4432-b6aa-67c80d64545d\n"
      "cookies 1\n"
      ".pinduoduo.com|_bee|7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5\n"
      "ext 1\n"
      "fjchbfkopknlodpfoaeogfbclcpppiog\n"
      "rules 1\n"
      ".pinduoduo.com|_bee|https://mms.pinduoduo.com|finger-key_0.1.5\n";
  // The sign line is part of the document bytes, so it is sealed too.
  return body + "sign 0123456789abcdef0123456789abcdef_v1_t1700000000\n";
}

// A 32-byte master key. It is a test fixture, not a secret: the point of
// the tests is that the key comes from the caller and never from a
// constant baked into the implementation.
L3EnvelopeKey TestKey() {
  return L3EnvelopeKey(std::string(32, 'K'));
}

L3EnvelopeKey OtherKey() {
  return L3EnvelopeKey(std::string(32, 'X'));
}

// A key of the wrong length must be rejected, not padded or truncated.
L3EnvelopeKey ShortKey() {
  return L3EnvelopeKey(std::string(16, 'K'));
}

TEST(L3EnvelopeTest, RoundTripReproducesDocumentByteForByte) {
  const std::string document = ReferenceDocument();
  const L3EnvelopeKey key = TestKey();

  std::string envelope;
  std::string error = "unset";
  ASSERT_TRUE(SealL3Envelope(document, key, 1000, &envelope, &error)) << error;
  EXPECT_TRUE(error.empty());
  EXPECT_TRUE(IsL3Envelope(envelope));

  std::string opened;
  ASSERT_TRUE(OpenL3Envelope(envelope, key, &opened, &error)) << error;
  EXPECT_EQ(opened, document);
}

TEST(L3EnvelopeTest, EnvelopeDoesNotContainThePlaintext) {
  const std::string document = ReferenceDocument();
  const L3EnvelopeKey key = TestKey();

  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Envelope(document, key, 1000, &envelope, &error)) << error;

  // The pinned value must not survive in the envelope as readable bytes.
  EXPECT_EQ(envelope.find("7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5"), std::string::npos);
  EXPECT_EQ(envelope.find("finger-key_0.1.5"), std::string::npos);
  EXPECT_EQ(envelope.find("L3W1"), std::string::npos);
}

TEST(L3EnvelopeTest, TwoEnvelopesOfTheSameDocumentDiffer) {
  // A fresh salt and nonce per seal, so the same document never produces
  // the same bytes twice on disk.
  const std::string document = ReferenceDocument();
  const L3EnvelopeKey key = TestKey();

  std::string first;
  std::string second;
  std::string error;
  ASSERT_TRUE(SealL3Envelope(document, key, 1000, &first, &error)) << error;
  ASSERT_TRUE(SealL3Envelope(document, key, 1000, &second, &error)) << error;
  EXPECT_NE(first, second);

  std::string opened_first;
  std::string opened_second;
  ASSERT_TRUE(OpenL3Envelope(first, key, &opened_first, &error)) << error;
  ASSERT_TRUE(OpenL3Envelope(second, key, &opened_second, &error)) << error;
  EXPECT_EQ(opened_first, document);
  EXPECT_EQ(opened_second, document);
}

TEST(L3EnvelopeTest, WrongKeyFailsAndReturnsNoPlaintext) {
  const std::string document = ReferenceDocument();
  const L3EnvelopeKey key = TestKey();
  const L3EnvelopeKey wrong = OtherKey();

  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Envelope(document, key, 1000, &envelope, &error)) << error;

  std::string opened = "sentinel";
  std::string failure;
  EXPECT_FALSE(OpenL3Envelope(envelope, wrong, &opened, &failure));
  EXPECT_FALSE(failure.empty());
  // Not even a prefix of the plaintext may escape.
  EXPECT_EQ(opened, "sentinel");
  EXPECT_EQ(opened.find("L3W1"), std::string::npos);
}

TEST(L3EnvelopeTest, TamperedCiphertextFailsAuthentication) {
  const std::string document = ReferenceDocument();
  const L3EnvelopeKey key = TestKey();

  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Envelope(document, key, 1000, &envelope, &error)) << error;

  // Flip one bit in the ciphertext body. The GCM tag covers it, so the
  // open must fail rather than return modified plaintext.
  std::string tampered = envelope;
  const std::size_t offset = kL3EnvelopeHeaderBytes + kL3EnvelopeSaltBytes +
                             kL3EnvelopeNonceBytes + 3;
  ASSERT_LT(offset, tampered.size());
  tampered[offset] = static_cast<char>(tampered[offset] ^ 0x01);

  std::string opened = "sentinel";
  std::string failure;
  EXPECT_FALSE(OpenL3Envelope(tampered, key, &opened, &failure));
  EXPECT_EQ(opened, "sentinel");
}

TEST(L3EnvelopeTest, TamperedAuthenticationTagFails) {
  const std::string document = ReferenceDocument();
  const L3EnvelopeKey key = TestKey();

  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Envelope(document, key, 1000, &envelope, &error)) << error;

  std::string tampered = envelope;
  tampered[tampered.size() - 1] =
      static_cast<char>(tampered[tampered.size() - 1] ^ 0x80);

  std::string opened = "sentinel";
  std::string failure;
  EXPECT_FALSE(OpenL3Envelope(tampered, key, &opened, &failure));
  EXPECT_EQ(opened, "sentinel");
}

TEST(L3EnvelopeTest, TamperedHeaderFailsAuthentication) {
  // The header is additional data: changing the iteration count must fail
  // the open, not silently re-derive a different key.
  const std::string document = ReferenceDocument();
  const L3EnvelopeKey key = TestKey();

  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Envelope(document, key, 1000, &envelope, &error)) << error;

  std::string tampered = envelope;
  tampered[12] = static_cast<char>(tampered[12] ^ 0x01);

  std::string opened = "sentinel";
  std::string failure;
  EXPECT_FALSE(OpenL3Envelope(tampered, key, &opened, &failure));
  EXPECT_EQ(opened, "sentinel");
}

TEST(L3EnvelopeTest, TruncatedEnvelopeFails) {
  const std::string document = ReferenceDocument();
  const L3EnvelopeKey key = TestKey();

  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Envelope(document, key, 1000, &envelope, &error)) << error;

  // Every truncation must fail: the length in the header no longer matches
  // the bytes present.
  for (std::size_t cut : {std::size_t{0}, std::size_t{1}, std::size_t{16},
                          kL3EnvelopeHeaderBytes, envelope.size() / 2,
                          envelope.size() - 1}) {
    std::string truncated = envelope.substr(0, cut);
    std::string opened = "sentinel";
    std::string failure;
    EXPECT_FALSE(OpenL3Envelope(truncated, key, &opened, &failure))
        << "truncation to " << cut << " bytes was accepted";
    EXPECT_EQ(opened, "sentinel");
  }
}

TEST(L3EnvelopeTest, EmptyInputFails) {
  const L3EnvelopeKey key = TestKey();
  std::string error;

  // Sealing an empty document is refused: an envelope with no payload
  // carries no L3 statement, and accepting it would let a truncated file
  // look like a valid one.
  std::string envelope = "sentinel";
  EXPECT_FALSE(SealL3Envelope("", key, 1000, &envelope, &error));
  EXPECT_EQ(envelope, "sentinel");

  // Opening an empty envelope is refused too.
  std::string opened = "sentinel";
  EXPECT_FALSE(OpenL3Envelope("", key, &opened, &error));
  EXPECT_EQ(opened, "sentinel");
}

TEST(L3EnvelopeTest, OversizedInputFails) {
  const L3EnvelopeKey key = TestKey();
  std::string error;
  const std::string huge(kMaxL3EnvelopeBytes + 1, 'x');

  std::string envelope = "sentinel";
  EXPECT_FALSE(SealL3Envelope(huge, key, 1000, &envelope, &error));
  EXPECT_EQ(envelope, "sentinel");

  std::string opened = "sentinel";
  EXPECT_FALSE(OpenL3Envelope(huge, key, &opened, &error));
  EXPECT_EQ(opened, "sentinel");
}

TEST(L3EnvelopeTest, DocumentSizeBoundaryIsRespected) {
  const L3EnvelopeKey key = TestKey();
  std::string error;

  // The largest plaintext that still fits in an envelope must seal and
  // open; one byte more must be refused.
  const std::string at_limit(kMaxL3EnvelopePlaintextBytes, 'd');
  std::string envelope;
  ASSERT_TRUE(SealL3Envelope(at_limit, key, 1000, &envelope, &error)) << error;
  EXPECT_LE(envelope.size(), kMaxL3EnvelopeBytes);

  std::string opened;
  ASSERT_TRUE(OpenL3Envelope(envelope, key, &opened, &error)) << error;
  EXPECT_EQ(opened.size(), at_limit.size());

  const std::string over_limit(kMaxL3EnvelopePlaintextBytes + 1, 'd');
  std::string rejected = "sentinel";
  EXPECT_FALSE(SealL3Envelope(over_limit, key, 1000, &rejected, &error));
  EXPECT_EQ(rejected, "sentinel");
}

TEST(L3EnvelopeTest, InvalidKeyLengthIsRejected) {
  // A wrong-length master key is refused rather than padded or truncated:
  // silently accepting it is how a "protected" document stays readable.
  const L3EnvelopeKey short_key = ShortKey();
  EXPECT_FALSE(short_key.valid());
  EXPECT_EQ(short_key.size(), 0u);

  std::string envelope = "sentinel";
  std::string error;
  EXPECT_FALSE(SealL3Envelope(ReferenceDocument(), short_key, 1000, &envelope,
                              &error));
  EXPECT_EQ(envelope, "sentinel");

  // An open with a rejected key fails as well.
  std::string opened = "sentinel";
  EXPECT_FALSE(OpenL3Envelope("L3E1garbage", short_key, &opened, &error));
  EXPECT_EQ(opened, "sentinel");
}

TEST(L3EnvelopeTest, ForeignDataIsNotAnEnvelope) {
  const L3EnvelopeKey key = TestKey();
  std::string opened = "sentinel";
  std::string error;

  // A plaintext L3W1 document is not an envelope: the reader must not
  // mistake the transitional base64 payload for a sealed one.
  EXPECT_FALSE(OpenL3Envelope(ReferenceDocument(), key, &opened, &error));
  EXPECT_EQ(opened, "sentinel");
  EXPECT_FALSE(IsL3Envelope(ReferenceDocument()));
}

TEST(L3EnvelopeTest, UnknownVersionIsRejected) {
  const L3EnvelopeKey key = TestKey();
  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Envelope(ReferenceDocument(), key, 1000, &envelope, &error))
      << error;

  std::string bumped = envelope;
  bumped[4] = static_cast<char>(99);

  std::string opened = "sentinel";
  std::string failure;
  EXPECT_FALSE(OpenL3Envelope(bumped, key, &opened, &failure));
  EXPECT_EQ(opened, "sentinel");
  EXPECT_FALSE(IsL3Envelope(bumped));
}

TEST(L3EnvelopeTest, BadMagicIsRejected) {
  const L3EnvelopeKey key = TestKey();
  std::string opened = "sentinel";
  std::string error;
  EXPECT_FALSE(OpenL3Envelope("XXXXnot-an-envelope-at-all", key, &opened, &error));
  EXPECT_EQ(opened, "sentinel");
}

TEST(L3EnvelopeTest, InconsistentLengthsAreRejected) {
  // A header whose ciphertext length does not match the bytes present must
  // fail before any decryption is attempted.
  const L3EnvelopeKey key = TestKey();
  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Envelope(ReferenceDocument(), key, 1000, &envelope, &error))
      << error;

  std::string stretched = envelope;
  stretched[28] = static_cast<char>(stretched[28] + 1);

  std::string opened = "sentinel";
  std::string failure;
  EXPECT_FALSE(OpenL3Envelope(stretched, key, &opened, &failure));
  EXPECT_EQ(opened, "sentinel");
}

TEST(L3EnvelopeTest, IterationCountIsHonoured) {
  // The iteration count travels in the header, so a caller can pick a
  // different cost and the reader must use the one that was written.
  const std::string document = ReferenceDocument();
  const L3EnvelopeKey key = TestKey();

  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Envelope(document, key, 2000, &envelope, &error)) << error;

  const std::uint32_t iterations =
      static_cast<std::uint32_t>(static_cast<unsigned char>(envelope[12])) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(envelope[13]))
       << 8) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(envelope[14]))
       << 16) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(envelope[15]))
       << 24);
  EXPECT_EQ(iterations, 2000u);

  std::string opened;
  ASSERT_TRUE(OpenL3Envelope(envelope, key, &opened, &error)) << error;
  EXPECT_EQ(opened, document);
}

TEST(L3EnvelopeTest, DefaultIterationCountIsUsedWhenUnspecified) {
  const L3EnvelopeKey key = TestKey();
  std::string envelope;
  std::string error;
  ASSERT_TRUE(SealL3Envelope(ReferenceDocument(), key, 0, &envelope, &error))
      << error;

  const std::uint32_t iterations =
      static_cast<std::uint32_t>(static_cast<unsigned char>(envelope[12])) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(envelope[13]))
       << 8) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(envelope[14]))
       << 16) |
      (static_cast<std::uint32_t>(static_cast<unsigned char>(envelope[15]))
       << 24);
  EXPECT_EQ(iterations, kDefaultL3EnvelopeIterations);

  std::string opened;
  ASSERT_TRUE(OpenL3Envelope(envelope, key, &opened, &error)) << error;
  EXPECT_EQ(opened, ReferenceDocument());
}

}  // namespace
}  // namespace lengsuo::fp
