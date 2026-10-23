// kernel_pipe_client - the browser side of the runner <-> kernel pipe
// (task Q).
//
// The pure half of the pipe lives in lengsuo/native/kernel_pipe.{h,cc}: the
// frame codec, the fail-closed handshake and the frozen config bundle. This
// file is the half that needs the operating system: it opens the endpoint,
// runs the handshake, and owns the uplink queue and its sender thread.
//
// Why the split: the pure half must compile standalone (plain g++, no
// Chromium headers) so the gtest target can check the bytes. The half here
// cannot: it needs sockets, threads and platform facts.
//
// Two rules shape everything below.
//
//   * Fail closed. A pipe that does not open, a Hello the runner rejects, an
//     envelope that does not authenticate - each of them is a startup failure,
//     never a silent "no configuration". A browser that keeps running with
//     the wrong fingerprint is worse than a browser that refuses to start.
//
//   * The uplink never blocks the browser. Events go into a queue; a
//     dedicated thread drains the queue and writes. If the pipe is dead the
//     events are dropped and counted. Nothing on the browser's own threads
//     ever waits for the runner.
#ifndef LENGSUO_INTEGRATION_KERNEL_PIPE_CLIENT_H_
#define LENGSUO_INTEGRATION_KERNEL_PIPE_CLIENT_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/command_line.h"
#include "base/files/scoped_file.h"
#include "base/memory/raw_ptr.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/threading/simple_thread.h"
#include "lengsuo/native/kernel_pipe.h"

namespace lengsuo {

// The command-line switch that carries the pipe name. The name only - never
// a key, never a configuration payload (wire contract section 1).
inline constexpr char kKernelPipeSwitch[] = "lengsuo-kernel-pipe";

// The prefix the runner and the kernel agree on for the OS endpoint.
//
// Pinned by docs/kernel-wire-contract.md section 1:
//
//   Windows: \\.\pipe\lengsuo-kernel-<name>
//   POSIX:   an abstract-namespace unix socket, "\0lengsuo-kernel-<name>"
//
// The abstract namespace is used on POSIX because it leaves no socket file
// behind: nothing to clean up, and no directory permission to get wrong.
// A filesystem path is explicitly forbidden by the contract - the two sides
// would disagree about it whenever their working directories differ.
inline constexpr char kKernelPipeEndpointPrefix[] = "lengsuo-kernel-";

// Collects this machine's four facts: {os, cpuid, hddid, computername}.
//
// The wire contract fixes the shape and the key order, not the values: the
// values have to be this machine's real ones (wire contract section 1).
// The machine code is the hash of these four, so a wrong value means the
// runner's envelope does not open, which means the handshake fails.
//
//   os            the operating system's name and version
//   cpuid         the processor's vendor, family, model and stepping
//   hddid         a stable identifier of the boot volume
//   computername  the machine's name
//
// The facts are hashed and never leave the process in clear. They are not
// fingerprint values in the sense of docs/fp-switch-contract.md, so they do
// not go through the switch contract; they are a machine binding for the
// session key.
fp::MachineFacts CollectMachineFacts();

// The statistics the pipe keeps. Failures are counted, never logged with
// their payload, and never turned into an error the browser has to care
// about (wire contract section 4).
struct KernelPipeStats {
  // Events that were queued but could not be written to the pipe.
  std::atomic<uint64_t> send_failures{0};
  // Events dropped before they reached the pipe, because their payload did
  // not satisfy the action's schema.
  std::atomic<uint64_t> rejected_events{0};
  // Frames received that did not decode.
  std::atomic<uint64_t> decode_failures{0};
};

// The browser's half of the pipe.
//
// Lifecycle: Initialize() opens the endpoint and runs the handshake on the
// calling thread (the browser's main thread, during early startup). The
// uplink sender thread starts only after the handshake has succeeded, so
// there is never a thread writing to a pipe whose session key is missing.
//
// The pipe is one per browser process, so the client is a process-level
// singleton: `GetKernelPipeClient()` returns it, and `Initialize()` sets it
// up. The singleton lives here rather than in chrome_browser_main.h because
// that header is upstream code this task does not own.
class KernelPipeClient {
 public:
  // Opens the pipe named by `pipe_name` and runs the handshake, then makes
  // the client the process's pipe.
  //
  // `machine_facts` are this machine's real values: they are the input to
  // the machine code, which is the key the session envelope is sealed with.
  // A wrong machine code means the envelope does not open, which means the
  // handshake fails, which means startup fails. There is no fallback.
  //
  // `now_unix` is the current Unix time in seconds; the envelope carries an
  // expiry and an expired one is refused.
  //
  // Returns true when the pipe is ready, or when it was not requested at
  // all (no switch on the command line - that is not a failure). Returns
  // false when the pipe was requested but the handshake failed.
  static bool Initialize(std::string_view pipe_name,
                         const fp::MachineFacts& machine_facts,
                         int64_t now_unix);

