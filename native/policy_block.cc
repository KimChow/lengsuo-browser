#include "policy_block.h"

#include <cctype>
#include <vector>

namespace lengsuo::fp {
namespace {

// Compares two strings without regard to case. A scheme is case-insensitive
// and the caller may hand it in either case.
bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    const int left = std::tolower(static_cast<unsigned char>(a[i]));
    const int right = std::tolower(static_cast<unsigned char>(b[i]));
    if (left != right) {
      return false;
    }
  }
  return true;
}

// The first pattern of the list that matches the URL, or nullptr.
const std::string* FirstUrlMatch(const std::vector<std::string>& patterns,
                                 std::string_view url) {
  for (const std::string& pattern : patterns) {
    if (MatchUrlPattern(pattern, url)) {
      return &pattern;
    }
  }
  return nullptr;
}

// The first pattern of the list that matches the host, or nullptr.
const std::string* FirstHostMatch(const std::vector<std::string>& patterns,
                                   std::string_view host) {
  for (const std::string& pattern : patterns) {
    if (MatchDomainPattern(pattern, host)) {
      return &pattern;
    }
  }
  return nullptr;
}

}  // namespace

PolicyDecision EvaluatePolicy(const AccessPolicy& policy,
                               const PolicyRequest& request) {
  PolicyDecision decision;

  // 1. The scheme list stands on its own: a refused scheme is refused
  //    whatever the URL lists say.
  for (const std::string& scheme : policy.black_schemes) {
    if (EqualsIgnoreCase(scheme, request.scheme)) {
      decision.action = PolicyAction::kCancelRequest;
      decision.reason = PolicyReason::kSchemeBlocked;
      decision.matched_rule = &scheme;
      return decision;
    }
  }

  // 2. The exception list overrides the white list and the black list.
  if (const std::string* hit =
          FirstUrlMatch(policy.exception_list, request.url)) {
    decision.reason = PolicyReason::kExceptionMatch;
    decision.matched_rule = hit;
    return decision;
  }

  // 3. The white list allows.
  if (const std::string* hit = FirstUrlMatch(policy.white_list, request.url)) {
    decision.reason = PolicyReason::kWhiteListMatch;
    decision.matched_rule = hit;
    return decision;
  }

  // 4. The frame's own lists. The frame kind decides which ones apply.
  const std::vector<std::string>* frame_black = nullptr;
  const FramePolicyLists* frame_rules = nullptr;
  switch (request.frame) {
    case PolicyFrame::kMainFrame:
      frame_black = &policy.main_frame_list;
      frame_rules = &policy.main_frame_rules;
      break;
    case PolicyFrame::kSubFrame:
      frame_black = &policy.sub_frame_black_list;
      frame_rules = &policy.sub_frame_rules;
      break;
  }

  // 5. The frame's white list allows.
  if (frame_rules != nullptr) {
    if (const std::string* hit =
            FirstUrlMatch(frame_rules->white, request.url)) {
      decision.reason = PolicyReason::kFrameWhiteMatch;
      decision.matched_rule = hit;
      return decision;
    }
  }

  // 6. The frame's black list refuses. There are two of them: the frame
  //    kind's own list, and the black list of the frame_black_white_list
  //    section.
  if (frame_black != nullptr) {
    if (const std::string* hit = FirstUrlMatch(*frame_black, request.url)) {
      decision.action = PolicyAction::kCancelRequest;
      decision.reason = PolicyReason::kFrameBlackMatch;
      decision.matched_rule = hit;
      return decision;
    }
  }
  if (frame_rules != nullptr) {
    if (const std::string* hit = FirstUrlMatch(frame_rules->black, request.url)) {
      decision.action = PolicyAction::kCancelRequest;
      decision.reason = PolicyReason::kFrameBlackMatch;
      decision.matched_rule = hit;
      return decision;
    }
  }

  // 7. The global black list refuses.
  if (const std::string* hit = FirstUrlMatch(policy.black_list, request.url)) {
    decision.action = PolicyAction::kCancelRequest;
    decision.reason = PolicyReason::kBlackListMatch;
    decision.matched_rule = hit;
    return decision;
  }

  // 8. The domain black list refuses.
  if (const std::string* hit =
          FirstHostMatch(policy.domain_black_list, request.host)) {
    decision.action = PolicyAction::kCancelRequest;
    decision.reason = PolicyReason::kDomainBlackMatch;
    decision.matched_rule = hit;
    return decision;
  }

  // 9. The domain check list watches. The request still goes out: this list
  //    says "notice this", not "stop this".
  if (const std::string* hit =
          FirstHostMatch(policy.domain_check_list, request.host)) {
    decision.reason = PolicyReason::kDomainCheckMatch;
    decision.observed = true;
    decision.matched_rule = hit;
    return decision;
  }

  return decision;
}

std::string_view PolicyReasonToString(PolicyReason reason) {
  switch (reason) {
    case PolicyReason::kNoMatch:
      return "no_match";
    case PolicyReason::kSchemeBlocked:
      return "black_schemes";
    case PolicyReason::kExceptionMatch:
      return "exception_list";
    case PolicyReason::kWhiteListMatch:
      return "white_list";
    case PolicyReason::kFrameWhiteMatch:
      return "frame_black_white_list";
    case PolicyReason::kFrameBlackMatch:
      return "frame_black_list";
    case PolicyReason::kBlackListMatch:
      return "black_list";
    case PolicyReason::kDomainBlackMatch:
      return "domain_black_list";
    case PolicyReason::kDomainCheckMatch:
      return "domain_check_list";
  }
  return "no_match";
}

std::string_view PolicyActionToString(PolicyAction action) {
  switch (action) {
    case PolicyAction::kSendRequest:
      return "send";
    case PolicyAction::kCancelRequest:
      return "cancel";
  }
  return "send";
}

}  // namespace lengsuo::fp
