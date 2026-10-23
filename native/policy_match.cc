#include "policy_match.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "profile_bundle.h"

namespace lengsuo::fp {
namespace {

// --- the pattern language ---------------------------------------------------
//
// The kernel evaluates a small, fixed regular expression language. It is not
// PCRE and it is not RE2: it is the part of both that a URL rule actually
// uses, written out so that the kernel carries no dependency and no
// exception.
//
//   .            any one byte
//   *  +  ?     zero or more, one or more, zero or one of the atom before
//   {m} {m,} {m,n}  a counted number of the atom before
//   [abc] [^abc] [a-z]   a byte, a negated set of bytes, a range
//   \d \D \w \W \s \S     the usual shorthands
//   \x            the byte x, whatever x is
//   ( )          a group; the group does not capture
//   |            an alternation
//   ^  $         the start and the end of the URL
//
// What is not in the language: back references, look-around, lazy
// quantifiers, and a count over a group. A pattern that uses one of those
// is not a pattern the kernel can evaluate; it is dropped and counted rather
// than half-applied.
//
// The matcher is a backtracking one with a step budget. A pattern can be
// written so that the backtracking explodes - (a+a+)+b against a long run of
// a's - and the budget turns that into "no match" instead of a browser that
// hangs.
constexpr std::size_t kMaxPatternLength = 1024;
constexpr std::uint64_t kMaxMatchSteps = 200000;
constexpr int kMaxGroupDepth = 64;

using Range = std::pair<unsigned char, unsigned char>;

enum class NodeKind {
  kLiteral,
  kAny,
  kClass,
  kConcat,
  kAlternate,
  kRepeat,
  kAnchorStart,
  kAnchorEnd,
};

struct Node {
  NodeKind kind = NodeKind::kLiteral;
  // The byte a kLiteral node matches.
  unsigned char literal = 0;
  // The ranges a kClass node matches.
  std::vector<Range> ranges;
  // True when a kClass node matches everything but its ranges.
  bool negated = false;
  // The children of a kConcat, kAlternate or kRepeat node. A kRepeat node has
  // exactly one child: the atom it repeats.
  std::vector<std::unique_ptr<Node>> children;
  // The repetition bounds of a kRepeat node. A max of -1 is unbounded.
  int min_count = 0;
  int max_count = 0;
};

using NodePtr = std::unique_ptr<Node>;

// The complement of a set of ranges over the whole byte range.
std::vector<Range> Complement(const std::vector<Range>& ranges) {
  std::vector<Range> sorted = ranges;
  std::sort(sorted.begin(), sorted.end());
  std::vector<Range> out;
  int next = 0;
  for (const Range& range : sorted) {
    if (static_cast<int>(range.second) < next) {
      continue;
    }
    if (static_cast<int>(range.first) > next) {
      out.push_back({static_cast<unsigned char>(next),
                     static_cast<unsigned char>(range.first - 1)});
    }
    if (range.second == 255) {
      next = 256;
      break;
    }
    next = static_cast<int>(range.second) + 1;
  }
  if (next <= 255) {
    out.push_back({static_cast<unsigned char>(next), 255});
  }
  return out;
}

// One item of a character class: a single byte, or a set of bytes.
struct ClassItem {
  bool is_literal = false;
  unsigned char literal = 0;
  std::vector<Range> ranges;
  bool negate_set = false;
};

class Parser {
 public:
  explicit Parser(std::string_view pattern) : pattern_(pattern) {}

  bool Parse(NodePtr* out) {
    NodePtr root;
    if (!ParseAlternation(&root)) {
      return false;
    }
    if (pos_ != pattern_.size()) {
      // A ')' without its '('.
      return false;
    }
    *out = std::move(root);
    return true;
  }

 private:
  bool ParseAlternation(NodePtr* out) {
    std::vector<NodePtr> branches;
    NodePtr first;
    if (!ParseSequence(&first)) {
      return false;
    }
    branches.push_back(std::move(first));
    while (pos_ < pattern_.size() && pattern_[pos_] == '|') {
      ++pos_;
      NodePtr branch;
      if (!ParseSequence(&branch)) {
        return false;
      }
      branches.push_back(std::move(branch));
    }
    if (branches.size() == 1) {
      *out = std::move(branches[0]);
      return true;
    }
    auto node = std::make_unique<Node>();
    node->kind = NodeKind::kAlternate;
    node->children = std::move(branches);
    *out = std::move(node);
    return true;
  }

  bool ParseSequence(NodePtr* out) {
    auto node = std::make_unique<Node>();
    node->kind = NodeKind::kConcat;
    while (pos_ < pattern_.size() && pattern_[pos_] != '|' &&
           pattern_[pos_] != ')') {
      NodePtr atom;
      if (!ParseAtom(&atom)) {
        return false;
      }
      if (!ParseQuantifier(&atom)) {
        return false;
      }
      node->children.push_back(std::move(atom));
    }
    *out = std::move(node);
    return true;
  }

