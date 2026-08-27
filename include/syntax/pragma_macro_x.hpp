#pragma once

// include/syntax/pragma_macro_x.hpp
// ЕДИНСТВЕННЫЙ источник прагм (препроцессорных макросов) @__PRAGMA_*/@__OPTION_*/@__HYGIENIC__.
// X-macro: X(Name, "name", ("description")). Описание - та же справка, что сидируется в
// Context::macroDocs (доки прагм задаются ЗДЕСЬ, а не в ветках кода, чтобы не рассинхронизироваться).

#define TRUST_PRAGMA_MACROS(X)                                                                                                                                 \
    X(Hygienic, "@__HYGIENIC__", ("Generate a hygienic (fresh, unique per file) identifier: @__HYGIENIC__(<ident>)."))                                         \
    X(Option, "@__OPTION__",                                                                                                                                   \
      ("Set or override a compiler option for the rest of the compilation. Feature flag: @__OPTION__(\"<flag>\", \"on|off|1|0|true|false|ignore\") or a flag " \
       "value. Diagnostic option: @__OPTION__(\"<name>\", \"error|warning|note|ignore\")."))                                                                   \
    X(OptionPush, "@__OPTION_PUSH__", ("Push the current compiler options onto a stack (restore later with @__OPTION_POP__)."))                                \
    X(OptionPop, "@__OPTION_POP__", ("Pop the compiler options stack, restoring the previous options."))                                                       \
    X(OptionTrue, "@__OPTION_TRUE__",                                                                                                                          \
      ("Conditionally expand <lex>... if the feature flag <flag> is enabled: @__OPTION_TRUE__(<flag>, <lex>...). Expands to nothing when the flag is "         \
       "disabled."))                                                                                                                                           \
    X(OptionFalse, "@__OPTION_FALSE__",                                                                                                                        \
      ("Conditionally expand <lex>... if the feature flag <flag> is disabled: @__OPTION_FALSE__(<flag>, <lex>...). Expands to nothing when the flag is "       \
       "enabled."))                                                                                                                                            \
    X(OptionIIf, "@__OPTION_IIF__",                                                                                                                            \
      ("Choose one of two branches by the feature flag: @__OPTION_IIF__(<flag>, <true>, <false>). Expands to <true> when the flag is enabled, otherwise to "   \
       "<false>."))                                                                                                                                            \
    X(PragmaMessage, "@__PRAGMA_MESSAGE__", ("Emit an informational note to the diagnostic output: @__PRAGMA_MESSAGE__(\"text\", ...)."))                      \
    X(PragmaWarning, "@__PRAGMA_WARNING__", ("Emit a warning to the diagnostic output: @__PRAGMA_WARNING__(\"text\", ...)."))                                  \
    X(PragmaError, "@__PRAGMA_ERROR__", ("Emit an error to the diagnostic output: @__PRAGMA_ERROR__(\"text\", ...)."))                                         \
    X(PragmaExpected, "@__PRAGMA_EXPECTED__", ("Assert the next token is one of the given string representations: @__PRAGMA_EXPECTED__(\"tok\", ...)."))       \
    X(PragmaDoc, "@__PRAGMA_DOC__",                                                                                                                            \
      ("Set or override the documentation of an existing macro: @__PRAGMA_DOC__(\"<name>\", \"<text>\"). Only overrides an already documented macro."))
