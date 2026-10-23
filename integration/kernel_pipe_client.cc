#include "kernel_pipe_client.h"

#include <errno.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/cpu.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "lengsuo/native/action_table.h"
#include "lengsuo/native/machine_facts.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace lengsuo {
namespace {

// The host name, as the OS reports it. Empty when the platform will not say.
std::string HostName() {
#if BUILDFLAG(IS_WIN)
  wchar_t buffer[256];
  DWORD size = static_cast<DWORD>(std::size(buffer));
  if (GetComputerNameW(buffer, &size)) {
    return base::WideToUTF8(base::StringPieceW(buffer, size));
  }
  return "";
#else
  char buffer[256];
  if (gethostname(buffer, sizeof(buffer)) != 0) {
    return "";
  }
  buffer[sizeof(buffer) - 1] = '\0';
  return std::string(buffer);
#endif
}

// Which platform this build is for. The tag is the platform *kind*, not its
// version: a patch release must not change the machine code, or every key
// already bound stops opening.
fp::Platform BuildPlatform() {
#if BUILDFLAG(IS_WIN)
  return fp::Platform::kWindows;
#elif BUILDFLAG(IS_MAC)
  return fp::Platform::kMacos;
#elif BUILDFLAG(IS_LINUX)
  return fp::Platform::kLinux;
#else
  return fp::Platform::kOther;
#endif
}

#if BUILDFLAG(IS_WIN)

// The storage device's serial number, on Windows.
//
// This is the two-pass query the storage driver needs:
//
//   1. ask with the 12-byte STORAGE_PROPERTY_QUERY and a 16-byte output
//      buffer. The driver does not answer ERROR_INSUFFICIENT_BUFFER; it
//      answers *success* and writes the real descriptor size into the
//      header's Size field.
//   2. allocate that many bytes, ask again, and read the serial at
//      buf + SerialNumberOffset.
//
// The three things the old code got wrong, each of which is a separate
// step here:
//
//   * it passed no input buffer, so the driver refused the call with
//     ERROR_BAD_LENGTH (24). The input is a STORAGE_PROPERTY_QUERY, and it
//     must be 12 bytes: the struct ends in AdditionalParameters[1], and an
//     8-byte form is refused the same way.
//   * it passed a 36-byte output buffer while the real descriptor is 396
//     bytes, so the serial offset (90) pointed past the end.
//   * it read SerialNumberOffset as a pointer. It is a byte offset into
//     the buffer.
//
// The driver pads the serial with leading spaces, so the value is trimmed.
std::string WindowsStorageSerial() {
  static_assert(sizeof(STORAGE_PROPERTY_QUERY) == 12,
                "the storage query must be 12 bytes; the driver rejects "
                "an 8-byte form with ERROR_BAD_LENGTH");
  static_assert(offsetof(STORAGE_DEVICE_DESCRIPTOR, Size) == 4,
                "the Size field is not where the reader expects it");
  static_assert(offsetof(STORAGE_DEVICE_DESCRIPTOR, SerialNumberOffset) == 24,
                "the SerialNumberOffset field is not where the reader "
                "expects it");

  const wchar_t kRoot[] = L"\\\\.\\PhysicalDrive0";
  HANDLE handle = ::CreateFileW(kRoot, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return "";
  }

  STORAGE_PROPERTY_QUERY query = {};
  query.PropertyId = StorageDeviceProperty;
  query.QueryType = PropertyStandardQuery;
  query.AdditionalParameters[0] = 0;

  // The first pass: a header-sized buffer, to learn the descriptor's size.
  std::array<std::uint8_t, 16> header = {};
  DWORD returned = 0;
  bool got = ::DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query,
                               sizeof(query), header.data(),
                               static_cast<DWORD>(header.size()), &returned,
                               nullptr);
  std::uint32_t descriptor_size = 0;
  if (!got ||
      !fp::ParseStorageDescriptorSize(header.data(), header.size(),
                                       &descriptor_size) ||
      descriptor_size < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
    ::CloseHandle(handle);
    return "";
  }

  // The second pass: a buffer of the size the driver named.
  std::vector<std::uint8_t> descriptor(descriptor_size);
  got = ::DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query,
                          sizeof(query), descriptor.data(),
                          static_cast<DWORD>(descriptor.size()), &returned,
                          nullptr);
  ::CloseHandle(handle);
  if (!got) {
    return "";
  }
  return fp::ParseStorageDescriptorSerial(descriptor.data(),
                                           descriptor.size());
}

#else

