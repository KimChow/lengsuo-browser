#include "kernel_pipe_frame.h"

#include <utility>

namespace lengsuo::fp {

bool IsKnownFrameType(std::uint8_t raw) {
  switch (raw) {
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x81:
    case 0x82:
      return true;
    default:
      return false;
  }
}

bool EncodeFrame(FrameType type,
                 std::string_view payload,
                 std::string* out) {
  if (out == nullptr) {
    return false;
  }
  if (payload.size() > kKernelPipeMaxPayloadBytes) {
    return false;
  }
  std::string frame;
  frame.reserve(kKernelPipeHeaderBytes + payload.size());
  wire::PutU32BE(&frame, static_cast<std::uint32_t>(payload.size()));
  frame.push_back(static_cast<char>(static_cast<std::uint8_t>(type)));
  frame.push_back('\0');
  frame.push_back('\0');
  frame.push_back('\0');
  frame.append(payload);
  *out = std::move(frame);
  return true;
}

bool DecodeFrameHeader(std::string_view header,
                       std::uint32_t* payload_len,
                       FrameType* type) {
  if (header.size() < kKernelPipeHeaderBytes) {
    return false;
  }
  const std::uint32_t length = wire::ReadU32BE(header, 0);
  if (length > kKernelPipeMaxPayloadBytes) {
    return false;
  }
  if (header[5] != '\0' || header[6] != '\0' || header[7] != '\0') {
    return false;
  }
  const auto raw = static_cast<std::uint8_t>(header[4]);
  if (!IsKnownFrameType(raw)) {
    return false;
  }
  if (payload_len != nullptr) {
    *payload_len = length;
  }
  if (type != nullptr) {
    *type = static_cast<FrameType>(raw);
  }
  return true;
}

bool DecodeFrame(std::string_view frame, Frame* out) {
  if (out == nullptr) {
    return false;
  }
  std::uint32_t length = 0;
  FrameType type = FrameType::kHello;
  if (!DecodeFrameHeader(frame.substr(0, kKernelPipeHeaderBytes), &length,
                         &type)) {
    return false;
  }
  if (frame.size() != kKernelPipeHeaderBytes + length) {
    return false;
  }
  Frame decoded;
  decoded.type = type;
  decoded.header = std::string(frame.substr(0, kKernelPipeHeaderBytes));
  decoded.payload = std::string(frame.substr(kKernelPipeHeaderBytes, length));
  *out = std::move(decoded);
  return true;
}

}  // namespace lengsuo::fp
