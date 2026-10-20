"""Apply the L3 storage pin wiring to a Chromium checkout (task K).

Every edit is an exact-string replacement against revision 0e66b94549a, so a
mismatch fails loudly instead of silently mis-patching. Pair with
tools/rebuild/replay_overlay_patches.sh to rebuild the tree from scratch.

Blink-side API details this file encodes (verified against 155.0.8059.85):
  * blink::String has FromUtf8(std::string_view), not FromUTF8.
  * StorageArea::GetDOMWindow() is not const while getItem() is, so the origin
    is cached in a member at construction time instead of being re-derived.
  * The origin form the pin table uses is the security origin's
    "https://host" spelling, which is what SecurityOrigin::ToString() returns.
"""

import pathlib
import sys


def patch(path, pairs):
    target = pathlib.Path(path)
    text = target.read_text(encoding="utf-8")
    for old, new in pairs:
        if old not in text:
            sys.exit(f"anchor not found in {path}:\n{old[:240]}")
        text = text.replace(old, new, 1)
    target.write_text(text, encoding="utf-8")
    print(f"patched {path}")


STORAGE_AREA_H = [
    (
        "  LocalDOMWindow* GetDOMWindow() override;\n",
        """  LocalDOMWindow* GetDOMWindow() override;

  // Storage-layer pins (L3). Seeds the frozen pinned key/value pairs for the
  // window's origin into this area's memory map and backing store. Called
  // right after creation so the first script observes them. No-op when the
  // runtime has no snapshot or the origin has no pins.
  void SeedStoragePins();

 private:
  // Cached origin of the owning document, in the "https://host" form the pin
  // table uses. Cached because GetDOMWindow() is not const while the read path
  // (getItem) is, and because the security origin does not change during the
  // lifetime of the area.
  String origin_;
""",
    ),
    (
        """#include "third_party/blink/renderer/platform/wtf/forward.h"
""",
        """#include "third_party/blink/renderer/platform/wtf/forward.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
""",
    ),
]

