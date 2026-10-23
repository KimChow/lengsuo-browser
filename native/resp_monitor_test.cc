// resp_monitor_test - the matching and the counting (task T1).
//
// Three questions decide whether a response is watched: may it be looked at
// at all, which rule wants it, and has that rule already said enough about
// that URL. The tests answer all three, and they answer them for the cases
// the platform cares about: a rule that does not compile, a content type
// that does not match, a URL that has been seen too often, and a rule set
// that is switched off.
#include "resp_monitor.h"

#include <gtest/gtest.h>

#include <string>

namespace lengsuo::fp {
namespace {

// A request from a page: an https URL with an initiator origin.
RespMonitorRequest PageRequest(const std::string& url) {
  RespMonitorRequest request;
  request.url = url;
  request.scheme = "https";
  request.initiator_scheme = "https";
  return request;
}

// Builds one rule from its text. The pattern is kept as written and is
// evaluated by the kernel's matcher when a response is checked.
RespMonitorRule MakeRule(const std::string& id,
                          const std::string& pattern,
                          std::uint64_t max_count) {
  RespMonitorRule rule;
  rule.id = id;
  rule.url_regex = pattern;
  rule.max_count = max_count;
  return rule;
}

// The state with one rule installed.
RespMonitorState StateWith(const RespMonitorRule& rule) {
  RespMonitorConfig config;
  config.enabled = true;
  config.rules.push_back(rule);
  RespMonitorState state;
  RespMonitorSetRules(&state, config);
  return state;
}

TEST(RespMonitor, AnOffConfigurationInstallsNothing) {
  RespMonitorConfig config;
  config.enabled = false;
  config.rules.push_back(MakeRule("r1", "^https://example\\.test/", 3));
  RespMonitorState state;
  RespMonitorSetRules(&state, config);
  // The whole feature is off: there are no rules, so nothing is registered,
  // nothing is counted and no buffer is held.
  EXPECT_TRUE(state.rules.empty());
  EXPECT_TRUE(state.hits.empty());
  EXPECT_EQ(MatchRespMonitorRule(&state, PageRequest("https://example.test/a"),
                                 "text/plain"),
            nullptr);
}

TEST(RespMonitor, AnEnabledConfigurationWithNoRulesMatchesNothing) {
  RespMonitorConfig config;
  config.enabled = true;
  RespMonitorState state;
  RespMonitorSetRules(&state, config);
  EXPECT_TRUE(state.rules.empty());
  EXPECT_EQ(MatchRespMonitorRule(&state, PageRequest("https://example.test/a"),
                                 "text/plain"),
            nullptr);
}

TEST(RespMonitor, ARuleThatDoesNotCompileIsNeverInstalled) {
  // The configuration layer drops such a rule; the matcher never sees one.
  RespMonitorConfig config;
  config.enabled = true;
  RespMonitorState state;
  RespMonitorSetRules(&state, config);
  EXPECT_TRUE(state.rules.empty());
}

TEST(RespMonitor, TheUrlIsMatchedByRegularExpression) {
  RespMonitorState state = StateWith(MakeRule("r1", "^https://example\\.test/a", 3));
  const RespMonitorRule* rule =
      MatchRespMonitorRule(&state, PageRequest("https://example.test/a"), "");
  ASSERT_NE(rule, nullptr);
  EXPECT_EQ(rule->id, "r1");
  // The pattern is anchored, so a different path does not match.
  EXPECT_EQ(MatchRespMonitorRule(&state, PageRequest("https://example.test/b"), ""),
            nullptr);
}

TEST(RespMonitor, ThePatternIsSearchedNotAnchored) {
  // A rule that wants the whole URL says so with its own anchors; a rule
  // that does not is a rule about a part of the URL.
  RespMonitorState state = StateWith(MakeRule("r1", "example\\.test", 3));
  EXPECT_NE(MatchRespMonitorRule(&state, PageRequest("https://example.test/a"), ""),
            nullptr);
  EXPECT_EQ(MatchRespMonitorRule(&state, PageRequest("https://other.test/a"), ""),
            nullptr);
}

TEST(RespMonitor, TheContentTypeFiltersAfterTheUrl) {
  RespMonitorRule rule = MakeRule("r1", "^https://example\\.test/", 3);
  rule.content_type = "text/html";
  RespMonitorState state = StateWith(rule);
  // The right URL and the wrong content type: no match.
  EXPECT_EQ(MatchRespMonitorRule(&state, PageRequest("https://example.test/a"),
                                 "application/json"),
            nullptr);
  // The right URL and the right content type: a match.
  EXPECT_NE(MatchRespMonitorRule(&state, PageRequest("https://example.test/a"),
                                 "text/html"),
            nullptr);
}

TEST(RespMonitor, TheContentTypeIsComparedWithoutItsParameters) {
  RespMonitorRule rule = MakeRule("r1", "^https://example\\.test/", 3);
  rule.content_type = "text/html";
  RespMonitorState state = StateWith(rule);
  EXPECT_NE(MatchRespMonitorRule(&state, PageRequest("https://example.test/a"),
                                 "Text/HTML; charset=utf-8"),
            nullptr);
}

TEST(RespMonitor, TheCountIsPerRuleAndPerUrl) {
  RespMonitorState state = StateWith(MakeRule("r1", "^https://example\\.test/", 2));
  // The same URL twice is allowed.
  EXPECT_NE(MatchRespMonitorRule(&state, PageRequest("https://example.test/a"), ""),
            nullptr);
  EXPECT_NE(MatchRespMonitorRule(&state, PageRequest("https://example.test/a"), ""),
            nullptr);
  // The third time is over the limit: this URL is not watched any more.
  EXPECT_EQ(MatchRespMonitorRule(&state, PageRequest("https://example.test/a"), ""),
            nullptr);
  // A different URL has its own allowance.
  EXPECT_NE(MatchRespMonitorRule(&state, PageRequest("https://example.test/b"), ""),
            nullptr);
}

TEST(RespMonitor, TheCountIsPerRule) {
  RespMonitorConfig config;
  config.enabled = true;
  config.rules.push_back(MakeRule("r1", "^https://example\\.test/a", 1));
  config.rules.push_back(MakeRule("r2", "^https://example\\.test/a", 1));
  RespMonitorState state;
  RespMonitorSetRules(&state, config);
  const RespMonitorRequest request = PageRequest("https://example.test/a");
  const RespMonitorRule* first = MatchRespMonitorRule(&state, request, "");
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->id, "r1");
  // The first rule has spent its allowance on this URL, so the second one
  // gets the URL.
  const RespMonitorRule* second = MatchRespMonitorRule(&state, request, "");
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->id, "r2");
  // And now both have.
  EXPECT_EQ(MatchRespMonitorRule(&state, request, ""), nullptr);
}

