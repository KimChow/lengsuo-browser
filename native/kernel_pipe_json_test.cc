// kernel_pipe_json_test - the JSON reader of the kernel side of the
// runner <-> kernel pipe (task Q).
//
// This file deliberately links no crypto: the reader is a layer of its own,
// and the test proves it.
#include "kernel_pipe_json.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace lengsuo::fp {
namespace {

TEST(KernelPipeJson, ParsesAFlatObject) {
  JsonObject object;
  ASSERT_TRUE(JsonObject::Parse(
      "{\"a\":\"x\",\"b\":12,\"c\":true,\"d\":null}", &object));
  EXPECT_TRUE(object.Has("a"));
  EXPECT_FALSE(object.Has("zz"));
  std::string text;
  ASSERT_TRUE(object.GetString("a", &text));
  EXPECT_EQ(text, "x");
  std::int64_t number = 0;
  ASSERT_TRUE(object.GetInt64("b", &number));
  EXPECT_EQ(number, 12);
  bool flag = false;
  ASSERT_TRUE(object.GetBool("c", &flag));
  EXPECT_TRUE(flag);
  // A string is not a number and a number is not a string.
  EXPECT_FALSE(object.GetInt64("a", &number));
  EXPECT_FALSE(object.GetString("b", &text));
  EXPECT_FALSE(object.GetBool("a", &flag));
}

TEST(KernelPipeJson, RejectsMalformedInput) {
  JsonObject object;
  EXPECT_FALSE(JsonObject::Parse("", &object));
  EXPECT_FALSE(JsonObject::Parse("{", &object));
  EXPECT_FALSE(JsonObject::Parse("{\"a\"}", &object));
  EXPECT_FALSE(JsonObject::Parse("{\"a\":}", &object));
  EXPECT_FALSE(JsonObject::Parse("{\"a\":1,}", &object));
  EXPECT_FALSE(JsonObject::Parse("{\"a\":1}trailing", &object));
  EXPECT_FALSE(JsonObject::Parse("[]", &object));
  EXPECT_FALSE(JsonObject::Parse("{\"a\":1,\"a\":2}", &object));
}

TEST(KernelPipeJson, ResolvesEscapes) {
  JsonObject object;
  ASSERT_TRUE(JsonObject::Parse("{\"a\":\"x\\ny\\u0041\"}", &object));
  std::string text;
  ASSERT_TRUE(object.GetString("a", &text));
  EXPECT_EQ(text, "x\nyA");
}

TEST(KernelPipeJson, KeepsNestedValuesAsRawText) {
  JsonObject object;
  ASSERT_TRUE(JsonObject::Parse(
      "{\"a\":{\"b\":1},\"c\":[1,2]}", &object));
  const JsonValue* nested = object.Find("a");
  ASSERT_NE(nested, nullptr);
  EXPECT_EQ(nested->kind, JsonKind::kObject);
  EXPECT_EQ(nested->text, "{\"b\":1}");
  const JsonValue* array = object.Find("c");
  ASSERT_NE(array, nullptr);
  EXPECT_EQ(array->kind, JsonKind::kArray);
  EXPECT_EQ(array->text, "[1,2]");
}

TEST(KernelPipeJson, RejectsNonIntegerNumbers) {
  JsonObject object;
  ASSERT_TRUE(JsonObject::Parse("{\"a\":1.5,\"b\":1e3}", &object));
  std::int64_t number = 0;
  EXPECT_FALSE(object.GetInt64("a", &number));
  EXPECT_FALSE(object.GetInt64("b", &number));
}

}  // namespace
}  // namespace lengsuo::fp
