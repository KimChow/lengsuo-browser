// ExtensionPin tests — PinedFile wire format, fail-closed parsing, idempotence.
//
// The three sample IDs are 对标端's real PinedFile entries from the L3 brief:
//   ["fjchbfkopknlodpfoaeogfbclcpppiog",
//    "fgbbdcdpliimpdmgcnahkcdfhdjegmin",
//    "hncdafoadpldclaaigooamfbebmaakjj"]
// They are written here in the brief's order; Serialize() emits sorted order.

#include "extension_pin.h"

#include <string>

#include "gtest/gtest.h"

namespace lengsuo::fp {
namespace {

constexpr char kRealId0[] = "fjchbfkopknlodpfoaeogfbclcpppiog";
constexpr char kRealId1[] = "fgbbdcdpliimpdmgcnahkcdfhdjegmin";
constexpr char kRealId2[] = "hncdafoadpldclaaigooamfbebmaakjj";

// The brief's sample set, in its order, and sorted as Serialize() emits it.
constexpr char kRealSample[] =
    "[\"fjchbfkopknlodpfoaeogfbclcpppiog\","
    "\"fgbbdcdpliimpdmgcnahkcdfhdjegmin\","
    "\"hncdafoadpldclaaigooamfbebmaakjj\"]";
constexpr char kRealSampleSorted[] =
    "[\"fgbbdcdpliimpdmgcnahkcdfhdjegmin\","
    "\"fjchbfkopknlodpfoaeogfbclcpppiog\","
    "\"hncdafoadpldclaaigooamfbebmaakjj\"]";

TEST(ExtensionPinIsValidId, AcceptsThreeRealReferenceIds) {
  EXPECT_TRUE(ExtensionPin::IsValidId(kRealId0));
  EXPECT_TRUE(ExtensionPin::IsValidId(kRealId1));
  EXPECT_TRUE(ExtensionPin::IsValidId(kRealId2));
}

TEST(ExtensionPinIsValidId, AcceptsAlphabetBoundaries) {
  EXPECT_TRUE(ExtensionPin::IsValidId(std::string(32, 'a')));
  EXPECT_TRUE(ExtensionPin::IsValidId(std::string(32, 'p')));
}

TEST(ExtensionPinIsValidId, RejectsOutsideChromiumAlphabet) {
  // 'q' is the first character past Chromium's [a-p] alphabet.
  EXPECT_FALSE(ExtensionPin::IsValidId("q" + std::string(31, 'a')));
  EXPECT_FALSE(ExtensionPin::IsValidId("A" + std::string(31, 'a')));
  EXPECT_FALSE(ExtensionPin::IsValidId(std::string(31, 'a') + "0"));
}

TEST(ExtensionPinIsValidId, RejectsWrongLength) {
  EXPECT_FALSE(ExtensionPin::IsValidId(std::string(31, 'a')));
  EXPECT_FALSE(ExtensionPin::IsValidId(std::string(33, 'a')));
  EXPECT_FALSE(ExtensionPin::IsValidId(""));
}

TEST(ExtensionPinParse, EmptyArraySucceedsAndSerializesEmpty) {
  ExtensionPin pin;
  ASSERT_TRUE(pin.Parse("[]"));
  EXPECT_EQ(pin.size(), 0u);
  EXPECT_EQ(pin.Serialize(), "[]");
}

TEST(ExtensionPinParse, WhitespaceAroundEmptyArraySucceeds) {
  ExtensionPin pin;
  ASSERT_TRUE(pin.Parse(" \t[\r\n ]\n"));
  EXPECT_EQ(pin.size(), 0u);
}

TEST(ExtensionPinParse, RealReferenceSampleParses) {
  ExtensionPin pin;
  ASSERT_TRUE(pin.Parse(kRealSample));
  EXPECT_EQ(pin.size(), 3u);
  EXPECT_TRUE(pin.Contains(kRealId0));
  EXPECT_TRUE(pin.Contains(kRealId1));
  EXPECT_TRUE(pin.Contains(kRealId2));
}

TEST(ExtensionPinParse, WhitespaceBetweenElementsParses) {
  ExtensionPin pin;
  ASSERT_TRUE(pin.Parse("[\n  \"" + std::string(kRealId0) + "\" ,\t\"" +
                        std::string(kRealId2) + "\" \r\n]"));
  EXPECT_EQ(pin.size(), 2u);
  EXPECT_TRUE(pin.Contains(kRealId0));
  EXPECT_TRUE(pin.Contains(kRealId2));
}

TEST(ExtensionPinParse, EscapedCharactersDecodeBeforeValidation) {
  // \u0066 == 'f', \u006f == 'o', \u0067 == 'g'.
  ExtensionPin pin;
  ASSERT_TRUE(pin.Parse("[\"\\u0066jchbfkopknlodpfoaeogfbclcpppi\\u006f\\u0067\"]"));
  EXPECT_EQ(pin.size(), 1u);
  EXPECT_TRUE(pin.Contains(kRealId0));
}

TEST(ExtensionPinParse, DuplicateIdsCollapseToOneEntry) {
  // PinedFile has no duplicate semantics; the pin is a set. Documented as an
  // open question in report.md — the wire does not say which duplicate wins.
  ExtensionPin pin;
  ASSERT_TRUE(pin.Parse("[\"" + std::string(kRealId0) + "\",\"" +
                        std::string(kRealId0) + "\"]"));
  EXPECT_EQ(pin.size(), 1u);
}

TEST(ExtensionPinParse, InvalidIdFailsClosedWithoutChangingState) {
  ExtensionPin pin;
  ASSERT_TRUE(pin.Parse("[\"" + std::string(kRealId0) + "\"]"));

  // One valid id plus one with 'q': the whole document is rejected, and the
  // previously parsed entry must survive untouched (not half-applied).
  EXPECT_FALSE(pin.Parse("[\"" + std::string(kRealId1) + "\",\"q" +
                         std::string(31, 'a') + "\"]"));
  EXPECT_EQ(pin.size(), 1u);
  EXPECT_TRUE(pin.Contains(kRealId0));
  EXPECT_FALSE(pin.Contains(kRealId1));
}

TEST(ExtensionPinParse, InvalidIdOnFreshPinLeavesItEmpty) {
  ExtensionPin pin;
  EXPECT_FALSE(pin.Parse("[\"" + std::string(31, 'a') + "\"]"));
  EXPECT_EQ(pin.size(), 0u);
  EXPECT_EQ(pin.Serialize(), "[]");
}

TEST(ExtensionPinParse, NonArrayFails) {
  ExtensionPin pin;
  EXPECT_FALSE(pin.Parse(""));
  EXPECT_FALSE(pin.Parse("null"));
  EXPECT_FALSE(pin.Parse("{}"));
  EXPECT_FALSE(pin.Parse("123"));
  EXPECT_FALSE(pin.Parse("\"" + std::string(kRealId0) + "\""));
  EXPECT_FALSE(pin.Parse("["));
  EXPECT_FALSE(pin.Parse("]"));
}

TEST(ExtensionPinParse, NonStringElementFails) {
  ExtensionPin pin;
  EXPECT_FALSE(pin.Parse("[123]"));
  EXPECT_FALSE(pin.Parse("[null]"));
  EXPECT_FALSE(pin.Parse("[true]"));
  EXPECT_FALSE(pin.Parse("[[\"" + std::string(kRealId0) + "\"]]"));
  EXPECT_FALSE(pin.Parse("[{\"id\":\"" + std::string(kRealId0) + "\"}]"));
}

TEST(ExtensionPinParse, MalformedJsonFails) {
  ExtensionPin pin;
  const std::string id = kRealId0;
  EXPECT_FALSE(pin.Parse("[\"" + id + "\","));          // unclosed array
  EXPECT_FALSE(pin.Parse("[\"" + id + "\",]"));          // trailing comma
  EXPECT_FALSE(pin.Parse("[\"" + id + "\" \"" + id + "\"]"));  // missing comma
  EXPECT_FALSE(pin.Parse("[\"" + id + "\"] x"));         // trailing garbage
  EXPECT_FALSE(pin.Parse("[\"" + id));                   // unterminated string
  EXPECT_FALSE(pin.Parse("[\"" + id + "\\x\"]"));        // invalid escape
  EXPECT_FALSE(pin.Parse("[\"" + id + "\\u12\"]"));      // short unicode escape
  EXPECT_FALSE(pin.Parse("[\"" + id.substr(0, 31) + "\n\"]"));  // control char
}

TEST(ExtensionPinAdd, DuplicateReturnsFalseAndSizeUnchanged) {
  ExtensionPin pin;
  ASSERT_TRUE(pin.Add(kRealId0));
  EXPECT_EQ(pin.size(), 1u);
  EXPECT_FALSE(pin.Add(kRealId0));
  EXPECT_EQ(pin.size(), 1u);
}

TEST(ExtensionPinAdd, InvalidIdReturnsFalseAndSizeUnchanged) {
  ExtensionPin pin;
  EXPECT_FALSE(pin.Add("q" + std::string(31, 'a')));
  EXPECT_FALSE(pin.Add(std::string(31, 'a')));
  EXPECT_FALSE(pin.Add(""));
  EXPECT_EQ(pin.size(), 0u);
}

TEST(ExtensionPinAdd, ValidIdAppends) {
  ExtensionPin pin;
  ASSERT_TRUE(pin.Parse("[\"" + std::string(kRealId1) + "\"]"));
  EXPECT_TRUE(pin.Add(kRealId0));
  EXPECT_EQ(pin.size(), 2u);
  EXPECT_TRUE(pin.Contains(kRealId0));
  EXPECT_EQ(pin.Serialize(),
            "[\"fgbbdcdpliimpdmgcnahkcdfhdjegmin\","
            "\"fjchbfkopknlodpfoaeogfbclcpppiog\"]");
}

TEST(ExtensionPinSerialize, SampleRoundTripIsIdempotent) {
  ExtensionPin first;
  ASSERT_TRUE(first.Parse(kRealSample));
  const std::string once = first.Serialize();
  EXPECT_EQ(once, kRealSampleSorted);

  ExtensionPin second;
  ASSERT_TRUE(second.Parse(once));
  EXPECT_EQ(second.Serialize(), once);
}

TEST(ExtensionPinSerialize, EmptyRoundTripIsIdempotent) {
  ExtensionPin pin;
  ASSERT_TRUE(pin.Parse("[]"));
  const std::string once = pin.Serialize();
  ASSERT_TRUE(pin.Parse(once));
  EXPECT_EQ(pin.Serialize(), once);
}

}  // namespace
}  // namespace lengsuo::fp
