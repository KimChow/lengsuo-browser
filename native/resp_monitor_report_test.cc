// resp_monitor_report_test - the report of a monitored response (task T1).
//
// The report goes out on the uplink, so the tests check two things: that the
// JSON it carries is well formed and says what it means to say, and that the
// action table - the last checkpoint before the bytes go on the wire -
// accepts it.
#include "resp_monitor_report.h"

#include <gtest/gtest.h>

#include <string>

#include "action_table.h"
#include "kernel_pipe_json.h"

namespace lengsuo::fp {
namespace {

// A report with every field filled in.
RespMonitorReport MakeReport() {
  RespMonitorReport report;
  report.rule_id = "r1";
  report.url = "https://example.test/a";
  report.status = 200;
  report.content_type = "text/plain";
  report.body_bytes = 12;
  report.truncated = false;
  report.body = "hello world";
  return report;
}

// Reads one member out of a JSON object.
std::string Member(const std::string& json, const std::string& key) {
  JsonObject object;
  if (!JsonObject::Parse(json, &object)) {
    return "";
  }
  const JsonValue* value = object.Find(key);
  if (value == nullptr) {
    return "";
  }
  return value->text;
}

TEST(RespMonitorReport, TheReportCarriesTheFieldsTheContractNames) {
  const RespMonitorReport report = MakeReport();
  const std::string json = BuildRespMonitorReportJson(report);
  JsonObject object;
  ASSERT_TRUE(JsonObject::Parse(json, &object)) << json;
  EXPECT_EQ(Member(json, "rule_id"), "r1");
  EXPECT_EQ(Member(json, "url"), "https://example.test/a");
  EXPECT_EQ(Member(json, "status"), "200");
  EXPECT_EQ(Member(json, "content_type"), "text/plain");
  EXPECT_EQ(Member(json, "body_bytes"), "12");
  EXPECT_EQ(Member(json, "truncated"), "false");
}

TEST(RespMonitorReport, TheBodyIsLeftOutUnlessItIsAskedFor) {
  RespMonitorReport report = MakeReport();
  report.include_body = false;
  const std::string json = BuildRespMonitorReportJson(report);
  JsonObject object;
  ASSERT_TRUE(JsonObject::Parse(json, &object)) << json;
  // An absent body and an empty body are different facts.
  EXPECT_FALSE(object.Has("body"));
}

TEST(RespMonitorReport, TheBodyIsCarriedWhenItIsAskedFor) {
  RespMonitorReport report = MakeReport();
  report.include_body = true;
  const std::string json = BuildRespMonitorReportJson(report);
  JsonObject object;
  ASSERT_TRUE(JsonObject::Parse(json, &object)) << json;
  EXPECT_TRUE(object.Has("body"));
  EXPECT_EQ(Member(json, "body"), "hello world");
}

TEST(RespMonitorReport, ThePayloadNamesTheActionAndTheSide) {
  const std::string payload = BuildRespMonitorActionPayload(MakeReport());
  JsonObject object;
  ASSERT_TRUE(JsonObject::Parse(payload, &object)) << payload;
  EXPECT_EQ(Member(payload, "from"), kRespMonitorFrom);
  EXPECT_EQ(Member(payload, "action"), kActionResponseMonitor);
}

TEST(RespMonitorReport, TheActionTableAcceptsThePayloadWithoutABody) {
  RespMonitorReport report = MakeReport();
  report.include_body = false;
  const std::string payload = BuildRespMonitorActionPayload(report);
  const ActionCollectorPolicy policy;
  const ActionValidation result = ValidateActionPayload(
      ActionTable::Get(), kActionResponseMonitor, payload, policy);
  EXPECT_TRUE(result.ok) << payload;
  EXPECT_EQ(result.reason, ActionRejectReason::kNone);
}

TEST(RespMonitorReport, TheActionTableAcceptsThePayloadWithABody) {
  // The pipe validates with the default policy, so a payload that carries a
  // body has to pass under that policy - otherwise the body could never get
  // out. Whether the body is carried at all is the collector's decision, and
  // the collector made it.
  RespMonitorReport report = MakeReport();
  report.include_body = true;
  const std::string payload = BuildRespMonitorActionPayload(report);
  const ActionCollectorPolicy policy;
  const ActionValidation result = ValidateActionPayload(
      ActionTable::Get(), kActionResponseMonitor, payload, policy);
  EXPECT_TRUE(result.ok) << payload;
  EXPECT_EQ(result.reason, ActionRejectReason::kNone);
}

TEST(RespMonitorReport, TheActionTableAcceptsThePayloadWithABodyUnderTheDefaultPolicy) {
  // The same check with the policy the pipe actually uses. It is spelled out
  // again because this is the one that would silently break the body: the
  // pipe's own EnqueueEvent builds a default policy and hands it over.
  RespMonitorReport report = MakeReport();
  report.include_body = true;
  const std::string payload = BuildRespMonitorActionPayload(report);
  ActionCollectorPolicy policy;
  EXPECT_FALSE(policy.data_url_allowed());
  const ActionValidation result = ValidateActionPayload(
      ActionTable::Get(), kActionResponseMonitor, payload, policy);
  EXPECT_TRUE(result.ok) << payload;
}

TEST(RespMonitorReport, TheReportCarriesNoRequestHeaders) {
  // The report is about a response. A header - a cookie, an authorization -
  // is where credentials live, and there is no field for one.
  const std::string json = BuildRespMonitorReportJson(MakeReport());
  for (const char* forbidden :
       {"cookie", "Cookie", "authorization", "Authorization", "headers",
        "set-cookie"}) {
    EXPECT_EQ(json.find(forbidden), std::string::npos) << forbidden;
  }
}

TEST(RespMonitorReport, TheJsonEscaperLeavesValidUtf8Alone) {
  EXPECT_EQ(EscapeRespMonitorJsonString("plain"), "\"plain\"");
  // A Chinese character is three bytes and passes through as it is.
  EXPECT_EQ(EscapeRespMonitorJsonString("\xe4\xb8\xad"), "\"\xe4\xb8\xad\"");
  EXPECT_EQ(EscapeRespMonitorJsonString(""), "\"\"");
}

TEST(RespMonitorReport, TheJsonEscaperEscapesTheSpecials) {
  EXPECT_EQ(EscapeRespMonitorJsonString("a\"b"), "\"a\\\"b\"");
  EXPECT_EQ(EscapeRespMonitorJsonString("a\\b"), "\"a\\\\b\"");
  EXPECT_EQ(EscapeRespMonitorJsonString("a\nb"), "\"a\\nb\"");
  EXPECT_EQ(EscapeRespMonitorJsonString("a\tb"), "\"a\\tb\"");
  EXPECT_EQ(EscapeRespMonitorJsonString("a\rb"), "\"a\\rb\"");
  EXPECT_EQ(EscapeRespMonitorJsonString("a\x08""b"), "\"a\\bb\"");
  EXPECT_EQ(EscapeRespMonitorJsonString("a\x0c""b"), "\"a\\fb\"");
}

TEST(RespMonitorReport, TheJsonEscaperEscapesAControlCharacter) {
  EXPECT_EQ(EscapeRespMonitorJsonString("a\x01""b"), "\"a\\u0001b\"");
  EXPECT_EQ(EscapeRespMonitorJsonString("\x1f"), "\"\\u001f\"");
}

TEST(RespMonitorReport, TheJsonEscaperEscapesAnInvalidByte) {
  // A byte that is not part of a valid UTF-8 sequence cannot go into a JSON
  // string as it is.
  EXPECT_EQ(EscapeRespMonitorJsonString("\xff"), "\"\\u00ff\"");
  EXPECT_EQ(EscapeRespMonitorJsonString("\xc3"), "\"\\u00c3\"");
  // A truncated multi-byte sequence is escaped byte by byte.
  EXPECT_EQ(EscapeRespMonitorJsonString("\xe4\xb8"), "\"\\u00e4\\u00b8\"");
}

TEST(RespMonitorReport, TheJsonEscaperRejectsAnOverlongForm) {
  // U+0020 written as two bytes is not valid UTF-8.
  EXPECT_EQ(EscapeRespMonitorJsonString("\xc0\xa0"), "\"\\u00c0\\u00a0\"");
  // A surrogate is not a code point either.
  EXPECT_EQ(EscapeRespMonitorJsonString("\xed\xa0\x80"), "\"\\u00ed\\u00a0\\u0080\"");
}

TEST(RespMonitorReport, TheEscapedBodyIsStillValidJson) {
  RespMonitorReport report = MakeReport();
  report.include_body = true;
  report.body = std::string("line\nbreak\t\"quoted\"") + "\xff";
  const std::string json = BuildRespMonitorReportJson(report);
  JsonObject object;
  EXPECT_TRUE(JsonObject::Parse(json, &object)) << json;
}

TEST(RespMonitorReport, TheStatusAndTheByteCountAreNumbers) {
  RespMonitorReport report = MakeReport();
  report.status = 404;
  report.body_bytes = 4096;
  const std::string json = BuildRespMonitorReportJson(report);
  JsonObject object;
  ASSERT_TRUE(JsonObject::Parse(json, &object)) << json;
  const JsonValue* status = object.Find("status");
  ASSERT_NE(status, nullptr);
  EXPECT_EQ(status->kind, JsonKind::kNumber);
  const JsonValue* bytes = object.Find("body_bytes");
  ASSERT_NE(bytes, nullptr);
  EXPECT_EQ(bytes->kind, JsonKind::kNumber);
}

}  // namespace
}  // namespace lengsuo::fp
