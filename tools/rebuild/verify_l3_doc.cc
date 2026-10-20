// verify_l3_doc - round-trip a generated L3W1 document through the decoder.
//
// The acceptance run needs to know the document it hands to the browser is
// actually accepted by the production decoder, including the launch
// consistency gate. This helper does that check outside the browser, so a
// rejection can be told apart from a browser-side failure.
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

#include "l3_wire.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <wire-file> [now-unix]\n", argv[0]);
    return 2;
  }
  std::ifstream fh(argv[1], std::ios::binary);
  if (!fh) {
    std::fprintf(stderr, "cannot open %s\n", argv[1]);
    return 1;
  }
  std::ostringstream buffer;
  buffer << fh.rdbuf();
  const std::string wire = buffer.str();

  int64_t now = 0;
  if (argc > 2) {
    now = std::strtoll(argv[2], nullptr, 10);
  } else {
    now = static_cast<int64_t>(std::time(nullptr));
  }

  lengsuo::fp::L3Config config;
  std::string error;
  // The freshness window has to match production: a zero window rejects any
  // document older than one second, which is not what the browser does.
  constexpr int64_t kMaxAge = 7 * 24 * 60 * 60;
  if (!lengsuo::fp::DecodeL3Wire(wire, now, kMaxAge, &config, &error)) {
    std::fprintf(stderr, "DECODE FAILED: %s\n", error.c_str());
    return 1;
  }
  std::printf("decode OK\n");
  std::printf("  pins      : %zu\n", config.pins.size());
  std::printf("  cookies   : %zu\n", config.cookies.size());
  std::printf("  rules     : %zu\n", config.rules.size());
  std::printf("  extensions: %zu\n", config.extensions.size());
  std::printf("  signed_at : %lld\n", static_cast<long long>(config.signed_unix));
  for (const auto& domain : config.pins.Domains()) {
    std::printf("  origin    : %s\n", domain.origin.c_str());
  }
  return 0;
}
