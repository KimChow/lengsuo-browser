// policy_report - the uplink payload for a policy decision (task T2).
//
// The policy is enforced in the browser process, so the browser process is
// what tells the runner about it. The report goes over the pipe that
// already carries the uplink events: one frame type, one action table, one
// more name in it. There is no new channel and no new frame type.
//
// The payload carries:
//
//   from      "browser" - the policy is enforced in the browser process
//   action    "PolicyEvent"
//   url       the URL, without the credentials in it
//   category  "access"
//   decision  "block", "allow" or "observe"
//   reason    the list that decided
//   rule      the pattern that matched, when one did
//
// The credentials are taken out of the URL before it is reported. A URL may
// carry a user name and a password, and neither of them belongs in a line
// that leaves the browser.
//
// The builder builds a payload for any decision; it does not decide which
// decisions are worth reporting. That is the caller's decision, and it is
// the caller's to change.
#ifndef LENGSUO_NATIVE_POLICY_REPORT_H_
#define LENGSUO_NATIVE_POLICY_REPORT_H_

#include <string>
#include <string_view>

#include "action_table.h"
#include "policy_block.h"

namespace lengsuo::fp {

// Who the report comes from.
inline constexpr char kPolicyReportFrom[] = "browser";

// The category the access policy reports under.
inline constexpr char kPolicyCategory[] = "access";

// The decisions a report can carry.
inline constexpr char kPolicyDecisionBlock[] = "block";
inline constexpr char kPolicyDecisionAllow[] = "allow";
inline constexpr char kPolicyDecisionObserve[] = "observe";

// Builds the uplink payload for one decision.
//
// The URL is reported without its credentials. The rule is reported only
// when a rule matched.
std::string BuildPolicyEventPayload(const PolicyRequest& request,
                                     const PolicyDecision& decision);

// The URL as it is reported: the request's URL without the credentials.
//
// The credentials are the part between the "://" and the "@" of the
// authority. A URL without a "://" has no authority and is left alone, so a
// "mailto:" URL keeps its address.
std::string StripUrlCredentials(std::string_view url);

// The decision name that goes on the wire.
std::string_view PolicyDecisionName(const PolicyDecision& decision);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_POLICY_REPORT_H_