TEST(RespMonitor, TheBrowserItsOwnRequestIsNotWatched) {
  RespMonitorState state = StateWith(MakeRule("r1", "^https://example\\.test/", 3));
  RespMonitorRequest request = PageRequest("https://example.test/a");
  // No initiator origin: the request is the browser's own.
  request.initiator_scheme.clear();
  EXPECT_FALSE(IsRespMonitorRequestEligible(request));
  EXPECT_EQ(MatchRespMonitorRule(&state, request, ""), nullptr);
}

TEST(RespMonitor, AnExtensionsRequestIsNotWatched) {
  RespMonitorState state = StateWith(MakeRule("r1", "^https://example\\.test/", 3));
  RespMonitorRequest request = PageRequest("https://example.test/a");
  request.initiator_scheme = "chrome-extension";
  EXPECT_FALSE(IsRespMonitorRequestEligible(request));
  EXPECT_EQ(MatchRespMonitorRule(&state, request, ""), nullptr);
}

TEST(RespMonitor, ANonHttpSchemeIsNotWatched) {
  RespMonitorState state = StateWith(MakeRule("r1", "example", 3));
  for (const char* scheme : {"data", "blob", "chrome", "file", "about", "ftp"}) {
    RespMonitorRequest request = PageRequest("example.test/a");
    request.scheme = scheme;
    EXPECT_FALSE(IsRespMonitorRequestEligible(request)) << scheme;
    EXPECT_EQ(MatchRespMonitorRule(&state, request, ""), nullptr) << scheme;
  }
}

