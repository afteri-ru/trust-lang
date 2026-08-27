#pragma once

// include/syntax/predef_macro_x.hpp
// ЕДИНСТВЕННЫЙ источник предопределённых макросов (@__...__ и др.), разделённый по природе
// на ДВЕ категории:
//   - TRUST_VALUE_MACROS   - «значение-макросы»: фиксированные значения, вычислимые на парсере
//                            (версия, FILE/LINE/DATE/TIME/COUNTER/MD5/TIMESTAMP, ROOT_DIR).
//                            Раскрываются на парсере, в т.ч. внутри {% %}.
//   - TRUST_CONTEXT_MACROS - «контекст-макросы» (информация анализатора): имена/значения, зависящие
//                            от контекста функции/пространства имён/класса/модуля. Проходят через
//                            парсер транзитными маркерами (MACRO_CONTEXT/NAMESPACE/NAME/MODULE) и
//                            разрешаются на этапе анализатора (NameResolutionPass); часть
//                            (@__MODULE_NAME__) статически вычислима на парсере.
// Прагмы (директивы/опции парсера) - отдельный список TRUST_PRAGMA_MACROS (pragma_macro_x.hpp).
// X-macro: X(Name, "name", ("description")). Описание берётся в скобки, чтобы запятые в тексте
// не разбивали аргумент макроса. Используется для генерации enum, таблиц имён/описаний и
// раскрытия - рассинхрон между реестром и реализацией исключён по построению.

#define TRUST_VALUE_MACROS(X)                                                  \
    X(TrustVersionMajor, "@__TRUST_VERSION_MAJOR__", ("Major version"))        \
    X(TrustVersionMinor, "@__TRUST_VERSION_MINOR__", ("Minor version"))        \
    X(TrustVersionPatch, "@__TRUST_VERSION_PATCH__", ("Patch version"))        \
    X(TrustVersion, "@__TRUST_VERSION__", ("Version in format 'X.Y.Z'"))       \
    X(TrustGitHash, "@__TRUST_GIT_HASH__", ("Short git hash"))                 \
    X(TrustVersionFull, "@__TRUST_VERSION_FULL__", ("Version with git hash"))  \
    X(TrustDateBuild, "@__TRUST_DATE_BUILD__", ("Date build"))                 \
    X(File, "@__FILE__", ("Current file name"))                                \
    X(FileName, "@__FILE_NAME__", ("Current file name"))                       \
    X(Line, "@__LINE__", ("Line number in current file"))                      \
    X(FileLine, "@__FILE_LINE__", ("Line number in current file"))             \
    X(FileMd5, "@__FILE_MD5__", ("MD5 hash for current file"))                 \
    X(FileTimestamp, "@__FILE_TIMESTAMP__", ("Timestamp current file"))        \
    X(Date, "@__DATE__", ("Current date"))                                     \
    X(Time, "@__TIME__", ("Current time"))                                     \
    X(Timestamp, "@__TIMESTAMP__", ("Current timestamp"))                      \
    X(TimestampISO, "@__TIMESTAMP_ISO__", ("Current timestamp as ISO format")) \
    X(Counter, "@__COUNTER__", ("Monotonically increasing counter from zero")) \
    X(RootDir, "@__ROOT_DIR__", ("Root directory with the main program module"))

#define TRUST_CONTEXT_MACROS(X)                                                                                                             \
    X(NamespaceFull, "@::", ("Full name of the current namespace"))                                                                         \
    X(ModuleFull, "$\\\\", ("Full name of the current module name"))                                                                        \
    X(Class, "@__CLASS__", ("Current class name"))                                                                                          \
    X(Namespace, "@__NAMESPACE__", ("Current namespace"))                                                                                   \
    X(Function, "@__FUNCTION__", ("Current function name"))                                                                                 \
    X(FuncDName, "@__FUNCDNAME__", ("Decorated of current function name"))                                                                  \
    X(FuncSig, "@__FUNCSIG__", ("Signature of current function"))                                                                           \
    X(ModuleName, "@__MODULE_NAME__", ("Current module name (without extension, relative to the main file, separators replaced with '_')")) \
    X(BareNamespace, "@$$", ("Current namespace (bare)"))
