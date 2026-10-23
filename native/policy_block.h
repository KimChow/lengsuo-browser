// policy_block - the interception decision (task T2).
//
// The decision is the whole point of the access policy: for one request, is
// it sent or is it stopped.
//
// There are exactly two answers, and there is no third one:
//
//   kSendRequest     the request goes out
//   kCancelRequest   the request does not go out
//
// "Does not go out" means the request is cancelled before the transaction
// starts - no connection, no DNS, no bytes. It never means "send it and
// withdraw it afterwards", and it never means "answer with an empty
// response". A request that was stopped leaves the machine the way it came.
//
// The order the lists are consulted in is fixed, and it is the same order
// everywhere:
//
//   1. black_schemes      a refused scheme is refused whatever the URL lists
//                         say. The scheme rule is about the transport, not
//                         about the address, and no URL pattern rescues it.
//   2. exception_list     a hit allows. It overrides the white list and the
//                         black list alike.
//   3. white_list         a hit allows.
//   4. the frame's white  a hit allows. The frame's own list is more
//      list                specific than the global one.
//   5. the frame's black  a hit refuses.
//      list
//   6. black_list         a hit refuses.
//   7. domain_black_list  a hit refuses.
//   8. domain_check_list  a hit is watched. The request still goes out; the
//                         decision says it was noticed.
//
// Two rules hold over the whole list: an allow beats a refuse (the white
// lists are consulted before the black ones), and a rule that cannot be
// read is not a rule at all - it was dropped when the policy was read.
//
// One thing is deliberately absent: an empty response. A page that was
// blocked gets an error, not a hollow page that looks like it worked.
#ifndef LENGSUO_NATIVE_POLICY_BLOCK_H_
#define LENGSUO_NATIVE_POLICY_BLOCK_H_

#include <string>
#include <string_view>

#include "policy_match.h"
#include "policy_rules.h"

namespace lengsuo::fp {

// What happens to the request.
enum class PolicyAction {
  kSendRequest,
  kCancelRequest,
};

// Which list decided.
enum class PolicyReason {
  kNoMatch,
  kSchemeBlocked,
  kExceptionMatch,
  kWhiteListMatch,
  kFrameWhiteMatch,
  kFrameBlackMatch,
  kBlackListMatch,
  kDomainBlackMatch,
  kDomainCheckMatch,
};

// The outcome of evaluating the policy for one request.
struct PolicyDecision {
  PolicyAction action = PolicyAction::kSendRequest;
  PolicyReason reason = PolicyReason::kNoMatch;
  // True when the request was watched rather than refused. Only
  // domain_check_list sets it, and only it.
  bool observed = false;
  // The rule that decided, or nullptr when no rule matched. It points into
  // the AccessPolicy that was evaluated, so that policy has to outlive the
  // decision. It is a pointer so that a decision costs no copy of a pattern
  // on the request path.
  const std::string* matched_rule = nullptr;
};

// Decides what happens to one request.
//
// The policy is read-only here and the request is read-only here: nothing
// is changed and nothing is remembered. The decision is the same for the
// same request and the same policy, every time.
PolicyDecision EvaluatePolicy(const AccessPolicy& policy,
                               const PolicyRequest& request);

// The stable name of a reason. These strings go on the wire, so they are
// part of the contract: they do not change with the wording of a comment.
std::string_view PolicyReasonToString(PolicyReason reason);

// The stable name of an action.
std::string_view PolicyActionToString(PolicyAction action);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_POLICY_BLOCK_H_
