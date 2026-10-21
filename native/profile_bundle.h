// profile_bundle - the collection rules for the profile upload (task R).
//
// When a container closes, the profile's files are packed into a zip and
// uploaded to the platform (channel 4). The kernel side owns *which* files
// are collected; the client side owns the packing and the upload.
//
// The rules are configuration, not code. The runner sends them in the
// ConfigBundle (frame type 0x03) and the kernel applies whatever the
// bundle says. Nothing in this file names a particular file: "History" is
// collected because the configuration lists it, not because this code
// knows what History is.
//
// What the configuration carries (contract section 8):
//
//   include            glob patterns; a file is collected when it matches
//                      at least one of them
//   exclude            glob patterns; a file is never collected when it
//                      matches one of them
//   max_package_bytes  the ceiling on the collected bytes
//
// Two rules are not negotiable:
//
//   * The exclude list wins. "Cookies" and "Network Cache" stay out
//     whatever else the configuration says.
//   * A file that cannot be read is skipped and counted. One locked file
//     never fails the collection: a browser that refuses to close because
//     of it is worse than one that uploads slightly less.
//
// The manifest this produces is the shape the two repos agree on:
//
//   * the path is relative to the profile root, always '/' separated,
//     never absolute, never "..";
//   * the entries are ordered byte-wise ascending by the path's bytes;
//   * the size is the file's size in bytes.
//
// See docs/kernel-wire-contract.md section 8 for the same rules in prose.
#ifndef LENGSUO_NATIVE_PROFILE_BUNDLE_H_
#define LENGSUO_NATIVE_PROFILE_BUNDLE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lengsuo::fp {

// The ceiling on one package: 32 MiB.
//
// A collection that reaches it is cut short and the cut is counted. The
// configuration may lower this ceiling; it may never raise it.
inline constexpr std::uint64_t kProfileBundleMaxBytes =
    32ULL * 1024ULL * 1024ULL;

// The name of the configuration section that carries the rules.
inline constexpr char kProfileSyncConfigKey[] = "profile_sync";

// The rules the configuration carries.
struct ProfileSyncConfig {
  // The patterns a file must match to be collected. An empty list
  // collects nothing: the list is a white list, and a white list that is
  // empty is an empty collection.
  std::vector<std::string> include;
  // The patterns a file must not match. An exclude wins over an include.
  std::vector<std::string> exclude;
  // The ceiling on the collected bytes. Zero means "unset", which is the
  // fixed ceiling above.
  std::uint64_t max_package_bytes = kProfileBundleMaxBytes;
};

// The default set of rules (contract section 8): the white list minus the
// black list, plus History.
//
// The runner sends this when the platform does not override it, so the
// constant here is the kernel's mirror of the runner's default rather than
// a second opinion about it. The contract keeps the two together.
ProfileSyncConfig DefaultProfileSyncConfig();

// Reads the profile_sync section out of a frozen configuration bundle.
//
// A bundle without the section means the platform did not ask for a
// profile upload: the function fails and the caller collects nothing.
// `*error` then says so. A malformed section fails the same way - a
// configuration that cannot be read is not a configuration that is
// partially obeyed.
bool ParseProfileSyncConfig(std::string_view bundle_json,
                            ProfileSyncConfig* out,
                            std::string* error);

// Matches `text` against a glob pattern.
//
//   '*'  matches any run of bytes, '/' included
//   '?'  matches exactly one byte
//
// The comparison is byte-wise and case-sensitive: the profile's own
// directory names are spelled the same way on every platform, and a
// locale-aware comparison would not agree across two repos.
bool MatchGlob(std::string_view pattern, std::string_view text);

// One file the caller found under the profile root.
struct ProfileFile {
  // The path relative to the profile root, '/' separated.
  std::string relative_path;
  // The file's size in bytes.
  std::uint64_t size = 0;
  // False when the file could not be read. Such a file is skipped and
  // counted; it never fails the collection.
  bool readable = true;
};

// One collected file.
struct ProfileBundleEntry {
  // The path relative to the profile root. Never absolute, never "..".
  std::string path;
  // The file's size in bytes.
  std::uint64_t size = 0;
};

// The result of applying the rules to a set of files.
struct ProfileBundle {
  // The collected files, byte-wise ascending by path.
  std::vector<ProfileBundleEntry> entries;
  // The sum of the collected files' sizes.
  std::uint64_t total_bytes = 0;
  // Files that could not be read. Skipped, never fatal.
  std::uint64_t skipped_unreadable = 0;
  // Files the rules did not collect: no include pattern matched, or an
  // exclude pattern did.
  std::uint64_t excluded = 0;
  // Files that would have been collected but did not fit under the
  // ceiling.
  std::uint64_t truncated = 0;
  // True when the ceiling cut the collection short.
  bool size_capped = false;
};

// Applies the rules to `files` and fills `*out`.
//
// The entries come out ordered byte-wise ascending by path, so two repos
// that collect the same profile agree on the order without talking to
// each other. The ceiling is applied to that order, not to the order the
// files arrived in: a collection that depended on the caller's directory
// walk would differ between two machines that see the same profile.
//
// A file that does not fit under the ceiling ends the collection; the
// files after it are counted as truncated.
void CollectProfileBundle(const ProfileSyncConfig& config,
                          const std::vector<ProfileFile>& files,
                          ProfileBundle* out);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_PROFILE_BUNDLE_H_
