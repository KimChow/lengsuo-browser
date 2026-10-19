"""Restore the combined fingerprint switch in AppendExtraCommandLineSwitches.

The hunk-level replay left this region at the older three-switch form because
two patches disagree on the same lines (one carries the 8-field version, the
newer one the 12-field version that docs/fp-switch-contract.md freezes).

Rather than let apply order decide which one wins, write the intended block
explicitly and verify it landed.
"""

import pathlib
import sys

SRC = pathlib.Path("/home/ubuntu/lengsuo-browser-dev/src")
TARGET = SRC / "chrome/browser/chrome_content_browser_client.cc"

ANCHOR_START = "  if (command_line->GetSwitchValueASCII(switches::kProcessType) =="
ANCHOR_END = "  crash_keys::AppendStringAnnotationsCommandLineSwitch(command_line);"

BLOCK = '''  if (command_line->GetSwitchValueASCII(switches::kProcessType) ==
      switches::kRendererProcess) {
    // Direct-passthrough: when the browser itself was launched with
    // --lengsuo-managed-fp (test harness mode, no profile wire), forward it
    // verbatim so the renderer's InitFromSwitch can pick it up.
    const base::CommandLine& browser_cmd =
        *base::CommandLine::ForCurrentProcess();
    if (browser_cmd.HasSwitch("lengsuo-managed-fp") &&
        !command_line->HasSwitch("lengsuo-managed-fp")) {
      command_line->AppendSwitchASCII(
          "lengsuo-managed-fp",
          browser_cmd.GetSwitchValueASCII("lengsuo-managed-fp"));
    }
    if (const auto* browser_profile = lengsuo::BrowserProfile()) {
      command_line->AppendSwitchASCII("lengsuo-managed-language",
                                      browser_profile->language);
      command_line->AppendSwitchASCII("lengsuo-managed-timezone",
                                      browser_profile->timezone);
      {
        std::uint64_t canvas_seed = 0;
        for (const std::uint8_t b : browser_profile->noise_seed) {
          canvas_seed = (canvas_seed << 8) | b;
        }
        command_line->AppendSwitchASCII(
            "lengsuo-managed-canvas",
            base::StringPrintf("%016llx:%u",
                               static_cast<unsigned long long>(canvas_seed),
                               browser_profile->slots[1]));
        // Combined fingerprint switch. Field order is fixed by
        // lengsuo/native/fp_overrides.cc:
        //   seed:canvas:audio:rects:fonts:gpu:ua:webglParams:
        //   vendor:renderer:hwc:mem
        // Any slot at 0 leaves that dimension native. GPU faces come from the
        // catalog's optional gpu section; absent entries stay empty so the
        // renderer keeps the driver's own strings.
        const lengsuo::CatalogGpuEntry* gpu_face = lengsuo::BrowserGpu();
        command_line->AppendSwitchASCII(
            "lengsuo-managed-fp",
            base::StringPrintf("%016llx:%u:%u:%u:%u:%u:%u:%u:%s:%s:%u:%u",
                               static_cast<unsigned long long>(canvas_seed),
                               browser_profile->slots[1],
                               browser_profile->slots[0],
                               browser_profile->slots[3],
                               browser_profile->slots[2],
                               browser_profile->slots[4],
                               browser_profile->slots[5],
                               browser_profile->slots[6],
                               gpu_face ? gpu_face->vendor.c_str() : "",
                               gpu_face ? gpu_face->renderer.c_str() : "",
                               browser_profile->cpu_cores,
                               browser_profile->memory_mib / 1024));
      }
    }
  }
'''


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    start = text.index(ANCHOR_START)
    end = text.index(ANCHOR_END, start)
    TARGET.write_bytes((text[:start] + BLOCK + text[end:]).encode("utf-8"))

    check = TARGET.read_text(encoding="utf-8")
    problems = []
    if check.count("lengsuo-managed-fp") < 2:
        problems.append("lengsuo-managed-fp missing")
    if "%016llx:%u:%u:%u:%u:%u:%u:%u:%s:%s:%u:%u" not in check:
        problems.append("12-field format missing")
    if check.count("lengsuo-managed-canvas") != 1:
        problems.append("canvas switch count is not 1")
    if check.count("lengsuo-managed-timezone") != 1:
        problems.append("timezone switch count is not 1")
    if problems:
        print("FAILED: " + ", ".join(problems))
        return 1
    print("ok: combined switch restored")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
