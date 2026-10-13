#pragma once

#if __clang__

#pragma clang attribute pop
#pragma clang diagnostic pop

#elif __GNUC__

#pragma GCC diagnostic pop

#elif _MSC_VER

#pragma warning(pop)

#endif