  bool ParseAtom(NodePtr* out) {
    if (pos_ >= pattern_.size()) {
      return false;
    }
    const char c = pattern_[pos_];
    if (c == '(') {
      ++pos_;
      if (++depth_ > kMaxGroupDepth) {
        return false;
      }
      NodePtr inner;
      if (!ParseAlternation(&inner)) {
        return false;
      }
      if (pos_ >= pattern_.size() || pattern_[pos_] != ')') {
        return false;
      }
      ++pos_;
      --depth_;
      *out = std::move(inner);
      return true;
    }
    if (c == '[') {
      return ParseClass(out);
    }
    if (c == '.') {
      ++pos_;
      auto node = std::make_unique<Node>();
      node->kind = NodeKind::kAny;
      *out = std::move(node);
      return true;
    }
    if (c == '^') {
      ++pos_;
      auto node = std::make_unique<Node>();
      node->kind = NodeKind::kAnchorStart;
      *out = std::move(node);
      return true;
    }
    if (c == '$') {
      ++pos_;
      auto node = std::make_unique<Node>();
      node->kind = NodeKind::kAnchorEnd;
      *out = std::move(node);
      return true;
    }
    if (c == '\\') {
      return ParseEscape(out);
    }
    if (c == '*' || c == '+' || c == '?') {
      // A quantifier with nothing to repeat.
      return false;
    }
    ++pos_;
    auto node = std::make_unique<Node>();
    node->kind = NodeKind::kLiteral;
    node->literal = static_cast<unsigned char>(c);
    *out = std::move(node);
    return true;
  }

  // Reads an escape outside a character class.
  bool ParseEscape(NodePtr* out) {
    ++pos_;  // the backslash
    if (pos_ >= pattern_.size()) {
      return false;
    }
    const char c = pattern_[pos_++];
    std::vector<Range> ranges;
    bool negated = false;
    switch (c) {
      case 'd':
        ranges = {{'0', '9'}};
        break;
      case 'D':
        ranges = {{'0', '9'}};
        negated = true;
        break;
      case 'w':
        ranges = {{'0', '9'}, {'A', 'Z'}, {'_', '_'}, {'a', 'z'}};
        break;
      case 'W':
        ranges = {{'0', '9'}, {'A', 'Z'}, {'_', '_'}, {'a', 'z'}};
        negated = true;
        break;
      case 's':
        ranges = {{'\t', '\r'}, {' ', ' '}};
        break;
      case 'S':
        ranges = {{'\t', '\r'}, {' ', ' '}};
        negated = true;
        break;
      default: {
        auto node = std::make_unique<Node>();
        node->kind = NodeKind::kLiteral;
        node->literal = static_cast<unsigned char>(c);
        *out = std::move(node);
        return true;
      }
    }
    auto node = std::make_unique<Node>();
    node->kind = NodeKind::kClass;
    node->ranges = negated ? Complement(ranges) : std::move(ranges);
    *out = std::move(node);
    return true;
  }

  // Reads one item of a character class.
  bool ParseClassItem(ClassItem* item) {
    if (pos_ >= pattern_.size()) {
      return false;
    }
    const char c = pattern_[pos_];
    if (c != '\\') {
      ++pos_;
      item->is_literal = true;
      item->literal = static_cast<unsigned char>(c);
      item->ranges.push_back({item->literal, item->literal});
      return true;
    }
    ++pos_;  // the backslash
    if (pos_ >= pattern_.size()) {
      return false;
    }
    const char escape = pattern_[pos_++];
    switch (escape) {
      case 'd':
        item->ranges = {{'0', '9'}};
        return true;
      case 'D':
        item->ranges = {{'0', '9'}};
        item->negate_set = true;
        return true;
      case 'w':
        item->ranges = {{'0', '9'}, {'A', 'Z'}, {'_', '_'}, {'a', 'z'}};
        return true;
      case 'W':
        item->ranges = {{'0', '9'}, {'A', 'Z'}, {'_', '_'}, {'a', 'z'}};
        item->negate_set = true;
        return true;
      case 's':
        item->ranges = {{'\t', '\r'}, {' ', ' '}};
        return true;
      case 'S':
        item->ranges = {{'\t', '\r'}, {' ', ' '}};
        item->negate_set = true;
        return true;
      default:
        item->is_literal = true;
        item->literal = static_cast<unsigned char>(escape);
        item->ranges.push_back({item->literal, item->literal});
        return true;
    }
  }

