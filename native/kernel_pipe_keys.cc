#include "kernel_pipe_keys.h"

#include <openssl/digest.h>
#include <openssl/hkdf.h>
#include <openssl/sha2.h>

#include <array>
#include <utility>

#include "kernel_pipe_frame.h"

namespace lengsuo::fp {
namespace {

// SHA-256 over `data`, as raw bytes.
bool Sha256(std::string_view data, std::string* out) {
  out->assign(kKernelPipeMachineKeyBytes, '\0');
  SHA256(reinterpret_cast<const std::uint8_t*>(data.data()), data.size(),
         reinterpret_cast<std::uint8_t*>(out->data()));
  return true;
}

// The machine-code JSON, in the contract's key order, with the contract's
// escaping rules. The values are JSON strings; a value that needs escaping
// is escaped the JSON way so the shape stays well-formed.
std::string MachineCodeJson(const MachineFacts& facts) {
  // The four facts in the contract's key order. string_view keeps the
  // compiler able to see the bounds; the views point into `facts`, which
  // outlives the loop.
  const std::array<std::string_view, 4> fields = {
      facts.os, facts.cpuid, facts.hddid, facts.computername};
  static constexpr std::array<std::string_view, 4> kKeys = {
      "os", "cpuid", "hddid", "computername"};
  static constexpr std::string_view kDigits = "0123456789abcdef";
  std::string json = "{";
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i != 0) {
      json += ",";
    }
    json += "\"";
    json += kKeys[i];
    json += "\":\"";
    for (const char c : fields[i]) {
      switch (c) {
        case '"': json += "\\\""; break;
        case '\\': json += "\\\\"; break;
        case '\b': json += "\\b"; break;
        case '\f': json += "\\f"; break;
        case '\n': json += "\\n"; break;
        case '\r': json += "\\r"; break;
        case '\t': json += "\\t"; break;
        default:
          if (static_cast<unsigned char>(c) < 0x20) {
            json += "\\u00";
            json += kDigits[(static_cast<unsigned char>(c) >> 4) & 0x0F];
            json += kDigits[static_cast<unsigned char>(c) & 0x0F];
          } else {
            json += c;
          }
      }
    }
    json += "\"";
  }
  json += "}";
  return json;
}

// UTF-16LE encoding of a UTF-8 string, without a BOM. The contract pins
// UTF-16LE for the machine-code input.
std::string Utf16Le(std::string_view utf8) {
  std::string out;
  std::size_t i = 0;
  while (i < utf8.size()) {
    const auto c = static_cast<unsigned char>(utf8[i]);
    std::uint32_t code = 0;
    std::size_t width = 1;
    if (c < 0x80) {
      code = c;
    } else if ((c & 0xE0) == 0xC0) {
      code = c & 0x1Fu;
      width = 2;
    } else if ((c & 0xF0) == 0xE0) {
      code = c & 0x0Fu;
      width = 3;
    } else if ((c & 0xF8) == 0xF0) {
      code = c & 0x07u;
      width = 4;
    } else {
      // A stray continuation byte cannot start a sequence; treat it as
      // latin-1 so the encoding stays total.
      code = c;
    }
    if (i + width > utf8.size()) {
      code = c;
      width = 1;
    } else {
      for (std::size_t k = 1; k < width; ++k) {
        code = (code << 6) |
               (static_cast<unsigned char>(utf8[i + k]) & 0x3Fu);
      }
    }
    i += width;
    if (code >= 0x10000) {
      // A non-BMP code point is a surrogate pair in UTF-16.
      const std::uint32_t adjusted = code - 0x10000;
      const std::uint32_t high = 0xD800 + (adjusted >> 10);
      const std::uint32_t low = 0xDC00 + (adjusted & 0x3FF);
      out += static_cast<char>(high & 0xFF);
      out += static_cast<char>((high >> 8) & 0xFF);
      out += static_cast<char>(low & 0xFF);
      out += static_cast<char>((low >> 8) & 0xFF);
    } else {
      out += static_cast<char>(code & 0xFF);
      out += static_cast<char>((code >> 8) & 0xFF);
    }
  }
  return out;
}

}  // namespace

std::string DeriveMachineCode(const MachineFacts& facts) {
  const std::string json = MachineCodeJson(facts);
  const std::string utf16 = Utf16Le(json);
  std::string digest;
  Sha256(utf16, &digest);
  std::string hex;
  hex.reserve(kKernelPipeMachineKeyBytes * 2);
  wire::AppendHex(&hex, digest);
  return hex;
}

bool DeriveMachineKey(std::string_view machine_code, std::string* key_out) {
  if (key_out == nullptr) {
    return false;
  }
  std::string digest;
  if (!Sha256(machine_code, &digest)) {
    return false;
  }
  *key_out = std::move(digest);
  return true;
}

bool DeriveSessionKeyHkdf(std::string_view machine_key,
                          std::string_view hello_nonce,
                          std::string* session_key_out) {
  if (session_key_out == nullptr) {
    return false;
  }
  if (machine_key.size() != kKernelPipeMachineKeyBytes) {
    return false;
  }
  if (hello_nonce.size() != kKernelPipeHelloNonceBytes) {
    return false;
  }
  std::string derived(kKernelPipeSessionKeyBytes, '\0');
  const int ok = HKDF(
      reinterpret_cast<std::uint8_t*>(derived.data()), derived.size(),
      EVP_sha256(), reinterpret_cast<const std::uint8_t*>(machine_key.data()),
      machine_key.size(),
      reinterpret_cast<const std::uint8_t*>(hello_nonce.data()),
      hello_nonce.size(),
      reinterpret_cast<const std::uint8_t*>(kKernelPipeSessionInfo),
      sizeof(kKernelPipeSessionInfo) - 1);
  if (ok != 1) {
    return false;
  }
  *session_key_out = std::move(derived);
  return true;
}

}  // namespace lengsuo::fp
