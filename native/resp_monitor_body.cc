#include "resp_monitor_body.h"

namespace lengsuo::fp {

void RespMonitorBodyReset(RespMonitorBody* body, std::size_t limit) {
  if (body == nullptr) {
    return;
  }
  body->bytes.clear();
  body->total_bytes = 0;
  body->limit = limit;
  body->truncated = false;
}

void RespMonitorBodyAppend(RespMonitorBody* body, std::string_view data) {
  if (body == nullptr || data.empty()) {
    return;
  }
  body->total_bytes += static_cast<std::uint64_t>(data.size());
  const std::size_t room = body->limit > body->bytes.size()
                               ? body->limit - body->bytes.size()
                               : 0;
  if (room == 0) {
    // The limit is already reached. The bytes are counted and dropped.
    body->truncated = true;
    return;
  }
  if (data.size() > room) {
    // The body is longer than the capture: keep the first part, drop the
    // rest, and say so.
    body->bytes.append(data.substr(0, room));
    body->truncated = true;
    return;
  }
  body->bytes.append(data);
}

std::size_t RespMonitorBodySize(const RespMonitorBody& body) {
  return body.bytes.size();
}

std::uint64_t RespMonitorBodyDigest(std::string_view data) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const char c : data) {
    hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
    hash *= 1099511628211ULL;
  }
  return hash;
}

}  // namespace lengsuo::fp