  bool ParseClass(NodePtr* out) {
    ++pos_;  // the '['
    bool negated = false;
    if (pos_ < pattern_.size() && pattern_[pos_] == '^') {
      negated = true;
      ++pos_;
    }
    std::vector<Range> ranges;
    bool first = true;
    while (true) {
      if (pos_ >= pattern_.size()) {
        return false;
      }
      if (pattern_[pos_] == ']' && !first) {
        ++pos_;
        break;
      }
      first = false;
      ClassItem low;
      if (!ParseClassItem(&low)) {
        return false;
      }
      // A dash that is not the last byte of the class opens a range, and a
      // range needs two literal bytes: '\d' cannot be an end of one.
      const bool opens_range = pos_ + 1 < pattern_.size() &&
                               pattern_[pos_] == '-' &&
                               pattern_[pos_ + 1] != ']';
      if (opens_range) {
        if (!low.is_literal) {
          return false;
        }
        ++pos_;
        ClassItem high;
        if (!ParseClassItem(&high)) {
          return false;
        }
        if (!high.is_literal || low.literal > high.literal) {
          return false;
        }
        ranges.push_back({low.literal, high.literal});
        continue;
      }
      if (low.negate_set) {
        const std::vector<Range> complement = Complement(low.ranges);
        ranges.insert(ranges.end(), complement.begin(), complement.end());
      } else {
        ranges.insert(ranges.end(), low.ranges.begin(), low.ranges.end());
      }
    }
    auto node = std::make_unique<Node>();
    node->kind = NodeKind::kClass;
    node->ranges = negated ? Complement(ranges) : std::move(ranges);
    *out = std::move(node);
    return true;
  }

  bool ParseQuantifier(NodePtr* atom) {
    if (pos_ >= pattern_.size()) {
      return true;
    }
    const char c = pattern_[pos_];
    int min_count = 0;
    int max_count = 0;
    if (c == '*') {
      max_count = -1;
      ++pos_;
    } else if (c == '+') {
      min_count = 1;
      max_count = -1;
      ++pos_;
    } else if (c == '?') {
      max_count = 1;
      ++pos_;
    } else if (c == '{') {
      if (!ParseCounted(&min_count, &max_count)) {
        return false;
      }
    } else {
      return true;
    }
    // A second quantifier on the same atom is a lazy quantifier, which this
    // language does not have.
    if (pos_ < pattern_.size()) {
      const char next = pattern_[pos_];
      if (next == '*' || next == '+' || next == '?' || next == '{') {
        return false;
      }
    }
    auto node = std::make_unique<Node>();
    node->kind = NodeKind::kRepeat;
    node->min_count = min_count;
    node->max_count = max_count;
    node->children.push_back(std::move(*atom));
    *atom = std::move(node);
    return true;
  }

  bool ParseCounted(int* min_count, int* max_count) {
    const std::size_t start = pos_;
    ++pos_;  // the '{'
    int low = 0;
    if (!ParseNumber(&low)) {
      pos_ = start;
      return false;
    }
    if (pos_ < pattern_.size() && pattern_[pos_] == '}') {
      ++pos_;
      *min_count = low;
      *max_count = low;
      return true;
    }
    if (pos_ >= pattern_.size() || pattern_[pos_] != ',') {
      pos_ = start;
      return false;
    }
    ++pos_;
    if (pos_ < pattern_.size() && pattern_[pos_] == '}') {
      ++pos_;
      *min_count = low;
      *max_count = -1;
      return true;
    }
    int high = 0;
    if (!ParseNumber(&high)) {
      pos_ = start;
      return false;
    }
    if (pos_ >= pattern_.size() || pattern_[pos_] != '}') {
      pos_ = start;
      return false;
    }
    ++pos_;
    if (high < low) {
      pos_ = start;
      return false;
    }
    *min_count = low;
    *max_count = high;
    return true;
  }

  bool ParseNumber(int* out) {
    if (pos_ >= pattern_.size() || pattern_[pos_] < '0' || pattern_[pos_] > '9') {
      return false;
    }
    int value = 0;
    while (pos_ < pattern_.size() && pattern_[pos_] >= '0' &&
           pattern_[pos_] <= '9') {
      if (value > 100000) {
        // A count that big is a mistake, and the step budget would stop it
        // anyway.
        return false;
      }
      value = value * 10 + (pattern_[pos_] - '0');
      ++pos_;
    }
    *out = value;
    return true;
  }

  std::string_view pattern_;
  std::size_t pos_ = 0;
  int depth_ = 0;
};

bool ParsePattern(std::string_view pattern, NodePtr* out) {
  if (pattern.size() > kMaxPatternLength) {
    return false;
  }
  Parser parser(pattern);
  return parser.Parse(out);
}

// --- the matcher ------------------------------------------------------------

using Continuation = std::function<bool(std::size_t)>;

struct Matcher {
  std::string_view text;
  std::uint64_t steps = 0;
  bool exhausted = false;

