// kernel_pipe_keys_test - the machine-code derivation of the kernel side of
// the runner <-> kernel pipe (task Q).
//
// The fixed test vectors are in kernel_pipe_test_util.h; they are copied
// byte for byte out of docs/kernel-wire-contract.md section 2.
#include "kernel_pipe_keys.h"

#include <gtest/gtest.h>

#include <string>

#include "kernel_pipe_test_util.h"

namespace lengsuo::fp {
namespace {

using test::BytesToHex;
using test::HexToBytes;
using test::kEnvelopeHelloNonce;
using test::kMachineCode;
using test::kMachineKeyHex;
using test::kSessionKeyHex;

// The machine facts the contract's derivation vector is built on.
MachineFacts ContractFacts() {
  MachineFacts facts;
  facts.os = "linux";
  facts.cpuid = "cpuid-test";
  facts.hddid = "hddid-test";
  facts.computername = "host-test";
  return facts;
}

TEST(KernelPipeKeys, MachineCodeMatchesTheContractVector) {
  EXPECT_EQ(DeriveMachineCode(ContractFacts()), std::string(kMachineCode));
}

TEST(KernelPipeKeys, MachineKeyMatchesTheContractVector) {
  std::string key;
  ASSERT_TRUE(DeriveMachineKey(kMachineCode, &key));
  EXPECT_EQ(BytesToHex(key), std::string(kMachineKeyHex));
}

TEST(KernelPipeKeys, SessionKeyMatchesTheContractVector) {
  std::string machine_key;
  ASSERT_TRUE(DeriveMachineKey(kMachineCode, &machine_key));
  std::string session_key;
  ASSERT_TRUE(DeriveSessionKeyHkdf(
      machine_key, HexToBytes(kEnvelopeHelloNonce), &session_key));
  EXPECT_EQ(BytesToHex(session_key), std::string(kSessionKeyHex));
}

TEST(KernelPipeKeys, HkdfRejectsWrongSizedInputs) {
  std::string session_key;
  EXPECT_FALSE(DeriveSessionKeyHkdf("short", HexToBytes(kEnvelopeHelloNonce),
                                   &session_key));
  EXPECT_FALSE(DeriveSessionKeyHkdf(std::string(32, '\0'), "short",
                                   &session_key));
}

}  // namespace
}  // namespace lengsuo::fp
