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
// The four machine facts the contract fixes, in the contract's key order.
//
// ⭐ What each field holds is a rule both repositories implement (task M0).
// The table below is the same table, word for word, as the one in the
// client repo's task book; the collection rules themselves live in
// machine_facts.h (the pure part) and in
// integration/kernel_pipe_client.cc (the part that talks to the machine).
//
//   field          Windows                        POSIX
//   -------------  ----------------------------  --------------------------------
//   os             "windows"                      "linux" / "macos"
//                  (a constant; no version)       (a constant; no version)
//   cpuid          base::CPU vendor/family/       base::CPU vendor/family/
//                  model/stepping, slash joined   model/stepping, slash joined
//   hddid          the serial number of           scan
//                  \\.\PhysicalDrive0, trimmed    /sys/block/*/device/serial,
//                  (two-pass storage query)       trimmed, sorted, first
//   computername   GetComputerNameW               gethostname()
//
// Three rules hold for every cell:
//
//   * a version never appears in a value. A patch release would change the
//     machine code, and every key already bound would stop opening.
//   * a value that cannot be obtained is the empty string, never an
//     invented one. The two sides then disagree, the handshake fails, and
//     that is the intended way to fail.
//   * the value is the machine's identity, not a fingerprint of the
//     session: it does not change when the browser is updated, and it is
//     the same for every process on the machine.
//
// The macOS hddid cell is empty on both sides: macOS exposes no portable
// per-disk serial to an unprivileged reader, and neither side gets to
// invent one. See docs/l3-reports/task-M0.md.
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
