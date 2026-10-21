// kernel_pipe_keys - the machine-code derivation of the runner <-> kernel
// pipe (task Q).
//
// Contract section 1: the kernel proves which machine it is by hashing four
// facts about itself into a machine code, and the runner seals the session
// key with the key derived from that code. This layer is the kernel's half
// of that exchange.
//
// It is a layer of its own because the derivation is not the envelope:
//   * the derivation is what the kernel computes for itself, and it is
//     checked against the contract's fixed vector;
//   * the envelope is what the runner sends, and it is opened with a key.
//
// The envelope lives in kernel_pipe_envelope.{h,cc}.
#ifndef LENGSUO_NATIVE_KERNEL_PIPE_KEYS_H_
#define LENGSUO_NATIVE_KERNEL_PIPE_KEYS_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lengsuo::fp {

// The four machine facts the contract fixes, in the contract's key order.
struct MachineFacts {
  std::string os;
  std::string cpuid;
  std::string hddid;
  std::string computername;
};

// Length of the session key and of the machine-code derived key.
inline constexpr std::size_t kKernelPipeSessionKeyBytes = 32;
inline constexpr std::size_t kKernelPipeMachineKeyBytes = 32;

// The Hello nonce size, and the size of the AAD that binds the envelope to
// the handshake that asked for it (frame header + hello nonce).
inline constexpr std::size_t kKernelPipeHelloNonceBytes = 8;

// The HKDF info string the contract pins.
inline constexpr char kKernelPipeSessionInfo[] =
    "lengsuo-kernel-session-v1";

// machine_code = SHA-256(UTF-16LE(fixed-shape JSON)) as lowercase hex.
// The JSON shape is the contract's: the four fields in the order os, cpuid,
// hddid, computername.
std::string DeriveMachineCode(const MachineFacts& facts);

// k_machine = SHA-256(machine_code's ASCII bytes), 32 bytes.
bool DeriveMachineKey(std::string_view machine_code, std::string* key_out);

// session_key = HKDF-SHA256(ikm=k_machine, salt=hello_nonce, info,
// L=32). The contract pins this derivation as candidate B; the kernel does
// not require it (the envelope always carries the key), so this is a
// cross-check helper, not a mandatory step of the handshake.
bool DeriveSessionKeyHkdf(std::string_view machine_key,
                          std::string_view hello_nonce,
                          std::string* session_key_out);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_KERNEL_PIPE_KEYS_H_
