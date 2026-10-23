#include "action_table.h"

#include <utility>

namespace lengsuo::fp {
namespace {

// The table's rows (contract section 4).
//
// The field names are copied from the contract's schema table; the types are
// the ones the reference implementation's payloads actually use. `drew_text`
// is optional for CanvasSample and absent for WebGLSample - the contract
// draws that distinction on purpose.
//
// `data_url` is listed as a required field because the reference payload
// carries it, but the collector policy has the last word: with the policy
// off, a payload that carries the field is rejected rather than trimmed.

ActionField String(std::string name, bool optional = false) {
  ActionField field;
  field.name = std::move(name);
  field.kind = FieldKind::kString;
  field.optional = optional;
  return field;
}

ActionField Integer(std::string name, bool optional = false) {
  ActionField field;
  field.name = std::move(name);
  field.kind = FieldKind::kInteger;
  field.optional = optional;
  return field;
}

ActionField Boolean(std::string name, bool optional = false) {
  ActionField field;
  field.name = std::move(name);
  field.kind = FieldKind::kBoolean;
  field.optional = optional;
  return field;
}

}  // namespace

ActionTable::~ActionTable() = default;

ActionTable::ActionTable() {
  schemas_.push_back(ActionSchema{
      kActionCanvasSample,
      {String("from"), String("action"), String("profile"),
       Integer("width"), Integer("length"), String("url"),
       Boolean("drew_text", /*optional=*/true), String("hash"),
       String(kActionDataUrlField)}});
  schemas_.push_back(ActionSchema{
      kActionWebGLSample,
      {String("from"), String("action"), String("profile"),
       Integer("width"), Integer("length"), String("url"), String("hash"),
       String(kActionDataUrlField)}});
  schemas_.push_back(ActionSchema{
      kActionPageEvent, {String("from"), String("action"), String("url")}});
  schemas_.push_back(ActionSchema{
      kActionHeartbeat,
      {String("from"), String("action"), Integer("ts_unix")}});
  schemas_.push_back(ActionSchema{
      kActionResponseMonitor,
      {String("from"), String("action"), String("rule_id"), String("url"),
       Integer("status"), String("content_type"), Integer("body_bytes"),
       Boolean("truncated"), String(kActionBodyField, /*optional=*/true)}});
}

const ActionTable& ActionTable::Get() {
  static const ActionTable* table = new ActionTable();
  return *table;
}

bool ActionTable::Knows(std::string_view action) const {
  return Find(action) != nullptr;
}

const ActionSchema* ActionTable::Find(std::string_view action) const {
  for (const ActionSchema& schema : schemas_) {
    if (schema.action == action) {
      return &schema;
    }
  }
  return nullptr;
}

std::vector<std::string> ActionTable::Names() const {
  std::vector<std::string> names;
  names.reserve(schemas_.size());
  for (const ActionSchema& schema : schemas_) {
    names.push_back(schema.action);
  }
  return names;
}

ActionValidation ValidateActionPayload(const ActionTable& table,
                                       std::string_view action,
                                       std::string_view payload,
                                       const ActionCollectorPolicy& policy,
                                       std::string* action_out) {
  ActionValidation result;
  if (payload.size() > kActionMaxPayloadBytes) {
    result.reason = ActionRejectReason::kPayloadTooLarge;
    return result;
  }
  const ActionSchema* schema = table.Find(action);
  if (schema == nullptr) {
    result.reason = ActionRejectReason::kUnknownAction;
    return result;
  }
  JsonObject object;
  if (!JsonObject::Parse(payload, &object)) {
    result.reason = ActionRejectReason::kMalformedJson;
    return result;
  }
  // The payload must name the action it is filed under. Without this a
  // CanvasSample's bytes could be filed as a PageEvent and pass the looser
  // schema.
  std::string named;
  if (!object.GetString("action", &named)) {
    result.reason = ActionRejectReason::kMissingField;
    result.field = "action";
    return result;
  }
  if (named != schema->action) {
    result.reason = ActionRejectReason::kActionMismatch;
    result.field = "action";
    return result;
  }
  for (const ActionField& field : schema->fields) {
    if (field.name == kActionDataUrlField && !policy.data_url_allowed()) {
      // The bitmap is not collected by default. A payload that carries one
      // is rejected, not trimmed: a trimmed sample would say something the
      // collector did not measure.
      if (object.Has(field.name)) {
        result.reason = ActionRejectReason::kDataUrlNotAllowed;
        result.field = field.name;
        return result;
      }
      continue;
    }
    const JsonValue* value = object.Find(field.name);
    if (value == nullptr) {
      if (field.optional) {
        continue;
      }
      result.reason = ActionRejectReason::kMissingField;
      result.field = field.name;
      return result;
    }
    switch (field.kind) {
      case FieldKind::kString:
        if (value->kind != JsonKind::kString) {
          result.reason = ActionRejectReason::kWrongType;
          result.field = field.name;
          return result;
        }
        break;
      case FieldKind::kInteger: {
        if (value->kind != JsonKind::kNumber) {
          result.reason = ActionRejectReason::kWrongType;
          result.field = field.name;
          return result;
        }
        std::int64_t number = 0;
        if (!object.GetInt64(field.name, &number)) {
          result.reason = ActionRejectReason::kWrongType;
          result.field = field.name;
          return result;
        }
        break;
      }
      case FieldKind::kBoolean:
        if (value->kind != JsonKind::kBoolean) {
          result.reason = ActionRejectReason::kWrongType;
          result.field = field.name;
          return result;
        }
        break;
    }
  }
  result.ok = true;
  result.reason = ActionRejectReason::kNone;
  if (action_out != nullptr) {
    *action_out = named;
  }
  return result;
}

std::size_t ActionEventLogBytes(std::string_view payload) {
  return payload.size();
}

}  // namespace lengsuo::fp
