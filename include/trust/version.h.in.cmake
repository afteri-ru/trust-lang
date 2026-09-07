#pragma once

#define TRUST_VERSION_MAJOR @TRUST_VERSION_MAJOR@
#define TRUST_VERSION_MINOR @TRUST_VERSION_MINOR@
#define TRUST_VERSION_PATCH @TRUST_VERSION_PATCH@

// Clean release number, e.g. "0.6.0" (no git hash).
#define TRUST_VERSION_SHORT "@TRUST_VERSION_SHORT@"

#define TRUST_GIT_HASH "@TRUST_GIT_HASH@"

// Full identifying string, always with the git hash, e.g. "0.6.0-<git_hash>".
#define TRUST_VERSION_FULL "@TRUST_VERSION_FULL@"

// Effective version selected by the build mode (cmake/version.cmake):
//   release -> TRUST_VERSION_SHORT (no git hash),
//   dev     -> TRUST_VERSION_FULL (with git hash).
#define TRUST_VERSION "@TRUST_VERSION@"

#define TRUST_DATE_BUILD "@TRUST_DATE_BUILD@"
