// resp_monitor_report - the report of a monitored response (task T1).
//
// The monitor reads a response that a rule matched and writes down what it
// saw. The report is the only thing that leaves the browser on the uplink,
// so it is deliberately narrow: it says which rule fired, which URL was
// hit, what came back, and - only when the platform asked for it - the
// first few bytes of the body.
//
// What the report does not say is as important as what it does:
//
//   * No request headers. No `Cookie`, no `Authorization`, no `Set-Cookie`.
//     The report is about a response, not about the request that caused it,
//     and a header is where credentials live.
//   * No body by default. The body is the page's own content; it is the most
//     sensitive thing on this channel and the most expensive. It is carried
//     only when the configuration asked for it explicitly, and even then it
//     is bounded by the rule's own limit.
//
// The report is written as JSON twice: once as the report's own body, and
// once wrapped in the uplink action's envelope. The two share their fields,
// so a rule that is changed changes both at once - there is one place where
// the fields are written.
#ifndef LENGSUO_NATIVE_RESP_MONITOR_REPORT_H_
#define LENGSUO_NATIVE_RESP_MONITOR_REPORT_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "action_table.h"

namespace lengsuo::fp {

// Where the report comes from, in the uplink's `from` field.
//
// The report is produced by the browser side: the network service decides
// what is monitored and the browser side puts the report on the uplink. The
// runner is told which side produced a payload so that it can tell a
// renderer's event from the browser's own.
inline constexpr char kRespMonitorFrom[] = "browser";

// The report of one monitored response.
struct RespMonitorReport {
  // The rule that fired.
  std::string rule_id;
  // The URL of the response.
  std::string url;
  // The HTTP status code of the response.
  int status = 0;
  // The response's media type, without its parameters.
  std::string content_type;
  // How many body bytes the response carried. This is the whole body, not
  // only the part that was kept.
  std::uint64_t body_bytes = 0;
  // True when the body was longer than the capture limit.
  bool truncated = false;
  // The captured prefix of the body. Empty when the body was not collected.
  std::string body;
  // Whether the body may be carried at all. Off by default.
  bool include_body = false;
};

// Builds the report's JSON: the report's fields and nothing else.
//
// The body is left out when the report says it may not be carried. An absent
// body and an empty body are different facts - one says the body was not
// collected, the other says the body was empty - and the runner is owed the
// difference.
std::string BuildRespMonitorReportJson(const RespMonitorReport& report);

// Builds the uplink payload: the action's name, where the report came from,
// and the report's fields.
//
// The payload is what the action table validates, so it is built to be
// accepted by the table's schema for the action.
std::string BuildRespMonitorActionPayload(const RespMonitorReport& report);

// Escapes a string so it can go into a JSON string as it is.
//
// Valid UTF-8 passes through unchanged: a page's body is usually UTF-8 and
// escaping all of it would double the size of every report. A byte that is
// not part of a valid UTF-8 sequence becomes a `\u00XX` escape, and so does
// a control character - a JSON string may not carry a raw one.
//
// The result includes the surrounding quotes.
std::string EscapeRespMonitorJsonString(std::string_view value);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_RESP_MONITOR_REPORT_H_
