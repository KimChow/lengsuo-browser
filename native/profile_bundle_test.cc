// profile_bundle_test - the collection rules for the profile upload.
//
// The tests are about the rules, not about the plumbing: which files are
// collected, which never are, and what happens when the collection does
// not fit.
//
// The four rulings this card implements each get a test of their own:
//
//   1B  the history is collected by default
//   2A  a file that cannot be read is skipped, not fatal
//   3A  the canvas bitmap is not collected (the action table's default,
//        which this card leaves alone - see the note below)
//   and the size ceiling cuts the collection short
//
// The bitmap rule is not this file's business: the action table owns it
// and its default is already off. This file only checks that nothing here
// turns it on.
#include "profile_bundle.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace lengsuo::fp {
namespace {

// A file the caller found under the profile root.
ProfileFile File(const std::string& path, std::uint64_t size) {
  ProfileFile file;
  file.relative_path = path;
  file.size = size;
  return file;
}

// The paths of the collected entries, in order.
std::vector<std::string> Paths(const ProfileBundle& bundle) {
  std::vector<std::string> paths;
  paths.reserve(bundle.entries.size());
  for (const ProfileBundleEntry& entry : bundle.entries) {
    paths.push_back(entry.path);
  }
  return paths;
}

// A configuration with the ruling's white list and black list.
ProfileSyncConfig Config() {
  return DefaultProfileSyncConfig();
}

TEST(ProfileSyncConfig, DefaultCarriesTheRulingLists) {
  const ProfileSyncConfig config = Config();
  // The white list: local storage, extension state, and - by ruling 1B -
  // the history.
  EXPECT_NE(std::find(config.include.begin(), config.include.end(),
                      "*Local Storage*"),
            config.include.end());
  EXPECT_NE(std::find(config.include.begin(), config.include.end(),
                      "*Extension State*"),
            config.include.end());
  EXPECT_NE(std::find(config.include.begin(), config.include.end(),
                      "*History*"),
            config.include.end());
  // The black list.
  EXPECT_NE(std::find(config.exclude.begin(), config.exclude.end(), "*Cache*"),
            config.exclude.end());
  EXPECT_NE(std::find(config.exclude.begin(), config.exclude.end(),
                      "*Cookies*"),
            config.exclude.end());
  EXPECT_NE(std::find(config.exclude.begin(), config.exclude.end(),
                      "*Network*"),
            config.exclude.end());
  EXPECT_EQ(config.max_package_bytes, kProfileBundleMaxBytes);
}

TEST(MatchGlob, MatchesThePatternsTheListsUse) {
  // ⭐ These are the contract's glob fixed vectors (section 4.6.3),
  // copied one for one. The client repo asserts the same list in
  // `kernel_pipe/profile_rules.rs`, and
  // `tools/cross_repo_rules.py` compares the two sides against the
  // contract, so a vector dropped or added on one side is caught.
  EXPECT_TRUE(MatchGlob("*Local Storage*", "Default/Local Storage/leveldb/0001.log"));
  EXPECT_TRUE(MatchGlob("*Local Storage*", "Local Storage"));
  EXPECT_TRUE(MatchGlob("*History*", "Default/History"));
  // The one vector that exposes a case-insensitive matcher. Without
  // it a matcher that stopped comparing case would keep every one of
  // these assertions green while computing a different manifest from
  // the client's.
  EXPECT_FALSE(MatchGlob("*History*", "Default/history"));
  EXPECT_TRUE(MatchGlob("*Extension State*", "Default/Extension State/1.dat"));
  EXPECT_TRUE(MatchGlob("*Cache*", "Default/Cache/data_0"));
  // The two floor patterns, so the floor itself is pinned by a vector
  // and not only by the code that reads it.
  EXPECT_TRUE(MatchGlob("*Cookies*", "Default/Cookies"));
  EXPECT_TRUE(MatchGlob("*Network*", "Default/Network/Cookies"));
  // A pattern without a star matches only itself.
  EXPECT_TRUE(MatchGlob("History", "History"));
  EXPECT_FALSE(MatchGlob("History", "Default/History"));
  // '?' matches exactly one byte.
  EXPECT_TRUE(MatchGlob("file?.dat", "file1.dat"));
  EXPECT_FALSE(MatchGlob("file?.dat", "file12.dat"));
  EXPECT_TRUE(MatchGlob("a?c", "abc"));
  EXPECT_FALSE(MatchGlob("a?c", "ac"));
  // An empty pattern matches only an empty text.
  EXPECT_TRUE(MatchGlob("", ""));
  EXPECT_FALSE(MatchGlob("", "x"));
  // '*' matches any run of bytes, the empty one included.
  EXPECT_TRUE(MatchGlob("*", ""));
  EXPECT_TRUE(MatchGlob("*", "anything"));
  // '*' spans '/', so it eats a byte run and not a path segment.
  EXPECT_TRUE(MatchGlob("*.log", "a/b.log"));
  EXPECT_TRUE(MatchGlob("*History*", "Default/History/x"));
}

TEST(CollectProfileBundle, DefaultSetContainsHistory) {
  // Ruling 1B: the history is uploaded by default.
  ProfileBundle bundle;
  CollectProfileBundle(Config(),
                       {File("Default/History", 163840),
                        File("Default/Local Storage/leveldb/0001.log", 4096)},
                       &bundle);
  const std::vector<std::string> paths = Paths(bundle);
  ASSERT_EQ(paths.size(), 2u);
  EXPECT_EQ(paths[0], "Default/History");
  EXPECT_EQ(paths[1], "Default/Local Storage/leveldb/0001.log");
  EXPECT_EQ(bundle.total_bytes, 163840u + 4096u);
}

TEST(CollectProfileBundle, HistoryScopeIsStillThePatternsScope) {
  // 1B does not mean "the whole history directory": the collection range
  // is the pattern's, so a pattern that names one file collects that file
  // and not its neighbours.
  ProfileSyncConfig config = Config();
  config.include = {"Default/History"};
  ProfileBundle bundle;
  CollectProfileBundle(config,
                       {File("Default/History", 100),
                        File("Default/History-journal", 100),
                        File("Default/Preferences", 100)},
                       &bundle);
  const std::vector<std::string> paths = Paths(bundle);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "Default/History");
  EXPECT_EQ(bundle.excluded, 2u);
}

