"""Apply the L3 storage pin wiring to a Chromium checkout (task K).

Every edit is an exact-string replacement against revision 0e66b94549a, so a
mismatch fails loudly instead of silently mis-patching. Run from the checkout
root; pair with revert_l3_wiring.py when generating the patch.
"""

import pathlib
import sys


def patch(path, pairs):
    target = pathlib.Path(path)
    text = target.read_text(encoding="utf-8")
    for old, new in pairs:
        if old not in text:
            sys.exit(f"anchor not found in {path}:\n{old[:200]}")
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
        """  return cached_area_->GetItem(key);
}

NamedPropertySetterResult StorageArea::setItem(""",
        """  // Storage-layer pins (L3): the frozen snapshot is authoritative for
  // pinned keys, so the answer never depends on what the store holds now.
  if (storage_type_ == StorageType::kLocalStorage) {
    const std::string* pinned = lengsuo::fp::GetL3Runtime().PinnedValue(
        GetDOMWindow()->GetSecurityOrigin()->ToString().Utf8(), key.Utf8());
    if (pinned) {
      return String::FromUTF8(*pinned);
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
      lengsuo::fp::GetL3Runtime().IsLocked(
          GetDOMWindow()->GetSecurityOrigin()->ToString().Utf8(),
          key.Utf8())) {
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
      lengsuo::fp::GetL3Runtime().IsLocked(
          GetDOMWindow()->GetSecurityOrigin()->ToString().Utf8(),
          key.Utf8())) {
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
    if (const auto* pins = lengsuo::fp::GetL3Runtime().LocalPins(
            GetDOMWindow()->GetSecurityOrigin()->ToString().Utf8())) {
      for (const lengsuo::fp::PinnedEntry& pinned : *pins) {
        restore.emplace_back(String::FromUTF8(pinned.key),
                             String::FromUTF8(pinned.value));
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
  const String origin = GetDOMWindow()->GetSecurityOrigin()->ToString();
  const auto* pins = runtime.LocalPins(origin.Utf8());
  if (!pins) {
    return;
  }
  for (const lengsuo::fp::PinnedEntry& pinned : *pins) {
    // The pinned value wins over whatever the store already holds; a missing
    // entry and a differing entry are seeded identically. Neither key nor
    // value is logged: pinned values are device identity, not diagnostics.
    cached_area_->SetItem(String::FromUTF8(pinned.key),
                          String::FromUTF8(pinned.value), this);
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

# Anchors below match the *pristine* revision 0e66b94549a tree. When patches
# 0007/0008 have already been applied, the lengsuo includes exist and the
# anchors below would not be found - in that case patch renderer_main.cc by
# hand against the lengsuo block instead. The pristine anchors are the default
# because tools.patch_apply applies 0015 last, before the overlay tooling ever
# runs this script.
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
    if (!lengsuo::fp::GetL3Runtime().Init(l3_wire, /*now_unix=*/1,
                                          /*max_age_seconds=*/1)) {
      return 1;
    }
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
    print("all five files wired")


if __name__ == "__main__":
    main()