  bool Step() {
    if (++steps > kMaxMatchSteps) {
      exhausted = true;
      return false;
    }
    return true;
  }
};

bool MatchClass(const Node& node, unsigned char c) {
  bool inside = false;
  for (const Range& range : node.ranges) {
    if (c >= range.first && c <= range.second) {
      inside = true;
      break;
    }
  }
  return node.negated ? !inside : inside;
}

bool MatchNode(const Node& node,
               std::size_t pos,
               Matcher& matcher,
               const Continuation& next);

bool MatchChildren(const Node& node,
                   std::size_t index,
                   std::size_t pos,
                   Matcher& matcher,
                   const Continuation& next) {
  if (index == node.children.size()) {
    return next(pos);
  }
  return MatchNode(*node.children[index], pos, matcher,
                   [&](std::size_t end) {
                     return MatchChildren(node, index + 1, end, matcher, next);
                   });
}

bool MatchRepeat(const Node& node,
                 std::size_t count,
                 std::size_t pos,
                 Matcher& matcher,
                 const Continuation& next) {
  const Node& atom = *node.children[0];
  // Greedy: take one more atom if the count allows it.
  if (node.max_count < 0 ||
      count < static_cast<std::size_t>(node.max_count)) {
    const bool matched = MatchNode(atom, pos, matcher, [&](std::size_t end) {
      if (end == pos) {
        // An atom that matches nothing would repeat forever.
        return false;
      }
      return MatchRepeat(node, count + 1, end, matcher, next);
    });
    if (matched) {
      return true;
    }
    if (matcher.exhausted) {
      return false;
    }
  }
  if (count >= static_cast<std::size_t>(node.min_count)) {
    return next(pos);
  }
  return false;
}

bool MatchNode(const Node& node,
               std::size_t pos,
               Matcher& matcher,
               const Continuation& next) {
  if (!matcher.Step()) {
    return false;
  }
  switch (node.kind) {
    case NodeKind::kLiteral:
      if (pos < matcher.text.size() &&
          static_cast<unsigned char>(matcher.text[pos]) == node.literal) {
        return next(pos + 1);
      }
      return false;
    case NodeKind::kAny:
      if (pos < matcher.text.size()) {
        return next(pos + 1);
      }
      return false;
    case NodeKind::kClass:
      if (pos < matcher.text.size() &&
          MatchClass(node, static_cast<unsigned char>(matcher.text[pos]))) {
        return next(pos + 1);
      }
      return false;
    case NodeKind::kConcat:
      return MatchChildren(node, 0, pos, matcher, next);
    case NodeKind::kAlternate:
      for (const NodePtr& child : node.children) {
        if (MatchNode(*child, pos, matcher, next)) {
          return true;
        }
        if (matcher.exhausted) {
          return false;
        }
      }
      return false;
    case NodeKind::kAnchorStart:
      return pos == 0 ? next(pos) : false;
    case NodeKind::kAnchorEnd:
      return pos == matcher.text.size() ? next(pos) : false;
    case NodeKind::kRepeat:
      return MatchRepeat(node, 0, pos, matcher, next);
  }
  return false;
}

}  // namespace

bool MatchUrlPattern(std::string_view pattern, std::string_view url) {
  NodePtr root;
  if (!ParsePattern(pattern, &root)) {
    return false;
  }
  Matcher matcher;
  matcher.text = url;
  const std::size_t size = url.size();
  Continuation finish = [size](std::size_t end) { return end == size; };
  return MatchNode(*root, 0, matcher, finish);
}

bool SearchUrlPattern(std::string_view pattern, std::string_view url) {
  NodePtr root;
  if (!ParsePattern(pattern, &root)) {
    return false;
  }
  Matcher matcher;
  matcher.text = url;
  // Any end is an end: the pattern has to describe a run of the URL, not the
  // whole of it.
  Continuation finish = [](std::size_t) { return true; };
  // One step budget for every start position rather than one for each. This
  // runs on the browser's UI thread, once per response that a rule claims, so
  // the work a single search may do has to be bounded by a number that does
  // not grow with the URL's length. The budget is the one a whole-URL match
  // already gets.
  for (std::size_t start = 0; start <= url.size(); ++start) {
    if (MatchNode(*root, start, matcher, finish)) {
      return true;
    }
    if (matcher.exhausted) {
      return false;
    }
  }
  return false;
}

bool IsValidUrlPattern(std::string_view pattern) {
  NodePtr root;
  return ParsePattern(pattern, &root);
}

bool MatchDomainPattern(std::string_view pattern, std::string_view host) {
  return MatchGlob(pattern, host);
}

}  // namespace lengsuo::fp
