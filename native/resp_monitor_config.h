// resp_monitor_config - the rule set of the response-body monitor (task T1).
//
// The rules arrive in the ConfigBundle's `response_monitor` section, which is
// JSON text like every other payload on the pipe. The shape is
//
//   {"response_monitor": {
//      "enabled": true,
//      "include_body": false,          // section default; per-rule may override
//      "rules": [
//        {"id": "api-json",
//         "url_regex": "^https://api\\.example\\.com/",
//         "content_type": "application/json",   // optional; empty = any
//         "max_count": 3,                       // per rule, per URL
//         "max_body_bytes": 4096,               // optional; 0 = the ceiling
//         "enabled": true,
//         "include_body": false}                // optional; overrides section
//      ]}}
//
// Two rules are not negotiable:
//
//   * The whole feature is off unless the section says `enabled: true`. A
//     monitor that is off does not register at all - no rules compiled, no
//     capture buffers. "Off" has to cost nothing, or a disabled feature still
//     pays for itself on every response.
//   * A rule whose regular expression does not compile is dropped and
//     counted. It never fails the configuration: one typo in one rule must
//     not take the other nine with it.
//
// The section is parsed in two steps because it crosses a process boundary.
// The browser process finds the section in the ConfigBundle and sends that
// text - and only that text - to the network service, which parses it here.
// The network service never sees the rest of the bundle.
//
// The hard work is reading a JSON array of objects. The reader in
// kernel_pipe_json.h is deliberately shallow and keeps a nested array as raw
// text, so the array is walked here and each element is handed back to that
// same reader.
#ifndef LENGSUO_NATIVE_RESP_MONITOR_CONFIG_H_
#define LENGSUO_NATIVE_RESP_MONITOR_CONFIG_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace lengsuo::fp {

// The ConfigBundle section that carries the rules.
inline constexpr char kRespMonitorConfigKey[] = "response_monitor";

// How many body bytes a rule captures when it does not say, and the ceiling
// no configuration may raise. The capture buffer lives in the network
// service, and an unbounded buffer there is a way to make the browser
// allocate on a hostile page's schedule.
//
// The ceiling is 64 KiB because the body does not go on the wire as it is:
// it goes into a JSON string, where a byte that is not valid UTF-8 becomes a
// six character escape. The uplink rejects a payload over 1 MiB, and 64 KiB
// times six is 384 KiB, which leaves room for the envelope.
inline constexpr std::size_t kRespMonitorMaxBodyBytesCeiling = 64 * 1024;

// How many hits one rule allows for one URL when it does not say.
inline constexpr std::uint64_t kRespMonitorDefaultMaxCount = 1;

// The grammar a rule's regular expression is written in. ECMAScript, not
// POSIX: the configuration comes from the runner, and ECMAScript is the
// grammar the wire contract's examples are written in. A rule that does not
// compile under this grammar is dropped.
inline constexpr std::regex::flag_type kRespMonitorRegexGrammar =
    std::regex::ECMAScript;

// True when `pattern` compiles under kRespMonitorRegexGrammar.
bool IsValidRespMonitorUrlRegex(std::string_view pattern);

// The media type of a Content-Type header, without its parameters and in
// lower case. `application/json; charset=utf-8` and `Application/JSON` name
// the same type, and a rule names the type the way its author wrote it.
std::string NormalizeRespMonitorContentType(std::string_view value);

// One rule, as the configuration states it.
struct RespMonitorRule {
  std::string id;
  std::string url_regex;
  // The compiled matcher. Never null: a rule whose pattern did not compile
  // is dropped while parsing and never reaches the monitor.
  std::shared_ptr<const std::regex> matcher;
  // The media type the response must have. Empty means "any". Parameters
  // (`; charset=...`) are stripped before the comparison.
  std::string content_type;
  // The per-URL hit ceiling.
  std::uint64_t max_count = kRespMonitorDefaultMaxCount;
  // The per-response capture ceiling, already clamped.
  std::size_t max_body_bytes = kRespMonitorMaxBodyBytesCeiling;
  // Whether this rule's reports may carry the body itself. Off by default:
  // the body is the expensive and the sensitive half of a report.
  bool include_body = false;
};

// The parsed section.
struct RespMonitorConfig {
  // False when the section is absent, or present and switched off. Nothing is
  // registered in that case.
  bool enabled = false;
  // The section-level default for per-rule `include_body`.
  bool include_body = false;
  // The rules that survived parsing, in configuration order.
  std::vector<RespMonitorRule> rules;
  // Rules dropped because they did not parse (bad regex, missing id, ...).
  std::uint64_t skipped_rules = 0;
  // Rules dropped because they said `enabled: false`.
  std::uint64_t disabled_rules = 0;
};

// Finds the raw JSON text of the `response_monitor` section in the
// ConfigBundle. `*section_json` is left empty when the bundle carries no such
// section - that is the ordinary "the platform did not ask for a monitor"
// case, not an error.
//
// Returns false only when `bundle_json` is not a JSON object.
bool FindRespMonitorSection(std::string_view bundle_json,
                            std::string* section_json,
                            std::string* error);

// Parses the `response_monitor` section's raw JSON text.
//
// Returns false only when the section has the wrong shape. A missing
// `enabled`, or `enabled: false`, is a successful parse of a switched off
// configuration. A bad rule is counted in `skipped_rules`, never fatal.
bool ParseRespMonitorConfig(std::string_view section_json,
                            RespMonitorConfig* out,
                            std::string* error);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_RESP_MONITOR_CONFIG_H_
