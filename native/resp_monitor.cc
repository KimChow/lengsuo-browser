#include "resp_monitor.h"

#include <regex>
#include <utility>

namespace lengsuo::fp {

void RespMonitorSetRules(RespMonitorState* state,
                         const RespMonitorConfig& config) {
  if (state == nullptr) {
    return;
  }
  state->rules.clear();
  if (config.enabled) {
    for (const RespMonitorRule& rule : config.rules) {
      // A rule that says it is off never reaches the config's rule list, so
      // everything here is on. A rule without a matcher is not usable and is
      // dropped.
      if (rule.matcher != nullptr) {
        state->rules.push_back(rule);
      }
    }
  }

  // Drop the counters and the switched-off marks of the rules that are no
  // longer installed. The counters of the rules that stay are kept: pushing
  // the same configuration down again - once per storage partition, for
  // instance - must not hand a rule a fresh allowance.
  std::set<std::string> installed;
  for (const RespMonitorRule& rule : state->rules) {
    installed.insert(rule.id);
  }
  for (auto it = state->hits.begin(); it != state->hits.end();) {
    if (installed.count(it->first.first) == 0) {
      it = state->hits.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = state->disabled_rules.begin();
       it != state->disabled_rules.end();) {
    if (installed.count(*it) == 0) {
      it = state->disabled_rules.erase(it);
    } else {
      ++it;
    }
  }
}

bool IsRespMonitorRequestEligible(const RespMonitorRequest& request) {
  // Only http(s). `data:`, `blob:`, `chrome:`, `chrome-extension:`, `file:`
  // and the rest are not responses the monitor reads.
  if (request.scheme != "http" && request.scheme != "https") {
    return false;
  }
  // A request with no initiator origin is the browser's own request: the
  // runner's traffic, the component updater, a typed navigation. None of
  // those is a page, and the monitor watches pages.
  if (request.initiator_scheme.empty()) {
    return false;
  }
  // An extension's request, whether it asks for an http(s) URL or not.
  if (request.initiator_scheme == "chrome-extension") {
    return false;
  }
  return true;
}

const RespMonitorRule* MatchRespMonitorRule(RespMonitorState* state,
                                            const RespMonitorRequest& request,
                                            std::string_view content_type) {
  if (state == nullptr || state->rules.empty()) {
    return nullptr;
  }
  if (!IsRespMonitorRequestEligible(request)) {
    return nullptr;
  }
  const std::string normalized = NormalizeRespMonitorContentType(content_type);
  for (const RespMonitorRule& rule : state->rules) {
    if (state->disabled_rules.count(rule.id) != 0) {
      continue;
    }
    if (rule.matcher == nullptr) {
      continue;
    }
    if (!std::regex_search(request.url.cbegin(), request.url.cend(),
                           *rule.matcher)) {
      continue;
    }
    if (!rule.content_type.empty() && rule.content_type != normalized) {
      continue;
    }
    const auto key = std::make_pair(rule.id, request.url);
    auto it = state->hits.find(key);
    const std::uint64_t used = it == state->hits.end() ? 0 : it->second;
    if (used >= rule.max_count) {
      // This rule has said what it had to say about this URL.
      continue;
    }
    if (it == state->hits.end()) {
      if (state->hits.size() >= kRespMonitorMaxTrackedUrls) {
        // The counter map is the one thing that grows with traffic. It is
        // full, so the rule that would grow it further is switched off.
        RespMonitorDisableRule(state, rule.id);
        continue;
      }
      state->hits.emplace(key, 1);
    } else {
      it->second = used + 1;
    }
    return &rule;
  }
  return nullptr;
}

void RespMonitorDisableRule(RespMonitorState* state,
                            const std::string& rule_id) {
  if (state == nullptr) {
    return;
  }
  if (state->disabled_rules.insert(rule_id).second) {
    ++state->failures;
  }
}

}  // namespace lengsuo::fp
