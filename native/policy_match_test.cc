// policy_match_test - matching a request against the access policy (T2).
//
// Three entry points are tested here and the differences between them are
// deliberate:
//
//   * the URL matcher is a regular expression over the whole URL;
//   * the searcher reads the same expression over a part of the URL. It is the
//     convention the response monitor's rules are written in, and it is what
//     std::regex_search used to be - see SearchUrlPattern in policy_match.h
//     for why std::regex cannot be used at all;
//   * the host matcher is the glob the profile sync rules already use.
//
// The tests that matter most are the ones about the edges: a pattern that
// covers only part of the URL does not match, a pattern the kernel cannot
// read is rejected rather than half-applied, and a pattern written to make
// the matcher work hard is stopped instead of hanging the browser.
#include "policy_match.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace lengsuo::fp {
namespace {

TEST(PolicyMatch, TheHostMatcherIsTheProfileGlob) {
  // The same glob the profile sync rules use: '*' spans anything, '?' is one
  // byte, and the pattern is anchored at both ends.
  EXPECT_TRUE(MatchDomainPattern("*.example.test", "a.example.test"));
  EXPECT_TRUE(MatchDomainPattern("*.example.test", "a.b.example.test"));
  EXPECT_TRUE(MatchDomainPattern("example.test", "example.test"));
  EXPECT_FALSE(MatchDomainPattern("example.test", "notexample.test"));
  EXPECT_FALSE(MatchDomainPattern("*.example.test", "example.test"));
  EXPECT_TRUE(MatchDomainPattern("a?c.test", "abc.test"));
  EXPECT_FALSE(MatchDomainPattern("a?c.test", "abbc.test"));
}

TEST(PolicyMatch, TheUrlMatcherCoversTheWholeUrl) {
  // A pattern that describes only the middle of the URL does not match: the
  // pattern has to be about the whole URL.
  EXPECT_FALSE(MatchUrlPattern("example\\.test", "https://example.test/a"));
  EXPECT_TRUE(MatchUrlPattern("https://example\\.test/a",
                              "https://example.test/a"));
  // A pattern that covers the whole URL matches, and only that URL.
  EXPECT_TRUE(MatchUrlPattern("https://.*", "https://example.test/a"));
  EXPECT_FALSE(MatchUrlPattern("https://.*", "http://example.test/a"));
}

TEST(PolicyMatch, TheUrlMatcherReadsTheUsualSyntax) {
  EXPECT_TRUE(MatchUrlPattern("https://example\\.test/.*", "https://example.test/x"));
  EXPECT_TRUE(MatchUrlPattern(".*\\.ads\\..*", "https://a.ads.example/x"));
  EXPECT_FALSE(MatchUrlPattern(".*\\.ads\\..*", "https://a.example/x"));
  EXPECT_TRUE(MatchUrlPattern("a+b", "aab"));
  EXPECT_TRUE(MatchUrlPattern("a+b", "ab"));
  EXPECT_FALSE(MatchUrlPattern("a+b", "aabbbc"));
  EXPECT_TRUE(MatchUrlPattern("colou?r", "color"));
  EXPECT_TRUE(MatchUrlPattern("colou?r", "colour"));
  EXPECT_TRUE(MatchUrlPattern("a{2,3}", "aa"));
  EXPECT_TRUE(MatchUrlPattern("a{2,3}", "aaa"));
  EXPECT_FALSE(MatchUrlPattern("a{2,3}", "a"));
  EXPECT_FALSE(MatchUrlPattern("a{2,3}", "aaaa"));
  EXPECT_TRUE(MatchUrlPattern("(ab|cd)ef", "abef"));
  EXPECT_TRUE(MatchUrlPattern("(ab|cd)ef", "cdef"));
  EXPECT_FALSE(MatchUrlPattern("(ab|cd)ef", "aef"));
  EXPECT_TRUE(MatchUrlPattern("[a-c]x", "bx"));
  EXPECT_FALSE(MatchUrlPattern("[a-c]x", "dx"));
  EXPECT_TRUE(MatchUrlPattern("[^a-c]x", "dx"));
  EXPECT_FALSE(MatchUrlPattern("[^a-c]x", "bx"));
  EXPECT_TRUE(MatchUrlPattern("\\d+", "12345"));
  EXPECT_FALSE(MatchUrlPattern("\\d+", "12a45"));
  EXPECT_TRUE(MatchUrlPattern("\\w+\\s\\w+", "hello world"));
  EXPECT_FALSE(MatchUrlPattern("\\w+\\s\\w+", "hello_world"));
}

TEST(PolicyMatch, TheAnchorsSayTheSameThingUnderAFullMatch) {
  // The match has to cover the whole URL, so the URL's own start and end
  // are already the anchors. Writing them says the same thing.
  EXPECT_TRUE(MatchUrlPattern("^https://a\\.test$", "https://a.test"));
  EXPECT_TRUE(MatchUrlPattern("https://a\\.test", "https://a.test"));
  // And a pattern that pins the URL down does not match a longer URL.
  EXPECT_FALSE(MatchUrlPattern("^https://a\\.test$", "https://a.test/b"));
  EXPECT_FALSE(MatchUrlPattern("^https://a\\.test$", "http://a.test"));
}

TEST(PolicyMatch, TheWholeUrlIsTheSubject) {
  // A pattern that names only the middle of the URL does not describe the
  // URL, so it does not match it.
  EXPECT_FALSE(MatchUrlPattern("example\\.test", "https://example.test/a"));
  // To say "any URL that mentions it", the pattern has to say so.
  EXPECT_TRUE(MatchUrlPattern(".*example\\.test.*", "https://example.test/a"));
}

TEST(PolicyMatch, TheUrlMatcherMatchesTheWholeStringOnly) {
  // ".*" on its own matches everything, including the empty URL.
  EXPECT_TRUE(MatchUrlPattern(".*", ""));
  EXPECT_TRUE(MatchUrlPattern(".*", "https://example.test/a"));
  // A pattern with nothing in it matches only the empty URL.
  EXPECT_TRUE(MatchUrlPattern("", ""));
  EXPECT_FALSE(MatchUrlPattern("", "x"));
}

TEST(PolicyMatch, AGroupIsNotCapturedButStillGroups) {
  // The group changes what the pattern means, not what it keeps.
  EXPECT_TRUE(MatchUrlPattern("(ab)+", "abab"));
  EXPECT_TRUE(MatchUrlPattern("(ab)+", "ab"));
  EXPECT_FALSE(MatchUrlPattern("(ab)+", "a"));
}

TEST(PolicyMatch, AClassMayHoldANegatedShorthand) {
  EXPECT_TRUE(MatchUrlPattern("[\\D]+", "abc"));
  EXPECT_FALSE(MatchUrlPattern("[\\D]+", "ab1"));
}

TEST(PolicyMatch, AClassMayHoldARange) {
  EXPECT_TRUE(MatchUrlPattern("[0-9a-f]+", "00ff"));
  EXPECT_FALSE(MatchUrlPattern("[0-9a-f]+", "00fg"));
  // A range with its ends the wrong way round is not a range.
  EXPECT_FALSE(IsValidUrlPattern("[z-a]"));
}

TEST(PolicyMatch, AClassMayEndWithADash) {
  EXPECT_TRUE(MatchUrlPattern("[a-]+", "a--"));
  EXPECT_FALSE(MatchUrlPattern("[a-]+", "ab"));
}

TEST(PolicyMatch, AnUnreadablePatternIsRejected) {
  // Every one of these is a pattern the kernel cannot read. Each of them
  // would have to be dropped and counted when the policy is read.
  for (const char* pattern :
       {"(", ")", "[", "[a", "a{", "a{2,1}", "*a", "a**", "a*?", "\\", "a{1,",
        "[\\d-\\w]"}) {
    EXPECT_FALSE(IsValidUrlPattern(pattern)) << pattern;
  }
}

TEST(PolicyMatch, AReadablePatternIsAccepted) {
  for (const char* pattern :
       {"", "a", "a*", "a+", "a?", "a{2}", "a{2,}", "a{2,3}", "(a|b)", "[a-z]",
        "\\d", "\\.", "^a$", "https://.*"}) {
    EXPECT_TRUE(IsValidUrlPattern(pattern)) << pattern;
  }
}

TEST(PolicyMatch, AnUnreadablePatternMatchesNothing) {
  // A pattern that could not be read is a rule that does not fire. It must
  // not match a URL by accident.
  EXPECT_FALSE(MatchUrlPattern("(unclosed", "https://example.test/"));
  EXPECT_FALSE(MatchUrlPattern("[unclosed", "https://example.test/"));
}

TEST(PolicyMatch, APatternThatWorksHardIsStopped) {
  // The classic backtracking bomb: (a+a+)+b against a long run of a's.
  // The matcher has to give up rather than hang the browser.
  const std::string bomb = "(a+a+)+b";
  const std::string subject(2000, 'a');
  EXPECT_FALSE(MatchUrlPattern(bomb, subject));
  // A pattern that backtracks a lot but does match still matches: the
  // budget stops a runaway, it does not turn every hard pattern into a
  // non-match.
  EXPECT_TRUE(MatchUrlPattern("(a+)+b", "aaab"));
}

TEST(PolicyMatch, AHostPatternIsNotAUrlPattern) {
  // The two matchers are different on purpose and neither stands in for the
  // other.
  EXPECT_FALSE(MatchDomainPattern("https://example.test", "example.test"));
  EXPECT_FALSE(MatchUrlPattern("*.example.test", "a.example.test"));
}

TEST(PolicyMatch, TheSearcherFindsAPatternInsideTheUrl) {
  // The response monitor's rules name an endpoint or a prefix, not a whole
  // address, so its entry point is the searcher: the same language and the
  // same engine as the whole-URL matcher, and only the beginning and the end
  // of the match are free.
  EXPECT_TRUE(SearchUrlPattern("example\\.test", "https://example.test/a"));
  EXPECT_FALSE(SearchUrlPattern("example\\.test", "https://other.test/a"));
  // The whole-URL matcher says the opposite about the same pattern, which is
  // the whole point of having the two.
  EXPECT_FALSE(MatchUrlPattern("example\\.test", "https://example.test/a"));
}
TEST(PolicyMatch, TheSearcherKeepsTheAnchors) {
  // '^' is still the start of the whole URL and '$' its end, so a pattern that
  // pins itself down stays pinned down under the searcher.
  EXPECT_TRUE(SearchUrlPattern("^https://a\\.test/", "https://a.test/b"));
  EXPECT_FALSE(SearchUrlPattern("^https://a\\.test/", "http://a.test/b"));
  EXPECT_TRUE(SearchUrlPattern("/b$", "https://a.test/b"));
  EXPECT_FALSE(SearchUrlPattern("/b$", "https://a.test/b/c"));
}
TEST(PolicyMatch, TheSearcherRejectsWhatTheParserRejects) {
  // One parser, so a pattern the whole-URL matcher refuses is refused here
  // too, and a refused pattern matches nothing rather than a part.
  EXPECT_FALSE(IsValidUrlPattern("([unclosed"));
  EXPECT_FALSE(SearchUrlPattern("([unclosed", "https://example.test/a"));
  EXPECT_FALSE(SearchUrlPattern("*bad", "https://example.test/a"));
}
TEST(PolicyMatch, TheSearcherAndTheMatcherAgreeOnAWholeUrlPattern) {
  // A pattern that describes the whole URL matches both ways, so the two entry
  // points differ only in where the match may begin and end.
  EXPECT_TRUE(MatchUrlPattern("https://example\\.test/a",
                              "https://example.test/a"));
  EXPECT_TRUE(SearchUrlPattern("https://example\\.test/a",
                               "https://example.test/a"));
  EXPECT_TRUE(SearchUrlPattern(".*", "https://example.test/a"));
  // The searcher is the looser of the two, and that loosening is the whole
  // difference: the same pattern also matches a URL that merely starts with
  // what it describes. A rule that wants the whole URL has to say so itself,
  // and the response monitor is the layer where that matters.
  EXPECT_FALSE(MatchUrlPattern("https://example\\.test/a",
                               "https://example.test/a/b"));
  EXPECT_TRUE(SearchUrlPattern("https://example\\.test/a",
                               "https://example.test/a/b"));
  // An empty pattern matches the empty run at position zero, which is what
  // std::regex_search did with it as well.
  EXPECT_TRUE(SearchUrlPattern("", "x"));
}
}  // namespace
}  // namespace lengsuo::fp