// The storage device's serial numbers, on POSIX.
//
// The rule is the client's, word for word: scan every block device's
// serial file, trim, drop the empty ones, sort, take the first. The sort
// is what makes a multi-disk machine agree across the two repositories:
// the directory order is not the same on both sides, so "the first one that
// answers" would pick two different disks.
//
// Every entry under /sys/block is tried, whatever its type: the entries
// are symlinks into sysfs, and a type filter would miss them.
std::vector<std::string> PosixStorageSerials() {
  std::vector<std::string> serials;
  DIR* directory = ::opendir("/sys/block");
  if (directory == nullptr) {
    return serials;
  }
  while (const struct dirent* entry = ::readdir(directory)) {
    const std::string name(entry->d_name);
    if (name == "." || name == "..") {
      continue;
    }
    const base::FilePath path(
        base::FilePath("/sys/block")
            .AppendASCII(name.c_str())
            .AppendASCII("/device/serial"));
    std::string serial;
    if (!base::ReadFileToString(path, &serial)) {
      continue;
    }
    serials.push_back(std::move(serial));
  }
  ::closedir(directory);
  return serials;
}

#endif  // BUILDFLAG(IS_WIN)

// The storage device's serial number: the fourth fact.
//
// It is the machine's storage identity, not a fingerprint and not a volume
// handle. The old Linux value was st_dev, the device number of the boot
// filesystem: that changes when the machine is re-imaged or repartitioned,
// so it is not a machine binding (and the client side never produced it
// anyway, so the two sides could not have agreed).
//
// A machine with no readable serial gets an empty value rather than a
// made-up one. The machine code then differs from the runner's expectation
// and the handshake fails, which is the fail-closed outcome; inventing a
// value would be worse, because it would let a machine that was never bound
// to this key open the envelope.
std::string StorageIdentifier() {
#if BUILDFLAG(IS_WIN)
  return WindowsStorageSerial();
#else
  return fp::SelectStorageSerial(PosixStorageSerials());
#endif
}

}  // namespace

fp::MachineFacts CollectMachineFacts() {
  fp::MachineFacts facts;

  facts.os = fp::PlatformOsTag(BuildPlatform());

  const base::CPU& cpu = base::CPU::GetInstanceNoAllocation();
  facts.cpuid = base::StrCat({cpu.vendor_name(), "/",
                               base::NumberToString(cpu.family()), "/",
                               base::NumberToString(cpu.model()), "/",
                               base::NumberToString(cpu.stepping())});

  facts.hddid = StorageIdentifier();
  facts.computername = HostName();
  return facts;
}

namespace {

// Reads exactly `count` bytes off a blocking socket.
//
// The loop is here because a pipe is a stream: a read can return fewer bytes
// than asked for even when the peer has more to give. The buffer is a span,
// so the compiler can see the bounds of every read.
bool ReadExactBytes(int fd, size_t count, std::string* out) {
  out->clear();
  out->resize(count);
  base::span<char> buffer(*out);
  while (!buffer.empty()) {
#if BUILDFLAG(IS_WIN)
    DWORD got = 0;
    if (!::ReadFile(reinterpret_cast<HANDLE>(fd), buffer.data(),
                    static_cast<DWORD>(buffer.size()), &got, nullptr)) {
      return false;
    }
    if (got == 0) {
      return false;
    }
    buffer = buffer.subspan(static_cast<size_t>(got));
#else
    const ssize_t got = read(fd, buffer.data(), buffer.size());
    if (got < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (got == 0) {
      return false;
    }
    buffer = buffer.subspan(static_cast<size_t>(got));
#endif
  }
  return true;
}

// Reads a whole frame off a blocking socket.
bool ReadFrameBytes(int fd, std::string* out) {
  std::string header;
  if (!ReadExactBytes(fd, fp::kKernelPipeHeaderBytes, &header)) {
    return false;
  }
  uint32_t payload_len = 0;
  fp::FrameType type = fp::FrameType::kHello;
  if (!fp::DecodeFrameHeader(header, &payload_len, &type)) {
    return false;
  }
  std::string payload;
  if (!ReadExactBytes(fd, payload_len, &payload)) {
    return false;
  }
  out->assign(std::move(header));
  out->append(payload);
  return true;
}

// Writes every byte of `data` to the pipe.
bool WriteAllBytes(int fd, std::string_view data) {
  base::span<const char> buffer(data);
  while (!buffer.empty()) {
#if BUILDFLAG(IS_WIN)
    DWORD put = 0;
    if (!::WriteFile(reinterpret_cast<HANDLE>(fd), buffer.data(),
                     static_cast<DWORD>(buffer.size()), &put, nullptr)) {
      return false;
    }
    if (put == 0) {
      return false;
    }
    buffer = buffer.subspan(static_cast<size_t>(put));
#else
    const ssize_t put = write(fd, buffer.data(), buffer.size());
    if (put < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (put == 0) {
      return false;
    }
    buffer = buffer.subspan(static_cast<size_t>(put));
#endif
  }
  return true;
}


}  // namespace

// static
base::ScopedFD KernelPipeClient::OpenEndpoint(std::string_view pipe_name) {
  const std::string endpoint = std::string(kKernelPipeEndpointPrefix) +
                               std::string(pipe_name);
#if BUILDFLAG(IS_WIN)
  // \\.\pipe\lengsuo-kernel-<name>
  const std::wstring path = L"\\\\.\\pipe\\" +
                            base::UTF8ToWide(endpoint);
  HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                0 /* no sharing: the pipe is ours */, nullptr,
                                OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return base::ScopedFD();
  }
  return base::ScopedFD(reinterpret_cast<int>(handle));
#else
  // An abstract-namespace unix socket: "\0lengsuo-kernel-<name>".
  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return base::ScopedFD();
  }
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  // The abstract namespace is entered by a leading NUL. The path is not in
  // the filesystem, so nothing is left behind when the socket closes.
  base::span<char> path(addr.sun_path);
  if (endpoint.size() + 1 > path.size()) {
    close(fd);
    return base::ScopedFD();
  }
  path[0] = '\0';
  std::copy(endpoint.begin(), endpoint.end(), path.begin() + 1);
  socklen_t len = static_cast<socklen_t>(
      offsetof(struct sockaddr_un, sun_path) + 1 + endpoint.size());
  if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), len) != 0) {
    close(fd);
    return base::ScopedFD();
  }
  return base::ScopedFD(fd);
