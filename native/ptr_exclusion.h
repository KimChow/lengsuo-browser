// The raw_ptr exclusion marker (standalone).
//
// Chromium's raw-ptr plugin (check-raw-ptr-fields) requires raw_ptr<T> for
// pointer fields. It reads an annotation named "raw_ptr_exclusion", which is
// what upstream's RAW_PTR_EXCLUSION macro expands to when the plugin is on
// (tools/clang/raw_ptr_plugin/RawPtrHelpers.h: isRawPtrExclusionAnnotated).
//
// The kernel's native layer cannot use raw_ptr: it is standalone pure logic
// that compiles for its own gtest without Chromium's base/ headers. Fields
// that are non-owning views into a caller-owned table - the caller owns the
// object, the field only names it for the duration of one call - are therefore
// marked here instead, and the annotation is the only thing the plugin needs.
//
// The marker is empty outside clang: the gtest builds with g++ -Werror
// -pedantic, and clang::annotate is not a g++ attribute.
#ifndef LENGSUO_NATIVE_PTR_EXCLUSION_H_
#define LENGSUO_NATIVE_PTR_EXCLUSION_H_

#if defined(__clang__)
#define LENGSUO_RAW_PTR_EXCLUSION [[clang::annotate("raw_ptr_exclusion")]]
#else
#define LENGSUO_RAW_PTR_EXCLUSION
#endif

#endif  // LENGSUO_NATIVE_PTR_EXCLUSION_H_