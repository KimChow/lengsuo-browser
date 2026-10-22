"""Guards the frame layer across the two repos.

Why this exists
---------------
The wire contract pins the whole frame layer: the header layout, the
payload ceiling, the protocol version, the seven frame type codes, and
the fixed legal/illegal frame vectors. Its own words for the vectors
are "the two sets of constants must be byte for byte identical -- change
the protocol and change the vectors on both sides".

Task P built the client's codec and task Q built the kernel's. Each side
hardcoded its own copy of every one of those constants -- the kernel in
`native/kernel_pipe_frame.h` and `native/kernel_pipe_test_util.h`, the
client in `crates/.../kernel_pipe/mod.rs` and `.../handshake.rs` -- and
each side then asserted its own output against its own copy. That proves
nothing about the wire.

This is the same trap task R fell into with the ConfigBundle payload and
the machine code: two copies of one constant, each checked only against
its own output, with nothing comparing the copies. Change a frame type
on one side and both sides stay green while the two sides no longer
agree on the wire.

So the contract is the third side of the comparison, exactly as it is
for the envelope vectors: a value that moves in one repo is caught even
when the other two still agree with each other. A side that cannot be
read is reported, never skipped -- an absent check looks exactly like a
passing one.

The tests below build a faithful pair of miniature repos and then move
one constant at a time, because a check that has only ever seen the
agreed state cannot show that it would notice a disagreement.
"""
import importlib.util
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def _load_tool():
    path = ROOT / "tools" / "cross_repo_frames.py"
    spec = importlib.util.spec_from_file_location("cross_repo_frames", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


tool = _load_tool()

# The contract's own text, reduced to the parts the frame layer reads.
# The bytes are the contract's fixed vectors; they are written once here
# and every fixture derives from them, so a transcription slip would
# have to be made twice to go unnoticed.
CONTRACT = """\
# 线协议契约

## 1. 握手

| 字段 | 类型 | 语义 |
|---|---|---|
| `version` | u32 | 协议版本，当前 `1`；不匹配 ⇒ 双方都拒绝并断链 |

## 2. 帧格式

```
偏移  长度  字段
0     4     len      u32 大端，**payload 字节数**（不含本头 8 字节）
4     1     type     u8
5     3     reserved u8×3，必须为 0
8     len   payload
```

| 约束 | 值 | 违反时 |
|---|---|---|
| `len` 上限 | 8 MiB | 解码失败（不等数据、不截断） |

#### ⭐ 固定测试向量

**合法帧**（`len` = payload 字节数，**大端**）：

| 向量 | len | 帧头 8 字节（十六进制） | payload | 整帧长度 |
|---|---|---|---|---|
| `V0` | 0 | `00 00 00 00 03 00 00 00` | 空 | 8 |
| `V1` | 1 | `00 00 00 01 03 00 00 00` | `5A` | 9 |
| `V3` | 3 | `00 00 00 03 03 00 00 00` | `5A` × 3 | 11 |
| `V255` | 255 | `00 00 00 ff 03 00 00 00` | `5A` × 255 | 263 |
| `V65535` | 65535 | `00 00 ff ff 03 00 00 00` | `5A` × 65535 | 65543 |
| `VMAX` | 8388608 | `00 80 00 00 03 00 00 00` | `5A` × 8388608 | 8388616 |

**非法帧**（解码必须失败，且**不得**返回部分明文）：

| 向量 | 触发条件 | 字节（十六进制） |
|---|---|---|
| `IL_LEN_OVER` | len 超上限 | `00 80 00 01 03 00 00 00` |
| `IL_RESERVED` | reserved 非 0 | `00 00 00 01 03 00 00 01 5A` |
| `IL_TYPE` | type 未知 | `00 00 00 01 07 00 00 00 5A` |
| `IL_TRUNC_HDR` | 头被截断（7 字节） | `00 00 00 01 03 00 00` |
| `IL_TRUNC_PAYLOAD` | payload 不足 | `00 00 00 03 03 00 00 00 5A`（声明 3，实给 1） |

## 3. 帧类型

| 值 | 名称 | 方向 | 载荷 |
|---|---|---|---|
| `0x01` | `Hello` | 内核 → runner | JSON `{version, nonce}` |
| `0x02` | `KeyEnvelope` | runner → 内核 | 二进制 |
| `0x03` | `ConfigBundle` | runner → 内核 | JSON |
| `0x04` | `ActionRequest` | runner → 内核 | JSON |
| `0x05` | `ActionResult` | 内核 → runner | JSON |
| `0x81` | `Event` | 内核 → runner | JSON |
| `0x82` | `EventAck` | runner → 内核 | JSON |
"""

# The kernel's frame header, as the repo declares it.
KERNEL_HEADER = """\
enum class FrameType : std::uint8_t {
  kHello = 0x01,
  kKeyEnvelope = 0x02,
  kConfigBundle = 0x03,
  kActionRequest = 0x04,
  kActionResult = 0x05,
  kEvent = 0x81,
  kEventAck = 0x82,
};

inline constexpr std::size_t kKernelPipeHeaderBytes = 8;

inline constexpr std::size_t kKernelPipeMaxPayloadBytes = 8 * 1024 * 1024;

inline constexpr std::uint32_t kKernelPipeProtocolVersion = 1;
"""

# The kernel's "is this a frame type we know" switch.
KERNEL_SOURCE = """\
bool IsKnownFrameType(std::uint8_t raw) {
  switch (raw) {
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x81:
    case 0x82:
      return true;
    default:
      return false;
  }
}
"""

# The kernel's copy of the contract's frame vectors.
KERNEL_VECTORS = """\
inline constexpr std::uint8_t kFill = 0x5A;

inline constexpr char kLegalHeaderV0[] = "00 00 00 00 03 00 00 00";
inline constexpr char kLegalHeaderV1[] = "00 00 00 01 03 00 00 00";
inline constexpr char kLegalHeaderV3[] = "00 00 00 03 03 00 00 00";
inline constexpr char kLegalHeaderV255[] = "00 00 00 ff 03 00 00 00";
inline constexpr char kLegalHeaderV65535[] = "00 00 ff ff 03 00 00 00";
inline constexpr char kLegalHeaderVMax[] = "00 80 00 00 03 00 00 00";

inline constexpr char kIllegalLenOver[] = "00 80 00 01 03 00 00 00";
inline constexpr char kIllegalReserved[] = "00 00 00 01 03 00 00 01 5a";
inline constexpr char kIllegalType[] = "00 00 00 01 07 00 00 00 5a";
inline constexpr char kIllegalTruncHeader[] = "00 00 00 01 03 00 00";
inline constexpr char kIllegalTruncPayload[] = "00 00 00 03 03 00 00 00 5a";
"""

# The client's frame layer, as the repo declares it.
CLIENT_MOD = """\
pub const HEADER_LEN: usize = 8;

pub const MAX_PAYLOAD_LEN: u32 = 8 * 1024 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[repr(u8)]
pub enum FrameType {
    /// `0x01` 内核 → runner。
    Hello = 0x01,
    /// `0x02` runner → 内核。
    KeyEnvelope = 0x02,
    /// `0x03` runner → 内核。
    ConfigBundle = 0x03,
    /// `0x04` runner → 内核。
    ActionRequest = 0x04,
    /// `0x05` 内核 → runner。
    ActionResult = 0x05,
    /// `0x81` 内核 → runner。
    Event = 0x81,
    /// `0x82` runner → 内核。
    EventAck = 0x82,
}

impl FrameType {
    pub const fn from_code(code: u8) -> Option<Self> {
        match code {
            0x01 => Some(Self::Hello),
            0x02 => Some(Self::KeyEnvelope),
            0x03 => Some(Self::ConfigBundle),
            0x04 => Some(Self::ActionRequest),
            0x05 => Some(Self::ActionResult),
            0x81 => Some(Self::Event),
            0x82 => Some(Self::EventAck),
            _ => None,
        }
    }
}

#[cfg(test)]
mod frame_tests {
    const LEGAL_HEADERS: [(u32, [u8; 8]); 6] = [
        (0, [0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00]),
        (1, [0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00]),
        (3, [0x00, 0x00, 0x00, 0x03, 0x03, 0x00, 0x00, 0x00]),
        (255, [0x00, 0x00, 0x00, 0xff, 0x03, 0x00, 0x00, 0x00]),
        (65535, [0x00, 0x00, 0xff, 0xff, 0x03, 0x00, 0x00, 0x00]),
        (8_388_608, [0x00, 0x80, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00]),
    ];

    const IL_LEN_OVER: [u8; 8] = [0x00, 0x80, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00];
    const IL_RESERVED: [u8; 9] = [0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x01, 0x5A];
    const IL_TYPE: [u8; 9] = [0x00, 0x00, 0x00, 0x01, 0x07, 0x00, 0x00, 0x00, 0x5A];
    const IL_TRUNC_HDR: [u8; 7] = [0x00, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00];
    const IL_TRUNC_PAYLOAD: [u8; 9] = [0x00, 0x00, 0x00, 0x03, 0x03, 0x00, 0x00, 0x00, 0x5A];
}
"""

CLIENT_HANDSHAKE = """\
pub const PROTOCOL_VERSION: u32 = 1;
"""


def _write(root, relative, text):
    target = root / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    return target


class _Pair:
    """A miniature kernel repo and client repo that do agree."""

    def __init__(self, contract=CONTRACT, kernel_header=KERNEL_HEADER,
                 kernel_source=KERNEL_SOURCE, kernel_vectors=KERNEL_VECTORS,
                 client_mod=CLIENT_MOD, client_handshake=CLIENT_HANDSHAKE):
        holder = tempfile.TemporaryDirectory()
        self.addCleanup = holder.cleanup
        self.kernel = pathlib.Path(holder.name) / "kernel"
        self.client = pathlib.Path(holder.name) / "client"
        _write(self.kernel, "docs/kernel-wire-contract.md", contract)
        _write(self.kernel, "native/kernel_pipe_frame.h", kernel_header)
        _write(self.kernel, "native/kernel_pipe_frame.cc", kernel_source)
        _write(self.kernel, "native/kernel_pipe_test_util.h", kernel_vectors)
        _write(self.client, tool.CLIENT_FRAME_REL, client_mod)
        _write(self.client, tool.CLIENT_HANDSHAKE_REL, client_handshake)


class FrameLayerAgreementTest(unittest.TestCase):
    """The two repos' frame layer must agree, and must notice when it does not."""

    def setUp(self):
        self.pair = _Pair()
        self.addCleanup(self.pair.addCleanup)

    def _results(self):
        return tool.compare_frames(self.pair.kernel, self.pair.client)

    def _failed(self):
        return [r for r in self._results() if not r.ok]

    def test_a_faithful_pair_agrees_on_everything(self):
        """The baseline: the two sides must agree, or nothing else means anything."""
        failed = self._failed()
        self.assertEqual(failed, [], "these checks failed: %s" %
                         [(r.label, r.values) for r in failed])

    def test_a_moved_frame_type_on_one_side_is_caught(self):
        """The trap: the client renames one wire value and stays green itself."""
        moved = CLIENT_MOD.replace("Event = 0x81,", "Event = 0x83,")
        self.assertNotEqual(moved, CLIENT_MOD, "the fixture must change")
        self.pair = _Pair(client_mod=moved)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(), "a moved frame type must be caught")

    def test_a_frame_type_known_to_one_side_only_is_caught(self):
        """The kernel's decoder must accept exactly the contract's types."""
        shrunk = KERNEL_SOURCE.replace("    case 0x81:\n", "")
        self.assertNotEqual(shrunk, KERNEL_SOURCE, "the fixture must change")
        self.pair = _Pair(kernel_source=shrunk)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(), "a dropped known type must be caught")

    def test_a_moved_legal_frame_vector_is_caught(self):
        """The legal frame headers are the contract's fixed vectors."""
        moved = KERNEL_VECTORS.replace(
            'kLegalHeaderV1[] = "00 00 00 01 03 00 00 00"',
            'kLegalHeaderV1[] = "01 00 00 00 03 00 00 00"')
        self.assertNotEqual(moved, KERNEL_VECTORS, "the fixture must change")
        self.pair = _Pair(kernel_vectors=moved)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(), "a moved legal vector must be caught")

    def test_a_moved_illegal_frame_vector_is_caught(self):
        """The illegal frames are fixed vectors too."""
        moved = CLIENT_MOD.replace(
            "const IL_TYPE: [u8; 9] = "
            "[0x00, 0x00, 0x00, 0x01, 0x07, 0x00, 0x00, 0x00, 0x5A];",
            "const IL_TYPE: [u8; 9] = "
            "[0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x5A];")
        self.assertNotEqual(moved, CLIENT_MOD, "the fixture must change")
        self.pair = _Pair(client_mod=moved)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(), "a moved illegal vector must be caught")

    def test_a_moved_header_length_is_caught(self):
        """The header is eight bytes on both sides."""
        moved = CLIENT_MOD.replace(
            "pub const HEADER_LEN: usize = 8;",
            "pub const HEADER_LEN: usize = 12;")
        self.assertNotEqual(moved, CLIENT_MOD, "the fixture must change")
        self.pair = _Pair(client_mod=moved)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(), "a moved header length must be caught")

    def test_a_moved_payload_ceiling_is_caught(self):
        """The ceiling is 8 MiB on both sides."""
        moved = CLIENT_MOD.replace(
            "pub const MAX_PAYLOAD_LEN: u32 = 8 * 1024 * 1024;",
            "pub const MAX_PAYLOAD_LEN: u32 = 4 * 1024 * 1024;")
        self.assertNotEqual(moved, CLIENT_MOD, "the fixture must change")
        self.pair = _Pair(client_mod=moved)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(), "a moved ceiling must be caught")

    def test_a_moved_protocol_version_is_caught(self):
        """The Hello frame carries the protocol version."""
        moved = CLIENT_HANDSHAKE.replace(
            "pub const PROTOCOL_VERSION: u32 = 1;",
            "pub const PROTOCOL_VERSION: u32 = 2;")
        self.assertNotEqual(moved, CLIENT_HANDSHAKE, "the fixture must change")
        self.pair = _Pair(client_handshake=moved)
        self.addCleanup(self.pair.addCleanup)
        self.assertTrue(self._failed(), "a moved version must be caught")

    def test_a_missing_side_is_reported_not_skipped(self):
        """A side that cannot be read must raise, not silently pass."""
        (self.pair.client / tool.CLIENT_FRAME_REL).unlink()
        with self.assertRaises(tool.FrameVectorError):
            tool.compare_frames(self.pair.kernel, self.pair.client)

    def test_a_contract_that_lost_a_table_is_reported(self):
        """The contract is the third side; a table that vanished must raise."""
        shrunk = CONTRACT.replace("## 3. 帧类型", "## 3. 别的")
        self.pair = _Pair(contract=shrunk)
        self.addCleanup(self.pair.addCleanup)
        with self.assertRaises(tool.FrameVectorError):
            tool.compare_frames(self.pair.kernel, self.pair.client)


