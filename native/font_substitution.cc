// The substitution decision is header-only (native/font_substitution.h). This
// translation unit exists so the CMake and GN targets have a stable source and
// the header is compiled and -Werror-checked even when only the library, not
// the test, is built.
#include "font_substitution.h"
