// resp_throttle - the response-body monitor on the response path
// (task 0017, the T1 wiring).
//
// native/resp_monitor.h decides which responses a rule claims,
// native/resp_monitor_body.h holds the bounded capture, native/resp_tee.h
// fans a body chunk out to the page and to the capture, and
// native/resp_monitor_report.h builds the uplink payload. All of them were in
// the kernel already and none of them had a caller: the monitor was a library
// with no driver. This file is the driver. It is a URLLoaderThrottle that
// copies the first bytes of the responses the rules claim and tells the
// runner what it saw.
//
// The one rule that shapes everything below: the page is not a consumer of
// this feature. The page receives its bytes completely unchanged, and a
// response the monitor cannot read without touching the page's copy is a
// response the monitor leaves alone.
//
// Where the rules come from. The ConfigBundle (frame type 0x03) the runner
// sent, which the browser process already holds: KernelPipeClient::Get()
// owns the frozen bundle, and the throttle reads the `response_monitor`
// section out of it. There is no second configuration channel and no new
// command-line switch - a monitor that is visible in the process list is a
// monitor the platform leaked.
//
// Where the split is. The top half of this header is pure logic: it takes a
// bundle, a request and a response head and answers with a decision and a
// report, so tools/rebuild/run_l3_gtest.sh compiles and tests it with a plain
// g++, no Chromium headers involved. The bottom half is one declaration, and
// the Chromium types live in the .cc, exactly as task 0018 did it with
// policy_throttle.h.
//
// Threading. CreateURLLoaderThrottles runs on the browser's UI thread
// (chrome/browser/chrome_content_browser_client.cc has
// DCHECK_CURRENTLY_ON(BrowserThread::UI) at the top of it), so the factory
// runs there. A URLLoaderThrottle's Will* methods run on the sequence the
// loader runs on, which is the UI thread for a browser-initiated load, and
// the mojo watcher the .cc arms is bound to that same sequence's default task
// runner. Nothing here waits on anything: the bundle is already in the
// process, the decision is string arithmetic, the body is copied as it flows
// past, and the uplink is a non-blocking enqueue (kernel_pipe_client.h).
#ifndef LENGSUO_INTEGRATION_RESP_THROTTLE_H_
#define LENGSUO_INTEGRATION_RESP_THROTTLE_H_

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "lengsuo/native/resp_monitor.h"
#include "lengsuo/native/resp_monitor_config.h"
#include "lengsuo/native/resp_monitor_report.h"
#include "lengsuo/native/resp_tee.h"

namespace blink {
class URLLoaderThrottle;
}  // namespace blink

namespace lengsuo {

// Reads the `response_monitor` section out of a frozen ConfigBundle.
//
// Returns false only when the bundle or its section cannot be read. A bundle
// that carries no `response_monitor` section is a success with a switched-off
// configuration: the platform did not ask for a monitor, and a monitor that
// is off registers nothing.
//
// On failure `*out` is left switched off, so the caller that ignores the
// return value still ends up mounting nothing.
inline bool ReadRespMonitorConfig(std::string_view bundle_json,
                                  fp::RespMonitorConfig* out) {
  if (out == nullptr) {
    return false;
  }
  *out = fp::RespMonitorConfig();
  std::string section;
  if (!fp::FindRespMonitorSection(bundle_json, &section, nullptr)) {
    return false;
  }
  if (section.empty()) {
    // No such section: the ordinary "the platform did not ask for a monitor"
    // case, not an error. The default is switched off.
    return true;
  }
  return fp::ParseRespMonitorConfig(section, out, nullptr);
}

// True when the configuration carries at least one rule that could fire.
//
// A switched-off monitor installs nothing, and a monitor with no rules cannot
// claim anything: every response would come back "no match", no body would be
// copied and nothing would be reported. Mounting a throttle for it would cost
// a virtual call on every response in the browser and buy nothing, so the
// factory does not.
inline bool RespMonitorHasRules(const fp::RespMonitorConfig& config) {
  return config.enabled && !config.rules.empty();
}

// The request the monitor is asked about.
//
// The URL and the scheme are the response's - the monitor decides which
// response to read, so it is that response's URL the rule is matched
// against. The initiator scheme is the request's: it says whether a page
// started this at all, and it is the throttle that captured it while the
// request was still in hand.
inline fp::RespMonitorRequest BuildRespMonitorRequest(
    std::string_view url,
    std::string_view scheme,
    std::string_view initiator_scheme) {
  fp::RespMonitorRequest request;
  request.url = std::string(url);
  request.scheme = std::string(scheme);
  request.initiator_scheme = std::string(initiator_scheme);
  return request;
}

// True when a body that arrived with this `Content-Encoding` may be read as
// it is.
//
// The monitor does not decompress, so a body that is still content-encoded is
// not monitored at all: reading it would mean either reporting compressed
// bytes, which says nothing about the page's content, or inflating it, which
// is a different job with its own failure modes. The header is the only
// signal the response head carries about this, so it is the signal used; a
// response that names any encoding other than `identity` is left alone.
//
// The value is a comma-separated list, and an empty value means "decoded",
// which is what an absent header looks like.
inline bool RespMonitorMayReadBody(std::string_view content_encoding) {
  std::size_t at = 0;
  while (at < content_encoding.size()) {
    const std::size_t comma = content_encoding.find(',', at);
    std::string_view token = content_encoding.substr(
        at, comma == std::string_view::npos ? std::string_view::npos
                                           : comma - at);
    // Trim the optional whitespace the header grammar allows.
    while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
      token.remove_prefix(1);
    }
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
      token.remove_suffix(1);
    }
    if (!token.empty()) {
      std::string lowered(token);
      for (char& c : lowered) {
        if (c >= 'A' && c <= 'Z') {
          c = static_cast<char>(c - 'A' + 'a');
        }
      }
      if (lowered != "identity") {
        return false;
      }
    }
    if (comma == std::string_view::npos) {
      break;
    }
    at = comma + 1;
  }
  return true;
}

