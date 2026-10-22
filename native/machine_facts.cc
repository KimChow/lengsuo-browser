#include "machine_facts.h"

#include <algorithm>
#include <utility>

namespace lengsuo::fp {
namespace {

bool IsAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
         c == '\f';
}

// Little endian, the byte order of the Windows structures this reads.
std::uint32_t ReadU32Le(const std::uint8_t* buffer) {
  return static_cast<std::uint32_t>(buffer[0]) |
         (static_cast<std::uint32_t>(buffer[1]) << 8) |
         (static_cast<std::uint32_t>(buffer[2]) << 16) |
         (static_cast<std::uint32_t>(buffer[3]) << 24);
}

}  // namespace

std::string TrimAscii(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && IsAsciiSpace(text[begin])) {
    ++begin;
  }
  while (end > begin && IsAsciiSpace(text[end - 1])) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

std::string PlatformOsTag(Platform platform) {
  switch (platform) {
    case Platform::kWindows:
      return "windows";
    case Platform::kMacos:
      return "macos";
    case Platform::kLinux:
      return "linux";
    case Platform::kOther:
      // A platform neither side supports: the empty tag makes the two
      // machine codes differ, which is the fail-closed outcome.
      return std::string();
  }
  return std::string();
}

std::string SelectStorageSerial(const std::vector<std::string>& candidates) {
  std::vector<std::string> usable;
  usable.reserve(candidates.size());
  for (const std::string& candidate : candidates) {
    std::string trimmed = TrimAscii(candidate);
    if (!trimmed.empty()) {
      usable.push_back(std::move(trimmed));
    }
  }
  if (usable.empty()) {
    return std::string();
  }
  std::sort(usable.begin(), usable.end());
  return usable.front();
}

bool ParseStorageDescriptorSize(const std::uint8_t* buffer,
                                std::size_t size,
                                std::uint32_t* size_out) {
  if (buffer == nullptr || size_out == nullptr ||
      size < kStorageDescriptorSizeOffset + sizeof(std::uint32_t)) {
    return false;
  }
  *size_out = ReadU32Le(buffer + kStorageDescriptorSizeOffset);
  return true;
}

std::string ParseStorageDescriptorSerial(const std::uint8_t* buffer,
                                         std::size_t size) {
  if (buffer == nullptr ||
      size < kStorageDescriptorSerialNumberFieldOffset + sizeof(std::uint32_t)) {
    return std::string();
  }
  // The field is a DWORD byte offset into the descriptor, not a pointer.
  const std::uint32_t offset =
      ReadU32Le(buffer + kStorageDescriptorSerialNumberFieldOffset);
  if (offset == 0 || offset >= size) {
    // 0 means the driver has no serial to report; an offset past the end
    // means the descriptor was cut short.
    return std::string();
  }
  const char* text =
      reinterpret_cast<const char*>(buffer + offset);
  const std::size_t available = size - offset;
  std::size_t length = 0;
  while (length < available && text[length] != '\0') {
    ++length;
  }
  return TrimAscii(std::string_view(text, length));
}

}  // namespace lengsuo::fp
