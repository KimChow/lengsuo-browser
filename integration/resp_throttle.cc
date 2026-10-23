// resp_throttle - the Chromium half (task 0017, the T1 wiring).
//
// The pure half of the decision is inline in resp_throttle.h, because
// tools/rebuild/run_l3_gtest.sh compiles that header with a plain g++ and no
// Chromium headers at all. This file is the half that cannot be tested that
// way: it derives from blink::URLLoaderThrottle, owns the two data pipes that
// carry the body past, reads the frozen ConfigBundle off the process pipe,
// and puts the report on the uplink.
//
// How the body is taken without being disturbed. A URLLoaderThrottle has
// exactly one route to a response body: Delegate::InterceptResponse, which
// hands the caller the data pipe the network filled and gives the page
// whatever pipe the caller passes back. The call rebinds the URLLoader and
// URLLoaderClient endpoints as well, and that is deliberate - its own doc
// comment says it "Replaces the URLLoader and URLLoaderClient endpoints held
// by the ThrottlingURLLoader instance".
//
// Two calls are made, both synchronous, in the same function:
//
//   1. The first takes the network's body handle and the two original
//      endpoints, and puts throwaway endpoints in their place for the moment
//      between the two calls. Nothing is dispatched in that gap: the code
//      between the two calls runs no message loop, so the transient state is
//      never observed.
//   2. The second gives both original endpoints straight back and hands the
//      page the pipe this file created. After it, the ThrottlingURLLoader
//      holds the network's own URLLoader and the page's own client again,
//      exactly as before, with the page's body pipe in place of the
//      network's.
//
// The net effect is one thing and only one thing: "give me the bytes that
// would have gone to the page, and give the page this instead". The loader
// still reaches the real network, the page still gets every callback, and
// only where the body bytes flow first has changed. That is why neither
// endpoint needs a forwarder object: there is nothing to forward through.
// A proxy throttle that kept the endpoints and re-sent every call would have
// to re-implement all five URLLoaderClient methods - including the
// OnReceiveResponse that carries a data pipe and the OnUploadProgress that
// carries a callback - and every one of them is on the page's critical path.
// (extensions/renderer/extension_localization_throttle.cc is that shape, and
// is why it is not copied here.)
//
// Three more things this file is careful about:
//
//   * It does not read synchronously. The body is pumped asynchronously by a
//     mojo::SimpleWatcher on the network's pipe, one chunk at a time, and a
//     chunk is read only when the page's previous chunk has been written
//     (native/resp_tee.h's gate). A slow page therefore reads slowly; it
//     never becomes an unbounded buffer in the browser.
//
//   * It mounts nothing when there is nothing to do. CreateRespThrottle
//     returns nullptr for "no pipe", "no section", "switched off" and "no
//     rules", so a switched-off monitor is not a throttle that inspects every
//     response and copies nothing.
//
//   * It never changes what the page receives. The page's pipe is written
//     with exactly the bytes read from the network's pipe, in order, and it
//     is closed when the network's is. A failure on this side - a broken
//     pipe, a page that has gone away - stops the copy and releases the
//     network's pipe; it never truncates or reorders what the page is
//     already reading.
#include "lengsuo/integration/resp_throttle.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_view_util.h"
#include "base/task/sequenced_task_runner.h"
#include "lengsuo/integration/kernel_pipe_client.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "mojo/public/cpp/system/data_pipe_producer.h"
#include "mojo/public/cpp/system/simple_watcher.h"
#include "mojo/public/cpp/system/string_data_source.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_loader.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "url/gurl.h"

namespace lengsuo {
namespace {

// The monitor's state: one per process, like the uplink it reports on.
//
// The counters it holds must survive: native/resp_monitor.h is explicit that
// pushing the same configuration down again - once per storage partition, for
// instance - must not hand a rule a fresh allowance. A state per throttle
// would hand every response a fresh allowance, so the state is here instead.
fp::RespMonitorState& SharedRespMonitorState() {
  static fp::RespMonitorState state;
  return state;
}

// The response monitor as a response-path hook.
class RespThrottle : public blink::URLLoaderThrottle {
 public:
  explicit RespThrottle(fp::RespMonitorState* state) : state_(state) {}
  RespThrottle(const RespThrottle&) = delete;
  RespThrottle& operator=(const RespThrottle&) = delete;
  ~RespThrottle() override;

  // Called once, before the resource request is started. The only thing
  // wanted from it is the initiator origin: the response phase no longer has
  // the request in hand, and "did a page start this?" is a question about the
  // request. `defer` is left alone - nothing here waits.
  void WillStartRequest(network::ResourceRequest* request,
                        bool* /*defer*/) override {
    if (request == nullptr) {
      return;
    }
    initiator_scheme_ = request->request_initiator
                            ? request->request_initiator->scheme()
                            : std::string();
  }

  // Called when the response head is available, before the body is forwarded
  // to the page. This is where the decision is made and the body is taken.
  void WillProcessResponse(const GURL& response_url,
                           network::mojom::URLResponseHead* response_head,
                           bool* /*defer*/) override;