// What the throttle does about one response.
struct RespMonitorOutcome {
  // The body is copied and a report will be sent. False when no rule claimed
  // this response, when the rule that claimed it has already said what it had
  // to say about this URL, or when the body must not be read.
  bool monitor = false;
  // The rule that claimed it. Empty when `monitor` is false.
  std::string rule_id;
  // The most bytes of this body the monitor may keep: the claiming rule's
  // `max_body_bytes`, as the configuration layer resolved it. For a rule that
  // claimed the response it is never zero - a rule that does not state a
  // limit gets the ceiling, and the layer treats 0 as "unstated" - so a zero
  // here only ever means that no rule claimed the response.
  std::size_t capture_limit = 0;
  // Whether this rule's reports may carry the body itself. Off by default.
  bool include_body = false;
};

// Decides one response out of a monitor whose rules are already installed.
//
// This is the whole of what the throttle decides, spelled out as a pure
// function so the gtest can pin it. It is deliberately the same shape as
// MatchRespMonitorRule: when a rule claims the response, one of its hits is
// spent, so the per-URL ceiling has to count it.
//
// The order matters and is asserted by the tests. The encoding guard runs
// first because it is about the response, not about any rule, and a hit must
// not be spent on a body that will then not be read.
inline RespMonitorOutcome DecideRespMonitor(fp::RespMonitorState* state,
                                            const fp::RespMonitorRequest& request,
                                            std::string_view content_type,
                                            std::string_view content_encoding) {
  RespMonitorOutcome outcome;
  if (!RespMonitorMayReadBody(content_encoding)) {
    return outcome;
  }
  const fp::RespMonitorRule* rule =
      fp::MatchRespMonitorRule(state, request, content_type);
  if (rule == nullptr) {
    return outcome;
  }
  outcome.monitor = true;
  outcome.rule_id = rule->id;
  outcome.capture_limit = rule->max_body_bytes;
  outcome.include_body = rule->include_body;
  return outcome;
}

// Builds the report of a response the monitor read.
//
// The report is what leaves the browser, so this is the last place at which
// something that should not travel could be added. It carries the response's
// own facts - the rule, the URL, the status, the media type, how many bytes
// went past, whether the capture was cut short - and the body only when the
// rule asked for it. It never carries a header: the report is about a
// response, and a header is where credentials live
// (native/resp_monitor_report.h states this at length).
inline fp::RespMonitorReport BuildRespMonitorReport(
    const RespMonitorOutcome& outcome,
    std::string_view url,
    int status,
    std::string_view content_type,
    const fp::RespTee& tee) {
  fp::RespMonitorReport report;
  report.rule_id = outcome.rule_id;
  report.url = std::string(url);
  report.status = status;
  report.content_type = std::string(content_type);
  report.body_bytes = tee.capture.total_bytes;
  report.truncated = tee.capture.truncated;
  report.include_body = outcome.include_body;
  // An absent body and an empty body are different facts, and the runner is
  // owed the difference: the body is carried only when the rule asked for it.
  if (outcome.include_body) {
    report.body = tee.capture.bytes;
  }
  return report;
}

// Creates the throttle, or nullptr when the monitor is not worth mounting.
//
// Returns nullptr when no pipe was requested, when the bundle carries no
// `response_monitor` section, when that section is switched off, or when it
// has no rules. Those are the ordinary "no monitor" cases and they must cost
// nothing, so the caller mounts nothing rather than mounting a throttle that
// copies nothing on every response.
//
// The throttle is a blink::URLLoaderThrottle; that type is Chromium's and is
// therefore only named here, never included.
std::unique_ptr<blink::URLLoaderThrottle> CreateRespThrottle();

}  // namespace lengsuo

#endif  // LENGSUO_INTEGRATION_RESP_THROTTLE_H_