// extension_pin - the PinedFile extension whitelist (L3 任务书 任务 H).
//
// 对标端 uses PinedFile to fix "which extensions must travel with the
// environment": a JSON array of Chromium extension IDs, e.g.
//   ["fjchbfkopknlodpfoaeogfbclcpppiog",
//    "fgbbdcdpliimpdmgcnahkcdfhdjegmin",
//    "hncdafoadpldclaaigooamfbebmaakjj"]
//
// Parsing is fail-closed: any invalid ID, non-array document or non-string
// element rejects the whole document and leaves prior state untouched. IDs
// use Chromium's alphabet: exactly 32 characters of [a-p].
#ifndef LENGSUO_NATIVE_EXTENSION_PIN_H_
#define LENGSUO_NATIVE_EXTENSION_PIN_H_

#include <cstddef>
#include <set>
#include <string>

namespace lengsuo::fp {

class ExtensionPin {
 public:
  // Parses a PinedFile document. On failure the pin keeps its previous
  // contents; nothing is half-applied.
  bool Parse(const std::string& json);

  // Serializes as a JSON string array in stable (sorted) order, so two pins
  // with the same membership serialize byte-identically.
  std::string Serialize() const;

  bool Contains(const std::string& id) const;

  // Returns false (and changes nothing) when `id` is already present or is
  // not a valid Chromium extension ID.
  bool Add(const std::string& id);

  size_t size() const { return ids_.size(); }

  // Sorted view; used by the L3 document encoder.
  const std::set<std::string>& Ids() const { return ids_; }

  // 32 characters of [a-p] (Chromium extension ID alphabet).
  static bool IsValidId(const std::string& id);

 private:
  std::set<std::string> ids_;
};

}  // namespace lengsuo::fp

#endif  // LENGSUO_NATIVE_EXTENSION_PIN_H_
