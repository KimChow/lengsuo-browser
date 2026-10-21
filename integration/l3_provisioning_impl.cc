#include "lengsuo/integration/l3_provisioning_impl.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "lengsuo/integration/browser_bootstrap.h"
#include "lengsuo/public/mojom/l3_provisioning.mojom.h"
#include "mojo/public/cpp/bindings/generic_pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"

namespace lengsuo {
namespace {

class L3ProvisioningImpl : public mojom::L3Provisioning {
 public:
  L3ProvisioningImpl() = default;

  // The answer is rebuilt from the snapshot frozen before this renderer was
  // launched; nothing is cached per renderer, so two renderers started from
  // the same browser always see the same bytes.
  void GetDocument(GetDocumentCallback callback) override {
    const std::string document = GetL3Document([] { return BrowserL3Config(); });
    auto reply = mojom::L3Document::New();
    reply->bytes = std::vector<uint8_t>(document.begin(), document.end());
    std::move(callback).Run(std::move(reply));
  }
};

}  // namespace

bool BindL3Provisioning(mojo::GenericPendingReceiver& receiver) {
  mojo::PendingReceiver<mojom::L3Provisioning> pending =
      receiver.As<mojom::L3Provisioning>();
  if (!pending.is_valid()) {
    return false;
  }
  // Self-owned: the browser has no per-renderer bookkeeping for this
  // interface, and the endpoint closes with the renderer's pipe.
  mojo::MakeSelfOwnedReceiver(std::make_unique<L3ProvisioningImpl>(),
                              std::move(pending));
  return true;
}

}  // namespace lengsuo
