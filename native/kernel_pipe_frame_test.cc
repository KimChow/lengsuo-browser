// kernel_pipe_frame_test - the frame layer of the kernel side of the
// runner <-> kernel pipe (task Q).
//
// The fixed test vectors are in kernel_pipe_test_util.h; they are copied
// byte for byte out of docs/kernel-wire-contract.md section 2.
#include "kernel_pipe_frame.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "kernel_pipe_test_util.h"

namespace lengsuo::fp {
namespace {

using test::BytesToHex;
using test::Fill;
using test::HexToBytes;
using test::kIllegalLenOver;
using test::kIllegalReserved;
using test::kIllegalTruncHeader;
using test::kIllegalTruncPayload;
using test::kIllegalType;
using test::kLegalHeaderV0;
using test::kLegalHeaderV1;
using test::kLegalHeaderV255;
using test::kLegalHeaderV3;
using test::kLegalHeaderV65535;
using test::kLegalHeaderVMax;

TEST(KernelPipeFrame, LegalHeadersDecodeToTheContractBytes) {
  struct Case {
    const char* hex;
    std::uint32_t length;
  };
  const Case cases[] = {
      {kLegalHeaderV0, 0},
      {kLegalHeaderV1, 1},
      {kLegalHeaderV3, 3},
      {kLegalHeaderV255, 255},
      {kLegalHeaderV65535, 65535},
      {kLegalHeaderVMax, 8388608},
  };
  for (const Case& c : cases) {
    const std::string header = HexToBytes(c.hex);
    ASSERT_EQ(header.size(), kKernelPipeHeaderBytes);
    std::uint32_t length = 0;
    FrameType type = FrameType::kHello;
    ASSERT_TRUE(DecodeFrameHeader(header, &length, &type)) << c.hex;
    EXPECT_EQ(length, c.length) << c.hex;
    EXPECT_EQ(type, FrameType::kConfigBundle) << c.hex;
  }
}

TEST(KernelPipeFrame, EncodedLegalFramesMatchTheContractBytes) {
  struct Case {
    const char* hex;
    std::size_t length;
  };
  const Case cases[] = {
      {kLegalHeaderV0, 0},
      {kLegalHeaderV1, 1},
      {kLegalHeaderV3, 3},
      {kLegalHeaderV255, 255},
      {kLegalHeaderV65535, 65535},
      {kLegalHeaderVMax, 8388608},
  };
  for (const Case& c : cases) {
    const std::string expected = HexToBytes(c.hex) + Fill(c.length);
    std::string frame;
    ASSERT_TRUE(EncodeFrame(FrameType::kConfigBundle, Fill(c.length), &frame));
    EXPECT_EQ(BytesToHex(frame), BytesToHex(expected)) << c.hex;
  }
}

TEST(KernelPipeFrame, LengthIsBigEndian) {
  // A little-endian implementation would write 01 00 00 00 here.
  std::string frame;
  ASSERT_TRUE(EncodeFrame(FrameType::kConfigBundle, "x", &frame));
  ASSERT_EQ(frame.size(), kKernelPipeHeaderBytes + 1);
  EXPECT_EQ(frame[0], '\0');
  EXPECT_EQ(frame[1], '\0');
  EXPECT_EQ(frame[2], '\0');
  EXPECT_EQ(frame[3], '\1');
  EXPECT_EQ(frame[4], static_cast<char>(0x03));
  EXPECT_EQ(frame[5], '\0');
  EXPECT_EQ(frame[6], '\0');
  EXPECT_EQ(frame[7], '\0');
}

TEST(KernelPipeFrame, IllegalFramesAreRejected) {
  const char* const cases[] = {
      kIllegalLenOver, kIllegalReserved, kIllegalType, kIllegalTruncHeader,
      kIllegalTruncPayload,
  };
  for (const char* hex : cases) {
    const std::string raw = HexToBytes(hex);
    Frame frame;
    EXPECT_FALSE(DecodeFrame(raw, &frame)) << hex;
    // A rejected frame yields no partial plaintext.
    EXPECT_TRUE(frame.payload.empty()) << hex;
    EXPECT_EQ(frame.type, FrameType::kHello) << hex;
  }
}

TEST(KernelPipeFrame, LenOverTheLimitIsRejected) {
  // The contract's limit is exactly 8 MiB; one byte more is illegal.
  std::string header;
  header.push_back('\0');
  header.push_back(static_cast<char>(0x80));
  header.push_back('\0');
  header.push_back('\1');
  header.push_back(static_cast<char>(0x03));
  header.append(3, '\0');
  std::uint32_t length = 0;
  FrameType type = FrameType::kHello;
  EXPECT_FALSE(DecodeFrameHeader(header, &length, &type));
  // And the limit itself is legal.
  std::string at_limit = header;
  at_limit[3] = '\0';
  EXPECT_TRUE(DecodeFrameHeader(at_limit, &length, &type));
  EXPECT_EQ(length, 8388608u);
}

TEST(KernelPipeFrame, ReservedBytesMustBeZero) {
  std::string header = HexToBytes(kLegalHeaderV1);
  for (std::size_t i = 5; i < 8; ++i) {
    std::string broken = header;
    broken[i] = '\1';
    std::uint32_t length = 0;
    FrameType type = FrameType::kHello;
    EXPECT_FALSE(DecodeFrameHeader(broken, &length, &type))
        << "reserved byte " << i << " must be rejected";
  }
}

TEST(KernelPipeFrame, UnknownTypeIsRejected) {
  for (std::uint8_t raw : {std::uint8_t{0x00}, std::uint8_t{0x06},
                           std::uint8_t{0x07}, std::uint8_t{0x80},
                           std::uint8_t{0x83}, std::uint8_t{0xFF}}) {
    EXPECT_FALSE(IsKnownFrameType(raw));
    std::string header = HexToBytes(kLegalHeaderV1);
    header[4] = static_cast<char>(raw);
    std::uint32_t length = 0;
    FrameType type = FrameType::kHello;
    EXPECT_FALSE(DecodeFrameHeader(header, &length, &type));
  }
}

TEST(KernelPipeFrame, KnownTypesAreAccepted) {
  EXPECT_TRUE(IsKnownFrameType(0x01));
  EXPECT_TRUE(IsKnownFrameType(0x02));
  EXPECT_TRUE(IsKnownFrameType(0x03));
  EXPECT_TRUE(IsKnownFrameType(0x04));
  EXPECT_TRUE(IsKnownFrameType(0x05));
  EXPECT_TRUE(IsKnownFrameType(0x81));
  EXPECT_TRUE(IsKnownFrameType(0x82));
}

TEST(KernelPipeFrame, RoundTripsAtEveryContractLength) {
  const std::size_t lengths[] = {0, 1, 3, 255, 65535};
  for (const std::size_t length : lengths) {
    const std::string payload = Fill(length);
    std::string frame;
    ASSERT_TRUE(EncodeFrame(FrameType::kConfigBundle, payload, &frame));
    Frame decoded;
    ASSERT_TRUE(DecodeFrame(frame, &decoded));
    EXPECT_EQ(decoded.type, FrameType::kConfigBundle);
    EXPECT_EQ(decoded.payload, payload);
    EXPECT_EQ(decoded.header.size(), kKernelPipeHeaderBytes);
    EXPECT_EQ(decoded.header, frame.substr(0, kKernelPipeHeaderBytes));
  }
}

TEST(KernelPipeFrame, EncodeRefusesAnOversizedPayload) {
  const std::string too_big(kKernelPipeMaxPayloadBytes + 1, '\0');
  std::string frame;
  EXPECT_FALSE(EncodeFrame(FrameType::kConfigBundle, too_big, &frame));
  EXPECT_TRUE(frame.empty());
}

TEST(KernelPipeFrame, DecodeRefusesAnOversizedDeclaration) {
  // A header that declares more than the limit must be rejected even when
  // the bytes are not there: the decoder must not wait for them.
  const std::string header = HexToBytes(kIllegalLenOver);
  Frame frame;
  EXPECT_FALSE(DecodeFrame(header, &frame));
  // And it must not wait for the missing payload either.
  EXPECT_FALSE(DecodeFrame(header + Fill(16), &frame));
}

}  // namespace
}  // namespace lengsuo::fp
