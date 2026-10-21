#include "l3_envelope_session.h"

#include <utility>

namespace lengsuo::fp {
namespace {

void SetError(std::string* error, const char* message) {
  if (error != nullptr) {
    *error = message;
  }
}

}  // namespace

bool MakeL3EnvelopeKey(std::string_view session_key,
                       L3EnvelopeKey* out,
                       std::string* error) {
  if (out == nullptr) {
    SetError(error, "l3session: no output");
    return false;
  }
  // The key is the caller's, and the caller's is the only source there is.
  // There is no default, no derivation and no fallback: a caller that has no
  // key has no envelope.
  //
  // The length is checked here and nowhere else, because the envelope's own
  // key constructor checks it too. A key of the wrong length is refused
  // rather than padded or truncated.
  if (session_key.size() != kL3EnvelopeSessionKeyBytes) {
    // The message says how long the key was, never what it was.
    SetError(error, "l3session: bad session key length");
    return false;
  }
  *out = L3EnvelopeKey(std::string(session_key));
  if (!out->valid()) {
    // Unreachable while the two length checks agree; kept so a future change
    // to either one fails closed instead of sealing under a bad key.
    SetError(error, "l3session: bad session key length");
    return false;
  }
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

bool SealL3Document(std::string_view document,
                    std::string_view session_key,
                    std::uint32_t iterations,
                    std::string* out,
                    std::string* error) {
  if (out == nullptr) {
    SetError(error, "l3session: no output");
    return false;
  }
  L3EnvelopeKey key;
  if (!MakeL3EnvelopeKey(session_key, &key, error)) {
    return false;
  }
  return SealL3Envelope(document, key, iterations, out, error);
}

bool OpenL3Document(std::string_view envelope,
                    std::string_view session_key,
                    std::string* out,
                    std::string* error) {
  if (out == nullptr) {
    SetError(error, "l3session: no output");
    return false;
  }
  L3EnvelopeKey key;
  if (!MakeL3EnvelopeKey(session_key, &key, error)) {
    return false;
  }
  return OpenL3Envelope(envelope, key, out, error);
}

}  // namespace lengsuo::fp
