// cache_record — the 对标端 cloud-cache record shape {name, sign, data}.
//
// Wire shape (docs/cc-task-brief-l3.md, task G):
//   {
//     "name": "COOKIE_v1",
//     "sign": "<32hex>_v1_t<unix seconds>",
//     "data": { ... }        <- kept as raw JSON text, never re-encoded
//   }
//
// `data_json` holds the exact bytes of the `data` value as it appeared in the
// input, including the surrounding quotes when that value is a JSON string.
// ParseRecord never re-serializes, reorders or reformats the payload.
//
// The sign is "<md5-hex>_v1_t<unix>". The digest covers sign_input only; the
// timestamp carries the anti-rollback check. VerifySign checks integrity
// (kBadDigest) before freshness (kTooNew/kTooOld).

#ifndef LENGSUO_NATIVE_CACHE_RECORD_H_
#define LENGSUO_NATIVE_CACHE_RECORD_H_

#include <cstdint>
#include <string>

namespace lengsuo::fp {

struct CacheRecord {
  std::string name;       // "COOKIE_v1"
  std::string sign;       // "<32hex>_v1_t<unix>"
  std::string data_json;  // raw JSON value text, preserved verbatim
};

// Parses a top-level JSON object carrying "name" (string), "sign" (string) and
// "data" (any JSON value). A missing key, a wrong value type, malformed JSON or
// trailing bytes yields false and leaves *out untouched (fail-closed).
bool ParseRecord(const std::string& json, CacheRecord* out);

// Emits {"name":...,"sign":...,"data":<data_json>}. `data_json` must be a
// valid JSON value; it is inserted verbatim.
std::string SerializeRecord(const CacheRecord& rec);

// Returns "<md5hex>_v1_t<unix_seconds>", with md5hex = MD5(sign_input).
std::string MakeSign(const std::string& sign_input, int64_t unix_seconds);

// Splits a sign without verifying it; `md5_hex` is lowercased. Returns false on
// any format violation: not 32 hex chars, no "_v1_t", or an empty/non-decimal/
// overflowing timestamp.
bool ParseSignFields(const std::string& sign, std::string* md5_hex,
                     int64_t* unix_seconds);

enum class SignVerdict {
  kOk,
  kBadDigest,
  kTooNew,
  kTooOld,
  kMalformed,
};

// Integrity before freshness: malformed -> bad digest -> too new -> too old.
// "Fresh" means now - max_age <= ts <= now.
SignVerdict VerifySign(const std::string& sign,
                       const std::string& sign_input,
                       int64_t now_unix,
                       int64_t max_age_seconds);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_CACHE_RECORD_H_