TEST(CollectProfileBundle, WhiteListHitsAndBlackListExcludes) {
  ProfileBundle bundle;
  CollectProfileBundle(
      Config(),
      {File("Default/Local Storage/leveldb/0001.log", 10),
       File("Default/Extension State/abc.dat", 20),
       File("Default/Cache/data_0", 30),
       File("Default/Cookies", 40),
       File("Default/Network Cache/data_1", 50),
       File("Default/Preferences", 60)},
      &bundle);
  const std::vector<std::string> paths = Paths(bundle);
  ASSERT_EQ(paths.size(), 2u);
  EXPECT_EQ(paths[0], "Default/Extension State/abc.dat");
  EXPECT_EQ(paths[1], "Default/Local Storage/leveldb/0001.log");
  EXPECT_EQ(bundle.excluded, 4u);
}

TEST(CollectProfileBundle, CookiesAndNetworkCacheNeverGoOut) {
  // The exclude list says so, but the configuration is remote input. A
  // platform that listed the cookies in the include list must still not
  // get them: the two patterns are a floor, not a preference.
  ProfileSyncConfig config;
  config.include = {"*"};
  config.exclude = {};
  ProfileBundle bundle;
  CollectProfileBundle(config,
                       {File("Default/Cookies", 10),
                        File("Default/Network/Cookies", 10),
                        File("Default/Local Storage/x", 10)},
                       &bundle);
  const std::vector<std::string> paths = Paths(bundle);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "Default/Local Storage/x");
}

TEST(CollectProfileBundle, ExcludeWinsOverInclude) {
  ProfileSyncConfig config;
  config.include = {"*Local Storage*"};
  config.exclude = {"*Cache*"};
  ProfileBundle bundle;
  CollectProfileBundle(config,
                       {File("Default/Local Storage/leveldb/Cache/0001.log", 10),
                        File("Default/Local Storage/leveldb/0002.log", 10)},
                       &bundle);
  const std::vector<std::string> paths = Paths(bundle);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "Default/Local Storage/leveldb/0002.log");
}

TEST(CollectProfileBundle, AnUnreadableFileIsSkippedAndCounted) {
  // Ruling 2A's kernel-side half: one file that cannot be read must not
  // fail the collection.
  ProfileFile locked = File("Default/Local Storage/locked.log", 100);
  locked.readable = false;
  ProfileBundle bundle;
  CollectProfileBundle(
      Config(), {locked, File("Default/Local Storage/ok.log", 10)}, &bundle);
  const std::vector<std::string> paths = Paths(bundle);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "Default/Local Storage/ok.log");
  EXPECT_EQ(bundle.skipped_unreadable, 1u);
  EXPECT_EQ(bundle.total_bytes, 10u);
}

