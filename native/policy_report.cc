#include "policy_report.h"

#include <string>

namespace lengsuo::fp {
namespace {

// Appends a JSON string, quotes included.
//
// The control characters are written as \u00XX because a raw byte in a JSON
// string is not always a JSON string: a URL may carry anything at all.
void AppendJsonString(std::string* out, std::string_view value) {
  static const char kHex[] = "0123456789abcdef";
  out->push_back('"');
  for (const char c : value) {
    switch (c) {
      case '"':
        out->append("\\\"");
        break;
      case '\\':
        out->append("\\\\");
        break;
      case '\b':
        out->append("\\b");
        break;
      case '\f':
        out->append("\\f");
        break;
      case '\n':
        out->append("\\n");
        break;
      case '\r':
        out->append("\\r");
        break;
      case '\t':
        out->append("\\t");
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out->append("\\u00");
          out->push_back(kHex[(static_cast<unsigned char>(c) >> 4) & 0x0f]);
          out->push_back(kHex[static_cast<unsigned char>(c) & 0x0f]);
        } else {
          out->push_back(c);
        }
        break;
    }
  }
  out->push_back('"');
}

}  // namespace

std::string StripUrlCredentials(std::string_view url) {
  const std::size_t scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    // No authority, so no credentials to take out.
    return std::string(url);
  }
  const std::size_t authority_start = scheme_end + 3;
  std::size_t authority_end = url.find_first_of("/?#", authority_start);
  if (authority_end == std::string_view::npos) {
    authority_end = url.size();
  }
  // The last "@" of the authority is the one that separates the
  // credentials from the host. A host may not carry one, so the last is the
  // only one that can be it.
  const std::size_t at = url.rfind('@', authority_end);
  if (at == std::string_view::npos || at < authority_start) {
    return std::string(url);
  }
  std::string out(url.substr(0, authority_start));
  out.append(url.substr(at + 1, authority_end - at - 1));
  out.append(url.substr(authority_end));
  return out;
}

std::string_view PolicyDecisionName(const PolicyDecision& decision) {
  if (decision.action == PolicyAction::kCancelRequest) {
    return kPolicyDecisionBlock;
  }
  if (decision.observed) {
    return kPolicyDecisionObserve;
  }
  return kPolicyDecisionAllow;
}

std::string BuildPolicyEventPayload(const PolicyRequest& request,
                                     const PolicyDecision& decision) {
  std::string out;
  out.reserve(256);
  out += "{\"from\":\"";
  out += kPolicyReportFrom;
  out += "\",\"action\":\"";
  out += kActionPolicyEvent;
  out += "\",\"url\":";
  AppendJsonString(&out, StripUrlCredentials(request.url));
  out += ",\"category\":\"";
  out += kPolicyCategory;
  out += "\",\"decision\":\"";
  out += PolicyDecisionName(decision);
  out += "\",\"reason\":\"";
  out += PolicyReasonToString(decision.reason);
  out += "\"";
  if (decision.matched_rule != nullptr) {
    out += ",\"rule\":";
    AppendJsonString(&out, *decision.matched_rule);
  }
  out += "}";
  return out;
}

}  // namespace lengsuo::fp
