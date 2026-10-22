// resp_monitor - the matching and the counting of the response monitor.
//
// The monitor sits in front of the network stack's responses and decides
// which of them are worth reporting. The decision has three parts:
//
//   * Is this request one the monitor may look at at all? Only http(s) is.
//     Everything else - `data:`, `blob:`, `chrome:`, `chrome-extension:`,
//     `file:`, `about:` - is out of reach by construction. The browser's own
//     requests are out of reach by construction too: they carry no
//     initiator origin, and an extension's requests carry an extension
//     origin. Neither is a page, and the monitor watches pages.
//
//   * Which rule does it belong to? The rule's regular expression is
//     searched in the URL - the pattern may anchor itself, and a rule that
//     wants the whole URL says so. A rule that names a media type only
//     fires for that media type.
//
//   * Has that rule already said enough about that URL? The ceiling is per
//     rule and per URL: a rule that says three allows three hits for one
//     URL and then lets it go, while the same rule still watches a different
//     URL.
//
// The rules are installed once, from the configuration. The counters live
// beside them and are the only thing that grows with traffic, so they are
// bounded: when the monitor has seen too many distinct URLs it switches the
// rule that crossed the line off and counts the failure. A rule that is
// switched off stops matching; it is not an error and it does not reach the
// browser.
#ifndef LENGSUO_NATIVE_RESP_MONITOR_H_
#define LENGSUO_NATIVE_RESP_MONITOR_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "resp_monitor_config.h"

namespace lengsuo::fp {

// How many distinct (rule, URL) pairs the monitor tracks before it switches
// a rule off. The counter map is the only state that grows with the traffic
// a page generates, and a page that generates an unbounded number of
// distinct URLs is either misconfigured or hostile. Either way the monitor
// stops rather than grows.
inline constexpr std::size_t kRespMonitorMaxTrackedUrls = 4096;

// What the monitor knows about a request.
//
// Everything here comes from what the network stack already knows. The
// monitor guesses nothing: an unknown scheme is not http(s) and an unknown
// initiator is the browser's own request.
struct RespMonitorRequest {
  // The URL of the request.
  std::string url;
  // The URL's scheme, lower case.
  std::string scheme;
  // The scheme of the origin that started the request, lower case. Empty
  // when the request has no initiator origin, which is how the browser's own
  // requests look.
  std::string initiator_scheme;
};

// The state of one monitor: the rules and the counters.
struct RespMonitorState {
  // The rules that are installed, in configuration order.
  std::vector<RespMonitorRule> rules;
  // The hits each rule has spent on each URL.
  std::map<std::pair<std::string, std::string>, std::uint64_t> hits;
  // The rules that were switched off after a failure.
  std::set<std::string> disabled_rules;
  // How many failures the monitor has counted.
  std::uint64_t failures = 0;
};

// Installs the rules.
//
// A configuration that is switched off installs nothing, so the monitor holds
// no rules, no counters and no buffers. The counters of the rules that stay
// are kept: installing the same rules again must not hand a rule a fresh
// allowance every time the configuration is pushed down.
void RespMonitorSetRules(RespMonitorState* state, const RespMonitorConfig& config);

// True when the request is one the monitor may look at.
bool IsRespMonitorRequestEligible(const RespMonitorRequest& request);

// Decides which rule the response belongs to.
//
// Returns the rule, or nullptr when no rule matched, when the rule that
// matched has already spent its allowance on that URL, or when the request
// is not eligible.
//
// When a rule matches, one of its hits is spent: the caller has decided to
// monitor the response, so the ceiling has to count it.
const RespMonitorRule* MatchRespMonitorRule(RespMonitorState* state,
                                            const RespMonitorRequest& request,
                                            std::string_view content_type);

// Switches a rule off and counts the failure. A rule that is already off is
// not counted twice.
void RespMonitorDisableRule(RespMonitorState* state, const std::string& rule_id);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_RESP_MONITOR_H_