TEST(CollectProfileBundle, TheCeilingCutsTheCollectionShort) {
  ProfileSyncConfig config = Config();
  config.max_package_bytes = 100;
  ProfileBundle bundle;
  CollectProfileBundle(config,
                       {File("Default/History", 60),
                        File("Default/Local Storage/a.log", 60),
                        File("Default/Local Storage/b.log", 10)},
                       &bundle);
  // The order is by path, so "Default/History" is first and fits; the
  // second file does not and cuts the collection short.
  const std::vector<std::string> paths = Paths(bundle);
  ASSERT_EQ(paths.size(), 1u);
  EXPECT_EQ(paths[0], "Default/History");
  EXPECT_TRUE(bundle.size_capped);
  EXPECT_EQ(bundle.truncated, 2u);
  EXPECT_LE(bundle.total_bytes, 100u);
}

TEST(CollectProfileBundle, TheCeilingIsNeverRaisedByTheConfiguration) {
  ProfileSyncConfig config = Config();
  config.max_package_bytes = kProfileBundleMaxBytes * 4;
  ProfileBundle bundle;
  CollectProfileBundle(config, {File("Default/History", 10)}, &bundle);
  EXPECT_EQ(bundle.size_capped, false);
}

TEST(CollectProfileBundle, TheOrderIsByPathNotByArrival) {
  // Two repos that hold the same profile must produce the same manifest
  // without exchanging anything, so the order cannot depend on the order
  // the caller's directory walk happened to produce.
  ProfileBundle first;
  CollectProfileBundle(Config(),
                       {File("Default/Local Storage/b.log", 1),
                        File("Default/History", 1),
                        File("Default/Extension State/a.dat", 1)},
                       &first);
  ProfileBundle second;
  CollectProfileBundle(Config(),
                       {File("Default/Extension State/a.dat", 1),
                        File("Default/History", 1),
                        File("Default/Local Storage/b.log", 1)},
                       &second);
  const std::vector<std::string> a = Paths(first);
  const std::vector<std::string> b = Paths(second);
  ASSERT_EQ(a.size(), 3u);
  EXPECT_EQ(a, b);
  EXPECT_EQ(a[0], "Default/Extension State/a.dat");
  EXPECT_EQ(a[1], "Default/History");
  EXPECT_EQ(a[2], "Default/Local Storage/b.log");
}

TEST(CollectProfileBundle, AnEmptyWhiteListCollectsNothing) {
  ProfileSyncConfig config;
  config.include = {};
  config.exclude = {};
  ProfileBundle bundle;
  CollectProfileBundle(config, {File("Default/History", 10)}, &bundle);
  EXPECT_TRUE(bundle.entries.empty());
  EXPECT_EQ(bundle.excluded, 1u);
}

TEST(ProfileSyncConfig, ParsesTheSectionTheRunnerSends) {
  const char kBundle[] =
      R"({"profile_sync":{"include":["*History*","*Local Storage*"],)"
      R"("exclude":["*Cache*","*Cookies*","*Network*"],)"
      R"("max_package_bytes":1048576}})";
  ProfileSyncConfig config;
  std::string error;
  ASSERT_TRUE(ParseProfileSyncConfig(kBundle, &config, &error)) << error;
  ASSERT_EQ(config.include.size(), 2u);
  EXPECT_EQ(config.include[0], "*History*");
  EXPECT_EQ(config.include[1], "*Local Storage*");
  ASSERT_EQ(config.exclude.size(), 3u);
  EXPECT_EQ(config.exclude[2], "*Network*");
  EXPECT_EQ(config.max_package_bytes, 1048576u);
}

TEST(ProfileSyncConfig, TheConfigurationMayLowerTheCeilingButNotRaiseIt) {
  const char kBundle[] =
      R"({"profile_sync":{"include":["*History*"],"max_package_bytes":999999999}})";
  ProfileSyncConfig config;
  std::string error;
  ASSERT_TRUE(ParseProfileSyncConfig(kBundle, &config, &error)) << error;
  EXPECT_EQ(config.max_package_bytes, kProfileBundleMaxBytes);
}