STORAGE_AREA_CC = [
    (
        '#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"\n',
        '#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"\n'
        '#include "lengsuo/native/l3_runtime.h"\n',
    ),
    (
        """    : ExecutionContextClient(window),
      cached_area_(std::move(storage_area)),
      storage_type_(storage_type),
      should_enqueue_events_(should_enqueue_events) {
  DCHECK(window);
  DCHECK(cached_area_);""",
        """    : ExecutionContextClient(window),
      cached_area_(std::move(storage_area)),
      storage_type_(storage_type),
      should_enqueue_events_(should_enqueue_events),
      origin_(window->GetSecurityOrigin()->ToString()) {
  DCHECK(window);
  DCHECK(cached_area_);""",
    ),
    (
        """  return cached_area_->GetItem(key);
}

NamedPropertySetterResult StorageArea::setItem(""",
        """  // Storage-layer pins (L3): the frozen snapshot is authoritative for
  // pinned keys, so the answer never depends on what the store holds now.
  if (storage_type_ == StorageType::kLocalStorage) {
    const std::string* pinned = lengsuo::fp::GetL3Runtime().PinnedValue(
        origin_.Utf8(), key.Utf8());
    if (pinned) {
      return String::FromUtf8(*pinned);
    }
  }
  return cached_area_->GetItem(key);
}

NamedPropertySetterResult StorageArea::setItem(""",
    ),
    (
        "  if (!cached_area_->SetItem(key, value, this)) {\n",
        """  // Pinned keys are read-only: the environment froze platform-issued device
  // fields, so page writes are dropped silently, matching a pinned store.
  if (storage_type_ == StorageType::kLocalStorage &&
      lengsuo::fp::GetL3Runtime().IsLocked(origin_.Utf8(), key.Utf8())) {
    return NamedPropertySetterResult::kIntercepted;
  }
  if (!cached_area_->SetItem(key, value, this)) {
""",
    ),
    (
        """  cached_area_->RemoveItem(key, this);
  return NamedPropertyDeleterResult::kDeleted;
""",
        """  // Pinned keys cannot be removed for the same reason they cannot be set.
  if (storage_type_ == StorageType::kLocalStorage &&
      lengsuo::fp::GetL3Runtime().IsLocked(origin_.Utf8(), key.Utf8())) {
    return NamedPropertyDeleterResult::kDidNotDelete;
  }
  cached_area_->RemoveItem(key, this);
  return NamedPropertyDeleterResult::kDeleted;
""",
    ),
    (
        """  cached_area_->Clear(this);
}

bool StorageArea::Contains(""",
        """  // clear() must still wipe everything non-pinned, so the pins are
  // snapshotted and re-written after the wipe. Refusing clear() outright
  // would break legitimate cleanup and leak the pin set via bisection.
  Vector<std::pair<String, String>> restore;
  if (storage_type_ == StorageType::kLocalStorage) {
    if (const auto* pins =
            lengsuo::fp::GetL3Runtime().LocalPins(origin_.Utf8())) {
      for (const lengsuo::fp::PinnedEntry& pinned : *pins) {
        restore.emplace_back(String::FromUtf8(pinned.key),
                             String::FromUtf8(pinned.value));
      }
    }
  }
  cached_area_->Clear(this);
  for (const auto& entry : restore) {
    cached_area_->SetItem(entry.first, entry.second, this);
  }
}

void StorageArea::SeedStoragePins() {
  if (storage_type_ != StorageType::kLocalStorage) {
    return;
  }
  const auto& runtime = lengsuo::fp::GetL3Runtime();
  if (!runtime.ready()) {
    return;
  }
  const auto* pins = runtime.LocalPins(origin_.Utf8());
  if (!pins) {
    return;
  }
  for (const lengsuo::fp::PinnedEntry& pinned : *pins) {
    // The pinned value wins over whatever the store already holds; a missing
    // entry and a differing entry are seeded identically. Neither key nor
    // value is logged: pinned values are device identity, not diagnostics.
    cached_area_->SetItem(String::FromUtf8(pinned.key),
                          String::FromUtf8(pinned.value), this);
  }
}

bool StorageArea::Contains(""",
    ),
]

DOM_WINDOW_STORAGE_CC = [
    (
        """  local_storage_ = StorageArea::Create(window, std::move(storage_area),
                                       StorageArea::StorageType::kLocalStorage);
  return local_storage_.Get();
}
""",
        """  local_storage_ = StorageArea::Create(window, std::move(storage_area),
                                       StorageArea::StorageType::kLocalStorage);
  // Storage-layer pins (L3): seed before the area reaches any script, so the
  // very first getItem already observes the frozen values.
  local_storage_->SeedStoragePins();
  return local_storage_.Get();
}
""",
    ),
]

STORAGE_BUILD_GN = [
    (
        """blink_modules_sources("storage") {
  sources = [
""",
        """blink_modules_sources("storage") {
  # Storage-layer pins (L3): Blink queries the frozen snapshot through this
  # target instead of parsing the managed document itself.
  deps = [ "//lengsuo/native:l3_runtime" ]

  sources = [
""",
    ),
]