 private:
  // One readable signal on the network's pipe.
  void OnSourceReadable(MojoResult result,
                        const mojo::HandleSignalsState& state);
  // The page's copy of one chunk has been taken over by the pipe.
  void OnPageWritten(MojoResult result);

  // Stop watching and release the network's pipe. Safe to call more than
  // once, and safe to call from inside a watcher callback: Cancel() promises
  // the callback will not be entered again.
  void StopReading();
  // The body ended: close the page's pipe, which is how the page learns that
  // the body is complete, then report what was seen. Reports once.
  void FinishBody();
  // The body cannot be read to the end - the page is gone, or the pipe broke.
  // Stop and report nothing: half a body reported as if it were complete
  // would be a lie.
  void AbandonBody();

  fp::RespMonitorState* const state_;
  // What the decision was for this response, and the facts the report needs.
  fp::RespMonitorOutcome outcome_;
  std::string initiator_scheme_;
  std::string url_;
  std::string content_type_;
  int status_ = 0;

  fp::RespTee tee_;
  // The network's body, read by this file; the page has its own pipe.
  mojo::ScopedDataPipeConsumerHandle network_body_;
  // The page's pipe, written by this file. Closing it ends the page's body.
  std::unique_ptr<mojo::DataPipeProducer> page_writer_;
  // The watcher has no default constructor, so it is made on demand and kept
  // only while there is something to watch.
  std::unique_ptr<mojo::SimpleWatcher> source_watcher_;
  // The buffer the page's current chunk is written from. It is a member
  // because the pipe's write outlives the call that started it.
  std::string page_chunk_;
  bool reported_ = false;

