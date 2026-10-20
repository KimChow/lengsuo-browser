// cache_record tests — record parsing, raw-data preservation, sign format and
// anti-rollback. Acceptance list: docs/cc-task-brief-l3.md, task G.

#include "cache_record.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace lengsuo::fp {
namespace {

// The decrypted 对标端 sample from the task brief. Note the real sign carries a
// vendor prefix before the 32-hex digest; ParseRecord treats sign as an opaque
// string, and the strict sign parser is exercised separately below.
constexpr char kReferenceRecord[] = R"json({
  "name": "COOKIE_v1",
  "sign": "wx-mini-r-zlink_55dab1843cef30db2b837ed14cde8485_v1_t1790855894",
  "data": { "containerId": "27895481424665", "cookies": [ {"name": "_bee", "value": "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5"} ] }
})json";

constexpr char kReferenceData[] =
    R"json({ "containerId": "27895481424665", "cookies": [ {"name": "_bee", "value": "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5"} ] })json";

// ---- 1. Three fields parse; data_json stays byte-identical. ----------------

TEST(CacheRecord, ParsesThreeSectionsAndPreservesDataBytes) {
  CacheRecord rec;
  ASSERT_TRUE(ParseRecord(kReferenceRecord, &rec));
  EXPECT_EQ(rec.name, "COOKIE_v1");
  EXPECT_EQ(rec.sign,
            "wx-mini-r-zlink_55dab1843cef30db2b837ed14cde8485_v1_t1790855894");
  EXPECT_EQ(rec.data_json, kReferenceData);
}

TEST(CacheRecord, DataMayBeAnyJsonValueAndKeepsItsRawText) {
  CacheRecord rec;
  ASSERT_TRUE(ParseRecord(R"({"name":"n","sign":"s","data":"x"})", &rec));
  EXPECT_EQ(rec.data_json, "\"x\"");

  ASSERT_TRUE(ParseRecord(R"({"name":"n","sign":"s","data":42})", &rec));
  EXPECT_EQ(rec.data_json, "42");

  ASSERT_TRUE(ParseRecord(R"({"name":"n","sign":"s","data":[1,{"a":2}]})",
                          &rec));
  EXPECT_EQ(rec.data_json, "[1,{\"a\":2}]");
}

// ---- 2. Each missing section fails, output untouched. ----------------------

TEST(CacheRecord, MissingSectionsFail) {
  CacheRecord rec;
  EXPECT_FALSE(ParseRecord(R"({"sign":"s","data":{}})", &rec));
  EXPECT_FALSE(ParseRecord(R"({"name":"n","data":{}})", &rec));
  EXPECT_FALSE(ParseRecord(R"({"name":"n","sign":"s"})", &rec));
  EXPECT_FALSE(ParseRecord(R"({})", &rec));
}

TEST(CacheRecord, FailedParseLeavesOutputUntouched) {
  CacheRecord rec;
  rec.name = "keep";
  rec.sign = "keep";
  rec.data_json = "keep";
  EXPECT_FALSE(ParseRecord(R"({"name":"n","sign":"s"})", &rec));
  EXPECT_EQ(rec.name, "keep");
  EXPECT_EQ(rec.sign, "keep");
  EXPECT_EQ(rec.data_json, "keep");
}

TEST(CacheRecord, RejectsMalformedShapes) {
  CacheRecord rec;
  EXPECT_FALSE(ParseRecord("", &rec));
  EXPECT_FALSE(ParseRecord("[]", &rec));
  EXPECT_FALSE(ParseRecord(R"({"name":"n","sign":"s","data":})", &rec));
  EXPECT_FALSE(ParseRecord(R"({"name":1,"sign":"s","data":{}})", &rec));
  EXPECT_FALSE(ParseRecord(R"({"name":"n","sign":2,"data":{}})", &rec));
  EXPECT_FALSE(ParseRecord(R"({"name":"n","sign":"s","data":{} trailing)",
                            &rec));
  EXPECT_FALSE(ParseRecord(R"({"name":"n","sign":"s","data":{"a":1})", &rec));
  EXPECT_FALSE(ParseRecord(R"({"name":"n","sign":"s","data":{}})", nullptr));
}

