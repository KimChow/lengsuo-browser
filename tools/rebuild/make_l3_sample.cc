// make_l3_sample - produce a real L3W1 document for the acceptance run.
//
// The sample is built through the production encoder (EncodeL3Wire) so the
// bytes on disk are exactly what the browser will parse, and the signature is
// a genuine MD5 over the document body. Nothing here is hand-rolled.
//
// The scenario mirrors the Reference finding: localStorage "finger-key_0.1.5"
// and the cookie "_bee" must carry the same value, and the rules block
// declares that equality so the launch consistency gate enforces it.
//
// Usage: make_l3_sample <output-path> <unix-seconds> [origin]
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "l3_wire.h"

namespace {

lengsuo::fp::ConsistencyRule Rule(const std::string& cookie_domain,
                                  const std::string& cookie_name,
                                  const std::string& storage_origin,
                                  const std::string& storage_key) {
  lengsuo::fp::ConsistencyRule rule;
  rule.cookie_domain = cookie_domain;
  rule.cookie_name = cookie_name;
  rule.storage_origin = storage_origin;
  rule.storage_key = storage_key;
  return rule;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <output> <unix-seconds> [origin]\n", argv[0]);
    return 2;
  }
  const std::string path = argv[1];
  const int64_t now = std::strtoll(argv[2], nullptr, 10);
  const std::string origin = argc > 3 ? argv[3] : "https://example.com";

  // One fingerprint value, used by the pinned storage key and the cookie.
  const std::string value = "fp-7a3c9e21b4d80652";
  const std::string cookie_value = value;

  lengsuo::fp::L3Config config;
  // StoragePin is built through its own parser, so the sample exercises the
  // same wire grammar the browser will.
  const std::string pin_wire = origin + "|local|finger-key_0.1.5|" + value + "\n";
  if (!config.pins.Parse(pin_wire)) {
    std::fprintf(stderr, "pin wire rejected\n");
    return 1;
  }
  config.cookies.push_back(
      lengsuo::fp::CookieFact{".example.com", "_bee", cookie_value});
  if (!config.extensions.Add("abcdefghijklmnopabcdefghijklmnop")) {
    std::fprintf(stderr, "extension id rejected\n");
    return 1;
  }
  config.rules.push_back(
      Rule(".example.com", "_bee", origin, "finger-key_0.1.5"));

  const std::string wire = lengsuo::fp::EncodeL3Wire(config, now);
  FILE* fh = std::fopen(path.c_str(), "wb");
  if (fh == nullptr) {
    std::fprintf(stderr, "cannot open %s\n", path.c_str());
    return 1;
  }
  std::fwrite(wire.data(), 1, wire.size(), fh);
  std::fclose(fh);
  std::printf("wrote %s (%zu bytes)\n", path.c_str(), wire.size());
  return 0;
}
