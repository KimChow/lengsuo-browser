// action_table_test - the uplink action table (task Q).
//
// The table is structural, so the tests are structural too: a payload that
// has the declared fields with the declared kinds passes, and everything else
// is rejected with a reason. The two rules the contract makes non-negotiable
// get their own tests: `data_url` is not collected by default, and a
// rejected payload is dropped rather than repaired.
#include "action_table.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace lengsuo::fp {
namespace {

// A payload that satisfies the CanvasSample schema, minus the bitmap.
const char kCanvas[] =
    R"({"from":"renderer","action":"CanvasSample","profile":"p1",)"
    R"("width":300,"length":150,"url":"https://example.test/a",)"
    R"("drew_text":true,"hash":"deadbeef"})";

const char kWebGL[] =
    R"({"from":"renderer","action":"WebGLSample","profile":"p1",)"
    R"("width":64,"length":64,"url":"https://example.test/b",)"
    R"("hash":"cafe"})";

const char kPageEvent[] =
    R"({"from":"browser","action":"PageEvent","url":"https://example.test/"})";

const char kHeartbeat[] =
    R"({"from":"kernel","action":"Heartbeat","ts_unix":1700000000})";

// The action name a payload declares.
std::string ActionOf(const char* payload) {
  JsonObject object;
  if (!JsonObject::Parse(payload, &object)) {
    return "";
  }
  std::string action;
  if (!object.GetString("action", &action)) {
    return "";
  }
  return action;
}

TEST(ActionTable, KnowsTheFourContractActions) {
  const ActionTable& table = ActionTable::Get();
  EXPECT_TRUE(table.Knows(kActionCanvasSample));
  EXPECT_TRUE(table.Knows(kActionWebGLSample));
  EXPECT_TRUE(table.Knows(kActionPageEvent));
  EXPECT_TRUE(table.Knows(kActionHeartbeat));
  EXPECT_TRUE(table.Knows(kActionPolicyEvent));
  EXPECT_FALSE(table.Knows("NoSuchAction"));
  EXPECT_FALSE(table.Knows(""));
  // A name is matched whole: a prefix is not a name.
  EXPECT_FALSE(table.Knows("Canvas"));
  EXPECT_FALSE(table.Knows("canvassample"));
}

TEST(ActionTable, NamesListsEveryActionInOrder) {
  const std::vector<std::string> names = ActionTable::Get().Names();
  ASSERT_EQ(names.size(), 5u);
  for (const std::string& name : names) {
    EXPECT_TRUE(ActionTable::Get().Knows(name));
  }
  // The order is fixed so the client's mirror can compare lists. The four
  // contract actions keep their places; the policy event was appended.
  EXPECT_EQ(names[0], kActionCanvasSample);
  EXPECT_EQ(names[1], kActionWebGLSample);
  EXPECT_EQ(names[2], kActionPageEvent);
  EXPECT_EQ(names[3], kActionHeartbeat);
  EXPECT_EQ(names[4], kActionPolicyEvent);
}

TEST(ActionTable, FindReturnsNullForAnUnknownName) {
  EXPECT_EQ(ActionTable::Get().Find("NoSuchAction"), nullptr);
  const ActionSchema* schema = ActionTable::Get().Find(kActionPageEvent);
  ASSERT_NE(schema, nullptr);
  EXPECT_EQ(schema->action, kActionPageEvent);
}

TEST(ActionTable, PolicyEventSchemaIsTheDecision) {
  const ActionSchema* schema = ActionTable::Get().Find(kActionPolicyEvent);
  ASSERT_NE(schema, nullptr);
  ASSERT_EQ(schema->fields.size(), 7u);
  EXPECT_EQ(schema->fields[0].name, "from");
  EXPECT_EQ(schema->fields[1].name, "action");
  EXPECT_EQ(schema->fields[2].name, "url");
  EXPECT_EQ(schema->fields[3].name, "category");
  EXPECT_EQ(schema->fields[4].name, "decision");
  EXPECT_EQ(schema->fields[5].name, "reason");
  // The rule is the one optional field: a decision that no rule matched has
  // no rule to report.
  EXPECT_EQ(schema->fields[6].name, "rule");
  EXPECT_TRUE(schema->fields[6].optional);
}

TEST(ActionValidate, AcceptsAPolicyEventPayload) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  const char kPolicyEvent[] =
      R"({"from":"browser","action":"PolicyEvent",)"
      R"("url":"https://example.test/x","category":"access",)"
      R"("decision":"block","reason":"black_list","rule":"^https://x$"})";
  const ActionValidation result = ValidateActionPayload(
      table, kActionPolicyEvent, kPolicyEvent, policy);
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.reason, ActionRejectReason::kNone);
}

