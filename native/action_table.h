// action_table - the uplink action table (task Q).
//
// The uplink carries events to the runner over the same pipe as the
// downlink. The contract (docs/kernel-wire-contract.md section 4) fixes the
// shape: every event is an `Event` frame whose JSON payload names an
// `action`, and the action's name selects a schema. There is no new
// interface per action - one frame type, one table, a growing list of names.
//
// The table's job is structural: is this payload the shape the action
// declares? It does not decide whether the values make sense (a canvas
// sample of width 0 is structurally fine and is not this table's business).
//
// Two rules are not negotiable:
//
//   * `data_url` is a canvas bitmap. It is NOT collected by default - the
//     privacy cost and the bandwidth cost are both real. A payload that
//     carries one is rejected unless collection was explicitly turned on.
//   * A payload that fails validation is dropped and counted. It is never
//     sent, and it never becomes an error the browser has to care about.
#ifndef LENGSUO_NATIVE_ACTION_TABLE_H_
#define LENGSUO_NATIVE_ACTION_TABLE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "kernel_pipe_json.h"

namespace lengsuo::fp {

// The action names the table knows (contract section 4).
inline constexpr char kActionCanvasSample[] = "CanvasSample";
inline constexpr char kActionWebGLSample[] = "WebGLSample";
inline constexpr char kActionPageEvent[] = "PageEvent";
inline constexpr char kActionHeartbeat[] = "Heartbeat";
// The access policy's decision (task T2). The table is append-only, so this
// one goes at the end and the four above keep their places.
inline constexpr char kActionPolicyEvent[] = "PolicyEvent";

// The field that carries the bitmap. The table checks for it by name so a
// payload cannot smuggle one past the check under a different spelling.
inline constexpr char kActionDataUrlField[] = "data_url";

// The largest payload the table will look at. A payload beyond this is
// dropped and counted rather than parsed: an unbounded payload is a way to
// make the browser allocate.
inline constexpr std::size_t kActionMaxPayloadBytes = 1024 * 1024;

// How one field is constrained.
enum class FieldKind {
  kString,   // a JSON string
  kInteger,  // a JSON integer
  kBoolean,  // a JSON boolean
};

// One field of an action's schema.
struct ActionField {
  std::string name;
  FieldKind kind = FieldKind::kString;
  // When true the field may be absent. A required field that is missing
  // fails the payload.
  bool optional = false;
};

// One row of the table: an action's name and the schema of its payload.
struct ActionSchema {
  std::string action;
  std::vector<ActionField> fields;
};

// The table of known actions.
class ActionTable {
 public:
  // The destructor is defined in the .cc: the class owns a vector, so it is
  // not a trivial type and Chromium's style checker wants the special
  // members out of line.
  ~ActionTable();
  ActionTable(const ActionTable&) = delete;
  ActionTable& operator=(const ActionTable&) = delete;
  ActionTable(ActionTable&&) = delete;
  ActionTable& operator=(ActionTable&&) = delete;

  // The singleton table: the action list is fixed by the contract, so
  // there is exactly one of it.
  static const ActionTable& Get();

  // True when `action` is a name the table knows.
  bool Knows(std::string_view action) const;

  // The schema for `action`, or nullptr when the name is unknown.
  const ActionSchema* Find(std::string_view action) const;

  // Every known action's name, in table order.
  std::vector<std::string> Names() const;

 private:
  ActionTable();
  std::vector<ActionSchema> schemas_;
};

// Why a payload was rejected. The reasons are structural; none of them
// carries a byte of the payload.
enum class ActionRejectReason {
  kNone,
  kPayloadTooLarge,
  kMalformedJson,
  kUnknownAction,
  kActionMismatch,   // the payload's action field is not the action asked for
  kMissingField,
  kWrongType,
  kDataUrlNotAllowed,
};

// The outcome of validating one uplink payload.
struct ActionValidation {
  bool ok = false;
  ActionRejectReason reason = ActionRejectReason::kNone;
  // The offending field's name, when the reason is about a field. Empty
  // otherwise. A field *name* is not payload content.
  std::string field;
};

// Whether the canvas bitmap may be collected. Off by default.
//
// The bitmap is the most sensitive thing on this channel and the most
// expensive: a full-page canvas is megabytes of base64 per sample. It is
// only collected when the caller explicitly says so.
class ActionCollectorPolicy {
 public:
  ActionCollectorPolicy() = default;
  ~ActionCollectorPolicy() = default;
  ActionCollectorPolicy(const ActionCollectorPolicy&) = default;
  ActionCollectorPolicy& operator=(const ActionCollectorPolicy&) = default;
  ActionCollectorPolicy(ActionCollectorPolicy&&) = default;
  ActionCollectorPolicy& operator=(ActionCollectorPolicy&&) = default;

  bool data_url_allowed() const { return data_url_allowed_; }
  // Turns the collection of `data_url` on or off. Turning it on is the
  // caller's decision and must be explicit.
  void set_data_url_allowed(bool allowed) { data_url_allowed_ = allowed; }

 private:
  bool data_url_allowed_ = false;
};

// Validates one uplink payload against the table.
//
// `action` is the action the caller believes it is sending; the payload
// must name the same action in its `action` field. That check is what keeps
// a payload from being filed under the wrong schema.
//
// `policy` decides whether `data_url` may be carried. A payload that has
// the field while the policy forbids it is rejected as kDataUrlNotAllowed -
// it is not quietly stripped, because a stripped payload would make the
// sample mean something different from what was collected.
//
// Returns a validation result. On success the payload's action name is
// written to `*action_out` when it is non-null.
ActionValidation ValidateActionPayload(const ActionTable& table,
                                       std::string_view action,
                                       std::string_view payload,
                                       const ActionCollectorPolicy& policy,
                                       std::string* action_out = nullptr);

// The number of bytes an event's log line should carry. The contract says
// the log records the action name and the byte count and nothing else, so
// this is the only size the caller is allowed to report.
std::size_t ActionEventLogBytes(std::string_view payload);

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_ACTION_TABLE_H_
