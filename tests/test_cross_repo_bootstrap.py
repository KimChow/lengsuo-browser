"""Guards the *bootstrap* layer across the two repos.

Why this exists
---------------
`docs/kernel-wire-contract.md` section 1.1 pins the constants the two
sides need **before a single frame byte is on the wire**: the endpoint
prefix, the command-line switch name, the HKDF info string, four byte
counts, the two OS endpoint shapes and the Hello JSON key names.

The kernel declares them in `integration/kernel_pipe_client.{h,cc}` and
`native/kernel_pipe_{keys,envelope}.h`; the client in
`crates/.../kernel_pipe/{launch,keys,handshake}.rs`. Each side wrote its
own copy, and each side then asserted its own output against its own
copy. That proves nothing about the two repos agreeing -- the trap task
R fell into with the ConfigBundle payload, already closed one layer at a
time for the envelope bytes, the machine code, the frame layer and the
profile rules layer.

Why this layer is worth its own guard
-------------------------------------
These values fail *earlier* and in a more misleading way than any other
layer: change the endpoint prefix on one side and the kernel dials a
different endpoint, so the runner's accept() times out. Neither side
reports a protocol error -- both report "cannot connect". Change the
HKDF info string and the envelope fails to authenticate, so the kernel
aborts as fail-closed. In both cases the two repos' own test suites stay
green, because each one still agrees with itself.

The tests below build a faithful pair of miniature repos that agree, and
then move one value at a time. A check that has only ever seen the
agreed state cannot show that it would notice a disagreement.

Three of these tests exist because the alternative is a guard that looks
green without doing the work:
* the kernel-side move tests -- a guard that hardcoded the kernel's
  values would still pass every client-side move;
* the "same value written differently" tests -- the switch name and the
  Windows pipe root are spelled differently in the two languages, so the
  comparison has to be by value;
* the unreadable-side tests -- an absent check looks exactly like a
  passing one, so a missing file must raise rather than drop a row.
"""
import importlib.util
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def _load_tool():
    path = ROOT / "tools" / "cross_repo_bootstrap.py"
    spec = importlib.util.spec_from_file_location("cross_repo_bootstrap", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


tool = _load_tool()

# ---------------------------------------------------------------------------
# The escape-sensitive values, built by arithmetic.
#
# A Windows pipe root and a C literal of that root differ by exactly one
# doubling of every backslash. Writing both out by hand invites a count
# error that the test would then bake in as the expected answer, so both
# are built from a single backslash constant instead.
# ---------------------------------------------------------------------------

BS = "\\"                                  # one backslash
NUL = "\x00"                               # one 0x00 byte

WINDOWS_ROOT = BS * 2 + "." + BS + "pipe" + BS          # \\.\pipe\
WINDOWS_ROOT_BROKEN = BS * 2 + "." + BS + "pip" + BS    # \\.\pip\

# The same root as a C string literal, which doubles every backslash.
C_WINDOWS_ROOT = 'L"' + BS * 4 + "." + BS * 2 + "pipe" + BS * 2 + '"'
C_WINDOWS_ROOT_BROKEN = 'L"' + BS * 4 + "." + BS * 2 + "pip" + BS * 2 + '"'

# The same root as a Rust raw string prefix (no doubling: it is raw).
R_WINDOWS_ROOT = 'r"' + WINDOWS_ROOT

# The contract's own text, reduced to the parts the bootstrap layer reads.
# The values are written once here and every fixture derives from them, so
# a transcription slip would have to be made twice to go unnoticed.
CONTRACT = (
    "# 线协议契约\n"
    "\n"
    "## 1. 启动与握手\n"
    "\n"
    "（前一节的内容，留作分节边界的夹具。）\n"
    "\n"
    "### 1.1 ⭐ 启动层固定常量（两侧逐字节相同）\n"
    "\n"
    "| 常量 | 值 | 内核侧名字 | 客户端侧名字 |\n"
    "|---|---|---|---|\n"
    "| 端点前缀 | `lengsuo-kernel-` | `kKernelPipeEndpointPrefix` | `ENDPOINT_PREFIX` |\n"
    "| 命令行开关名 | `lengsuo-kernel-pipe` | `kKernelPipeSwitch` | `PIPE_ARG_PREFIX` |\n"
    "| HKDF info 串 | `lengsuo-kernel-session-v1` | `kKernelPipeSessionInfo` | `SESSION_KEY_INFO` |\n"
    "| 会话密钥字节 | `32` | `kKernelPipeSessionKeyBytes` | `SESSION_KEY_LEN` |\n"
    "| 机器码密钥字节 | `32` | `kKernelPipeMachineKeyBytes` | `K_MACHINE_LEN` |\n"
    "| Hello nonce 字节 | `8` | `kKernelPipeHelloNonceBytes` | `HELLO_NONCE_LEN` |\n"
    "| GCM nonce 字节 | `12` | `kKernelPipeGcmNonceBytes` | `GCM_NONCE_LEN` |\n"
    "| Windows 管道根 | `" + WINDOWS_ROOT + "` | `" + C_WINDOWS_ROOT +
    "`（代码内） | `windows_endpoint` |\n"
    "| POSIX 抽象标记 | `<NUL>` | `path[0] = '" + BS + "0'`（代码内） | `posix_endpoint` |\n"
    "| Hello JSON 键 | `version,nonce` | （代码内） | （代码内） |\n"
    "\n"
    "## 2. 帧格式\n"
)

KERNEL_CLIENT_H = (
    "namespace lengsuo {\n"
    "\n"
    'inline constexpr char kKernelPipeSwitch[] = "lengsuo-kernel-pipe";\n'
    'inline constexpr char kKernelPipeEndpointPrefix[] = "lengsuo-kernel-";\n'
    "\n"
    "}  // namespace lengsuo\n"
)

KERNEL_CLIENT_CC = (
    "// static\n"
    "base::ScopedFD KernelPipeClient::OpenEndpoint(std::string_view pipe_name) {\n"
    "  const std::string endpoint = std::string(kKernelPipeEndpointPrefix) +\n"
    "                               std::string(pipe_name);\n"
    "#if BUILDFLAG(IS_WIN)\n"
    "  // " + BS * 2 + "." + BS + "pipe" + BS + "lengsuo-kernel-<name>\n"
    "  const std::wstring path = " + C_WINDOWS_ROOT + " +\n"
    "                            base::UTF8ToWide(endpoint);\n"
    "  HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,\n"
    "                                0, nullptr, OPEN_EXISTING, 0, nullptr);\n"
    "  if (handle == INVALID_HANDLE_VALUE) {\n"
    "    return base::ScopedFD();\n"
    "  }\n"
    "  return base::ScopedFD(reinterpret_cast<int>(handle));\n"
    "#else\n"
    "  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);\n"
    "  struct sockaddr_un addr;\n"
    "  memset(&addr, 0, sizeof(addr));\n"
    "  addr.sun_family = AF_UNIX;\n"
    "  base::span<char> path(addr.sun_path);\n"
    "  path[0] = '" + BS + "0';\n"
    "  std::copy(endpoint.begin(), endpoint.end(), path.begin() + 1);\n"
    "  return base::ScopedFD(fd);\n"
    "#endif\n"
    "}\n"
)

KERNEL_KEYS_H = (
    "namespace lengsuo {\n"
    "\n"
    "inline constexpr std::size_t kKernelPipeSessionKeyBytes = 32;\n"
    "inline constexpr std::size_t kKernelPipeMachineKeyBytes = 32;\n"
    "inline constexpr std::size_t kKernelPipeHelloNonceBytes = 8;\n"
    "\n"
    "inline constexpr char kKernelPipeSessionInfo[] =\n"
    '    "lengsuo-kernel-session-v1";\n'
    "\n"
    "}  // namespace lengsuo\n"
)

KERNEL_ENVELOPE_H = (
    "namespace lengsuo {\n"
    "\n"
    "inline constexpr std::size_t kKernelPipeGcmNonceBytes = 12;\n"
    "\n"
    "}  // namespace lengsuo\n"
)

KERNEL_PIPE_CC = (
    "bool KernelPipeHandshake::SendHello(std::string_view hello_nonce,\n"
    "                                    std::string* frame_out) {\n"
    "  if (hello_nonce.size() != kKernelPipeHelloNonceBytes) {\n"
    "    state_ = KernelPipeState::kFailed;\n"
    "    return false;\n"
    "  }\n"
    '  std::string payload = "{"' + BS + '"version' + BS + '":' + '";\n'
    "  payload += std::to_string(kKernelPipeProtocolVersion);\n"
    '  payload += ",' + BS + '"nonce' + BS + '":' + BS + '"";\n'
    "  payload += hex;\n"
    '  payload += "' + BS + '"}";\n'
    "  return true;\n"
    "}\n"
)

CLIENT_LAUNCH = (
    "/// 端点前缀（线协议 §1）。\n"
    'pub const ENDPOINT_PREFIX: &str = "lengsuo-kernel-";\n'
    "\n"
    "/// 内核命令行里的管道参数前缀。\n"
    'pub const PIPE_ARG_PREFIX: &str = "--lengsuo-kernel-pipe=";\n'
    "\n"
    "/// Windows 端点。\n"
    "#[must_use]\n"
    "pub fn windows_endpoint(name: &str) -> String {\n"
    '    format!(' + R_WINDOWS_ROOT + '{ENDPOINT_PREFIX}{name}")\n'
    "}\n"
    "\n"
    "/// POSIX 端点：抽象命名空间。\n"
    "#[must_use]\n"
    "pub fn posix_endpoint(name: &str) -> String {\n"
    '    format!("' + BS + '0{ENDPOINT_PREFIX}{name}")\n'
    "}\n"
)

CLIENT_KEYS = (
    "pub const SESSION_KEY_LEN: usize = 32;\n"
    "\n"
    "pub const GCM_NONCE_LEN: usize = 12;\n"
    "\n"
    "pub const HELLO_NONCE_LEN: usize = 8;\n"
    "\n"
    'pub const SESSION_KEY_INFO: &[u8] = b"lengsuo-kernel-session-v1";\n'
    "\n"
    "pub const K_MACHINE_LEN: usize = 32;\n"
)

CLIENT_HANDSHAKE = (
    "fn parse_hello(payload: &[u8]) -> Option<(u32, [u8; HELLO_NONCE_LEN])> {\n"
    "    let value: serde_json::Value = serde_json::from_slice(payload).ok()?;\n"
    "    let object = value.as_object()?;\n"
    '    let version = object.get("version")?.as_u64()?;\n'
    '    let raw = object.get("nonce")?;\n'
    "    let nonce = parse_nonce(raw)?;\n"
    "    Some((version, nonce))\n"
    "}\n"
)


def _write(root, relative, text):
    target = root / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    return target


class _Pair:
    """A miniature kernel repo and client repo that do agree."""

    def __init__(self, contract=CONTRACT, kernel_client_h=KERNEL_CLIENT_H,
                 kernel_client_cc=KERNEL_CLIENT_CC, kernel_keys_h=KERNEL_KEYS_H,
                 kernel_envelope_h=KERNEL_ENVELOPE_H, kernel_pipe_cc=KERNEL_PIPE_CC,
                 client_launch=CLIENT_LAUNCH, client_keys=CLIENT_KEYS,
                 client_handshake=CLIENT_HANDSHAKE):
        holder = tempfile.TemporaryDirectory()
        self.addCleanup = holder.cleanup
        self.kernel = pathlib.Path(holder.name) / "kernel"
        self.client = pathlib.Path(holder.name) / "client"
        _write(self.kernel, "docs/kernel-wire-contract.md", contract)
        _write(self.kernel, tool.KERNEL_CLIENT_H_REL, kernel_client_h)
        _write(self.kernel, tool.KERNEL_CLIENT_CC_REL, kernel_client_cc)
        _write(self.kernel, tool.KERNEL_KEYS_H_REL, kernel_keys_h)
        _write(self.kernel, tool.KERNEL_ENVELOPE_H_REL, kernel_envelope_h)
        _write(self.kernel, tool.KERNEL_PIPE_CC_REL, kernel_pipe_cc)
        _write(self.client, tool.CLIENT_LAUNCH_REL, client_launch)
        _write(self.client, tool.CLIENT_KEYS_REL, client_keys)
        _write(self.client, tool.CLIENT_HANDSHAKE_REL, client_handshake)


class BootstrapLayerAgreementTest(unittest.TestCase):
    """The two repos' bootstrap layer must agree, and must notice drift."""

    def setUp(self):
        self.pair = _Pair()
        self.addCleanup(self.pair.addCleanup)

    def _failed(self):
        return [r for r in tool.compare_bootstrap(self.pair.kernel, self.pair.client)
                if not r.ok]

    def _replace(self, **changes):
        """Rebuild the pair with one side's file replaced."""
        self.pair = _Pair(**changes)
        self.addCleanup(self.pair.addCleanup)
        return self._failed()

    def test_a_faithful_pair_agrees_on_everything(self):
        """The baseline: the two sides must agree, or nothing else means anything."""
        failed = self._failed()
        self.assertEqual(failed, [], "these checks failed: %s" %
                         [(r.label, r.values) for r in failed])

    def test_every_contract_row_has_a_check(self):
        """A row in the contract with no check is a promise nobody keeps."""
        labels = {r.label for r in
                  tool.compare_bootstrap(self.pair.kernel, self.pair.client)}
        self.assertEqual(len(labels), 10, "the bootstrap layer has ten rows: %s"
                         % sorted(labels))

    # -- the two strings that decide *where* the kernel dials ---------------

    def test_a_moved_endpoint_prefix_on_the_client_side_is_caught(self):
        """The trap: one repo renames the prefix and both suites stay green."""
        moved = CLIENT_LAUNCH.replace(
            'pub const ENDPOINT_PREFIX: &str = "lengsuo-kernel-";',
            'pub const ENDPOINT_PREFIX: &str = "lengsuo-kern-";')
        self.assertNotEqual(moved, CLIENT_LAUNCH, "the fixture must change")
        failed = self._replace(client_launch=moved)
        self.assertTrue(failed, "a moved endpoint prefix must be caught")
        self.assertIn("endpoint prefix", [r.label for r in failed])

    def test_a_moved_endpoint_prefix_on_the_kernel_side_is_caught(self):
        """A guard that hardcoded the kernel's copy would pass every client move."""
        moved = KERNEL_CLIENT_H.replace(
            'inline constexpr char kKernelPipeEndpointPrefix[] = "lengsuo-kernel-";',
            'inline constexpr char kKernelPipeEndpointPrefix[] = "lengsuo-kern-";')
        self.assertNotEqual(moved, KERNEL_CLIENT_H, "the fixture must change")
        failed = self._replace(kernel_client_h=moved)
        self.assertTrue(failed, "a moved kernel endpoint prefix must be caught")
        self.assertIn("endpoint prefix", [r.label for r in failed])

    def test_a_moved_switch_name_on_the_client_side_is_caught(self):
        """The switch name rides in the command line, not in a frame."""
        moved = CLIENT_LAUNCH.replace(
            'pub const PIPE_ARG_PREFIX: &str = "--lengsuo-kernel-pipe=";',
            'pub const PIPE_ARG_PREFIX: &str = "--lengsuo-pipe=";')
        self.assertNotEqual(moved, CLIENT_LAUNCH, "the fixture must change")
        failed = self._replace(client_launch=moved)
        self.assertTrue(failed, "a moved switch name must be caught")
        self.assertIn("pipe switch name", [r.label for r in failed])

    def test_a_moved_switch_name_on_the_kernel_side_is_caught(self):
        """The kernel's copy of the switch name must be read, too."""
        moved = KERNEL_CLIENT_H.replace(
            'inline constexpr char kKernelPipeSwitch[] = "lengsuo-kernel-pipe";',
            'inline constexpr char kKernelPipeSwitch[] = "lengsuo-pipe";')
        self.assertNotEqual(moved, KERNEL_CLIENT_H, "the fixture must change")
        failed = self._replace(kernel_client_h=moved)
        self.assertTrue(failed, "a moved kernel switch name must be caught")
        self.assertIn("pipe switch name", [r.label for r in failed])

    def test_the_switch_name_is_compared_by_value_not_spelling(self):
        """`--name=` and `name` are one switch, so they must not clash.

        The kernel holds the bare name and reads it through
        base::CommandLine; the client holds the `--name=` prefix it
        appends. What has to agree is the switch, not how it is spelled.
        """
        failed = self._failed()
        self.assertEqual(failed, [],
                         "the two spellings denote one switch: %s" %
                         [(r.label, r.values) for r in failed])

    # -- the string that decides whether the envelope authenticates ---------

    def test_a_moved_hkdf_info_on_the_client_side_is_caught(self):
        """The info string never reaches the wire; drift shows up as fail-closed."""
        moved = CLIENT_KEYS.replace(
            'b"lengsuo-kernel-session-v1"', 'b"lengsuo-session-v1"')
        self.assertNotEqual(moved, CLIENT_KEYS, "the fixture must change")
        failed = self._replace(client_keys=moved)
        self.assertTrue(failed, "a moved HKDF info string must be caught")
        self.assertIn("hkdf info", [r.label for r in failed])

    def test_a_moved_hkdf_info_on_the_kernel_side_is_caught(self):
        """The kernel's info string is a wrapped C literal; read it anyway."""
        moved = KERNEL_KEYS_H.replace(
            '"lengsuo-kernel-session-v1"', '"lengsuo-session-v1"')
        self.assertNotEqual(moved, KERNEL_KEYS_H, "the fixture must change")
        failed = self._replace(kernel_keys_h=moved)
        self.assertTrue(failed, "a moved kernel HKDF info string must be caught")
        self.assertIn("hkdf info", [r.label for r in failed])

    def test_the_hkdf_info_is_read_across_a_wrapped_c_literal(self):
        """The kernel's literal is wrapped; a line-based reader would miss it."""
        parsed = tool.compare_bootstrap(self.pair.kernel, self.pair.client)
        hkdf = [r for r in parsed if r.label == "hkdf info"][0]
        self.assertEqual(hkdf.values["kernel"], "lengsuo-kernel-session-v1",
                         "the wrapped C literal must be joined, not truncated")

    # -- the four byte counts ----------------------------------------------

    def test_a_moved_session_key_length_on_the_client_side_is_caught(self):
        """A 16-byte session key is a different handshake, not a shorter one."""
        moved = CLIENT_KEYS.replace(
            "pub const SESSION_KEY_LEN: usize = 32;",
            "pub const SESSION_KEY_LEN: usize = 16;")
        self.assertNotEqual(moved, CLIENT_KEYS, "the fixture must change")
        failed = self._replace(client_keys=moved)
        self.assertTrue(failed, "a moved session key length must be caught")
        self.assertIn("session key bytes", [r.label for r in failed])

    def test_a_moved_session_key_length_on_the_kernel_side_is_caught(self):
        """The kernel's copy of the length must be read, too."""
        moved = KERNEL_KEYS_H.replace(
            "kKernelPipeSessionKeyBytes = 32;", "kKernelPipeSessionKeyBytes = 16;")
        self.assertNotEqual(moved, KERNEL_KEYS_H, "the fixture must change")
        failed = self._replace(kernel_keys_h=moved)
        self.assertTrue(failed, "a moved kernel session key length must be caught")
        self.assertIn("session key bytes", [r.label for r in failed])

    def test_a_moved_hello_nonce_length_on_the_client_side_is_caught(self):
        """The Hello nonce is the AAD's second half; its length is load-bearing."""
        moved = CLIENT_KEYS.replace(
            "pub const HELLO_NONCE_LEN: usize = 8;",
            "pub const HELLO_NONCE_LEN: usize = 16;")
        self.assertNotEqual(moved, CLIENT_KEYS, "the fixture must change")
        failed = self._replace(client_keys=moved)
        self.assertTrue(failed, "a moved hello nonce length must be caught")
        self.assertIn("hello nonce bytes", [r.label for r in failed])

    def test_a_moved_gcm_nonce_length_on_the_kernel_side_is_caught(self):
        """The GCM nonce lives in a different kernel header; still read it."""
        moved = KERNEL_ENVELOPE_H.replace(
            "kKernelPipeGcmNonceBytes = 12;", "kKernelPipeGcmNonceBytes = 8;")
        self.assertNotEqual(moved, KERNEL_ENVELOPE_H, "the fixture must change")
        failed = self._replace(kernel_envelope_h=moved)
        self.assertTrue(failed, "a moved GCM nonce length must be caught")
        self.assertIn("gcm nonce bytes", [r.label for r in failed])

    def test_a_moved_machine_key_length_on_the_client_side_is_caught(self):
        """k_machine is a SHA-256 output; a different length is a different key."""
        moved = CLIENT_KEYS.replace(
            "pub const K_MACHINE_LEN: usize = 32;",
            "pub const K_MACHINE_LEN: usize = 16;")
        self.assertNotEqual(moved, CLIENT_KEYS, "the fixture must change")
        failed = self._replace(client_keys=moved)
        self.assertTrue(failed, "a moved machine key length must be caught")
        self.assertIn("machine key bytes", [r.label for r in failed])

    # -- the two OS endpoint shapes ----------------------------------------

    def test_a_moved_windows_pipe_root_is_caught(self):
        """Same prefix on a different pipe namespace is a different endpoint."""
        moved = KERNEL_CLIENT_CC.replace(C_WINDOWS_ROOT, C_WINDOWS_ROOT_BROKEN)
        self.assertNotEqual(moved, KERNEL_CLIENT_CC, "the fixture must change")
        failed = self._replace(kernel_client_cc=moved)
        self.assertTrue(failed, "a moved windows pipe root must be caught")
        self.assertIn("windows pipe root", [r.label for r in failed])

    def test_a_moved_windows_pipe_root_on_the_client_side_is_caught(self):
        """The client's raw-string root must be read, not assumed."""
        moved = CLIENT_LAUNCH.replace(WINDOWS_ROOT, WINDOWS_ROOT_BROKEN)
        self.assertNotEqual(moved, CLIENT_LAUNCH, "the fixture must change")
        failed = self._replace(client_launch=moved)
        self.assertTrue(failed, "a moved client pipe root must be caught")
        self.assertIn("windows pipe root", [r.label for r in failed])

    def test_the_windows_pipe_root_is_compared_after_unescaping(self):
        """C escapes and Rust raw strings must decode to the same bytes.

        The kernel writes the root as a C literal and the client as a Rust
        raw string. Comparing the literals as written would report a
        mismatch where the bytes on the wire are identical.
        """
        parsed = tool.compare_bootstrap(self.pair.kernel, self.pair.client)
        root = [r for r in parsed if r.label == "windows pipe root"][0]
        self.assertEqual(root.values["contract"], WINDOWS_ROOT)
        self.assertEqual(root.values["kernel"], WINDOWS_ROOT)
        self.assertEqual(root.values["client"], WINDOWS_ROOT)

    def test_a_posix_endpoint_that_left_the_abstract_namespace_is_caught(self):
        """The contract forbids a filesystem path; the marker is the NUL."""
        moved = CLIENT_LAUNCH.replace(
            'format!("' + BS + '0{ENDPOINT_PREFIX}{name}")',
            'format!("/tmp/{ENDPOINT_PREFIX}{name}")')
        self.assertNotEqual(moved, CLIENT_LAUNCH, "the fixture must change")
        failed = self._replace(client_launch=moved)
        self.assertTrue(failed,
                        "a filesystem path instead of the abstract namespace "
                        "must be caught")
        self.assertIn("posix abstract marker", [r.label for r in failed])

    def test_a_kernel_posix_endpoint_that_dropped_the_nul_is_caught(self):
        """The kernel's marker must be read from its own branch, not assumed."""
        moved = KERNEL_CLIENT_CC.replace("path[0] = '" + BS + "0';",
                                         "path[0] = '/';")
        self.assertNotEqual(moved, KERNEL_CLIENT_CC, "the fixture must change")
        failed = self._replace(kernel_client_cc=moved)
        self.assertTrue(failed, "a kernel that dropped the NUL must be caught")
        self.assertIn("posix abstract marker", [r.label for r in failed])

    # -- the Hello JSON keys ------------------------------------------------

    def test_a_renamed_hello_key_on_the_client_side_is_caught(self):
        """A renamed key means the client reads a field the kernel never sends."""
        moved = CLIENT_HANDSHAKE.replace(
            'object.get("nonce")', 'object.get("nonse")')
        self.assertNotEqual(moved, CLIENT_HANDSHAKE, "the fixture must change")
        failed = self._replace(client_handshake=moved)
        self.assertTrue(failed, "a renamed hello key must be caught")
        self.assertIn("hello json keys", [r.label for r in failed])

    def test_a_renamed_hello_key_on_the_kernel_side_is_caught(self):
        """The kernel builds the payload with C-escaped quotes; decode them."""
        moved = KERNEL_PIPE_CC.replace(BS + '"nonce' + BS + '"',
                                       BS + '"nonse' + BS + '"')
        self.assertNotEqual(moved, KERNEL_PIPE_CC, "the fixture must change")
        failed = self._replace(kernel_pipe_cc=moved)
        self.assertTrue(failed, "a renamed kernel hello key must be caught")
        self.assertIn("hello json keys", [r.label for r in failed])

    def test_the_hello_key_order_alone_is_not_a_mismatch(self):
        """JSON objects are unordered; reordering the keys is not drift."""
        reordered = KERNEL_PIPE_CC.replace(
            '  std::string payload = "{"' + BS + '"version' + BS + '":' + '";\n',
            '  std::string payload = "{"' + BS + '"nonce' + BS + '":' + BS +
            '"";\n  payload += "' + BS + '"version' + BS + '":' + '";\n')
        self.assertNotEqual(reordered, KERNEL_PIPE_CC, "the fixture must change")
        failed = self._replace(kernel_pipe_cc=reordered)
        self.assertNotIn("hello json keys", [r.label for r in failed],
                         "key order is not part of the constant")


class BootstrapLayerShapeTest(unittest.TestCase):
    """The readers must survive the shapes the real files have.

    These four cases are not hypothetical. The first one is the defect
    this guard was written with: `Endpoint::path()` calls
    `windows_endpoint(name)` *before* the function is defined, so a
    reader that searched for the bare name returned the body of the
    caller, which has no `format!` in it, and the check died with
    "windows_endpoint has no format! call" against the real repo while
    every fixture-based test stayed green.
    """

    def test_a_call_site_before_the_definition_does_not_confuse_the_reader(self):
        """The client calls the function it defines: anchor on `fn`, not the name."""
        text = (
            "pub fn path(&self) -> String {\n"
            "    match self {\n"
            "        Self::WindowsNamedPipe { name } => windows_endpoint(name),\n"
            "    }\n"
            "}\n"
            "\n"
            "pub fn windows_endpoint(name: &str) -> String {\n"
            '    format!(r"' + WINDOWS_ROOT + '{ENDPOINT_PREFIX}{name}")\n'
            "}\n"
        )
        self.assertEqual(tool._format_prefix(text, "windows_endpoint"),
                         WINDOWS_ROOT)

    def test_braces_inside_a_string_literal_do_not_end_the_body(self):
        """A JSON payload is full of braces; they must not close the function."""
        text = KERNEL_PIPE_CC
        keys = tool.kernel_hello_keys(text)
        self.assertEqual(keys, frozenset({"version", "nonce"}))

    def test_a_wrapped_c_declaration_is_read_as_one_value(self):
        """The kernel wraps long declarations; a line-based reader truncates."""
        text = (
            "inline constexpr char kKernelPipeSessionInfo[] =\n"
            '    "lengsuo-kernel-session-v1";\n'
        )
        self.assertEqual(tool._cpp_string(text, "kKernelPipeSessionInfo"),
                         "lengsuo-kernel-session-v1")

    def test_a_member_function_is_found_by_its_qualified_name(self):
        """C++ member functions are defined as `Class::Name(`."""
        text = (
            "base::ScopedFD KernelPipeClient::OpenEndpoint(std::string_view name) {\n"
            "  const std::wstring path = " + C_WINDOWS_ROOT + " + wide(name);\n"
            "  return base::ScopedFD();\n"
            "}\n"
        )
        self.assertEqual(tool._cpp_wide_prefix(text, "OpenEndpoint"),
                         WINDOWS_ROOT)


class BootstrapLayerUnreadableTest(unittest.TestCase):
    """A side that cannot be read must be reported, never skipped."""

    def test_a_missing_kernel_file_is_an_error_not_a_pass(self):
        pair = _Pair()
        self.addCleanup(pair.addCleanup)
        (pair.kernel / tool.KERNEL_ENVELOPE_H_REL).unlink()
        with self.assertRaises(tool.BootstrapVectorError):
            tool.compare_bootstrap(pair.kernel, pair.client)

    def test_a_missing_client_file_is_an_error_not_a_pass(self):
        pair = _Pair()
        self.addCleanup(pair.addCleanup)
        (pair.client / tool.CLIENT_KEYS_REL).unlink()
        with self.assertRaises(tool.BootstrapVectorError):
            tool.compare_bootstrap(pair.kernel, pair.client)

    def test_a_contract_without_the_section_is_an_error(self):
        """The section moved: that is a contract that cannot be read."""
        stripped = CONTRACT.replace("### 1.1 ⭐ 启动层固定常量（两侧逐字节相同）",
                                    "### 1.9 别的东西")
        self.assertNotEqual(stripped, CONTRACT, "the fixture must change")
        pair = _Pair(contract=stripped)
        self.addCleanup(pair.addCleanup)
        with self.assertRaises(tool.BootstrapVectorError):
            tool.compare_bootstrap(pair.kernel, pair.client)

    def test_a_contract_row_that_vanished_is_an_error(self):
        """Dropping a row would silently drop a check; that must be loud."""
        gone = ("| GCM nonce 字节 | `12` | `kKernelPipeGcmNonceBytes` "
                "| `GCM_NONCE_LEN` |\n")
        stripped = CONTRACT.replace(gone, "")
        self.assertNotEqual(stripped, CONTRACT, "the fixture must change")
        pair = _Pair(contract=stripped)
        self.addCleanup(pair.addCleanup)
        with self.assertRaises(tool.BootstrapVectorError):
            tool.compare_bootstrap(pair.kernel, pair.client)


if __name__ == "__main__":
    unittest.main()