TEST(ActionValidate, AcceptsAPolicyEventPayloadWithoutARule) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  const char kPolicyEvent[] =
      R"({"from":"browser","action":"PolicyEvent",)"
      R"("url":"https://example.test/x","category":"access",)"
      R"("decision":"allow","reason":"no_match"})";
  const ActionValidation result = ValidateActionPayload(
      table, kActionPolicyEvent, kPolicyEvent, policy);
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.reason, ActionRejectReason::kNone);
}

TEST(ActionTable, CanvasSchemaHasDrewTextAndDataUrl) {
  const ActionSchema* schema = ActionTable::Get().Find(kActionCanvasSample);
  ASSERT_NE(schema, nullptr);
  const ActionField* drew_text = nullptr;
  const ActionField* data_url = nullptr;
  for (const ActionField& field : schema->fields) {
    if (field.name == "drew_text") {
      drew_text = &field;
    }
    if (field.name == kActionDataUrlField) {
      data_url = &field;
    }
  }
  ASSERT_NE(drew_text, nullptr);
  EXPECT_EQ(drew_text->kind, FieldKind::kBoolean);
  EXPECT_TRUE(drew_text->optional);
  ASSERT_NE(data_url, nullptr);
  EXPECT_EQ(data_url->kind, FieldKind::kString);
}

TEST(ActionTable, WebGlSchemaHasNoDrewText) {
  const ActionSchema* schema = ActionTable::Get().Find(kActionWebGLSample);
  ASSERT_NE(schema, nullptr);
  for (const ActionField& field : schema->fields) {
    EXPECT_NE(field.name, "drew_text");
  }
}

TEST(ActionTable, PageEventSchemaIsFromActionUrl) {
  const ActionSchema* schema = ActionTable::Get().Find(kActionPageEvent);
  ASSERT_NE(schema, nullptr);
  ASSERT_EQ(schema->fields.size(), 3u);
  EXPECT_EQ(schema->fields[0].name, "from");
  EXPECT_EQ(schema->fields[1].name, "action");
  EXPECT_EQ(schema->fields[2].name, "url");
}

TEST(ActionTable, HeartbeatSchemaCarriesATimestamp) {
  const ActionSchema* schema = ActionTable::Get().Find(kActionHeartbeat);
  ASSERT_NE(schema, nullptr);
  const ActionField* ts = nullptr;
  for (const ActionField& field : schema->fields) {
    if (field.name == "ts_unix") {
      ts = &field;
    }
  }
  ASSERT_NE(ts, nullptr);
  EXPECT_EQ(ts->kind, FieldKind::kInteger);
  EXPECT_FALSE(ts->optional);
}

TEST(ActionValidate, AcceptsTheFourContractPayloads) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  for (const char* payload : {kCanvas, kWebGL, kPageEvent, kHeartbeat}) {
    const std::string action = ActionOf(payload);
    std::string reported;
    const ActionValidation result =
        ValidateActionPayload(table, action, payload, policy, &reported);
    EXPECT_TRUE(result.ok) << payload;
    EXPECT_EQ(result.reason, ActionRejectReason::kNone) << payload;
    EXPECT_EQ(reported, action);
  }
}

TEST(ActionValidate, RejectsAnUnknownAction) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  const ActionValidation result =
      ValidateActionPayload(table, "NoSuchAction", kPageEvent, policy);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.reason, ActionRejectReason::kUnknownAction);
}

TEST(ActionValidate, RejectsAPayloadFiledUnderTheWrongAction) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  // A CanvasSample filed as a PageEvent must not slip through the looser
  // PageEvent schema.
  const ActionValidation result =
      ValidateActionPayload(table, kActionPageEvent, kCanvas, policy);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.reason, ActionRejectReason::kActionMismatch);
  EXPECT_EQ(result.field, "action");
}

TEST(ActionValidate, RejectsMalformedJson) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  for (const char* payload : {"", "{", "not json", "{\"a\":}"}) {
    const ActionValidation result =
        ValidateActionPayload(table, kActionPageEvent, payload, policy);
    EXPECT_FALSE(result.ok) << payload;
    EXPECT_EQ(result.reason, ActionRejectReason::kMalformedJson) << payload;
  }
}

TEST(ActionValidate, RejectsAMissingRequiredField) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  // "url" is required on PageEvent.
  const char kNoUrl[] = R"({"from":"browser","action":"PageEvent"})";
  const ActionValidation result =
      ValidateActionPayload(table, kActionPageEvent, kNoUrl, policy);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.reason, ActionRejectReason::kMissingField);
  EXPECT_EQ(result.field, "url");
}