class RealRepoFrameLayerTest(unittest.TestCase):
    """The real repos must agree, and the real kernel repo must be readable."""

    def setUp(self):
        self.kernel = ROOT
        self.client = pathlib.Path(
            __import__("os").environ.get(
                "LENGSUO_DESKTOP_REPO",
                "/home/ubuntu/lengsuo-desktop-dev/repo"))

    def test_the_real_kernel_repo_ships_the_frame_constants(self):
        """This repository must satisfy the readers the tool uses."""
        header = (self.kernel / "native/kernel_pipe_frame.h").read_text(
            encoding="utf-8")
        self.assertEqual(tool.kernel_frame_types(header),
                         tool.CONTRACT_FRAME_TYPES)
        constants = tool.kernel_frame_constants(header)
        self.assertEqual(constants["header_bytes"], 8)
        self.assertEqual(constants["max_payload_bytes"], 8 * 1024 * 1024)
        self.assertEqual(constants["protocol_version"], 1)

    def test_the_real_kernel_repo_ships_the_frame_vectors(self):
        """The kernel's copy of the contract's vectors must be findable."""
        vectors = (self.kernel / "native/kernel_pipe_test_util.h").read_text(
            encoding="utf-8")
        legal = tool.kernel_legal_frames(vectors)
        illegal = tool.kernel_illegal_frames(vectors)
        self.assertEqual(len(legal), 6)
        self.assertEqual(len(illegal), 5)

    def test_the_real_client_repo_agrees_with_the_real_kernel_repo(self):
        """The whole point: the two real repos must agree on the frame layer."""
        if not (self.client / tool.CLIENT_FRAME_REL).is_file():
            self.skipTest("the client repo is not available on this host")
        results = tool.compare_frames(self.kernel, self.client)
        failed = [r for r in results if not r.ok]
        self.assertEqual(failed, [], "the two repos' frame layer differs: %s" %
                         [(r.label, r.values) for r in failed])


if __name__ == "__main__":
    unittest.main()