#endif
}

KernelPipeClient::KernelPipeClient() = default;

KernelPipeClient::~KernelPipeClient() {
  {
    base::AutoLock guard(queue_lock_);
    stop_ = true;
  }
  queue_cv_.Signal();
  if (sender_) {
    sender_->Join();
  }
}

// The sender thread: a plain OS thread that drains the queue and writes the
// frames. It holds a pointer to the client, which outlives it because the
// client's destructor joins the thread before the client is destroyed.
KernelPipeClient::SenderThread::SenderThread(base::raw_ptr<KernelPipeClient> client)
    : base::SimpleThread("lengsuo-kernel-pipe"), client_(client) {}

KernelPipeClient::SenderThread::~SenderThread() = default;

void KernelPipeClient::SenderThread::Run() {
  client_->RunSender();
}

namespace {

// The process's pipe client. A raw pointer, not a unique_ptr: the client
// outlives every static destructor that might still want to queue an event,
// and the browser exits the process anyway.
KernelPipeClient* g_client = nullptr;

}  // namespace

// static
KernelPipeClient* KernelPipeClient::Get() {
  return g_client;
}

// static
std::string KernelPipeClient::PipeNameFromCommandLine(
    const base::CommandLine& command_line) {
  return command_line.GetSwitchValueASCII(kKernelPipeSwitch);
}

