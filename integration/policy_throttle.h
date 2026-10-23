// policy_throttle - the access policy, enforced on the request path
// (task 0018).
//
// native/policy_rules.h reads the rules, native/policy_match.h matches a
// request against them, native/policy_block.h decides, and
// native/policy_report.h builds the uplink payload. All four were in the
// kernel already and none of them had a caller: the policy was a library
// with no driver. This file is the driver. It is a URLLoaderThrottle that
// decides every request before it leaves the browser and tells the runner
// what it decided.
//
// Why this layer. "The request does not go out" is the whole point of
// policy_block (see its header), and WillStartRequest plus
// Delegate::CancelWithError is cancel-before-send. A response filter or a
// renderer-side check would both be too late: by the time either of them
// looks, the bytes have already left the machine.
//
// Where the rules come from. The ConfigBundle (frame type 0x03) the runner
// sent, which the browser process already holds: KernelPipeClient::Get()
// owns the frozen bundle, and the throttle reads the `access_policy`
// section out of it. There is no second configuration channel and no new
// command-line switch - a policy that is visible in the process list is a
// policy the platform leaked.
//
// Where the split is. The top half of this header is pure logic: it takes a
// bundle and a request and answers with a decision, so
// tools/rebuild/run_l3_gtest.sh compiles and tests it with a plain g++, no
// Chromium headers involved. The bottom half is one declaration, and the
// Chromium types live in the .cc, exactly as task O did it with
// l3_provisioning_impl.h.
//
// Threading. CreateURLLoaderThrottles runs on the browser's UI thread
// (chrome/browser/chrome_content_browser_client.cc has
// DCHECK_CURRENTLY_ON(BrowserThread::UI) at the top of it), so both the
// factory and every callback below run there. Nothing here waits on
// anything: the bundle is already in the process, the decision is string
// arithmetic, and the uplink is a non-blocking enqueue
// (kernel_pipe_client.h). There is no defer/Resume pair because there is
// nothing to wait for.
#ifndef LENGSUO_INTEGRATION_POLICY_THROTTLE_H_
#define LENGSUO_INTEGRATION_POLICY_THROTTLE_H_

#include <memory>
#include <string>
#include <string_view>

#include "lengsuo/native/policy_block.h"
#include "lengsuo/native/policy_match.h"
#include "lengsuo/native/policy_report.h"
#include "lengsuo/native/policy_rules.h"

namespace blink {
class URLLoaderThrottle;
}  // namespace blink

namespace lengsuo {

// Reads the `access_policy` section out of a frozen ConfigBundle.
//
// Returns false only when the bundle or its section cannot be read. A bundle
// that carries no `access_policy` section is a success with an empty policy:
// the platform did not ask for one, and an empty policy refuses nothing.
// policy_rules.h states the rule this follows - "a browser that blocks
// everything because the runner is not up yet is a broken browser".
//
// On failure `*out` is left as an empty policy, so the caller that ignores
// the return value still ends up mounting nothing.
inline bool ReadAccessPolicy(std::string_view bundle_json,
                             fp::AccessPolicy* out) {
  return fp::ParseAccessPolicy(bundle_json, out, nullptr);
}

// True when the policy carries at least one rule that could fire.
//
// A policy with no rules cannot decide anything: every request would come
// back "no match", no request would be cancelled and nothing would be
// reported. Mounting a throttle for it would cost a virtual call on every
// request in the browser and buy nothing, so the factory does not.
inline bool PolicyHasRules(const fp::AccessPolicy& policy) {
  return !policy.black_list.empty() || !policy.white_list.empty() ||
         !policy.exception_list.empty() || !policy.black_schemes.empty() ||
         !policy.main_frame_list.empty() ||
         !policy.sub_frame_black_list.empty() ||
         !policy.main_frame_rules.black.empty() ||
         !policy.main_frame_rules.white.empty() ||
         !policy.sub_frame_rules.black.empty() ||
         !policy.sub_frame_rules.white.empty() ||
         !policy.domain_black_list.empty() ||
         !policy.domain_check_list.empty();
}

// The request the policy is asked about.
//
// The URL, the host and the scheme are handed in rather than parsed here:
// the browser already has them (GURL::spec/host/scheme), and parsing a URL
// is the browser's job, not a rule engine's.
inline fp::PolicyRequest BuildPolicyRequest(std::string_view url,
                                            std::string_view host,
                                            std::string_view scheme,
                                            fp::PolicyFrame frame) {
  fp::PolicyRequest request;
  request.url = std::string(url);
  request.host = std::string(host);
  request.scheme = std::string(scheme);
  request.frame = frame;
  return request;
}

// What the throttle does about one request.
struct PolicyOutcome {
  // The request is stopped before it is sent. Set when, and only when, the
  // decision is a cancel.
  bool cancel = false;
  // The decision goes on the uplink. False when no rule matched.
  //
  // "No rule matched" is not a decision, it is the absence of one, so it is
  // not reported: a report per request the browser makes would flood the
  // uplink with lines that all say the policy had no opinion.
  bool report = false;
  // The decision itself. `matched_rule` points into the policy that was
  // evaluated, so that policy has to outlive the outcome.
  fp::PolicyDecision decision;
  // The `PolicyEvent` payload, built only when `report` is true. Empty
  // otherwise, so nothing builds a payload it does not send.
  std::string payload;
};

// Decides one request out of a policy that has already been read.
//
// This is the whole of what the throttle does to a request, spelled out as
// a pure function so the gtest can pin it: a block hit cancels, an allow hit
// does not, and a request no rule matched is neither cancelled nor reported.
inline PolicyOutcome DecidePolicy(const fp::AccessPolicy& policy,
                                  const fp::PolicyRequest& request) {
  PolicyOutcome outcome;
  outcome.decision = fp::EvaluatePolicy(policy, request);
  outcome.cancel = outcome.decision.action == fp::PolicyAction::kCancelRequest;
  outcome.report = outcome.decision.reason != fp::PolicyReason::kNoMatch;
  if (outcome.report) {
    outcome.payload = fp::BuildPolicyEventPayload(request, outcome.decision);
  }
  return outcome;
}

// Creates the throttle, or nullptr when the policy is not worth mounting.
//
// Returns nullptr when no pipe was requested, when the bundle carries no
// `access_policy` section, or when that section has no rules. Those are the
// ordinary "no policy" cases and they must cost nothing, so the caller
// mounts nothing rather than mounting a throttle that decides "allow" on
// every request.
//
// The throttle is a blink::URLLoaderThrottle; that type is Chromium's and is
// therefore only named here, never included.
std::unique_ptr<blink::URLLoaderThrottle> CreatePolicyThrottle();

}  // namespace lengsuo

#endif  // LENGSUO_INTEGRATION_POLICY_THROTTLE_H_