  // Last member on purpose: it is destroyed first, so no watcher callback can
  // reach a half-destroyed throttle.
  base::WeakPtrFactory<RespThrottle> weak_factory_{this};
};

void RespThrottle::WillProcessResponse(
    const GURL& response_url,
    network::mojom::URLResponseHead* response_head,
    bool* /*defer*/) {
  if (response_head == nullptr) {
    return;
  }
  content_type_ = response_head->mime_type;
  status_ = 0;
  std::string content_encoding;
  if (response_head->headers) {
    status_ = response_head->headers->response_code();
    content_encoding =
        response_head->headers->GetNormalizedHeader("Content-Encoding")
            .value_or(std::string());
  }

  url_ = response_url.spec();
  const fp::RespMonitorRequest request =
      BuildRespMonitorRequest(url_, response_url.scheme(), initiator_scheme_);
  const RespMonitorOutcome outcome =
      DecideRespMonitor(state_, request, content_type_, content_encoding);
  if (!outcome.monitor) {
    // No rule claimed this response, or its body must not be read, or the
    // rule has said all it had to say about this URL. The response is left
    // completely alone.
    return;
  }

  // The page's own pipe is created before anything is touched, so a failure
  // here leaves the response exactly as it was.
  mojo::ScopedDataPipeProducerHandle page_producer;
  mojo::ScopedDataPipeConsumerHandle page_consumer;
  if (mojo::CreateDataPipe(nullptr, page_producer, page_consumer) !=
      MOJO_RESULT_OK) {
    // A pipe could not be made. The rule is switched off - it has failed to
    // serve its purpose once - and this response is left alone.
    fp::RespMonitorDisableRule(state_, outcome.rule_id);
    return;
  }

  // Take the network's body and the two original endpoints.
  mojo::PendingRemote<network::mojom::URLLoader> scratch_loader;
  mojo::PendingRemote<network::mojom::URLLoaderClient> scratch_client;
  mojo::PendingRemote<network::mojom::URLLoader> source_loader;
  mojo::PendingReceiver<network::mojom::URLLoaderClient> source_client_receiver;
  mojo::ScopedDataPipeConsumerHandle network_body;
  delegate_->InterceptResponse(
      std::move(scratch_loader),
      scratch_client.InitWithNewPipeAndPassReceiver(), &source_loader,
      &source_client_receiver, &network_body);

  // Give both originals straight back, with the page's pipe where the
  // network's body was. Both out-parameters have to be non-null: the
  // ThrottlingURLLoader binds its new endpoints on every call, and binding
  // over a bound endpoint is a check failure.
  mojo::PendingRemote<network::mojom::URLLoader> discarded_loader;
  mojo::PendingReceiver<network::mojom::URLLoaderClient> discarded_client;
  delegate_->InterceptResponse(std::move(source_loader),
                               std::move(source_client_receiver),
                               &discarded_loader, &discarded_client,
                               &page_consumer);

  fp::RespTeeReset(&tee_, outcome.capture_limit);
  outcome_ = outcome;
  network_body_ = std::move(network_body);
  page_writer_ =
      std::make_unique<mojo::DataPipeProducer>(std::move(page_producer));

  if (!network_body_.is_valid()) {
    // The response carries no body at all (204, a HEAD response). Closing the
    // page's pipe is the whole of what the page is owed.
    FinishBody();
    return;
  }

  source_watcher_ =
      std::make_unique<mojo::SimpleWatcher>(
          FROM_HERE, mojo::SimpleWatcher::ArmingPolicy::MANUAL,
          base::SequencedTaskRunner::GetCurrentDefault());
  source_watcher_->Watch(
      network_body_.get(), MOJO_HANDLE_SIGNAL_READABLE,
      MOJO_TRIGGER_CONDITION_SIGNALS_SATISFIED,
      base::BindRepeating(&RespThrottle::OnSourceReadable,
                          weak_factory_.GetWeakPtr()));
  // The first read is armed here; every later one is armed by the read, or by
  // the page's write, that unblocks it.
  source_watcher_->ArmOrNotify();
}

void RespThrottle::OnSourceReadable(MojoResult result,
                                    const mojo::HandleSignalsState& /*state*/) {
  if (result == MOJO_RESULT_FAILED_PRECONDITION) {
    // The network closed its end: the body is over.
    FinishBody();
    return;
  }
  if (result != MOJO_RESULT_OK) {
    AbandonBody();
    return;
  }
  if (!fp::RespTeeWantsRead(tee_)) {
    // A write to the page is still in flight. The page's copy is the copy
    // that must not be delayed, so this read waits, and OnPageWritten re-arms
    // the watcher.
    return;
  }

  base::span<const uint8_t> data;
  const MojoResult read =
      network_body_->BeginReadData(MOJO_READ_DATA_FLAG_NONE, data);
  if (read == MOJO_RESULT_FAILED_PRECONDITION) {
    FinishBody();
    return;
  }
  if (read == MOJO_RESULT_SHOULD_WAIT) {
    source_watcher_->ArmOrNotify();
    return;
  }
  if (read != MOJO_RESULT_OK) {
    AbandonBody();
    return;
  }

  // The one line that makes this a tee: the whole chunk is copied for the
  // page and the bounded prefix of it is copied for the monitor.
  fp::RespTeeFan(&tee_, base::as_string_view(data), &page_chunk_);
  const MojoResult ended = network_body_->EndReadData(data.size());
  if (ended != MOJO_RESULT_OK) {
    AbandonBody();
    return;
  }

  fp::RespTeeWriteStarted(&tee_);
  page_writer_->Write(
      std::make_unique<mojo::StringDataSource>(
          page_chunk_, mojo::StringDataSource::AsyncWritingMode::
                           STRING_STAYS_VALID_UNTIL_COMPLETION),
      base::BindOnce(&RespThrottle::OnPageWritten,
                     weak_factory_.GetWeakPtr()));
}

void RespThrottle::OnPageWritten(MojoResult result) {
  fp::RespTeeWriteFinished(&tee_);
  if (result != MOJO_RESULT_OK) {
    // The page is gone. Stop reading and let the network's pipe close.
    AbandonBody();
    return;
  }
  source_watcher_->ArmOrNotify();
}

void RespThrottle::StopReading() {
  if (source_watcher_) {
    source_watcher_->Cancel();
  }
  // Closing the network's pipe is what tells the network the body is no
  // longer wanted; it only closes once the watcher has let go of it.
  network_body_.reset();
}

void RespThrottle::FinishBody() {
  // Closing the page's producer is how the page learns that the body ended.
  // Nothing is in flight here: a chunk is read only when the previous write
  // has been acknowledged.
  page_writer_.reset();
  StopReading();
  if (reported_) {
    return;
  }
  reported_ = true;
  const fp::RespMonitorReport report =
      BuildRespMonitorReport(outcome_, url_, status_, content_type_, tee_);
  if (KernelPipeClient* client = KernelPipeClient::Get()) {
    // Non-blocking by contract: it validates the payload and drops it into a
    // bounded queue that a sender thread drains.
    client->EnqueueEvent(fp::kActionResponseMonitor,
                         fp::BuildRespMonitorActionPayload(report));
  }
}

void RespThrottle::AbandonBody() {
  page_writer_.reset();
  StopReading();
}

RespThrottle::~RespThrottle() {
  AbandonBody();
}

}  // namespace

std::unique_ptr<blink::URLLoaderThrottle> CreateRespThrottle() {
  KernelPipeClient* client = KernelPipeClient::Get();
  if (client == nullptr) {
    // No pipe was requested, so the platform sent no configuration. There is
    // no monitor to mount.
    return nullptr;
  }
  fp::RespMonitorConfig config;
  if (!ReadRespMonitorConfig(client->config(), &config)) {
    // The bundle does not carry a readable section. Mount nothing - never a
    // monitor that reads every response because the configuration could not
    // be read.
    return nullptr;
  }
  if (!RespMonitorHasRules(config)) {
    // Switched off, or no rule could ever fire. Mounting a throttle for it
    // would be a cost on every response with no benefit.
    return nullptr;
  }
  fp::RespMonitorState& state = SharedRespMonitorState();
  // Installing the same rules again is what the native layer is built for: it
  // keeps the counters of the rules that stay, so a rule's per-URL allowance
  // is not renewed by a second mount.
  fp::RespMonitorSetRules(&state, config);
  return std::make_unique<RespThrottle>(&state);
}

}  // namespace lengsuo