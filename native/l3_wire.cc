#include "l3_wire.h"

#include <string>
#include <utility>

namespace lengsuo::fp {
namespace {

// Sequential '\n' reader over the signed body. Every accessor returns false
// once the body is exhausted, so a short document can never read stale data.
class BodyReader {
 public:
  explicit BodyReader(std::string_view body) : body_(body) {}

  bool Next(std::string_view* line) {
    if (pos_ >= body_.size()) return false;
    const std::size_t newline = body_.find('\n', pos_);
    if (newline == std::string_view::npos) return false;
    std::string_view text = body_.substr(pos_, newline - pos_);
    pos_ = newline + 1;
    if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
    *line = text;
    return true;
  }

  bool AtEnd() const { return pos_ == body_.size(); }

 private:
  std::string_view body_;
  std::size_t pos_ = 0;
};

// "<keyword> <decimal>" with no other content. Zero is accepted: a document
// may legitimately declare no cookies/extensions/rules, and the encoder emits
// "cookies 0" / "rules 0" for exactly that case. The pins section is the one
// exception and is checked by the caller.
bool ParseCountLine(std::string_view line, std::string_view keyword,
                    std::size_t* value) {
  if (line.size() <= keyword.size() + 1 ||
      line.substr(0, keyword.size()) != keyword ||
      line[keyword.size()] != ' ') {
    return false;
  }
  std::size_t total = 0;
  for (const char c : line.substr(keyword.size() + 1)) {
    if (c < '0' || c > '9') return false;
    total = total * 10 + static_cast<std::size_t>(c - '0');
    if (total > 4096) return false;
  }
  *value = total;
  return true;
}

// Splits on '|' into exactly `count` fields.
bool SplitFields(std::string_view line, std::size_t count,
                 std::string* fields) {
  std::size_t start = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t bar = line.find('|', start);
    if (i + 1 == count) {
      if (bar != std::string_view::npos) return false;
      fields[i] = std::string(line.substr(start));
      return true;
    }
    if (bar == std::string_view::npos) return false;
    fields[i] = std::string(line.substr(start, bar - start));
    start = bar + 1;
  }
  return false;
}

bool SplitSign(std::string_view sign, std::string_view* md5, int64_t* unix) {
  const std::size_t sep = sign.find("_v1_t");
  if (sep != 32) return false;
  const std::string_view digits = sign.substr(37);
  if (digits.empty()) return false;
  int64_t value = 0;
  for (const char c : digits) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
    if (value > 9007199254740992LL) return false;
  }
  *md5 = sign.substr(0, sep);
  *unix = value;
  return true;
}

}  // namespace

bool DecodeL3Wire(std::string_view wire, int64_t now_unix,
                  int64_t max_age_seconds, L3Config* out, std::string* error) {
  const auto fail = [error](const char* message) {
    if (error) *error = message;
    return false;
  };
  if (out == nullptr) return fail("l3: null output");
  if (wire.empty() || wire.size() > kMaxL3WireBytes) {
    return fail("l3: empty or oversized document");
  }

  // The sign line closes the document, so its position also delimits the
  // signed bytes. A document with no sign line is rejected: integrity is not
  // optional.
  const std::size_t sign_start = wire.rfind("\nsign ");
  if (sign_start == std::string_view::npos) {
    return fail("l3: missing sign line");
  }
  const std::string body(wire.substr(0, sign_start + 1));
  std::string_view sign_line = wire.substr(sign_start + 1);
  if (sign_line.size() < 6 || sign_line.back() != '\n') {
    return fail("l3: sign line must end with a newline");
  }
  sign_line.remove_suffix(1);
  if (!sign_line.empty() && sign_line.back() == '\r') sign_line.remove_suffix(1);
  const std::string sign_text(sign_line.substr(5));

  BodyReader reader(body);
  std::string_view line;
  if (!reader.Next(&line) || line != "L3W1") {
    return fail("l3: bad magic");
  }

  L3Config config;

  std::size_t count = 0;
  if (!reader.Next(&line) || !ParseCountLine(line, "pins", &count) ||
      count == 0) {
    // A snapshot with no pins carries no L3 statement at all; treat it as
    // malformed rather than as a no-op so a truncated document cannot pass.
    return fail("l3: bad pins header");
  }
  std::string pin_wire;
  for (std::size_t i = 0; i < count; ++i) {
    if (!reader.Next(&line) || line.empty()) {
      return fail("l3: truncated pins section");
    }
    pin_wire.append(line);
    pin_wire.push_back('\n');
  }
  if (!config.pins.Parse(pin_wire)) {
    return fail("l3: invalid pin entry");
  }

  if (!reader.Next(&line) || !ParseCountLine(line, "cookies", &count)) {
    return fail("l3: bad cookies header");
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (!reader.Next(&line)) return fail("l3: truncated cookies section");
    std::string fields[3];
    if (!SplitFields(line, 3, fields) || fields[1].empty()) {
      return fail("l3: invalid cookie entry");
    }
    config.cookies.push_back(
        CookieFact{fields[0], fields[1], fields[2]});
  }

  if (!reader.Next(&line) || !ParseCountLine(line, "ext", &count)) {
    return fail("l3: bad ext header");
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (!reader.Next(&line)) return fail("l3: truncated ext section");
    if (!config.extensions.Add(std::string(line))) {
      return fail("l3: invalid extension id");
    }
  }

  if (!reader.Next(&line) || !ParseCountLine(line, "rules", &count)) {
    return fail("l3: bad rules header");
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (!reader.Next(&line)) return fail("l3: truncated rules section");
    std::string fields[4];
    if (!SplitFields(line, 4, fields) || fields[1].empty() ||
        fields[3].empty()) {
      return fail("l3: invalid rule entry");
    }
    config.rules.push_back(ConsistencyRule{fields[0], fields[1], fields[2],
                                           fields[3]});
  }

  if (!reader.AtEnd()) {
    return fail("l3: unexpected content before sign");
  }

  // Integrity before freshness (cache_record order). now_unix <= 0 means the
  // caller has already judged freshness elsewhere (the renderer path): the
  // digest is still verified, only the age window is skipped.
  const bool check_freshness = now_unix > 0;
  if (check_freshness) {
    if (VerifySign(sign_text, body, now_unix, max_age_seconds) !=
        SignVerdict::kOk) {
      return fail("l3: sign verification failed");
    }
  } else {
    // Digest-only check. Verifying against the timestamp the document itself
    // carries makes the age window vacuous (max_age covers the gap to now)
    // while the MD5 still has to match the body byte for byte.
    std::string digest;
    int64_t timestamp = 0;
    if (!ParseSignFields(sign_text, &digest, &timestamp)) {
      return fail("l3: bad sign fields");
    }
    if (VerifySign(sign_text, body, timestamp,
                   /*max_age_seconds=*/0) != SignVerdict::kOk) {
      return fail("l3: sign verification failed");
    }
  }
  std::string_view md5;
  if (!SplitSign(sign_text, &md5, &config.signed_unix)) {
    return fail("l3: bad sign fields");
  }
  config.sign = sign_text;

  // Launch gate: a snapshot that would fail the platform's own cross-check
  // must never be applied, otherwise the first navigation disagrees with
  // itself.
  if (!L3SnapshotConsistent(config)) {
    return fail("l3: pinned snapshot is internally inconsistent");
  }

  *out = std::move(config);
  if (error) error->clear();
  return true;
}

