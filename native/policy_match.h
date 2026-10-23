// policy_match - matching a request against the access policy (task T2).
//
// The policy matches a request in two different ways, and the two are kept
// apart on purpose:
//
//   * a URL is matched by a regular expression, over the whole URL;
//   * a host is matched by a glob, over the host.
//
// The glob is the one the profile sync rules already use - the MatchGlob in
// native/profile_bundle.cc - so there is one glob in this codebase and not
// two. The regular expression is the kernel's own; the reason is in
// policy_match.cc.
//
// The kernel evaluates the pattern in a fixed little language rather than
// pulling in an expression engine:
//
//   * std::regex is not an option. Its constructor throws on a pattern it
//     cannot read, and the browser is built without exceptions, so a bad
//     pattern would take the browser down instead of taking one rule out.
//   * RE2 would work, but in this tree it needs Abseil, and a rule engine
//     that drags half of Abseil in cannot be tested on its own.
//
// The language the kernel does evaluate is written out in policy_match.cc,
// together with what it leaves out.
#ifndef LENGSUO_NATIVE_POLICY_MATCH_H_
#define LENGSUO_NATIVE_POLICY_MATCH_H_

#include <string>
#include <string_view>

#include "policy_rules.h"

namespace lengsuo::fp {

// One request the policy is asked about.
//
// The URL, the host and the scheme are handed in rather than parsed out of
// the URL here: the browser already has them, and parsing a URL is a job
// for the browser's URL class, not for a rule engine.
struct PolicyRequest {
  // The whole URL, as the browser has it.
  std::string url;
  // The host, without the port. Empty when the URL has no host.
  std::string host;
  // The scheme, without the colon. Empty when the URL has no scheme.
  std::string scheme;
  // Which frame the request belongs to.
  PolicyFrame frame = PolicyFrame::kMainFrame;
};

// Matches a URL pattern against a URL.
//
// The pattern is a regular expression and the match has to cover the whole
// URL, not a part of it. This is the same anchoring the host glob has: the
// pattern describes the whole thing or it does not describe it at all.
// A pattern that means "any URL that mentions this" has to say so -
// ".*example\.test.*".
//
// The anchors are therefore no-ops: the match already starts at the start
// of the URL and ends at its end.
//
// A pattern outside the language the kernel evaluates does not match
// anything. IsValidUrlPattern says so before the rule is ever used, and a
// rule that fails it is dropped and counted when the policy is read.
bool MatchUrlPattern(std::string_view pattern, std::string_view url);

// True when the pattern is a regular expression the matcher can evaluate.
bool IsValidUrlPattern(std::string_view pattern);

// Matches `pattern` against a run of `url`, anywhere in it.
//
// The same language and the same engine as MatchUrlPattern; the difference is
// where the match may begin and end. MatchUrlPattern requires the pattern to
// describe the whole URL, and this one succeeds when the pattern describes a
// part of it. `^` still means the start of the whole URL and `$` its end, so a
// pattern that anchors itself stays anchored, and a pattern that does not is a
// pattern about a part of the URL.
//
// This is the convention the response monitor's rules are written in: they
// name an endpoint or a prefix, not a whole address. It replaces
// std::regex_search, which the browser cannot use at all - see above.
bool SearchUrlPattern(std::string_view pattern, std::string_view url);

// Matches a host pattern against a host.
//
// The pattern is a glob - '*' matches any run of bytes, '?' matches one -
// and it is the same glob the profile sync rules use.
bool MatchDomainPattern(std::string_view pattern, std::string_view host);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_POLICY_MATCH_H_