  // The process's pipe client, or nullptr when the pipe was not requested.
  //
  // The pointer is not const: the uplink is the only way out of the browser
  // and it is the caller's job to use it. The client itself is a process
  // singleton, so there is one of it and everybody gets the same one.
  static KernelPipeClient* Get();

  // Reads the pipe name off a command line. Returns an empty string when
  // the switch is absent.
  static std::string PipeNameFromCommandLine(const base::CommandLine& command_line);

  ~KernelPipeClient();

  KernelPipeClient(const KernelPipeClient&) = delete;
  KernelPipeClient& operator=(const KernelPipeClient&) = delete;

  // The frozen configuration bundle. Empty when the pipe was not requested.
  //
  // The bundle is frozen once (the L3 "one-shot" rule) and never re-read.
  const std::string& config() const { return config_.json(); }
  bool config_frozen() const { return config_.frozen(); }

  // Queues one uplink event. The payload must already satisfy the action's
  // schema - see lengsuo/native/action_table.h.
  //
  // Returns false when the payload was rejected (and counted) or the queue
  // is full (and counted). It never blocks and never fails the caller.
  bool EnqueueEvent(std::string_view action, std::string_view payload);

  // The counters.
  const KernelPipeStats& stats() const { return stats_; }

  // The container id the runner named in the envelope. Empty when the pipe
  // was not requested.
  const std::string& container_id() const { return handshake_.container_id(); }

 private:
  KernelPipeClient();

  // Opens the endpoint. Returns an invalid handle on failure.
  static base::ScopedFD OpenEndpoint(std::string_view pipe_name);

  // Reads one frame off the pipe. Returns false when the frame does not
  // decode (and counts the failure).
  bool ReadFrame(fp::Frame* out);

  // The sender thread's body: drains the queue and writes the frames.
  void RunSender();

  fp::KernelPipeHandshake handshake_;
  fp::KernelPipeConfig config_;

  // The connection. Closed on destruction; the sender thread holds a
  // reference to the object, so the object outlives the thread.
  base::ScopedFD fd_;

  // The uplink queue. Bounded on purpose: an unbounded queue turns a dead
  // runner into a memory leak in the browser.
  static constexpr size_t kMaxQueuedEvents = 1024;
  std::vector<std::pair<std::string, std::string>> queue_;
  base::Lock queue_lock_;
  // The condition variable is bound to the queue's lock: base's
  // ConditionVariable takes the lock it is used with.
  base::ConditionVariable queue_cv_{&queue_lock_};

  // Set when the sender thread should stop.
  bool stop_ = false;

  KernelPipeStats stats_;

  // The sender thread. Started only after the handshake succeeded.
  //
  // It is a plain OS thread with no message loop: it does blocking writes
  // and nothing else, and a message loop would only give it tasks to run
  // that it does not want.
  //
  // The thread type is defined here, not in the .cc, because the
  // unique_ptr needs a complete type where it is destroyed.
  class SenderThread : public base::SimpleThread {
   public:
    explicit SenderThread(base::raw_ptr<KernelPipeClient> client);
    ~SenderThread() override;

    void Run() override;

   private:
    base::raw_ptr<KernelPipeClient> client_;
  };
  std::unique_ptr<SenderThread> sender_;
};

}  // namespace lengsuo

#endif  // LENGSUO_INTEGRATION_KERNEL_PIPE_CLIENT_H_
