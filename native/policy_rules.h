// policy_rules - the access policy rules (task T2).
//
// The access policy is configuration, not code. The runner sends it in the
// ConfigBundle (frame type 0x03) and the kernel applies whatever the bundle
// says. Nothing in this file names a site: "example.com" is blocked because
// the configuration lists it, not because this code knows the site.
//
// The configuration carries three URL lists whose semantics do not overlap:
//
//   black_list      a hit means refuse
//   white_list      a hit means allow
//   exception_list  a hit overrides the conclusion of the two above
//
// The priority is fixed here because two repos that each decide their own
// would decide differently:
//
//   exception_list > white_list > black_list
//
// `black_schemes` is independent of the URL lists: a scheme on that list is
// refused whatever the URL lists say, and no URL pattern rescues it. The
// scheme rule is a statement about the transport, not about the address.
//
// The rest of the lists:
//
//   main_frame_list      the main frame's own list
//   sub_frame_black_list the sub frame's own list
//   frame_black_white_list  a black and a white list per frame kind
//   domain_black_list    host patterns that are refused
//   domain_check_list    host patterns that are watched but not refused
//
// What the two kinds of list item mean:
//
//   * the URL lists hold regular expressions, matched against the whole URL
//     (see policy_match.h);
//   * the domain lists hold globs, matched against the host. The glob is the
//     one the profile sync rules already use - native/profile_bundle.cc's
//     MatchGlob - so there is exactly one glob in this codebase.
//
// Two rules are not negotiable:
//
//   * A rule that cannot be read is dropped and counted. One bad pattern
//     never fails the whole policy and never fails the browser.
//   * A configuration that has not arrived is an empty policy, and an empty
//     policy refuses nothing. A browser that blocks everything because the
//     runner is not up yet is a broken browser.
#ifndef LENGSUO_NATIVE_POLICY_RULES_H_
#define LENGSUO_NATIVE_POLICY_RULES_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lengsuo::fp {

// The name of the configuration section that carries the access policy.
inline constexpr char kAccessPolicyConfigKey[] = "access_policy";

// Which frame a request belongs to.
enum class PolicyFrame {
  kMainFrame,
  kSubFrame,
};

// One frame's black and white lists.
//
// The lists are separate because a frame can be on the black list for one
// address and on the white list for another; the priority between them is
// the same as everywhere else (white wins over black, an exception wins over
// both).
struct FramePolicyLists {
  std::vector<std::string> black;
  std::vector<std::string> white;
};

// The rules the configuration carries.
struct AccessPolicy {
  // The URL lists. The items are regular expressions matched against the
  // whole URL.
  std::vector<std::string> black_list;
  std::vector<std::string> white_list;
  std::vector<std::string> exception_list;

  // The schemes that are refused whatever the URL lists say. The names carry
  // no colon ("file", not "file:").
  std::vector<std::string> black_schemes;

  // main_frame_list: the main frame's own list. Read as a black list: the
  // brief names the sub frame's list "black" and does not name the main
  // frame's, so the two are read the same way.
  std::vector<std::string> main_frame_list;
  // sub_frame_black_list: the sub frame's own list.
  std::vector<std::string> sub_frame_black_list;

  // The frame_black_white_list section: a black and a white list per frame
  // kind.
  FramePolicyLists main_frame_rules;
  FramePolicyLists sub_frame_rules;

  // The domain lists. The items are globs matched against the host.
  std::vector<std::string> domain_black_list;
  std::vector<std::string> domain_check_list;

  // The rules that were dropped because they could not be read. Counted, not
  // fatal.
  std::uint64_t skipped_rules = 0;
};

// Reads the access_policy section out of a frozen configuration bundle.
//
// Returns false only when the bundle itself cannot be read. A bundle that
// carries no access_policy section yields an empty policy and returns true:
// the platform did not ask for one, so there is nothing to enforce.
//
// A list that cannot be read is dropped and counted; the rest of the policy
// still applies. A rule that cannot be read is a rule that does not fire.
bool ParseAccessPolicy(std::string_view bundle_json,
                        AccessPolicy* out,
                        std::string* error);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_POLICY_RULES_H_