TEST(CacheRecord, DecodesEscapedNameAndSign) {
  CacheRecord rec;
  ASSERT_TRUE(ParseRecord(
      R"({"name":"a\"b\\c\n","sign":"s","data":{}})", &rec));
  EXPECT_EQ(rec.name, "a\"b\\c\n");
}

// ---- 3/4. MD5 and sign generation. -----------------------------------------

// RFC 1321 test vectors, observed through MakeSign's digest segment.
TEST(CacheRecord, MakeSignUsesRfc1321Md5) {
  EXPECT_EQ(MakeSign("", 123), "d41d8cd98f00b204e9800998ecf8427e_v1_t123");
  EXPECT_EQ(MakeSign("abc", 0), "900150983cd24fb0d6963f7d28e17f72_v1_t0");
  EXPECT_EQ(MakeSign("message digest", 7),
            "f96b697d7cb7938d525a2f31aaf161d0_v1_t7");
  EXPECT_EQ(MakeSign("abcdefghijklmnopqrstuvwxyz", 1),
            "c3fcd3d76192e4007dfb496cca67e13b_v1_t1");
  // > 64 bytes, exercises the two-block path.
  EXPECT_EQ(MakeSign("1234567890123456789012345678901234567890"
                     "1234567890123456789012345678901234567890",
                     2),
            "57edf4a22be3c955ac49da2e2107b67a_v1_t2");
}

TEST(CacheRecord, MakeSignVerifySignRoundTrip) {
  const std::string sign = MakeSign("container-27895481424665", 1789548390);
  EXPECT_EQ(VerifySign(sign, "container-27895481424665", 1789548390, 0),
            SignVerdict::kOk);
  EXPECT_EQ(VerifySign(sign, "container-27895481424665", 1789548390, 60),
            SignVerdict::kOk);
}

TEST(CacheRecord, TamperedDigestIsBadDigest) {
  std::string sign = MakeSign("container", 1000);
  sign[0] = sign[0] == '0' ? '1' : '0';
  EXPECT_EQ(VerifySign(sign, "container", 1000, 0),
            SignVerdict::kBadDigest);
}

TEST(CacheRecord, WrongSignInputIsBadDigest) {
  const std::string sign = MakeSign("container-a", 1000);
  EXPECT_EQ(VerifySign(sign, "container-b", 1000, 0),
            SignVerdict::kBadDigest);
}

// ---- 5/6. Freshness window. ------------------------------------------------

TEST(CacheRecord, FutureTimestampIsTooNew) {
  const std::string sign = MakeSign("container", 1001);
  EXPECT_EQ(VerifySign(sign, "container", 1000, 3600),
            SignVerdict::kTooNew);
}

TEST(CacheRecord, ExpiredTimestampIsTooOld) {
  const std::string sign = MakeSign("container", 899);
  EXPECT_EQ(VerifySign(sign, "container", 1000, 100),
            SignVerdict::kTooOld);
  // Boundary: age == max_age is still fresh.
  EXPECT_EQ(VerifySign(MakeSign("container", 900), "container", 1000, 101),
            SignVerdict::kOk);
}

// ---- 7. Malformed signs. ---------------------------------------------------

