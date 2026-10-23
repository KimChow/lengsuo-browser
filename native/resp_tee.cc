#include "resp_tee.h"

namespace lengsuo::fp {
namespace {

// The multiplier of the FNV-1a that RespMonitorBodyDigest uses. Kept here so
// the page-side digest and the capture's digest are the same function.
constexpr std::uint64_t kDigestPrime = 1099511628211ULL;

}  // namespace

void RespTeeReset(RespTee* tee, std::size_t capture_limit) {
  if (tee == nullptr) {
    return;
  }
  RespMonitorBodyReset(&tee->capture, capture_limit);
  tee->page_bytes = 0;
  tee->page_digest = kRespTeeDigestSeed;
  tee->write_in_flight = false;
}

void RespTeeFan(RespTee* tee, std::string_view data, std::string* page_out) {
  if (tee == nullptr) {
    return;
  }
  // The page's copy: the whole chunk, and in order. This is the line the
  // whole module is about - everything else may change, this may not.
  if (page_out != nullptr) {
    *page_out = data;
  }
  for (const char c : data) {
    tee->page_digest ^=
        static_cast<std::uint64_t>(static_cast<unsigned char>(c));
    tee->page_digest *= kDigestPrime;
  }
  tee->page_bytes += static_cast<std::uint64_t>(data.size());
  // The monitor's copy: bounded, and allowed to lose its tail.
  RespMonitorBodyAppend(&tee->capture, data);
}

std::uint64_t RespTeePageDigest(const RespTee& tee) {
  return tee.page_digest;
}

bool RespTeeWantsRead(const RespTee& tee) {
  return !tee.write_in_flight;
}

void RespTeeWriteStarted(RespTee* tee) {
  if (tee != nullptr) {
    tee->write_in_flight = true;
  }
}

void RespTeeWriteFinished(RespTee* tee) {
  if (tee != nullptr) {
    tee->write_in_flight = false;
  }
}

}  // namespace lengsuo::fp