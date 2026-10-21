// l3_provisioning_impl - browser side of the L3 Mojo channel (task O).
//
// Before this file the frozen document travelled to every renderer as base64
// on the command line, which made it readable from /proc/<pid>/cmdline,
// chrome://version and crash dumps. It now leaves the browser only as the
// answer to a renderer's Mojo request.
//
// The pure part below has no Mojo dependency and is compiled standalone by
// tools/rebuild/run_l3_gtest.sh, so "frozen snapshot -> bytes" is covered by a
// unit test. The binding adapter at the bottom of the .cc needs generated
// bindings, so it is covered by the real build and the end-to-end acceptance
// run instead (see docs/l3-reports/task-O.md).
#ifndef LENGSUO_INTEGRATION_L3_PROVISIONING_IMPL_H_
#define LENGSUO_INTEGRATION_L3_PROVISIONING_IMPL_H_

#include <functional>
#include <string>
#include <utility>

#include "lengsuo/native/l3_wire.h"

namespace mojo {
class GenericPendingReceiver;
}

namespace lengsuo {

// Re-serializes a frozen snapshot in exactly the form the browser verified.
// The signed timestamp is preserved: substituting "now" would hand the
// renderer a different document than the one the browser checked.
inline std::string EncodeL3Document(const fp::L3Config& config) {
  return fp::EncodeL3Wire(config, config.signed_unix);
}

// What the browser answers a renderer's GetDocument() with. Empty means
// "this session has no L3 statement" and the renderer keeps upstream storage
// behaviour.
//
// |reader| returns the frozen snapshot, or nullptr when none was loaded. It is
// called on every request, so all renderers of one session receive identical
// bytes.
inline std::string GetL3Document(
    const std::function<const fp::L3Config*()>& reader) {
  const fp::L3Config* config = reader();
  return config ? EncodeL3Document(*config) : std::string();
}

// Binds |receiver| when it names L3Provisioning and returns true. Returns
// false for any other interface, leaving the receiver untouched, so the
// chrome-layer dispatcher can keep looking.
bool BindL3Provisioning(mojo::GenericPendingReceiver& receiver);

}  // namespace lengsuo

#endif  // LENGSUO_INTEGRATION_L3_PROVISIONING_IMPL_H_