TEST(CacheRecord, MalformedSigns) {
  EXPECT_EQ(VerifySign("", "x", 1000, 0), SignVerdict::kMalformed);
  EXPECT_EQ(VerifySign("abc_v1_t1", "x", 1000, 0),
            SignVerdict::kMalformed);
  // 32 chars but no "_v1_t" separator.
  EXPECT_EQ(VerifySign("00000000000000000000000000000000", "x", 1000, 0),
            SignVerdict::kMalformed);
  // Non-hex character in the digest segment.
  EXPECT_EQ(VerifySign("g00150983cd24fb0d6963f7d28e17f72_v1_t1", "x", 1000, 0),
            SignVerdict::kMalformed);
  // Empty and non-decimal timestamps.
  EXPECT_EQ(VerifySign("900150983cd24fb0d6963f7d28e17f72_v1_t", "x", 1000, 0),
            SignVerdict::kMalformed);
  EXPECT_EQ(VerifySign("900150983cd24fb0d6963f7d28e17f72_v1_t-1", "x", 1000, 0),
            SignVerdict::kMalformed);
  EXPECT_EQ(VerifySign("900150983cd24fb0d6963f7d28e17f72_v1_t1x", "x", 1000, 0),
            SignVerdict::kMalformed);
  // Timestamp beyond int64.
  EXPECT_EQ(VerifySign("900150983cd24fb0d6963f7d28e17f72_v1_t"
                       "99999999999999999999",
                       "x", 1000, 0),
            SignVerdict::kMalformed);
}

TEST(CacheRecord, DigestComparisonIsCaseInsensitive) {
  const std::string sign = MakeSign("container", 5);
  std::string upper = sign;
  for (std::size_t i = 0; i < 32; ++i) {
    if (upper[i] >= 'a' && upper[i] <= 'f') {
      upper[i] = static_cast<char>(upper[i] - 'a' + 'A');
    }
  }
  EXPECT_EQ(VerifySign(upper, "container", 5, 0), SignVerdict::kOk);
}

// ---- 8. Serialize -> Parse idempotence. -----------------------------------

TEST(CacheRecord, SerializeParseIsIdempotent) {
  CacheRecord first;
  ASSERT_TRUE(ParseRecord(kReferenceRecord, &first));
  const std::string s1 = SerializeRecord(first);

  CacheRecord second;
  ASSERT_TRUE(ParseRecord(s1, &second));
  EXPECT_EQ(second.name, first.name);
  EXPECT_EQ(second.sign, first.sign);
  EXPECT_EQ(second.data_json, first.data_json);

  const std::string s2 = SerializeRecord(second);
  EXPECT_EQ(s1, s2);
  EXPECT_EQ(s1,
            "{\"name\":\"COOKIE_v1\",\"sign\":\"wx-mini-r-zlink_"
            "55dab1843cef30db2b837ed14cde8485_v1_t1790855894\",\"data\":" +
                std::string(kReferenceData) + "}");
}

TEST(CacheRecord, EscapedStringsSurviveRoundTrip) {
  CacheRecord rec;
  rec.name = "a\"b\\c\n\t\x01";
  rec.sign = "s";
  rec.data_json = R"({"k":"v"})";
  CacheRecord parsed;
  ASSERT_TRUE(ParseRecord(SerializeRecord(rec), &parsed));
  EXPECT_EQ(parsed.name, rec.name);
  EXPECT_EQ(parsed.sign, rec.sign);
  EXPECT_EQ(parsed.data_json, rec.data_json);
}

// ---- 9. Real 对标端 sign sample. --------------------------------------------

TEST(CacheRecord, ParsesRealReferenceSignFields) {
  constexpr char kSign[] =
      "4a119fbe3fa151227c0c266307318305_v1_t1789548390";
  std::string digest;
  int64_t ts = 0;
  ASSERT_TRUE(ParseSignFields(kSign, &digest, &ts));
  EXPECT_EQ(digest, "4a119fbe3fa151227c0c266307318305");
  EXPECT_EQ(ts, 1789548390);

  // Format parses, so verification reaches the digest comparison (the 对标端
  // key is unknown, hence kBadDigest rather than kMalformed).
  EXPECT_EQ(VerifySign(kSign, "unknown-key", 1789548390, 0),
            SignVerdict::kBadDigest);
}

}  // namespace
}  // namespace lengsuo::fp
