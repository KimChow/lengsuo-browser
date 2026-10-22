#include "fp_target_match.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include "profile_bundle.h"

namespace lengsuo::fp {
namespace {

// ---------------------------------------------------------------------------
// The pattern's syntax tree
// ---------------------------------------------------------------------------

struct RegexNode {
  enum class Kind {
    kLiteral,
    kAny,
    kClass,
    kConcat,
    kAlternate,
    kRepeat,
    kAnchorStart,
    kAnchorEnd,
  };
  Kind kind = Kind::kLiteral;
  std::uint8_t literal = 0;
  bool negated = false;
  std::vector<std::pair<std::uint8_t, std::uint8_t>> ranges;
  std::vector<std::size_t> children;
  std::uint32_t min = 1;
  std::uint32_t max = 1;
};

using Ranges = std::vector<std::pair<std::uint8_t, std::uint8_t>>;

// The complement of a set of ranges over the 256 bytes.
Ranges ComplementRanges(const Ranges& ranges) {
  Ranges sorted = ranges;
  std::sort(sorted.begin(), sorted.end());
  Ranges result;
  std::uint32_t next = 0;
  for (const auto& range : sorted) {
    if (range.first > next) {
      result.emplace_back(static_cast<std::uint8_t>(next),
                          static_cast<std::uint8_t>(range.first - 1));
    }
    next = std::max(next, static_cast<std::uint32_t>(range.second) + 1);
    if (next > 255) {
      break;
    }
  }
  if (next <= 255) {
    result.emplace_back(static_cast<std::uint8_t>(next), 255);
  }
  return result;
}

Ranges DigitRanges() {
  return {{'0', '9'}};
}

Ranges WordRanges() {
  return {{'0', '9'}, {'A', 'Z'}, {'a', 'z'}, {'_', '_'}};
}

Ranges SpaceRanges() {
  return {{' ', ' '}, {'\t', '\t'}, {'\n', '\n'}, {'\r', '\r'}, {'\f', '\f'},
          {'\v', '\v'}};
}

// ---------------------------------------------------------------------------
// The parser
// ---------------------------------------------------------------------------

class RegexParser {
 public:
  explicit RegexParser(std::string_view pattern) : pattern_(pattern) {}

  bool Parse(std::vector<RegexNode>* nodes, std::size_t* root);

 private:
  bool ParsePattern(std::vector<RegexNode>* nodes, std::size_t* out);
  bool ParseConcat(std::vector<RegexNode>* nodes, std::size_t* out);
  bool ParseRepeat(std::vector<RegexNode>* nodes, std::size_t* out);
  bool ParseAtom(std::vector<RegexNode>* nodes, std::size_t* out);
  bool ParseEscape(std::vector<RegexNode>* nodes, std::size_t* out);
  bool ParseClass(std::vector<RegexNode>* nodes, std::size_t* out);
  bool ParseEscapeByte(Ranges* ranges,
                       std::uint8_t* single,
                       bool* has_single);

