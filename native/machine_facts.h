// machine_facts - the collection rules for the kernel's four machine facts
// (task M0).
//
// The wire contract fixes the *shape* of the machine code (four fields, one
// key order) but not what each field holds. Task M0 pins the values: the
// kernel side and the client side must collect the same four values on the
// same machine, or the two sides derive different machine keys and the
// handshake fails closed.
//
// The rules live here, apart from the Chromium-bound collector in
// integration/kernel_pipe_client.cc, because they are pure: given a set of
// candidates they say which one wins. That is what makes them testable
// without a machine - the tests feed them fake data.
//
// The rule table (four fields, two platforms). Both repos implement this
// table word for word; a cell that cannot be filled stays empty, and an
// empty cell makes the two sides' machine codes differ, which is the
// fail-closed outcome.
//
//   field          Windows                          POSIX
//   -------------  ------------------------------  ------------------------------
//   os             "windows"                        "linux" / "macos"
//   cpuid          base::CPU vendor/family/model/    the same
//                  stepping                         base::CPU shape
//   hddid          the serial number of             scan
//                  \\.\PhysicalDrive0               /sys/block/*/device/serial,
//                  (trimmed)                        trimmed, sorted, first
//   computername   GetComputerNameW                 gethostname()
#ifndef LENGSUO_NATIVE_MACHINE_FACTS_H_
#define LENGSUO_NATIVE_MACHINE_FACTS_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lengsuo::fp {

// The platforms the two repositories support.
//
// The platform is an argument rather than a compile-time constant so the
// mapping of every cell is testable from any host: the test asks for each
// platform and gets the tag the rule table names.
enum class Platform {
  kWindows,
  kMacos,
  kLinux,
  // A platform neither side supports.
  kOther,
};

// The platform tag: "windows", "macos" or "linux".
//
// Lower case, no version: the tag says which *kind* of machine this is, so
// every machine of a kind agrees on it. A version would change the machine
// code when the browser is patched, and every key already bound would stop
// opening.
//
// kOther returns the empty string: the machine code then differs from the
// runner's expectation and the handshake fails, which is the fail-closed
// outcome. (The client side uses the same three-value mapping; it is not
// Rust's OS name, which would differ on a platform the two repos do not
// support.)
std::string PlatformOsTag(Platform platform);

// Picks the storage serial number out of the candidates.
//
// The rule is the client's, copied word for word: trim each candidate, drop
// the empty ones, sort what is left byte by byte, take the first.
//
// The sort is what makes the choice the same on a multi-disk machine. The
// directory order is not the same on both sides, so "the first one that
// answers" would pick two different disks.
std::string SelectStorageSerial(const std::vector<std::string>& candidates);

// The offset of the Size field inside STORAGE_DEVICE_DESCRIPTOR. It is a
// DWORD (STORAGE_DEVICE_DESCRIPTOR: Version, Size, DeviceType, ...).
inline constexpr std::size_t kStorageDescriptorSizeOffset = 4;

// The offset of the SerialNumberOffset field inside
// STORAGE_DEVICE_DESCRIPTOR. It is a DWORD, not a byte: the header is
// Version, Size, DeviceType, DeviceTypeModifier, RemovableMedia,
// CommandQueueing, VendorIdOffset, ProductIdOffset, ProductRevisionOffset,
// SerialNumberOffset, BusType, RawPropertiesLength.
//
// integration/kernel_pipe_client.cc static_asserts both offsets against
// the real Windows struct, so a platform whose header disagrees fails the
// build instead of reading the wrong four bytes.
inline constexpr std::size_t kStorageDescriptorSerialNumberFieldOffset = 24;

// Reads the Size field of a storage descriptor header.
//
// The Windows driver answers a storage-property query with success even when
// the buffer is too small: it writes the real size into the header instead
// of failing with ERROR_INSUFFICIENT_BUFFER. So the collector asks with a
// small buffer, reads the size here, and asks again with a buffer of that
// size.
bool ParseStorageDescriptorSize(const std::uint8_t* buffer,
                                std::size_t size,
                                std::uint32_t* size_out);

// Reads the serial number out of a storage descriptor.
//
// The descriptor carries the serial as a NUL terminated string at the offset
// the header names; the offset is a position in the buffer, not an address.
// The driver pads the serial with leading spaces, so the value is trimmed
// before it is used.
//
// A descriptor with no serial, an offset that points outside the buffer, and
// a truncated buffer all yield the empty string.
std::string ParseStorageDescriptorSerial(const std::uint8_t* buffer,
                                         std::size_t size);

// Trims ASCII whitespace off both ends.
std::string TrimAscii(std::string_view text);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_MACHINE_FACTS_H_
