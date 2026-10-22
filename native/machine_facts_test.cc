// machine_facts_test - the machine-fact collection rules (task M0).
//
// The point of this file is that it runs on Linux CI while testing the
// Windows path too: the collector in integration/kernel_pipe_client.cc is
// split into "ask the device" (compiled only on Windows) and "parse the
// answer" (here, pure). The Windows descriptor below is a synthetic
// 396-byte buffer shaped like the one the driver actually returns on the
// research machine, so the three defects task M0 lists are each pinned by a
// case that fails if the defect comes back.
//
// The rule vectors at the bottom are the cross-repo agreement: the client
// repo hardcodes the same seven values per row, and
// tools/cross_repo_machine.py refuses to pass unless both sides agree and
// the digests recompute from the four fields.
#include "machine_facts.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "kernel_pipe_keys.h"
#include "kernel_pipe_test_util.h"

namespace lengsuo::fp {
namespace {
namespace test_util = ::lengsuo::fp::test;

void WriteU32Le(std::uint8_t* out, std::uint32_t value) {
  out[0] = static_cast<std::uint8_t>(value & 0xFF);
  out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
  out[2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
  out[3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

// --- the os cell ---------------------------------------------------------
//
// The tag is the platform *kind*, not the platform version: a patch release
// must not change the machine code, or every key already bound stops
// opening (R2).

TEST(MachineFacts, OsTagIsTheLowerCasePlatformWithoutVersion) {
  EXPECT_EQ(PlatformOsTag(Platform::kWindows), "windows");
  EXPECT_EQ(PlatformOsTag(Platform::kMacos), "macos");
  EXPECT_EQ(PlatformOsTag(Platform::kLinux), "linux");
  // A platform neither side supports gets no tag: the two sides then
  // disagree, which is the fail-closed outcome.
  EXPECT_EQ(PlatformOsTag(Platform::kOther), "");
}

TEST(MachineFacts, OsTagCarriesNoSpaceAndNoVersion) {
  for (const Platform platform : {Platform::kWindows, Platform::kMacos,
                                 Platform::kLinux}) {
    const std::string tag = PlatformOsTag(platform);
    EXPECT_EQ(tag.find(' '), std::string::npos) << tag;
    EXPECT_EQ(tag.find('.'), std::string::npos) << tag;
    for (const char c : tag) {
      EXPECT_TRUE(c >= 'a' && c <= 'z') << "not lower case: " << tag;
    }
  }
}

// --- the hddid cell, POSIX side -----------------------------------------
//
// The rule is the client's, word for word: trim, drop the empty ones, sort,
// take the first. The sort is the part that matters on a multi-disk
// machine: the directory order differs between the two sides, so "the first
// one that answers" would pick two different disks (R1).

TEST(MachineFacts, StorageSerialSortsAndTakesTheFirst) {
  EXPECT_EQ(SelectStorageSerial({"SERIAL-B", "SERIAL-A", "SERIAL-C"}),
            "SERIAL-A");
  // The order the candidates arrive in does not matter.
  EXPECT_EQ(SelectStorageSerial({"SERIAL-C", "SERIAL-A", "SERIAL-B"}),
            "SERIAL-A");
}

TEST(MachineFacts, StorageSerialTrimsBeforeChoosing) {
  // "/sys/block/*/device/serial" files end in a newline.
  EXPECT_EQ(SelectStorageSerial({"  SERIAL-A\n"}), "SERIAL-A");
  EXPECT_EQ(SelectStorageSerial({" \tSERIAL-A \r\n"}), "SERIAL-A");
  // Trimming happens before the sort, so the space does not decide it.
  EXPECT_EQ(SelectStorageSerial({" SERIAL-B", "SERIAL-A"}), "SERIAL-A");
}

TEST(MachineFacts, StorageSerialDropsEmptyCandidates) {
  EXPECT_EQ(SelectStorageSerial({"", "   ", "\n", "SERIAL-A"}), "SERIAL-A");
}

TEST(MachineFacts, StorageSerialIsEmptyWhenNothingUsable) {
  // Constraint 5: a value that cannot be obtained is the empty string, not
  // an invented one. Empty makes the two sides disagree, which is the
  // fail-closed outcome.
  EXPECT_EQ(SelectStorageSerial({}), "");
  EXPECT_EQ(SelectStorageSerial({"", " ", "\t\r\n"}), "");
}

TEST(MachineFacts, StorageSerialComparesBytesNotCase) {
  // Sorting is byte-wise, as the client's `Vec<String>::sort` is. This is
  // not a locale-aware comparison.
  EXPECT_EQ(SelectStorageSerial({"a", "B", "A"}), "A");
}

// --- the hddid cell, Windows side ---------------------------------------
//
// A descriptor shaped like the one the driver returns on the research
// machine: 396 bytes, the serial at offset 90, and the driver's leading
// spaces. The header fields are at the offsets the Windows struct pins.

constexpr std::size_t kRealDescriptorSize = 396;
constexpr std::uint32_t kRealSerialOffset = 90;
// Exactly what the research machine's driver returned, spaces included.
constexpr char kRealSerialWithPadding[] = "        154310E1E661";

std::array<std::uint8_t, kRealDescriptorSize> RealShapedDescriptor() {
  std::array<std::uint8_t, kRealDescriptorSize> buffer{};
  WriteU32Le(buffer.data() + kStorageDescriptorSizeOffset,
             static_cast<std::uint32_t>(kRealDescriptorSize));
  WriteU32Le(buffer.data() + kStorageDescriptorSerialNumberFieldOffset,
             kRealSerialOffset);
  const std::size_t length = sizeof(kRealSerialWithPadding) - 1;
  for (std::size_t i = 0; i < length; ++i) {
    buffer[kRealSerialOffset + i] =
        static_cast<std::uint8_t>(kRealSerialWithPadding[i]);
  }
  return buffer;
}

TEST(MachineFacts, DescriptorOffsetsMatchTheWindowsStruct) {
  // These are the two offsets the reader depends on. If a Windows SDK puts
  // the fields elsewhere, this fails here as well as in the static_assert in
  // integration/kernel_pipe_client.cc.
  EXPECT_EQ(kStorageDescriptorSizeOffset, 4u);
  EXPECT_EQ(kStorageDescriptorSerialNumberFieldOffset, 24u);
}

TEST(MachineFacts, DescriptorSizeIsReadBack) {
  const auto buffer = RealShapedDescriptor();
  std::uint32_t size = 0;
  ASSERT_TRUE(ParseStorageDescriptorSize(buffer.data(), buffer.size(), &size));
  EXPECT_EQ(size, kRealDescriptorSize);
}

TEST(MachineFacts, DescriptorSizeRejectsATruncatedHeader) {
  std::array<std::uint8_t, 7> buffer{};
  std::uint32_t size = 0;
  EXPECT_FALSE(ParseStorageDescriptorSize(buffer.data(), buffer.size(), &size));
  EXPECT_FALSE(ParseStorageDescriptorSize(nullptr, 16, &size));
  EXPECT_FALSE(
      ParseStorageDescriptorSize(buffer.data(), buffer.size(), nullptr));
}

TEST(MachineFacts, DescriptorSerialIsReadAtTheOffsetAndTrimmed) {
  // Defects 2 and 3 together: the serial is at byte offset 90, which is
  // outside the 36-byte header, and the field names an offset, not an
  // address. The driver's leading spaces are trimmed.
  const auto buffer = RealShapedDescriptor();
  EXPECT_EQ(ParseStorageDescriptorSerial(buffer.data(), buffer.size()),
            "154310E1E661");
}

TEST(MachineFacts, DescriptorSerialOffsetIsA32BitValue) {
  // The offset field is a DWORD. An offset past 255 would be read as a
  // different value if the field were read as a byte, so this descriptor
  // uses one that does not fit in a byte.
  std::array<std::uint8_t, 512> buffer{};
  constexpr std::uint32_t kOffset = 300;
  WriteU32Le(buffer.data() + kStorageDescriptorSizeOffset, 512);
  WriteU32Le(buffer.data() + kStorageDescriptorSerialNumberFieldOffset,
             kOffset);
  const std::string serial = "SERIAL-PAST-255";
  for (std::size_t i = 0; i < serial.size(); ++i) {
    buffer[kOffset + i] = static_cast<std::uint8_t>(serial[i]);
  }
  EXPECT_EQ(ParseStorageDescriptorSerial(buffer.data(), buffer.size()),
            "SERIAL-PAST-255");
}

TEST(MachineFacts, DescriptorSerialTrailingSpaceIsTrimmedToo) {
  auto buffer = RealShapedDescriptor();
  std::fill(buffer.begin() + kRealSerialOffset, buffer.end(), 0);
  const std::string padded = "154310E1E661   ";
  for (std::size_t i = 0; i < padded.size(); ++i) {
    buffer[kRealSerialOffset + i] =
        static_cast<std::uint8_t>(padded[i]);
  }
  EXPECT_EQ(ParseStorageDescriptorSerial(buffer.data(), buffer.size()),
            "154310E1E661");
}

TEST(MachineFacts, DescriptorSerialIsEmptyWhenTheDriverHasNone) {
  auto buffer = RealShapedDescriptor();
  WriteU32Le(buffer.data() + kStorageDescriptorSerialNumberFieldOffset, 0);
  EXPECT_EQ(ParseStorageDescriptorSerial(buffer.data(), buffer.size()), "");
}

TEST(MachineFacts, DescriptorSerialIsEmptyWhenTheOffsetFallsOutside) {
  auto buffer = RealShapedDescriptor();
  WriteU32Le(buffer.data() + kStorageDescriptorSerialNumberFieldOffset,
             static_cast<std::uint32_t>(kRealDescriptorSize));
  EXPECT_EQ(ParseStorageDescriptorSerial(buffer.data(), buffer.size()), "");
  WriteU32Le(buffer.data() + kStorageDescriptorSerialNumberFieldOffset, 0xFFFF);
  EXPECT_EQ(ParseStorageDescriptorSerial(buffer.data(), buffer.size()), "");
}

TEST(MachineFacts, DescriptorSerialIsEmptyWhenTheHeaderIsTruncated) {
  std::array<std::uint8_t, 16> buffer{};
  WriteU32Le(buffer.data() + kStorageDescriptorSizeOffset, 16);
  EXPECT_EQ(ParseStorageDescriptorSerial(buffer.data(), buffer.size()), "");
  EXPECT_EQ(ParseStorageDescriptorSerial(nullptr, 396), "");
}

TEST(MachineFacts, DescriptorSerialMayRunToTheEndWithoutANul) {
  // The driver does not always NUL terminate inside the buffer.
  std::array<std::uint8_t, 128> buffer{};
  WriteU32Le(buffer.data() + kStorageDescriptorSerialNumberFieldOffset, 100);
  const std::string serial = "SERIAL-NO-NUL";
  for (std::size_t i = 0; i < serial.size(); ++i) {
    buffer[100 + i] = static_cast<std::uint8_t>(serial[i]);
  }
  EXPECT_EQ(ParseStorageDescriptorSerial(buffer.data(), buffer.size()),
            "SERIAL-NO-NUL");
}

TEST(MachineFacts, DescriptorSerialOfOnlySpacesIsEmpty) {
  std::array<std::uint8_t, 128> buffer{};
  WriteU32Le(buffer.data() + kStorageDescriptorSerialNumberFieldOffset, 100);
  for (std::size_t i = 100; i < buffer.size(); ++i) {
    buffer[i] = ' ';
  }
  EXPECT_EQ(ParseStorageDescriptorSerial(buffer.data(), buffer.size()), "");
}

// --- the cross-repo rule vectors ----------------------------------------
//
// Each row is a machine the two repositories must agree on, expressed as
// the four facts plus the two digests derived from them. The values are
// fake (constraint 7: never commit a real serial number or host name), and
// the digests are computed, not copied from a live machine.
//
// The same seven values per row live in the client repo's rule-vector
// table; tools/cross_repo_machine.py compares the two and refuses to pass
// if they disagree. Keeping these in the test is what makes the agreement
// checkable rather than a matter of someone reading two files side by side.

struct RuleVector {
  const char* label;
  const char* os;
  const char* cpuid;
  const char* hddid;
  const char* computername;
  const char* machine_code;
  const char* k_machine;
};

// @@begin machine-facts-vectors
inline constexpr RuleVector kRuleVectors[] = {
    {"linux-demo", "linux", "GenuineIntel/6/183/1", "DEMO-SERIAL-0001",
     "demo-host",
     "a64a48e92bef5ce24a4c3982bf33ea210a2dd5d0ad4fa5f23dc0172e7ee1409c",
     "a917d3ac761be29fcea63dc80fadd30586ee21250ea3ea56e31f5b39f47f04ec"},
    {"windows-demo", "windows", "AuthenticAMD/25/33/0", "DEMO-SERIAL-0002",
     "demo-win",
     "550635314afdee4c9b6b07370c2f6b0760b5dd6bc39cae6741645a46e8fa0891",
     "d401aa6686460d0cd36059fa0a1a05afd876414a8b4c632694e9d829e4ce5f95"},
    // macOS has no portable serial source, so the cell is empty on both
    // sides. Empty is the rule, not a bug: see report.md.
    {"macos-demo", "macos", "Apple/0/0/0", "", "demo-mac",
     "38ac35734aedf6623c2c30c4513dbffd08f54e3442af7a51a75a5b8ad68024de",
     "cde8543cfbfde4028579b96b39ee34cdd7bc2ca612c3c3b7eb67388014ceab3f"},
    // Every cell empty: the shape is still the four keys in order, and the
    // digests are still defined.
    {"empty-demo", "", "", "", "",
     "c93a0514938359dddef7414908ff15348d058d8fed1cd0a238bd2e72357d065c",
     "0aea56c49b250fecbfe6c5cb4099987572519e5da82620eaf743be882815e684"},
};
// @@end machine-facts-vectors

TEST(MachineFacts, RuleVectorMachineCodesRecompute) {
  for (const RuleVector& vector : kRuleVectors) {
    MachineFacts facts;
    facts.os = vector.os;
    facts.cpuid = vector.cpuid;
    facts.hddid = vector.hddid;
    facts.computername = vector.computername;
    EXPECT_EQ(DeriveMachineCode(facts), vector.machine_code)
        << "rule vector " << vector.label;
  }
}

TEST(MachineFacts, RuleVectorMachineKeysRecompute) {
  for (const RuleVector& vector : kRuleVectors) {
    std::string key;
    ASSERT_TRUE(DeriveMachineKey(vector.machine_code, &key))
        << "rule vector " << vector.label;
    EXPECT_EQ(test_util::BytesToHex(key), vector.k_machine)
        << "rule vector " << vector.label;
  }
}

TEST(MachineFacts, RuleVectorsCoverEveryPlatformTag) {
  // The eight-cell table has one `os` cell per platform save the ones the
  // client cannot produce; the vector group must exercise all three tags
  // plus the empty row, or a platform's cell could drift unnoticed.
  for (const char* tag : {"linux", "windows", "macos", ""}) {
    bool found = false;
    for (const RuleVector& vector : kRuleVectors) {
      found = found || std::string(vector.os) == tag;
    }
    EXPECT_TRUE(found) << "no rule vector for os=\"" << tag << "\"";
  }
}

}  // namespace
}  // namespace lengsuo::fp
