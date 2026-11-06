// Umbrella header for string/identifier utilities:
//   strings_text   - trim/tokenize/glob/filename/path helpers
//   strings_utf8   - UTF8 decode helpers
//   strings_ident  - identifier parsing and sigil stripping
//   strings_names  - trust<->C++ name mangling
//   strings_embed  - {% ... %} embed name transformation
//   strings_escape - C++ string literal escaping
//   strings_json   - JSON string escaping (shared by diag/serialize and verify)
#pragma once

#include "utils/strings/strings_text.hpp"
#include "utils/strings/strings_utf8.hpp"
#include "utils/strings/strings_ident.hpp"
#include "utils/strings/strings_names.hpp"
#include "utils/strings/strings_embed.hpp"
#include "utils/strings/strings_escape.hpp"
#include "utils/strings/strings_json.hpp"