// static
bool KernelPipeClient::Initialize(std::string_view pipe_name,
                                  const fp::MachineFacts& machine_facts,
                                  int64_t now_unix) {
  if (g_client != nullptr) {
    // Already set up. Initializing twice would open a second pipe and start
    // a second sender thread.
    return true;
  }
  if (pipe_name.empty()) {
    // Not requested. The browser runs with its normal configuration; this is
    // not a failure.
    return true;
  }

  std::unique_ptr<KernelPipeClient> client(new KernelPipeClient());
  client->fd_ = OpenEndpoint(pipe_name);
  if (!client->fd_.is_valid()) {
    LOG(ERROR) << "Lengsuo kernel pipe: cannot open the endpoint";
    return false;
  }

  // The Hello nonce is 8 random bytes. It goes into the AAD of the
  // envelope, which is what stops an envelope from being replayed into
  // another handshake.
  const std::string hello_nonce = base::RandBytesAsString(
      fp::kKernelPipeHelloNonceBytes);
  std::string hello_frame;
  if (!client->handshake_.SendHello(hello_nonce, &hello_frame)) {
    LOG(ERROR) << "Lengsuo kernel pipe: cannot build the Hello frame";
    return false;
  }
  if (!WriteAllBytes(client->fd_.get(), hello_frame)) {
    LOG(ERROR) << "Lengsuo kernel pipe: cannot send the Hello frame";
    return false;
  }

  // The runner answers with the KeyEnvelope. Anything else is a broken
  // protocol, and a broken protocol is a startup failure.
  fp::Frame frame;
  if (!client->ReadFrame(&frame)) {
    LOG(ERROR) << "Lengsuo kernel pipe: cannot read the KeyEnvelope";
    return false;
  }
  if (frame.type != fp::FrameType::kKeyEnvelope) {
    LOG(ERROR) << "Lengsuo kernel pipe: expected a KeyEnvelope";
    return false;
  }

  // k_machine = SHA-256(machine_code). The machine code is this machine's
  // own facts, hashed; it is never a constant and never leaves the process
  // in clear.
  const std::string machine_code = fp::DeriveMachineCode(machine_facts);
  std::string machine_key;
  if (!fp::DeriveMachineKey(machine_code, &machine_key)) {
    LOG(ERROR) << "Lengsuo kernel pipe: cannot derive the machine key";
    return false;
  }

  // The AAD is the envelope frame's own header followed by the Hello nonce.
  std::string error;
  if (!client->handshake_.AcceptKeyEnvelope(frame, machine_key, now_unix,
                                            &error)) {
    // The error text carries no payload bytes and no key. It is logged
    // because a startup failure with no reason is a support problem.
    LOG(ERROR) << "Lengsuo kernel pipe: handshake rejected: " << error;
    return false;
  }

  // The config bundle arrives next and is frozen once.
  fp::Frame config_frame;
  if (!client->ReadFrame(&config_frame)) {
    LOG(ERROR) << "Lengsuo kernel pipe: cannot read the ConfigBundle";
    return false;
  }
  if (config_frame.type != fp::FrameType::kConfigBundle) {
    LOG(ERROR) << "Lengsuo kernel pipe: expected a ConfigBundle";
    return false;
  }
  if (!client->config_.Freeze(config_frame.payload, &error)) {
    LOG(ERROR) << "Lengsuo kernel pipe: the ConfigBundle is malformed";
    return false;
  }

  // Only now does the sender thread start. There is never a thread writing
  // to a pipe whose session key is missing.
  client->sender_ = std::make_unique<KernelPipeClient::SenderThread>(client.get());
  client->sender_->Start();

  g_client = client.release();
  return true;
}

bool KernelPipeClient::ReadFrame(fp::Frame* out) {
  std::string bytes;
  if (!ReadFrameBytes(fd_.get(), &bytes)) {
    return false;
  }
  if (!fp::DecodeFrame(bytes, out)) {
    stats_.decode_failures.fetch_add(1);
    return false;
  }
  return true;
}

bool KernelPipeClient::EnqueueEvent(std::string_view action,
                                    std::string_view payload) {
  // The payload is checked against the action's schema before it is queued.
  // A payload that does not fit is dropped and counted, never sent.
  const fp::ActionCollectorPolicy policy;
  std::string named;
  const fp::ActionValidation validation = fp::ValidateActionPayload(
      fp::ActionTable::Get(), action, payload, policy, &named);
  if (!validation.ok) {
    stats_.rejected_events.fetch_add(1);
    return false;
  }
  {
    base::AutoLock guard(queue_lock_);
    if (stop_ || queue_.size() >= kMaxQueuedEvents) {
      stats_.send_failures.fetch_add(1);
      return false;
    }
    queue_.emplace_back(named, std::string(payload));
  }
  queue_cv_.Signal();
  return true;
}

void KernelPipeClient::RunSender() {
  while (true) {
    std::vector<std::pair<std::string, std::string>> batch;
    {
      base::AutoLock guard(queue_lock_);
      // base's ConditionVariable takes the lock it is used with, so the
      // guard is held across the wait and released inside it.
      while (!stop_ && queue_.empty()) {
        queue_cv_.Wait();
      }
      if (stop_ && queue_.empty()) {
        return;
      }
      batch.swap(queue_);
    }
    for (const auto& event : batch) {
      std::string frame;
      if (!fp::EncodeFrame(fp::FrameType::kEvent, event.second, &frame)) {
        stats_.send_failures.fetch_add(1);
        continue;
      }
      if (!WriteAllBytes(fd_.get(), frame)) {
        // The pipe is gone. The events are lost and counted; the browser
        // carries on. The uplink is lossy by design.
        stats_.send_failures.fetch_add(1);
        return;
      }
    }
  }
}

}  // namespace lengsuo