TEST(RespMonitor, APageRequestIsEligible) {
  RespMonitorState state = StateWith(MakeRule("r1", "^https://example\\.test/", 3));
  const RespMonitorRequest request = PageRequest("https://example.test/a");
  EXPECT_TRUE(IsRespMonitorRequestEligible(request));
}

TEST(RespMonitor, ADisabledRuleStopsMatching) {
  RespMonitorState state = StateWith(MakeRule("r1", "^https://example\\.test/", 3));
  RespMonitorDisableRule(&state, "r1");
  EXPECT_EQ(MatchRespMonitorRule(&state, PageRequest("https://example.test/a"), ""),
            nullptr);
  // The failure is counted, once.
  EXPECT_EQ(state.failures, 1u);
  RespMonitorDisableRule(&state, "r1");
  EXPECT_EQ(state.failures, 1u);
}

TEST(RespMonitor, TheCounterMapIsBounded) {
  RespMonitorState state = StateWith(MakeRule("r1", "^https://example\\.test/", 1));
  // Fill the counter map up to its bound.
  for (std::size_t i = 0; i < kRespMonitorMaxTrackedUrls; ++i) {
    const RespMonitorRequest request =
        PageRequest("https://example.test/" + std::to_string(i));
    EXPECT_NE(MatchRespMonitorRule(&state, request, ""), nullptr) << i;
  }
  EXPECT_EQ(state.hits.size(), kRespMonitorMaxTrackedUrls);
  // The next URL would need one more entry, so the rule that would grow it
  // is switched off and the failure is counted.
  const RespMonitorRequest over =
      PageRequest("https://example.test/" + std::to_string(kRespMonitorMaxTrackedUrls));
  EXPECT_EQ(MatchRespMonitorRule(&state, over, ""), nullptr);
  EXPECT_EQ(state.disabled_rules.count("r1"), 1u);
  EXPECT_EQ(state.failures, 1u);
}

TEST(RespMonitor, InstallingTheRulesAgainDoesNotResetTheCounters) {
  RespMonitorConfig config;
  config.enabled = true;
  config.rules.push_back(MakeRule("r1", "^https://example\\.test/", 1));
  RespMonitorState state;
  RespMonitorSetRules(&state, config);
  EXPECT_NE(MatchRespMonitorRule(&state, PageRequest("https://example.test/a"), ""),
            nullptr);
  // The configuration is pushed down again - once per storage partition,
  // perhaps - and the rule does not get a fresh allowance for it.
  RespMonitorSetRules(&state, config);
  EXPECT_EQ(MatchRespMonitorRule(&state, PageRequest("https://example.test/a"), ""),
            nullptr);
}

TEST(RespMonitor, ARuleThatIsNoLongerInstalledTakesItsCountersWithIt) {
  RespMonitorConfig config;
  config.enabled = true;
  config.rules.push_back(MakeRule("r1", "^https://example\\.test/", 3));
  RespMonitorState state;
  RespMonitorSetRules(&state, config);
  EXPECT_NE(MatchRespMonitorRule(&state, PageRequest("https://example.test/a"), ""),
            nullptr);
  ASSERT_EQ(state.hits.size(), 1u);
  config.rules.clear();
  RespMonitorSetRules(&state, config);
  EXPECT_TRUE(state.hits.empty());
  EXPECT_TRUE(state.rules.empty());
}

}  // namespace
}  // namespace lengsuo::fp