TEST(ProfileSyncConfig, ABundleWithoutTheSectionIsNotACollection) {
  const char kBundle[] = R"({"something_else":1})";
  ProfileSyncConfig config;
  std::string error;
  EXPECT_FALSE(ParseProfileSyncConfig(kBundle, &config, &error));
  EXPECT_FALSE(error.empty());
}

TEST(ProfileSyncConfig, AMalformedSectionIsRefusedNotPartiallyObeyed) {
  const char kBundle[] = R"({"profile_sync":{"include":"not an array"}})";
  ProfileSyncConfig config;
  std::string error;
  EXPECT_FALSE(ParseProfileSyncConfig(kBundle, &config, &error));
  EXPECT_FALSE(error.empty());
}

TEST(ProfileSyncConfig, AMalformedBundleIsRefused) {
  ProfileSyncConfig config;
  std::string error;
  EXPECT_FALSE(ParseProfileSyncConfig("{not json", &config, &error));
  EXPECT_FALSE(error.empty());
}

TEST(ProfileSyncConfig, TheDefaultSectionRoundTripsThroughTheParser) {
  // The runner sends the default when the platform does not override it.
  // The kernel's default and the runner's default must be the same rules.
  const ProfileSyncConfig kernel_default = DefaultProfileSyncConfig();
  std::string json = "{\"profile_sync\":{\"include\":[";
  for (std::size_t i = 0; i < kernel_default.include.size(); ++i) {
    if (i != 0) {
      json += ",";
    }
    json += "\"" + kernel_default.include[i] + "\"";
  }
  json += "],\"exclude\":[";
  for (std::size_t i = 0; i < kernel_default.exclude.size(); ++i) {
    if (i != 0) {
      json += ",";
    }
    json += "\"" + kernel_default.exclude[i] + "\"";
  }
  json += "]}}";
  ProfileSyncConfig parsed;
  std::string error;
  ASSERT_TRUE(ParseProfileSyncConfig(json, &parsed, &error)) << error;
  EXPECT_EQ(parsed.include, kernel_default.include);
  EXPECT_EQ(parsed.exclude, kernel_default.exclude);
  EXPECT_EQ(parsed.max_package_bytes, kernel_default.max_package_bytes);
}

TEST(ProfileSyncConfig, TheRunnerPayloadIsTheCrossRepoVector) {
  // The bytes the runner actually sends, hardcoded.
  //
  // The client repo hardcodes the same 155 bytes in
  // `crates/lengsuo-desktop/src/kernel_pipe/profile_vectors.rs`
  // (`CONFIG_BUNDLE_PAYLOAD`) and asserts that its own output matches
  // them. This is the kernel's copy of that vector.
  //
  // Why it is hardcoded rather than assembled: a test that built the
  // expected value out of the same constants it is checking would agree
  // with a broken implementation, and "both sides are wrong" would look
  // like "both sides match". The two repos compare the literals
  // directly -- `tools/cross_repo_vectors.py` reads this one and the
  // client's, and requires them to be identical.
  //
  // Note the payload carries `max_package_bytes` explicitly. The
  // round-trip test above leaves it out, which is a shorter string: the
  // runner's payload and the kernel's own assembly are not the same
  // bytes, and only the runner's bytes travel on the wire.
  constexpr char kRunnerPayload[] =
      R"({"profile_sync":{"include":["*Local Storage*","*Extension State*","*History*"],"exclude":["*Cache*","*Cookies*","*Network*"],"max_package_bytes":33554432}})";

  // The kernel's own default must produce exactly these rules.
  ProfileSyncConfig parsed;
  std::string error;
  ASSERT_TRUE(ParseProfileSyncConfig(kRunnerPayload, &parsed, &error))
      << error;

  const ProfileSyncConfig kernel_default = DefaultProfileSyncConfig();
  EXPECT_EQ(parsed.include, kernel_default.include);
  EXPECT_EQ(parsed.exclude, kernel_default.exclude);
  EXPECT_EQ(parsed.max_package_bytes, kernel_default.max_package_bytes);

  // The length is pinned because it is the frame header's `len` field.
  EXPECT_EQ(std::string(kRunnerPayload).size(), 155u);

  // The payload carries rules only: no key, no session key, no bitmap.
  const std::string payload(kRunnerPayload);
  EXPECT_EQ(payload.find("key"), std::string::npos);
  EXPECT_EQ(payload.find("data_url"), std::string::npos);
}

}  // namespace
}  // namespace lengsuo::fp