  std::string_view pattern_;
  std::size_t pos_ = 0;
};

// Reads the byte that follows a backslash.
//
// A set escape produces a set of ranges; every other escape produces one
// byte. `has_single` says which of the two it was.
bool RegexParser::ParseEscapeByte(Ranges* ranges,
                                  std::uint8_t* single,
                                  bool* has_single) {
  if (pos_ >= pattern_.size()) {
    return false;
  }
  const char c = pattern_[pos_];
  ++pos_;
  switch (c) {
    case 'd':
      *ranges = DigitRanges();
      *has_single = false;
      return true;
    case 'D':
      *ranges = ComplementRanges(DigitRanges());
      *has_single = false;
      return true;
    case 'w':
      *ranges = WordRanges();
      *has_single = false;
      return true;
    case 'W':
      *ranges = ComplementRanges(WordRanges());
      *has_single = false;
      return true;
    case 's':
      *ranges = SpaceRanges();
      *has_single = false;
      return true;
    case 'S':
      *ranges = ComplementRanges(SpaceRanges());
      *has_single = false;
      return true;
    default:
      break;
  }
  *ranges = {{static_cast<std::uint8_t>(c), static_cast<std::uint8_t>(c)}};
  *single = static_cast<std::uint8_t>(c);
  *has_single = true;
  return true;
}

bool RegexParser::ParseEscape(std::vector<RegexNode>* nodes,
                              std::size_t* out) {
  ++pos_;  // the backslash
  if (pos_ >= pattern_.size()) {
    return false;
  }
  Ranges ranges;
  std::uint8_t single = 0;
  bool has_single = false;
  if (!ParseEscapeByte(&ranges, &single, &has_single)) {
    return false;
  }
  RegexNode node;
  if (has_single) {
    node.kind = RegexNode::Kind::kLiteral;
    node.literal = single;
  } else {
    node.kind = RegexNode::Kind::kClass;
    node.ranges = ranges;
  }
  nodes->push_back(node);
  *out = nodes->size() - 1;
  return true;
}

bool RegexParser::ParseClass(std::vector<RegexNode>* nodes,
                              std::size_t* out) {
  ++pos_;  // the '['
  bool negated = false;
  if (pos_ < pattern_.size() && pattern_[pos_] == '^') {
    negated = true;
    ++pos_;
  }
  Ranges ranges;
  bool first = true;
  while (true) {
    if (pos_ >= pattern_.size()) {
      return false;
    }
    if (pattern_[pos_] == ']' && !first) {
      ++pos_;
      break;
    }
    Ranges element;
    std::uint8_t single = 0;
    bool has_single = false;
    if (!ParseEscapeByte(&element, &single, &has_single)) {
      return false;
    }
    // A '-' between two single bytes is a range. A '-' after a set is
    // the byte '-', because a set has no end to range from.
    if (has_single && pos_ + 1 < pattern_.size() &&
        pattern_[pos_] == '-' && pattern_[pos_ + 1] != ']') {
      ++pos_;
      Ranges high_element;
      std::uint8_t high_single = 0;
      bool high_has_single = false;
      if (!ParseEscapeByte(&high_element, &high_single, &high_has_single)) {
        return false;
      }
      if (!high_has_single || single > high_single) {
        return false;
      }
      ranges.emplace_back(single, high_single);
    } else {
      for (const auto& range : element) {
        ranges.push_back(range);
      }
    }
    first = false;
  }
  RegexNode node;
  node.kind = RegexNode::Kind::kClass;
  node.negated = negated;
  node.ranges = ranges;
  nodes->push_back(node);
  *out = nodes->size() - 1;
  return true;
}

bool RegexParser::ParseAtom(std::vector<RegexNode>* nodes, std::size_t* out) {
  if (pos_ >= pattern_.size()) {
    return false;
  }
  const char c = pattern_[pos_];
  if (c == '(') {
    ++pos_;
    std::size_t inner = 0;
    if (!ParsePattern(nodes, &inner)) {
      return false;
    }
    if (pos_ >= pattern_.size() || pattern_[pos_] != ')') {
      return false;
    }
    ++pos_;
    *out = inner;
    return true;
  }
  if (c == '[') {
    return ParseClass(nodes, out);
  }
  if (c == '\\') {
    return ParseEscape(nodes, out);
  }
  if (c == '|' || c == ')' || c == '*' || c == '+' || c == '?') {
    // A quantifier without an element before it, or a separator where an
    // element belongs.
    return false;
  }
  ++pos_;
  RegexNode node;
  node.kind = RegexNode::Kind::kLiteral;
  node.literal = static_cast<std::uint8_t>(c);
  if (c == '.') {
    node.kind = RegexNode::Kind::kAny;
  } else if (c == '^') {
    node.kind = RegexNode::Kind::kAnchorStart;
  } else if (c == '$') {
    node.kind = RegexNode::Kind::kAnchorEnd;
  }
  nodes->push_back(node);
  *out = nodes->size() - 1;
  return true;
}

bool RegexParser::ParseRepeat(std::vector<RegexNode>* nodes, std::size_t* out) {
  std::size_t atom = 0;
  if (!ParseAtom(nodes, &atom)) {
    return false;
  }
  if (pos_ >= pattern_.size()) {
    *out = atom;
    return true;
  }
  const char c = pattern_[pos_];
  std::uint32_t min = 1;
  std::uint32_t max = 1;
  if (c == '*') {
    min = 0;
    max = std::numeric_limits<std::uint32_t>::max();
    ++pos_;
  } else if (c == '+') {
    min = 1;
    max = std::numeric_limits<std::uint32_t>::max();
    ++pos_;
  } else if (c == '?') {
    min = 0;
    max = 1;
    ++pos_;
  } else {
    *out = atom;
    return true;
  }
  RegexNode node;
  node.kind = RegexNode::Kind::kRepeat;
  node.children.push_back(atom);
  node.min = min;
  node.max = max;
  nodes->push_back(node);
  *out = nodes->size() - 1;
  return true;
}

bool RegexParser::ParseConcat(std::vector<RegexNode>* nodes,
                               std::size_t* out) {
  std::vector<std::size_t> children;
  while (pos_ < pattern_.size() && pattern_[pos_] != '|' &&
         pattern_[pos_] != ')') {
    std::size_t child = 0;
    if (!ParseRepeat(nodes, &child)) {
      return false;
    }
    children.push_back(child);
  }
  RegexNode node;
  node.kind = RegexNode::Kind::kConcat;
  node.children = children;
  nodes->push_back(node);
  *out = nodes->size() - 1;
  return true;
}

bool RegexParser::ParsePattern(std::vector<RegexNode>* nodes,
                               std::size_t* out) {
  std::vector<std::size_t> alternatives;
  while (true) {
    std::size_t concat = 0;
    if (!ParseConcat(nodes, &concat)) {
      return false;
    }
    alternatives.push_back(concat);
    if (pos_ < pattern_.size() && pattern_[pos_] == '|') {
      ++pos_;
      continue;
    }
    break;
  }
  if (alternatives.size() == 1) {
    *out = alternatives[0];
    return true;
  }
  RegexNode node;
  node.kind = RegexNode::Kind::kAlternate;
  node.children = alternatives;
  nodes->push_back(node);
  *out = nodes->size() - 1;
  return true;
}

bool RegexParser::Parse(std::vector<RegexNode>* nodes, std::size_t* root) {
  std::size_t parsed = 0;
  if (!ParsePattern(nodes, &parsed)) {
    return false;
  }
  // The whole pattern must be consumed: a leftover byte means the pattern
  // is malformed, and a malformed pattern never matches.
  if (pos_ != pattern_.size()) {
    return false;
  }
  *root = parsed;
  return true;
}

// ---------------------------------------------------------------------------
// The matcher
// ---------------------------------------------------------------------------

// What to do with the position after a match.
using Continuation = std::function<bool(std::size_t)>;

bool MatchRepeat(const std::vector<RegexNode>& nodes,
                 const RegexNode& repeat,
                 std::uint32_t count,
                 std::string_view text,
                 std::size_t pos,
                 const Continuation& next);

bool MatchNodeOnce(const std::vector<RegexNode>& nodes,
                   std::size_t index,
                   std::string_view text,
                   std::size_t pos,
                   const Continuation& next);

bool MatchSeq(const std::vector<RegexNode>& nodes,
              const std::vector<std::size_t>& seq,
              std::size_t from,
              std::string_view text,
              std::size_t pos,
              const Continuation& next) {
  if (from == seq.size()) {
    return next(pos);
  }
  const RegexNode& node = nodes[seq[from]];
  if (node.kind == RegexNode::Kind::kRepeat) {
    return MatchRepeat(
        nodes, node, 0, text, pos, [&](std::size_t end) {
          return MatchSeq(nodes, seq, from + 1, text, end, next);
        });
  }
  return MatchNodeOnce(nodes, seq[from], text, pos, [&](std::size_t end) {
    return MatchSeq(nodes, seq, from + 1, text, end, next);
  });
}

bool MatchRepeat(const std::vector<RegexNode>& nodes,
                 const RegexNode& repeat,
                 std::uint32_t count,
                 std::string_view text,
                 std::size_t pos,
                 const Continuation& next) {
  if (count < repeat.max) {
    // Greedy: one more occurrence first, and only then the shorter way.
    const bool matched =
        MatchNodeOnce(nodes, repeat.children[0], text, pos,
                      [&](std::size_t end) {
                        return MatchRepeat(nodes, repeat, count + 1, text, end,
                                           next);
                      });
    if (matched) {
      return true;
    }
  }
  if (count >= repeat.min) {
    return next(pos);
  }
  return false;
}

bool ClassMatches(const RegexNode& node, char c) {
  const std::uint8_t byte = static_cast<std::uint8_t>(c);
  bool inside = false;
  for (const auto& range : node.ranges) {
    if (byte >= range.first && byte <= range.second) {
      inside = true;
      break;
    }
  }
  return node.negated ? !inside : inside;
}

bool MatchNodeOnce(const std::vector<RegexNode>& nodes,
                   std::size_t index,
                   std::string_view text,
                   std::size_t pos,
                   const Continuation& next) {
  const RegexNode& node = nodes[index];
  switch (node.kind) {
    case RegexNode::Kind::kLiteral:
      return pos < text.size() &&
             text[pos] == static_cast<char>(node.literal) && next(pos + 1);
    case RegexNode::Kind::kAny:
      return pos < text.size() && next(pos + 1);
    case RegexNode::Kind::kClass:
      return pos < text.size() && ClassMatches(node, text[pos]) &&
             next(pos + 1);
    case RegexNode::Kind::kAnchorStart:
      return pos == 0 && next(pos);
    case RegexNode::Kind::kAnchorEnd:
      return pos == text.size() && next(pos);
    case RegexNode::Kind::kConcat:
      return MatchSeq(nodes, node.children, 0, text, pos, next);
    case RegexNode::Kind::kAlternate:
      for (const std::size_t alternative : node.children) {
        if (MatchNodeOnce(nodes, alternative, text, pos, next)) {
          return true;
        }
      }
      return false;
    case RegexNode::Kind::kRepeat:
      return MatchRepeat(nodes, node, 0, text, pos, next);
  }
  return false;
}

// ---------------------------------------------------------------------------
// The targeting
// ---------------------------------------------------------------------------

// The pattern that selects the URLs of a target.
//
// The regular expression wins when both forms are given.
const std::string& EffectivePattern(const FpTarget& target) {
  return target.url_regex.empty() ? target.url_glob : target.url_regex;
}

// True when the target speaks for the URL.
bool PatternMatches(const FpTarget& target, std::string_view url) {
  if (!target.url_regex.empty()) {
    return MatchUrlRegex(target.url_regex, url);
  }
  if (!target.url_glob.empty()) {
    return MatchGlob(target.url_glob, url);
  }
  // The dimension default: it speaks for every URL of its dimension.
  return true;
}

}  // namespace

bool MatchUrlRegex(std::string_view pattern, std::string_view url) {
  std::vector<RegexNode> nodes;
  std::size_t root = 0;
  RegexParser parser(pattern);
  if (!parser.Parse(&nodes, &root)) {
    // A pattern that cannot be read never matches. It disables its
    // target, and it never aborts the renderer.
    return false;
  }
  return MatchNodeOnce(nodes, root, url, 0,
                       [&](std::size_t pos) { return pos == url.size(); });
}

const FpTarget* SelectFpTarget(const FpTargetTable& table,
                                std::string_view url,
                                FpDimension dimension) {
  const FpTarget* best = nullptr;
  std::size_t best_length = 0;
  for (const FpTarget& target : table.targets) {
    if (target.dimension != dimension) {
      continue;
    }
    if (!PatternMatches(target, url)) {
      continue;
    }
    // The longest pattern is the most specific one. A tie keeps the
    // earlier entry, so the table's order decides it and the answer does
    // not depend on how the table was built.
    const std::size_t length = EffectivePattern(target).size();
    if (best == nullptr || length > best_length) {
      best = &target;
      best_length = length;
    }
  }
  return best;
}

FpTargetDecision DecideForUrl(const FpTargetTable& table,
                               std::string_view url,
                               FpDimension dimension) {
  FpTargetDecision decision;
  const FpTarget* target = SelectFpTarget(table, url, dimension);
  if (target == nullptr) {
    return decision;
  }
  decision.target = target;
  decision.apply = true;
  decision.skip = target->skip;
  return decision;
}

}  // namespace lengsuo::fp