RENDERER_MAIN_CC = [
    (
        """#if BUILDFLAG(MOJO_RANDOM_DELAYS_ENABLED)
#include "mojo/public/cpp/bindings/lib/test_random_mojo_delays.h"
#endif
""",
        """#if BUILDFLAG(MOJO_RANDOM_DELAYS_ENABLED)
#include "mojo/public/cpp/bindings/lib/test_random_mojo_delays.h"
#endif

#include "lengsuo/native/l3_runtime.h"
#include "base/base64.h"
""",
    ),
    (
        """  const base::CommandLine& command_line = *parameters.command_line;
""",
        """  const base::CommandLine& command_line = *parameters.command_line;

  // Storage-layer pins (L3). Transitional transport: the document rides the
  // command line for now; the target design is a managed file read by the
  // browser and forwarded over Mojo (Reference never puts fingerprint state on
  // the command line). A present-but-invalid document aborts the renderer: a
  // platform that already observed a pinned value must not see it vanish.
  // Freshness was enforced browser-side before forwarding, so the renderer
  // does not re-check age with its own clock.
  if (command_line.HasSwitch("lengsuo-managed-l3")) {
    const std::string l3_wire =
        command_line.GetSwitchValueASCII("lengsuo-managed-l3");
    if (l3_wire.empty()) return 1;
    // now_unix = 0: the browser already judged freshness against its own
    // clock before forwarding, so the renderer verifies the digest and the
    // launch consistency gate only. A real timestamp here would reject every
    // document as "too new", because the renderer has no clock to compare it
    // against.
    if (!lengsuo::fp::GetL3Runtime().Init(l3_wire, /*now_unix=*/0,
                                          /*max_age_seconds=*/0)) {
      return 1;
    }
  }
""",
    ),
]


BROWSER_MAIN_CC = [
    (
        """  if (!lengsuo::InitializeBrowserProfile(*base::CommandLine::ForCurrentProcess())) {
    LOG(ERROR) << "Lengsuo profile bootstrap rejected";
    return CHROME_RESULT_CODE_INVALID_CMDLINE_URL;
  }
""",
        """  if (!lengsuo::InitializeBrowserProfile(*base::CommandLine::ForCurrentProcess())) {
    LOG(ERROR) << "Lengsuo profile bootstrap rejected";
    return CHROME_RESULT_CODE_INVALID_CMDLINE_URL;
  }

  // Storage-layer pins (L3). A requested but unreadable or unverifiable
  // document aborts startup: continuing would unpin values a platform may
  // already have observed, which reads as "the device changed".
  if (!lengsuo::InitializeBrowserL3(*base::CommandLine::ForCurrentProcess())) {
    LOG(ERROR) << "Lengsuo L3 bootstrap rejected";
    return CHROME_RESULT_CODE_INVALID_CMDLINE_URL;
  }
""",
    ),
]

BROWSER_CLIENT_CC = [
    (
        """    if (const auto* browser_profile = lengsuo::BrowserProfile())
      command_line->AppendSwitchASCII("lengsuo-managed-language",
                                      browser_profile->language);
""",
        """    if (const auto* browser_profile = lengsuo::BrowserProfile())
      command_line->AppendSwitchASCII("lengsuo-managed-language",
                                      browser_profile->language);
    // Storage-layer pins (L3). The document is re-serialized from the frozen
    // snapshot and base64-encoded, so the renderer receives exactly what the
    // browser verified. Transitional transport; see docs/fp-switch-contract.md
    // for the move to a managed file plus Mojo.
    if (const std::string l3_b64 = lengsuo::BrowserL3Base64();
        !l3_b64.empty() && !command_line->HasSwitch("lengsuo-managed-l3")) {
      command_line->AppendSwitchASCII("lengsuo-managed-l3", l3_b64);
    }
""",
    ),
]


def main():
    patch("third_party/blink/renderer/modules/storage/storage_area.h",
          STORAGE_AREA_H)
    patch("third_party/blink/renderer/modules/storage/storage_area.cc",
          STORAGE_AREA_CC)
    patch("third_party/blink/renderer/modules/storage/dom_window_storage.cc",
          DOM_WINDOW_STORAGE_CC)
    patch("third_party/blink/renderer/modules/storage/BUILD.gn",
          STORAGE_BUILD_GN)
    patch("content/renderer/renderer_main.cc", RENDERER_MAIN_CC)
    patch("chrome/browser/chrome_browser_main.cc", BROWSER_MAIN_CC)
    patch("chrome/browser/chrome_content_browser_client.cc", BROWSER_CLIENT_CC)
    print("all seven files wired")


if __name__ == "__main__":
    main()