bool L3SnapshotConsistent(const L3Config& config) {
  return CheckConsistency(config.cookies, CollectStorageFacts(config.pins),
                          config.rules)
      .empty();
}

std::vector<StorageFact> CollectStorageFacts(const StoragePin& pins) {
  std::vector<StorageFact> facts;
  for (const DomainPin& domain : pins.Domains()) {
    for (const PinnedEntry& entry : domain.local_storage) {
      facts.push_back(StorageFact{domain.origin, entry.key, entry.value});
    }
    for (const PinnedEntry& entry : domain.session_storage) {
      facts.push_back(StorageFact{domain.origin, entry.key, entry.value});
    }
  }
  return facts;
}

std::string EncodePinWire(const StoragePin& pins) {
  std::string wire;
  for (const DomainPin& domain : pins.Domains()) {
    for (const std::vector<PinnedEntry>* area :
         {&domain.local_storage, &domain.session_storage}) {
      const char* scope = area == &domain.local_storage ? "local" : "session";
      for (const PinnedEntry& entry : *area) {
        wire += domain.origin;
        wire += '|';
        wire += scope;
        wire += '|';
        wire += entry.key;
        wire += '|';
        wire += entry.value;
        wire += '\n';
      }
    }
  }
  return wire;
}

std::string EncodeL3Wire(const L3Config& config, int64_t unix_seconds) {
  const std::string pin_wire = EncodePinWire(config.pins);
  std::size_t pin_count = 0;
  if (!pin_wire.empty()) {
    pin_count = 1;
    for (const char c : pin_wire) {
      if (c == '\n') ++pin_count;
    }
    --pin_count;
  }

  std::string body = "L3W1\n";
  body += "pins " + std::to_string(pin_count) + "\n";
  body += pin_wire;
  body += "cookies " + std::to_string(config.cookies.size()) + "\n";
  for (const CookieFact& fact : config.cookies) {
    body += fact.domain + '|' + fact.name + '|' + fact.value + '\n';
  }
  body += "ext " + std::to_string(config.extensions.size()) + "\n";
  // Parse() keeps a std::set, so iteration is already sorted.
  for (const std::string& id : config.extensions.Ids()) {
    body += id + '\n';
  }
  body += "rules " + std::to_string(config.rules.size()) + "\n";
  for (const ConsistencyRule& rule : config.rules) {
    body += rule.cookie_domain + '|' + rule.cookie_name + '|' +
            rule.storage_origin + '|' + rule.storage_key + '\n';
  }
  const std::string sign = MakeSign(body, unix_seconds);
  return body + "sign " + sign + "\n";
}

}  // namespace lengsuo::fp
