#include "resp_monitor_report.h"

#include <string>

namespace lengsuo::fp {
namespace {

// Writes a byte as a `\u00XX` escape.
void AppendEscapedByte(std::string* out, unsigned char byte) {
  static const char kHex[] = "0123456789abcdef";
  out->append("\\u00");
  out->push_back(kHex[byte >> 4]);
  out->push_back(kHex[byte & 0x0f]);
}

// The number of bytes in the UTF-8 sequence that starts with `lead`, or zero
// when `lead` does not start a sequence at all.
//
// The ranges are the ones UTF-8 fixes: the second byte of a three byte
// sequence may not be a surrogate's, and the second byte of a four byte
// sequence may not be above U+10FFFF. The narrow ranges are applied by
// `IsValidUtf8`, not here.
std::size_t Utf8SequenceLength(unsigned char lead) {
  if (lead < 0x80) {
    return 1;
  }
  if (lead >= 0xc2 && lead <= 0xdf) {
    return 2;
  }
  if (lead >= 0xe0 && lead <= 0xef) {
    return 3;
  }
  if (lead >= 0xf0 && lead <= 0xf4) {
    return 4;
  }
  return 0;
}

bool IsContinuation(unsigned char byte) {
  return (byte & 0xc0) == 0x80;
}

// True when the bytes at `pos` are a valid UTF-8 sequence of `length` bytes.
bool IsValidUtf8(std::string_view value, std::size_t pos, std::size_t length) {
  if (pos + length > value.size()) {
    return false;
  }
  if (length == 1) {
    // A single byte below 0x80 is the whole sequence.
    return true;
  }
  const unsigned char lead = static_cast<unsigned char>(value[pos]);
  // The second byte's range is narrower for the sequences that would
  // otherwise accept an overlong form or a surrogate.
  unsigned char second_min = 0x80;
  unsigned char second_max = 0xbf;
  if (length == 3) {
    second_min = (lead == 0xe0) ? 0xa0 : 0x80;
    second_max = (lead == 0xed) ? 0x9f : 0xbf;
  } else if (length == 4) {
    second_min = (lead == 0xf0) ? 0x90 : 0x80;
    second_max = (lead == 0xf4) ? 0x8f : 0xbf;
  }
  const unsigned char second = static_cast<unsigned char>(value[pos + 1]);
  if (second < second_min || second > second_max) {
    return false;
  }
  for (std::size_t i = 2; i < length; ++i) {
    if (!IsContinuation(static_cast<unsigned char>(value[pos + i]))) {
      return false;
    }
  }
  return true;
}

// Appends the report's fields to `out`.
//
// `first` says whether `out` already holds a field: the report's own JSON
// starts empty, and the uplink payload already holds the action's name and
// needs a comma before the first field.
void AppendReportFields(std::string* out,
                        const RespMonitorReport& report,
                        bool first) {
  auto key = [&](const char* name) {
    if (!first) {
      out->push_back(',');
    }
    first = false;
    out->push_back('"');
    out->append(name);
    out->append("\":");
  };

  key("rule_id");
  out->append(EscapeRespMonitorJsonString(report.rule_id));
  key("url");
  out->append(EscapeRespMonitorJsonString(report.url));
  key("status");
  out->append(std::to_string(report.status));
  key("content_type");
  out->append(EscapeRespMonitorJsonString(report.content_type));
  key("body_bytes");
  out->append(std::to_string(report.body_bytes));
  key("truncated");
  out->append(report.truncated ? "true" : "false");
  if (report.include_body) {
    key("body");
    out->append(EscapeRespMonitorJsonString(report.body));
  }
}

}  // namespace

std::string BuildRespMonitorReportJson(const RespMonitorReport& report) {
  std::string out = "{";
  AppendReportFields(&out, report, true);
  out.append("}");
  return out;
}

std::string BuildRespMonitorActionPayload(const RespMonitorReport& report) {
  std::string out = "{\"from\":\"";
  out.append(kRespMonitorFrom);
  out.append("\",\"action\":\"");
  out.append(kActionResponseMonitor);
  out.append("\"");
  AppendReportFields(&out, report, false);
  out.append("}");
  return out;
}

std::string EscapeRespMonitorJsonString(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('"');
  std::size_t i = 0;
  while (i < value.size()) {
    const unsigned char byte = static_cast<unsigned char>(value[i]);
    switch (byte) {
      case '"':
        out.append("\\\"");
        ++i;
        continue;
      case '\\':
        out.append("\\\\");
        ++i;
        continue;
      case '\b':
        out.append("\\b");
        ++i;
        continue;
      case '\f':
        out.append("\\f");
        ++i;
        continue;
      case '\n':
        out.append("\\n");
        ++i;
        continue;
      case '\r':
        out.append("\\r");
        ++i;
        continue;
      case '\t':
        out.append("\\t");
        ++i;
        continue;
      default:
        break;
    }
    if (byte < 0x20) {
      // A control character may not go into a JSON string as it is.
      AppendEscapedByte(&out, byte);
      ++i;
      continue;
    }
    const std::size_t length = Utf8SequenceLength(byte);
    if (length == 0 || !IsValidUtf8(value, i, length)) {
      // Not a byte of a valid sequence. It cannot go into a JSON string as
      // it is, so it is escaped.
      AppendEscapedByte(&out, byte);
      ++i;
      continue;
    }
    out.append(value.substr(i, length));
    i += length;
  }
  out.push_back('"');
  return out;
}

}  // namespace lengsuo::fp