TEST(ActionValidate, RejectsAWrongType) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  // "ts_unix" must be an integer.
  const char kStringTs[] =
      R"({"from":"kernel","action":"Heartbeat","ts_unix":"1700000000"})";
  ActionValidation result =
      ValidateActionPayload(table, kActionHeartbeat, kStringTs, policy);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.reason, ActionRejectReason::kWrongType);
  EXPECT_EQ(result.field, "ts_unix");

  // "from" must be a string.
  const char kNumberFrom[] =
      R"({"from":7,"action":"PageEvent","url":"https://example.test/"})";
  result = ValidateActionPayload(table, kActionPageEvent, kNumberFrom, policy);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.reason, ActionRejectReason::kWrongType);
  EXPECT_EQ(result.field, "from");
}

TEST(ActionValidate, RejectsAFractionalInteger) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  const char kFractionalTs[] =
      R"({"from":"kernel","action":"Heartbeat","ts_unix":1.5})";
  const ActionValidation result =
      ValidateActionPayload(table, kActionHeartbeat, kFractionalTs, policy);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.reason, ActionRejectReason::kWrongType);
  EXPECT_EQ(result.field, "ts_unix");
}

TEST(ActionValidate, DrewTextIsOptional) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  const char kNoDrewText[] =
      R"({"from":"renderer","action":"CanvasSample","profile":"p1",)"
      R"("width":300,"length":150,"url":"https://example.test/a",)"
      R"("hash":"deadbeef"})";
  const ActionValidation result =
      ValidateActionPayload(table, kActionCanvasSample, kNoDrewText, policy);
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.reason, ActionRejectReason::kNone);
}

TEST(ActionValidate, DataUrlIsNotCollectedByDefault) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  EXPECT_FALSE(policy.data_url_allowed());
  const char kWithBitmap[] =
      R"({"from":"renderer","action":"CanvasSample","profile":"p1",)"
      R"("width":300,"length":150,"url":"https://example.test/a",)"
      R"("hash":"deadbeef","data_url":"data:image/png;base64,AAAA"})";
  const ActionValidation result =
      ValidateActionPayload(table, kActionCanvasSample, kWithBitmap, policy);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.reason, ActionRejectReason::kDataUrlNotAllowed);
  EXPECT_EQ(result.field, kActionDataUrlField);
}

TEST(ActionValidate, DataUrlIsAcceptedWhenExplicitlyEnabled) {
  const ActionTable& table = ActionTable::Get();
  ActionCollectorPolicy policy;
  policy.set_data_url_allowed(true);
  EXPECT_TRUE(policy.data_url_allowed());
  const char kWithBitmap[] =
      R"({"from":"renderer","action":"CanvasSample","profile":"p1",)"
      R"("width":300,"length":150,"url":"https://example.test/a",)"
      R"("hash":"deadbeef","data_url":"data:image/png;base64,AAAA"})";
  const ActionValidation result =
      ValidateActionPayload(table, kActionCanvasSample, kWithBitmap, policy);
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.reason, ActionRejectReason::kNone);
}

TEST(ActionValidate, TheBitmapIsRejectedNotTrimmed) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  // With collection off, the payload is rejected outright. It is not
  // stripped down to a bitmap-less sample: a trimmed payload would claim a
  // measurement that was never taken.
  const char kWithBitmap[] =
      R"({"from":"renderer","action":"WebGLSample","profile":"p1",)"
      R"("width":64,"length":64,"url":"https://example.test/b",)"
      R"("hash":"cafe","data_url":"data:image/png;base64,AAAA"})";
  const ActionValidation result =
      ValidateActionPayload(table, kActionWebGLSample, kWithBitmap, policy);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.reason, ActionRejectReason::kDataUrlNotAllowed);
}

TEST(ActionValidate, RejectsAnOversizedPayload) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  const std::string huge(kActionMaxPayloadBytes + 1, 'x');
  const ActionValidation result =
      ValidateActionPayload(table, kActionPageEvent, huge, policy);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.reason, ActionRejectReason::kPayloadTooLarge);
}

TEST(ActionValidate, TheActionOutParameterIsOnlyWrittenOnSuccess) {
  const ActionTable& table = ActionTable::Get();
  const ActionCollectorPolicy policy;
  std::string reported = "untouched";
  const ActionValidation result =
      ValidateActionPayload(table, kActionPageEvent, kCanvas, policy, &reported);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(reported, "untouched");
}

TEST(ActionEventLog, CountsThePayloadBytes) {
  EXPECT_EQ(ActionEventLogBytes(kPageEvent), std::string(kPageEvent).size());
  EXPECT_EQ(ActionEventLogBytes(""), 0u);
  // The log carries a size, not the payload: the helper returns a count and
  // nothing else, so there is no way to leak the bytes through it.
  EXPECT_EQ(ActionEventLogBytes(kCanvas), std::string(kCanvas).size());
}

}  // namespace
}  // namespace lengsuo::fp
