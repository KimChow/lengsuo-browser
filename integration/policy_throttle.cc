// policy_throttle - the Chromium half (task 0018).
//
// The pure half of the decision is inline in policy_throttle.h, because
// tools/rebuild/run_l3_gtest.sh compiles that header with a plain g++ and no
// Chromium headers at all. This file is the half that cannot be tested that
// way: it derives from blink::URLLoaderThrottle, reads the frozen
// ConfigBundle off the process pipe, and puts the decision on the uplink.
//
// Three things this file is careful about:
//
//   * It does not decide anything itself. ReadAccessPolicy,
//     PolicyHasRules and DecidePolicy are the header's, so the gtest covers
//     the decision and this file only carries it out.
//
//   * It never blocks. WillStartRequest runs on the browser's UI thread, and
//     everything it touches is local: the bundle is already in the process
//     and EnqueueEvent is a lock-and-signal, not a write. A policy that had
//     to wait for the runner would have to defer and resume asynchronously;
//     this one does not, so it does not.
//
//   * It mounts nothing when there is nothing to enforce. CreatePolicyThrottle
//     returns nullptr for "no pipe", "no section" and "no rules", so a
//     switched-off policy is not a throttle that always says allow.
#include "lengsuo/integration/policy_throttle.h"

#include <memory>
#include <utility>

#include "lengsuo/integration/kernel_pipe_client.h"
#include "net/base/net_error_list.h"
#include "services/network/public/cpp/resource_request.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"

namespace lengsuo {
namespace {

// The error a cancelled request carries.
//
// It is the browser's ordinary "this request was blocked on purpose" net
// error - the same one an extension's declarativeNetRequest block produces -
// so a page sees a blocked navigation rather than a network failure. The
// policy's own reason travels separately, on the uplink report; this code is
// what the page gets.
constexpr int kBlockedError = net::ERR_BLOCKED_BY_CLIENT;

// The access policy as a request-path hook.
//
// The policy is copied in at construction and read-only afterwards, so the
// decision needs no lock: the throttle is created on the UI thread, lives on
// it, and is only ever called on it.
class PolicyThrottle : public blink::URLLoaderThrottle {
 public:
  explicit PolicyThrottle(fp::AccessPolicy policy)
      : policy_(std::move(policy)) {}
  PolicyThrottle(const PolicyThrottle&) = delete;
  PolicyThrottle& operator=(const PolicyThrottle&) = delete;
  ~PolicyThrottle() override = default;

  // Called once, before the resource request is started.
  //
  // `defer` is left alone: the decision is made here and now, so the request
  // is either cancelled or allowed to continue without any waiting.
  void WillStartRequest(network::ResourceRequest* request,
                        bool* /*defer*/) override {
    if (request == nullptr) {
      return;
    }
    const fp::PolicyRequest policy_request = BuildPolicyRequest(
        request->url.spec(), request->url.host(), request->url.scheme(),
        request->is_outermost_main_frame ? fp::PolicyFrame::kMainFrame
                                         : fp::PolicyFrame::kSubFrame);
    const PolicyOutcome outcome = DecidePolicy(policy_, policy_request);

    // The report goes out even when the request is allowed: the point of the
    // policy is to say what it decided, and "allowed by this rule" is a
    // decision. A request no rule matched is not reported at all.
    if (outcome.report) {
      if (KernelPipeClient* client = KernelPipeClient::Get()) {
        // Non-blocking by contract: it validates the payload and drops it
        // into a bounded queue that a sender thread drains.
        client->EnqueueEvent(fp::kActionPolicyEvent, outcome.payload);
      }
    }

    if (outcome.cancel) {
      // Cancel before the transaction starts: no connection, no DNS, no
      // bytes. This is the one place the policy changes what the browser
      // does, and it does it in-band, which the Delegate's contract allows.
      delegate_->CancelWithError(kBlockedError);
    }
  }

 private:
  fp::AccessPolicy policy_;
};

}  // namespace

std::unique_ptr<blink::URLLoaderThrottle> CreatePolicyThrottle() {
  KernelPipeClient* client = KernelPipeClient::Get();
  if (client == nullptr) {
    // No pipe was requested, so the platform sent no configuration. There is
    // no policy to enforce and nothing to mount.
    return nullptr;
  }
  fp::AccessPolicy policy;
  if (!ReadAccessPolicy(client->config(), &policy)) {
    // The bundle does not carry a readable policy. An empty policy refuses
    // nothing, so mount nothing - never a throttle that blocks everything
    // because the configuration could not be read.
    return nullptr;
  }
  if (!PolicyHasRules(policy)) {
    // No rules means no request could ever be decided. Mounting a throttle
    // for it would be a cost on every request with no benefit.
    return nullptr;
  }
  return std::make_unique<PolicyThrottle>(std::move(policy));
}

}  // namespace lengsuo