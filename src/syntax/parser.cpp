#include "syntax/parser.h"
#include "syntax/lexer.h"
#include "diag/context.hpp"

#include "syntax/warning_push.h"
#include <sys/stat.h>
#include "syntax/warning_pop.h"

#include "syntax/term.h"
#include "trust/version.h"

using namespace trust;

int Parser::m_counter = 0;

Parser::Parser(trust::Context& ctx, PostLexerType* postlex, bool pragma_enable, bool macro_expand)
: m_ctx(ctx) {
    char* source_date_epoch = std::getenv("SOURCE_DATE_EPOCH");
    if (source_date_epoch) {
        std::istringstream iss(source_date_epoch);
        iss >> m_timestamp;
        if (iss.fail() || !iss.eof()) {
            throw ParserError("Error: Cannot parse SOURCE_DATE_EPOCH (%s) as integer", source_date_epoch);
        }
    } else {
        m_timestamp = std::time(NULL);
    }

    // TODO(cleanup): m_is_runing unused — commented out, see task 1785675437901
    // m_is_runing = false;
    m_is_lexer_complete = false;

    m_macro = macro_expand ? ctx.macro() : nullptr;
    m_postlex = postlex;
    // TODO(cleanup): unused — only for m_annotation pragmas, commented out, see task 1785678668891
    // m_annotation = Term::Create(TermID::ARGS, "", parser::token_type::ARGS);
    m_no_macro = false;
    m_enable_pragma = pragma_enable;
    //    m_name_module = "\\\\__main__";

    m_ast = nullptr;
}

TermPtr Parser::ParseText(std::string_view text, std::string_view sourceName, bool expand_module) {
    // Парсит «голый» текстовый фрагмент под фиктивным именем источника (например,
    // встроенный DSL под именем "@dsl"). Фиктивные имена помечаются префиксом '@'
    // (in-memory, файла на диске нет). Для реальных файлов/модулей используйте
    // ParseWithSource(зарегистрированный MapperFile).
    // Явное std::string(...) обязательно: неявное string_view -> string не
    // компилируется в GCC 14 / C++23 для by-value параметров add_source.
    trust::MapperFile src = m_ctx.source().add_source(std::string(sourceName), std::string(text));
    return ParseWithSource(src, expand_module);
}

// Парсит из уже зарегистрированного source-файла (не создавая псевдо-источник
// с префиксом '@' / in-memory). Используется модульным загрузчиком для главного
// файла/модуля, чтобы маппинги (mapStart/mapStop) привязывались к реальному
// файлу, а не к новому одноимённому входу — иначе source map невозможно
// сопоставить с исходным .src.
TermPtr Parser::ParseWithSource(trust::MapperFile src, bool expand_module) {
    m_expand_module = expand_module;
    m_ast = Term::Create(TermID::END, "");
    //    m_ast->SetSource(std::make_shared<std::string>(input));

    Scanner scanner(m_ctx, src);

    lexer = &scanner;

    parser parser(*this);
    if (parser.parse() != 0) {
        lexer = nullptr;
        return m_ast;
    }

    // Исходники требуются для вывода информации ошибках во время анализа типов
    lexer = nullptr;
    return m_ast;
}

void Parser::error(const std::string& m) {
    if (lexer) {
        size_t off = static_cast<size_t>(std::max(1, lexer->tokenStartOffset()));
        auto loc = trust::MapperLocation::makeLoc(lexer->m_srcIdx, off);
        m_ctx.diag().report(Severity::Error, trust::MapperRange(loc, loc), "{}", m);
    }
}

void trust::parser::error(const std::string& msg) {
    driver.error(msg);
}

TermPtr Parser::GetAst() {
    return m_ast;
}

void Parser::AstAddTerm(TermPtr term) {
    ASSERT(m_ast);
    if (m_ast->m_id == TermID::END) {
        m_ast = term;
    } else {
        if (m_ast->m_id != TermID::SEQUENCE) {
            TermPtr temp = Term::Create(TermID::SEQUENCE, "", m_ast->m_mapperRange);
            m_ast.swap(temp);
            m_ast->m_block.push_back(temp);
        }
        if (term->m_id == TermID::SEQUENCE) {
            m_ast->m_block.insert(m_ast->m_block.end(), term->m_block.begin(), term->m_block.end());
        } else {
            m_ast->m_block.push_back(term);
        }
    }
}

__attribute__((weak)) TermPtr trust::ProcessMacro(Parser&, TermPtr& term) {
    return term;
}

__attribute__((weak)) ExpandMacroResult trust::ExpandTermMacro(Parser& parser) {
    return ExpandMacroResult::Break;
}

// TermPtr Parser::MacroEval(const TermPtr &term) {
//
//     if (term->m_bracket_depth) {
//         NL_PARSER(&m_ctx, term, "Macro definitions allowed at the top level only, not inside conditions, namespace or any brackets!");
//     }
//
//     if (m_macro) {
//         return m_macro->EvalOpMacros(term);
//     }
//     return term;
// }

bool Parser::PragmaCheck(const TermPtr& term) {
    if (term && term->getText().size() > 5 && term->getText().find("@__") == 0 && term->getText().rfind("__") == term->getText().size() - 2) {
        return !CheckPredefMacro(term);
    }
    return false;
}

bool Parser::PragmaEval(const TermPtr& term, BlockType& buffer, BlockType& seq) {
    /*
     *
     * https://javarush.com/groups/posts/1896-java-annotacii-chto-ehto-i-kak-ehtim-poljhzovatjhsja
     * https://habr.com/ru/companies/otus/articles/764244/
     *
        @__PRAGMA_DIAG__(push)
        @__PRAGMA_DIAG__(ignored, "-Wundef")
        @__PRAGMA_DIAG__(warning, "-Wformat" , "-Wundef", "-Wuninitialized")
        @__PRAGMA_DIAG__(error, "-Wuninitialized")
        @__PRAGMA_DIAG__(pop)

     @__PRAGMA_DIAG__(once, "-Wuninitialized") ??????????????

        #pragma message "Compiling " __FILE__ "..."
        @__PRAGMA_MESSAGE__("Compiling ", __FILE__, "...")


    #define DO_PRAGMA(x) _Pragma (#x)
    #define TODO(x) DO_PRAGMA(message ("TODO - " #x))

    @@TODO( ... )@@ := @__PRAGMA_MESSAGE__("TODO - ", @#...)

    @TODO(Remember to fix this)  # note: TODO - Remember to fix this

    \\.__lexer__ignore_space__ = 1;
    \\.__lexer__ignore_indent__ = 1;
    \\.__lexer__ignore_comment__ = 1;
    \\.__lexer__ignore_crlf__ = 1;
     *
     *  @__PRAGMA_MACRO__(push)
     *  @__PRAGMA_MACRO__(push, if, this)
     *  @__PRAGMA_MACRO__(pop)
     *  @__PRAGMA_MACRO__(pop, if, this)
     *
     *  @__PRAGMA_MACRO_COND__(ndef, if)  @__PRAGMA_ERROR__("Macro if not defined!")
     *  @__PRAGMA_MACRO_COND__(lt, __VERSION__, 0.5)  @__PRAGMA_ERROR__("This functional supported since version 0.5 only!")
     */

    static const char* __PRAGMA_TYPE_DEFINE__ = "@__PRAGMA_TYPE_DEFINE__";
    static const char* __PRAGMA_IGNORE__ = "@__PRAGMA_IGNORE__";
    static const char* __PRAGMA_MACRO__ = "@__PRAGMA_MACRO__";
    static const char* __PRAGMA_MACRO_COND__ = "@__PRAGMA_MACRO_COND__";

    static const char* __PRAGMA_MESSAGE__ = "@__PRAGMA_MESSAGE__";
    static const char* __PRAGMA_WARNING__ = "@__PRAGMA_WARNING__";
    static const char* __PRAGMA_ERROR__ = "@__PRAGMA_ERROR__";

    static const char* __PRAGMA_EXPECTED__ = "@__PRAGMA_EXPECTED__";
    static const char* __PRAGMA_UNEXPECTED__ = "@__PRAGMA_UNEXPECTED__";
    static const char* __PRAGMA_FINALIZE__ = "@__PRAGMA_FINALIZE__";

    static const char* __PRAGMA_NO_MACRO__ = "@__PRAGMA_NO_MACRO__";

    // TODO(cleanup): unused — only for m_annotation pragmas, commented out, see task 1785678668891
    // static const char* __ANNOTATION_SET__ = "@__ANNOTATION_SET__";
    // static const char* __ANNOTATION_CHECK__ = "@__ANNOTATION_CHECK__";
    // static const char* __ANNOTATION_IIF__ = "@__ANNOTATION_IIF__";

    ASSERT(term);
    if (term->getText().compare("@__HYGIENIC__") == 0) {

        if (term->size() != 1) {
            m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "@__HYGIENIC__ expects exactly one argument!");
        }

        std::string ident(term->at(0).second->getText());

        auto iter = m_hygienic_names.find(ident);
        std::string gen;
        if (iter != m_hygienic_names.end()) {
            gen = iter->second;
        } else {
            m_hygienic_counter++;
            gen = ident + "__G" + std::to_string(m_hygienic_counter) + "_";
            m_hygienic_names.insert({ident, gen});
        }

        buffer.insert(buffer.begin(), Term::Create(TermID::NAME, gen, {}, parser::token_type::NAME));
        return true;

    } else if (term->getText().compare("@__OPTION__") == 0) {

        if (term->size() != 2) {
            m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "@__OPTION__ expects exactly two arguments!");
        }

        std::string name(term->at(0).second->getText());
        std::string sev_name(term->at(1).second->getText());

        if (!m_ctx.opts().is_registered(name)) {
            m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Unknown option '{}' in @__OPTION__!", name);
        }

        auto sev = Options::parseSeverityName(sev_name);
        if (!sev.has_value() && sev_name != "ignore") {
            m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Unknown status '{}' in @__OPTION__!", sev_name);
        }

        m_ctx.opts().set(name, sev);
        return true;

    } else if (term->getText().compare("@__OPTION_PUSH__") == 0) {

        m_ctx.opts().push();
        return true;

    } else if (term->getText().compare("@__OPTION_POP__") == 0) {

        try {
            m_ctx.opts().pop();
        } catch (const std::runtime_error& e) {
            m_ctx.diag().report(Severity::Error, term->m_mapperRange, "{}", e.what());
        }
        return true;

    } else if (term->getText().compare(__PRAGMA_TYPE_DEFINE__) == 0) {

        if (term->size() != 1 || !term->at(1).first.empty()) {
            m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Expected argument in pragma macro '{}'", term->toString());
        }

        if (term->at(1).first.empty() && term->at(1).second->getTermID() == TermID::INTEGER) {
            return true;
        }

    } else if (term->getText().compare(__PRAGMA_MESSAGE__) == 0 || term->getText().compare(__PRAGMA_WARNING__) == 0 ||
               term->getText().compare(__PRAGMA_ERROR__) == 0) {

        std::string message;
        for (int i = 0; i < term->size(); i++) {
            message += term->at(i).second->getText();
        }

        if (term->getText().compare(__PRAGMA_MESSAGE__) == 0) {
            fprintf(stderr, "note: %s\n", message.c_str());
        } else if (term->getText().compare(__PRAGMA_WARNING__) == 0) {
            fprintf(stderr, "W: %s\n", message.c_str());
        } else {
            ASSERT(term->getText().compare(__PRAGMA_ERROR__) == 0);
            throw ParserError("error: %s", message.c_str());
        }

    } else if (term->getText().compare(__PRAGMA_IGNORE__) == 0) {

        throw ParserError("Pragma @__PRAGMA_IGNORE__ not implemented!");

        static const char* ignore_space = "space";
        static const char* ignore_indent = "indent";
        static const char* ignore_comment = "comment";
        static const char* ignore_crlf = "crlf";

    } else if (term->getText().compare(__PRAGMA_MACRO__) == 0) {

        if (term->size() == 0) {
            m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Expected argument in pragma macro '{}'", term->toString());
        }

        if (term->at(0).second->getText().compare("push") == 0) {

            throw ParserError("Pragma push not implemented!");
            //            m_macro->Push(term);
            return true;

        } else if (term->at(0).second->getText().compare("pop") == 0) {

            throw ParserError("Pragma pop not implemented!");
            //            m_macro->Pop(term);
            return true;

        } else {
            m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Pragma macro '{}' not recognized!", term->toString());
        }

    } else if (term->getText().compare(__PRAGMA_MACRO_COND__) == 0) {

        if (term->size() == 0) {
            m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Expected argument in pragma macro '{}'", term->toString());
        }

        throw ParserError("Pragma @__PRAGMA_MACRO_COND__ not implemented!");

        // TODO(cleanup): m_expected/m_unexpected/m_finalize unused — commented out, see task 1785675437901
        // } else if (term->getText().compare(__PRAGMA_EXPECTED__) == 0) {
        //
        //     m_expected = term;
        //
        // } else if (term->getText().compare(__PRAGMA_UNEXPECTED__) == 0) {
        //
        //     m_unexpected = term;
        //
        // } else if (term->getText().compare(__PRAGMA_FINALIZE__) == 0) {
        //
        //     if (m_finalize || m_finalize_counter) {
        //         throw ParserError("Nested definitions of pragma @__PRAGMA_FINALIZE__ not implemented!");
        //     }
        //
        //     m_finalize = term;
        //     m_finalize_counter = 0;
        //
    } else if (term->getText().compare(__PRAGMA_NO_MACRO__) == 0) {

        ASSERT(term->size() == 0);

        m_no_macro = true;

        // TODO(cleanup): unused — only for m_annotation pragmas, commented out, see task 1785678668891
        // } else if (term->getText().compare(__ANNOTATION_SET__) == 0) {
        //
        //     if (term->size() == 1) {
        //         // Set `name` = 1;
        //         std::string name = term->at(0).second->getText();
        //
        //         auto iter = m_annotation->find(term->at(0).second->getText());
        //         if (iter == m_annotation->end()) {
        //             m_annotation->push_back(Term::Create(TermID::INTEGER, "1", parser::token_type::INTEGER, 1, term->m_mapperRange), name);
        //         } else {
        //             //                iter->second =
        //             m_annotation->push_back(Term::Create(TermID::INTEGER, "1", parser::token_type::INTEGER, 1, term->m_mapperRange), name);
        //         }
        //
        //     } else if (term->size() == 2) {
        //         // Set `name` = value;
        //         m_annotation->push_back(term->at(1).second, term->at(0).second->getText());
        //     } else {
        //         m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Annotation args in '{}' not recognized!", term->toString());
        //     }
        //
        //     //        LOG_DEBUG("ANNOT: %s", m_annotation->toString().c_str());
        //
        // } else if (term->getText().compare(__ANNOTATION_CHECK__) == 0) {
        //
        //     ASSERT(m_annotation);
        //
        //     throw ParserError("Pragma __ANNOTATION_CHECK__ not implemented!");
        //
        //     //        if (term->size() != 3) {
        //     //            NL_PARSER(&m_ctx, term, "Annotation IIF must have three arguments!");
        //     //        }
        //     //
        //     //        //        LOG_DEBUG("Annot %s %d", m_annotation->toString().c_str(), (int) m_annotation->size());
        //     //
        //     //        auto iter = m_annotation->find(term->at(0).second->getText());
        //     //        if (iter == m_annotation->end() || iter->second->getText().empty() || iter->second->getText().compare("0") == 0) {
        //     //            buffer.insert(buffer.begin(), term->at(2).second);
        //     //        } else {
        //     //            buffer.insert(buffer.begin(), term->at(1).second);
        //     //        }
        //
        // } else if (term->getText().compare(__ANNOTATION_IIF__) == 0) {
        //
        //     ASSERT(m_annotation);
        //
        //     if (term->size() != 3) {
        //         m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Annotation IIF must have three arguments!");
        //     }
        //
        //     //        LOG_DEBUG("Annot %s %d", m_annotation->toString().c_str(), (int) m_annotation->size());
        //
        //     auto iter = m_annotation->find(term->at(0).second->getText());
        //     if (iter == m_annotation->end() || iter->second->getText().empty() || iter->second->getText().compare("0") == 0) {
        //         buffer.insert(buffer.begin(), term->at(2).second);
        //     } else {
        //         buffer.insert(buffer.begin(), term->at(1).second);
        //     }
        //
        // } else {
        m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Uknown pragma '{}'", term->toString());
    }
    return true;
}

bool Parser::RegisterPredefMacro(const char* name, const char* desc) {
    if (m_predef_macro.find(name) != m_predef_macro.end()) {
        fprintf(stderr, "E: Predef macro '%s' redefined!\n", name);
        return false;
    }
    m_predef_macro.insert({name, desc});
    return true;
}

void Parser::InitPredefMacro() {
    if (m_predef_macro.empty()) {

        VERIFY(RegisterPredefMacro("@__TRUST_VERSION_MAJOR__", "Major version"));
        VERIFY(RegisterPredefMacro("@__TRUST_VERSION_MINOR__", "Minor version"));
        VERIFY(RegisterPredefMacro("@__TRUST_VERSION_PATCH__", "Patch version"));
        VERIFY(RegisterPredefMacro("@__TRUST_VERSION__", "Version in format 'X.Y.Z'"));
        VERIFY(RegisterPredefMacro("@__TRUST_GIT_HASH__", "Short git hash"));
        VERIFY(RegisterPredefMacro("@__TRUST_VERSION_FULL__", "Version with git hash"));
        VERIFY(RegisterPredefMacro("@__TRUST_DATE_BUILD__", "Date build"));

        VERIFY(RegisterPredefMacro("@__FILE__", "Current file name"));
        VERIFY(RegisterPredefMacro("@__FILE_NAME__", "Current file name"));

        VERIFY(RegisterPredefMacro("@__CLASS__", "Current class name"));
        VERIFY(RegisterPredefMacro("@__NAMESPACE__", "Current namespace"));
        VERIFY(RegisterPredefMacro("@__FUNCTION__", "Current function name"));
        VERIFY(RegisterPredefMacro("@__FUNCDNAME__", "Decorated of current function name"));
        VERIFY(RegisterPredefMacro("@__FUNCSIG__", "Signature of current function"));
        //        VERIFY(RegisterPredefMacro("@__FUNC_BLOCK__", "Full namespace function name"));

        VERIFY(RegisterPredefMacro("@__LINE__", "Line number in current file"));
        VERIFY(RegisterPredefMacro("@__FILE_LINE__", "Line number in current file"));

        VERIFY(RegisterPredefMacro("@__FILE_MD5__", "MD5 hash for current file"));
        VERIFY(RegisterPredefMacro("@__FILE_TIMESTAMP__", "Timestamp current file"));

        VERIFY(RegisterPredefMacro("@__DATE__", "Current date"));
        VERIFY(RegisterPredefMacro("@__TIME__", "Current time"));
        // определяется как строковый литерал, содержащий дату и время последнего изменения текущего исходного файла
        // в сокращенной форме с постоянной длиной, которые возвращаются функцией asctime библиотеки CRT,
        // например: Fri 19 Aug 13:32:58 2016. Этот макрос определяется всегда.
        VERIFY(RegisterPredefMacro("@__TIMESTAMP__", "Current timestamp"));
        VERIFY(RegisterPredefMacro("@__TIMESTAMP_ISO__", "Current timestamp as ISO format")); // 2013-07-06T00:50:06Z

        // Развертывается до целочисленного литерала, начинающегося с 0.
        // Значение увеличивается на 1 каждый раз, когда используется в файле исходного кода или во включенных заголовках файла исходного кода.
        VERIFY(RegisterPredefMacro("@__COUNTER__", "Monotonically increasing counter from zero"));

        VERIFY(RegisterPredefMacro("@::", "Full name of the current namespace"));
        VERIFY(RegisterPredefMacro("$\\\\", "Full name of the current module name"));
        VERIFY(RegisterPredefMacro("@\\\\", "Root directory with the main program module"));
    }
}

bool Parser::CheckPredefMacro(const TermPtr& term) {
    if (term->m_id != TermID::NAME) {
        return false;
    }

    std::string_view text = term->getText();
    if (text.find("@") == 0) {
        text.remove_prefix(1);
    }

    return m_predef_macro.find(text.begin()) != m_predef_macro.end();
}

std::string Parser::GetCurrentDate(time_t ts) {
    std::string buf("Jul 27 2012");
    strftime(buf.data(), buf.size(), "%b %e %Y", localtime(&ts));
    return buf;
}

std::string Parser::GetCurrentTime(time_t ts) {
    std::string buf("07:07:09");
    strftime(buf.data(), buf.size(), "%T", localtime(&ts));
    return buf;
}

std::string Parser::GetCurrentTimeStamp(time_t ts) {
    std::string result = asctime(localtime(&ts));
    result = result.substr(0, 24); // Remove \n on the end line
    return result;
}

std::string Parser::GetCurrentTimeStampISO(time_t ts) {
    std::string buf("2011-10-08T07:07:09Z");
    strftime(buf.data(), buf.size(), "%FT%TZ", localtime(&ts));
    return buf;
}

parser::token_type Parser::ExpandPredefMacro(TermPtr& term) {

    InitPredefMacro();

    if (!term) {
        throw ParserError("Environment variable not defined!");
    }
    if (term->m_id != TermID::MACRO) {
        return term->m_lexer_type;
    }

    std::string_view text = term->getText();
    //    if (text.find("@") == 0) {
    //        text.remove_prefix(1);
    //    }

    ASSERT(!m_predef_macro.empty());
    if (m_predef_macro.find(text.begin()) == m_predef_macro.end()) {
        return term->m_lexer_type;
    }

    const TermID str_type = TermID::STRWIDE;
    const parser::token_type str_token = parser::token_type::STRWIDE;

    if (text.compare("@__COUNTER__") == 0) {

        term->m_id = TermID::INTEGER;
        term->getText() = std::to_string(m_counter);
        m_counter++;
        term->m_lexer_type = parser::token_type::INTEGER;
        return term->m_lexer_type;

    } else if (text.compare("@__TRUST_VERSION_MAJOR__") == 0) {

        term->m_id = TermID::INTEGER;
        term->getText() = std::to_string(TRUST_VERSION_MAJOR);
        term->m_lexer_type = parser::token_type::INTEGER;
        return term->m_lexer_type;

    } else if (text.compare("@__TRUST_VERSION_MINOR__") == 0) {

        term->m_id = TermID::INTEGER;
        term->getText() = std::to_string(TRUST_VERSION_MINOR);
        term->m_lexer_type = parser::token_type::INTEGER;
        return term->m_lexer_type;

    } else if (text.compare("@__TRUST_VERSION_PATCH__") == 0) {

        term->m_id = TermID::INTEGER;
        term->getText() = std::to_string(TRUST_VERSION_PATCH);
        term->m_lexer_type = parser::token_type::INTEGER;
        return term->m_lexer_type;

    } else if (text.compare("@__TRUST_VERSION__") == 0) {
        term->getText() = TRUST_VERSION;
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    } else if (text.compare("@__TRUST_GIT_HASH__") == 0) {
        term->getText() = TRUST_GIT_HASH;
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    } else if (text.compare("@__TRUST_VERSION_FULL__") == 0) {
        term->getText() = TRUST_VERSION_FULL;
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    } else if (text.compare("@__TRUST_DATE_BUILD__") == 0) {
        term->getText() = TRUST_DATE_BUILD;
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    } else if (text.compare("@__LINE__") == 0 || text.compare("@__FILE_LINE__") == 0) {

        EXPECT(!term->m_mapperRange.begin.isInvalid() && "@__LINE__/@__FILE_LINE__: invalid location");
        EXPECT(term->m_mapperRange.begin.offset() > 0 && "@__LINE__/@__FILE_LINE__: zero offset (not mapped)");

        term->m_id = TermID::INTEGER;
        {
            auto lc = m_ctx.source().line_column(term->m_mapperRange.begin);
            term->getText() = std::to_string(lc.line);
        }
        term->m_lexer_type = parser::token_type::INTEGER;
        return term->m_lexer_type;

    } else if (text.compare("@__FILE__") == 0 || text.compare("@__FILE_NAME__") == 0) {

        EXPECT(!term->m_mapperRange.begin.isInvalid() && "@__FILE__/@__FILE_NAME__: invalid location");
        EXPECT(!term->m_mapperRange.begin.fileIdx().isInvalid() && "@__FILE__/@__FILE_NAME__: invalid file index");

        term->m_id = str_type;
        {
            term->getText() = std::string(m_ctx.source().filename(term->m_mapperRange.begin.fileIdx()));
        }
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    } else if (text.compare("@__FILE_TIMESTAMP__") == 0) {

        term->m_id = str_type;
        {
            std::string fname = std::string(m_ctx.source().filename(term->m_mapperRange.begin));
            struct stat st;
            if (stat(fname.c_str(), &st) == 0) {
                char time_str[64];
                struct tm* timeinfo = localtime(&st.st_mtime);
                strftime(time_str, sizeof(time_str), "%a %b %d %H:%M:%S %Y", timeinfo);
                term->getText() = std::string(time_str, 24);
            } else {
                term->getText() = "??? ??? ?? ??:??:?? ????";
            }
        }
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    } else if (text.compare("@__FILE_MD5__") == 0) {

        term->m_id = str_type;
        {
            auto fileIdx = term->m_mapperRange.begin.fileIdx();
            if (!fileIdx.isInvalid()) {
                uint64_t hash = m_ctx.source().getFileHash(fileIdx);
                char hex[17];
                snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
                term->getText() = std::string(hex);
            } else {
                term->getText() = "?????????????????????????????????";
            }
        }
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    } else if (text.compare("@__DATE__") == 0) {

        term->getText() = GetCurrentDate(m_timestamp);
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    } else if (text.compare("@__TIME__") == 0) {

        term->getText() = GetCurrentTime(m_timestamp);
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    } else if (text.compare("@__TIMESTAMP__") == 0) {

        term->getText() = GetCurrentTimeStamp(m_timestamp);
        term->getText() = term->getText().substr(0, 24); // Remove \n on the end line
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    } else if (text.compare("@__TIMESTAMP_ISO__") == 0) {

        term->getText() = GetCurrentTimeStampISO(m_timestamp);
        term->m_id = str_type;
        term->m_lexer_type = str_token;
        return term->m_lexer_type;

    } else if (text.compare("@__CLASS__") == 0 || text.compare("@__NAMESPACE__") == 0 || // text.compare("@__FUNC_BLOCK__") == 0 ||
               text.compare("@__FUNCTION__") == 0 || text.compare("@__FUNCDNAME__") == 0 || text.compare("@__FUNCSIG__") == 0) {

        term->m_id = TermID::NAMESPACE;
        term->m_lexer_type = parser::token_type::NAMESPACE;
        return term->m_lexer_type;

    } else if (text.compare("@::") == 0) {

        term->m_id = TermID::NAMESPACE;
        term->m_lexer_type = parser::token_type::NAMESPACE;
        return term->m_lexer_type;

        //        ASSERT(text.compare("@::") != 0);

    } else if (text.compare("@$$") == 0) {

        term->m_id = TermID::NAMESPACE;
        term->m_lexer_type = parser::token_type::NAMESPACE;
        return term->m_lexer_type;

        //        // Внешний блок или функция
        //        ASSERT(text.compare("@$$") != 0);

    } else if (text.compare("@\\\\") == 0) {

        term->m_id = TermID::NAME;
        {
            term->getText() = std::string(m_ctx.source().filename(term->m_mapperRange.begin));
        }
        term->m_lexer_type = parser::token_type::NAME;
        return term->m_lexer_type;

    } else if (text.compare("$\\\\") == 0) {

        term->getText() = "";
        term->m_id = TermID::MODULE;
        term->m_lexer_type = parser::token_type::MODULE;
        return term->m_lexer_type;
    }

    m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Predef macro '{}' not implemented!", term->toString());
    return term->m_lexer_type;
}

TermPtr Parser::ParseTerm(const char* proto, trust::Context& ctx, bool pragma_enable, bool macro_expand) {
    try {
        // Термин или термин + тип парсятся без ошибок
        Parser p(ctx, nullptr, pragma_enable, macro_expand);
        return p.ParseText(proto);
    } catch (std::exception&) {
        std::string func(proto);
        try {
            func += ":={}";
            Parser p(ctx, nullptr, pragma_enable, macro_expand);
            return p.ParseText(func)->m_left;
        } catch (std::exception& e) {
            throw ParserError("Fail parsing prototype '%s' as '%s'!", func.c_str(), e.what());
        }
    }
}

size_t Parser::SkipBrackets(const BlockType& buffer, const size_t offset) {

    if (offset >= buffer.size()) {
        return 0;
    }

    std::string br_end;
    if (buffer[offset]->getText().compare("(") == 0) {
        br_end = ")";
        //    } else if (buffer[offset]->getText().compare("<") == 0) {
        //        br_end = ">";
    } else if (buffer[offset]->getText().compare("[") == 0) {
        br_end = "]";
    } else {
        return 0;
    }

    size_t shift = 1;
    int count = 1;
    while (offset + shift < buffer.size()) {
        if (buffer[offset]->getText().compare(buffer[offset + shift]->getText()) == 0) {
            count++; // Next level bracket
        } else if (br_end.compare(buffer[offset + shift]->getText()) == 0) {
            count--; // // Leave level bracket
            if (count == 0) {
                return shift + 1;
            }
        }
        shift++;
    }
    throw ParserError("Closed bracket '%s' not found!", br_end.c_str());
}

// size_t Parser::SkipBrackets(const MacroBuffer& buffer, const size_t offset) {

//     if (offset >= buffer.size()) {
//         return 0;
//     }

//     std::string br_end;
//     std::string_view text = buffer[offset].GetText();
//     if (text == "(") {
//         br_end = ")";
//     } else if (text == "[") {
//         br_end = "]";
//     } else {
//         return 0;
//     }

//     size_t shift = 1;
//     int count = 1;
//     while (offset + shift < buffer.size()) {
//         std::string_view cur = buffer[offset + shift].GetText();
//         if (cur == text) {
//             count++;
//         } else if (cur == br_end) {
//             count--;
//             if (count == 0) {
//                 return shift + 1;
//             }
//         }
//         shift++;
//     }
//     return 0;
// }

size_t Parser::ParseTerm(TermPtr& result, const BlockType& buffer, trust::Context& ctx, size_t offset, bool pragma_enable, bool macro_expand) {

    if (offset >= buffer.size()) {
        throw ParserError("Fail skip count %d or buffer size %d!", static_cast<int>(offset), static_cast<int>(buffer.size()));
    }

    /* term
     * func()
     *
     * term: type
     * func(): type
     *
     * term: type[]
     * func(): type[]
     *
     */

    std::string source = buffer[offset]->toString();
    offset++;
    size_t skip = SkipBrackets(buffer, offset);

    if (skip) {
        /*
         * term
         * func()
         */
        for (size_t i = 0; i < skip; i++) {
            if (buffer[offset + i]) {
                source += buffer[offset + i]->toString();
            }
        }
        offset += skip;
    }

    if (offset + 1 < buffer.size() && buffer[offset + 1]->getText().compare(":") == 0) {
        offset++;
        source += buffer[offset]->toString();

        /*
         * term: type
         * func(): type
         */

        if (offset + 1 >= buffer.size()) {
            throw ParserError("Typename missing!");
        }

        offset++;
        source += buffer[offset]->toString();

        skip = SkipBrackets(buffer, offset + 1);
        if (skip) {
            /*
             * term: type[]
             * func(): type[]
             *
             */
            for (size_t i = 0; i < skip; i++) {
                source += buffer[offset + i]->toString();
            }

            offset += (skip + 1);
        }
    }
    //        LOG_DEBUG("ParseTerm: '%s' - %d", source.c_str(), (int) offset);
    result = ParseTerm(source.c_str(), ctx, pragma_enable, macro_expand);
    return offset;
}

TermPtr Parser::CheckModuleTerm(const TermPtr& term) {
    if (!isModuleName(term->getText())) {
        return term;
    }
    if (!CheckCharModuleName(term->getText())) {
        m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Module name - backslash, underscore, lowercase English letters or number!");
    }
    if (!m_expand_module) {
        return term;
    }
    if (term->isCall()) {
        // Загрузка модуля: \module(func) — рекурсивный вызов парсера.
        (void)m_ctx.loader().ensureLoaded(term->getText(), term->m_mapperRange);
    } else {
        // Обращение \module::var — не грузим, а проверяем факт загрузки модуля.
        auto idx = m_ctx.loader().indexOf(term->getText());
        if (!idx || !m_ctx.loader().isLoaded(*idx)) {
            m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Module '{}' is not loaded", term->getText());
        }
    }
    return term;
}

parser::token_type Parser::GetNextToken(TermPtr* yylval) {

    parser::token_type result;

    ASSERT(yylval);

    /*
     * Новая логика работы парсера.
     *
     * Термины считываются из лексера до символа ';' или до конца файла.
     * Каждый термин буфера проверяется на макрос и если находится - заменяется.
     * Для обычного макроса просто заменяются токены и тела макроса,
     * а для текстового макроса используется отдельный буфер до завершения работы лексичсекского анализатора,
     * после чего этот буфер вставляется на место макроса в основонм бефере данных.
     * После обработки всего буфера его элеменеты передаются в парсер для обработки.
     */

    parser::token_type type;
    bool lexer_complete;

go_parse_string:

    // TODO(cleanup): m_is_runing unused — commented out, see task 1785675437901
    // m_is_runing = true;

    TermPtr term;
    bool is_escape = false;

    if (m_macro_analisys_buff.empty()) {

        lexer_complete = false;

        while (!m_is_lexer_complete) {

        next_escape_token:

            term = Term::Create(TermID::END, "");
            {
                type = lexer->lex(&term);
            }

            ASSERT(type == term->m_lexer_type);

            if (is_escape) {

                if (type == parser::token_type::END) {
                    m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Unexpected end of file '{}'", term->toString());
                }
                is_escape = false;
                term->m_id = TermID::ESCAPE;
                term->m_lexer_type = parser::token_type::ESCAPE;
                type = parser::token_type::ESCAPE;

            } else if (type == parser::token_type::ESCAPE) {

                is_escape = true;
                goto next_escape_token;
            }

            //            if (type == parser::token_type::MACRO && term->getText().compare("@::") == 0) {
            //
            //                type = parser::token_type::NAMESPACE;
            //                term->m_lexer_type = type;
            //                term->m_id = TermID::NAMESPACE;
            //                term->getText() = m_ns_stack.NamespaceCurrent();
            //
            //            } else

            //            if (type == MACRO_MODULE) {
            //
            //                type = parser::token_type::MODULE;
            //                term->m_lexer_type = type;
            //                term->m_id = TermID::MODULE;
            //                term->getText() = GetCurrentModule();
            //
            //            }

            //            if (m_next_string != static_cast<uint8_t> (TermID::NONE)) {
            //
            //                if (m_next_string == static_cast<uint8_t> (TermID::STRCHAR)) {
            //                    type = parser::token_type::STRCHAR;
            //                } else {
            //                    ASSERT(m_next_string == static_cast<uint8_t> (TermID::STRWIDE));
            //                    type = parser::token_type::STRWIDE;
            //                }
            //
            //                term->m_lexer_type = type;
            //                term->m_id = static_cast<TermID> (m_next_string);
            //
            //                m_next_string = static_cast<uint8_t> (TermID::NONE);
            //
            //            } else if (type == parser::token_type::MACRO_TOSTR && lexer->m_macro_count == 0) {
            //
            //                if (term->getText().compare("@#\"") == 0) {
            //                    m_next_string = static_cast<uint8_t> (TermID::STRWIDE);
            //                } else if (term->getText().compare("@#'") == 0) {
            //                    m_next_string = static_cast<uint8_t> (TermID::STRCHAR);
            //                } else {
            //                    ASSERT(term->getText().compare("@#") == 0);
            //                    //@todo Set string type default by global settings
            //                    m_next_string = static_cast<uint8_t> (TermID::STRWIDE);
            //                }
            //
            //                goto next_escape_token;
            //            }

            if (type == parser::token_type::END) {
                m_is_lexer_complete = true;
                //                if(lexer->m_data.empty()) {
                //                    m_is_lexer_complete = true;
                //                } else {
                //                    lexer->yypop_buffer_state();
                //                    *lexer->m_loc = lexer->m_data[lexer->m_data.size() - 1].loc;
                //
                //                    delete lexer->m_data[lexer->m_data.size() - 1].iss;
                //                    lexer->m_data.pop_back();
                //
                //                    if(lexer->m_data.empty()) {
                //                        lexer->source_string = lexer->source_base;
                //                    } else {
                //                        lexer->source_string = lexer->m_data[lexer->m_data.size() - 1].data;
                //                    }
                //                    continue;
                //                }
            } else if (lexer->m_macro_count == 1) {

                ASSERT(type == parser::token_type::MACRO_SEQ);

                if (lexer->m_macro_del) {
                    m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Invalid token '{}' at given position!", term->getText());
                }

                lexer->m_macro_count = 2;
                lexer->m_macro_body = term;
                continue;

            } else if (lexer->m_macro_count == 3) {

                ASSERT(lexer->m_macro_body);
                ASSERT(type == parser::token_type::MACRO_SEQ);

                lexer->m_macro_count = 0;
                term.swap(lexer->m_macro_body);
                lexer->m_macro_body = nullptr;

            } else if (lexer->m_macro_count == 2 || lexer->m_macro_del == 2) {

                ASSERT(lexer->m_macro_body);

                if (lexer->m_macro_del && !(type == parser::token_type::NAME || type == parser::token_type::LOCAL)) {
                    m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Invalid token '{}' at given position!", term->getText());
                }

                lexer->m_macro_body->m_block.push_back(term);
                continue;

            } else if (lexer->m_macro_del == 1) {

                ASSERT(type == parser::token_type::MACRO_DEL);

                if (lexer->m_macro_count) {
                    m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Invalid token '{}' at given position!", term->getText());
                }

                lexer->m_macro_del = 2;
                lexer->m_macro_body = term;
                continue;

            } else if (lexer->m_macro_del == 3) {

                ASSERT(type == parser::token_type::MACRO_DEL);
                if (!lexer->m_macro_body) {
                    ASSERT(lexer->m_macro_body);
                }

                if (lexer->m_macro_body->m_block.empty()) {
                    m_ctx.diag().report(Severity::Fatal, term->m_mapperRange, "Empty sequence not allowed!");
                }

                lexer->m_macro_del = 0;

                if (lexer->m_macro_body) {
                    term.swap(lexer->m_macro_body);
                }
                lexer->m_macro_body = nullptr;
            }

            m_macro_analisys_buff.push_back(term);
            if (term->getText().compare(";") == 0) {
                lexer_complete = true;
                break;
            }

            //            if(lexer_complete && lexer->m_macro_iss == nullptr) {
            //                break;
            //            }
        }

        if (lexer->m_macro_del || lexer->m_macro_count) {
            TermPtr bag_position;

            bag_position = (m_macro_analisys_buff.size() > 1) ? m_macro_analisys_buff[m_macro_analisys_buff.size() - 2] : term;
            if (!bag_position) {
                ASSERT(bag_position);
            }

            m_ctx.diag().report(Severity::Fatal, bag_position->m_mapperRange, "Incomplete syntax near '{}' in file {}!", bag_position->getText(),
                                m_ctx.source().filename(term->m_mapperRange.begin));
        }
    }

    TermPtr pragma;
    while (lexer->m_macro_count == 0 && !m_macro_analisys_buff.empty()) {

        if (m_enable_pragma) {

            ExpandPredefMacro(m_macro_analisys_buff[0]);

            // Обработка команд парсера @__PRAGMA ... __
            if (PragmaCheck(m_macro_analisys_buff[0])) {

                size_t size;
                size = Parser::ParseTerm(pragma, m_macro_analisys_buff, m_ctx, 0, false);

                ASSERT(size);
                ASSERT(pragma);

                BlockType temp(m_macro_analisys_buff.begin(), m_macro_analisys_buff.begin() + size);

                m_macro_analisys_buff.erase(m_macro_analisys_buff.begin(), m_macro_analisys_buff.begin() + size);

                //                while (!m_macro_analisys_buff.empty() && m_macro_analisys_buff[0]->getText().compare(";") == 0) {
                //                    LOG_DEBUG("Erase '%s' !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!", m_macro_analisys_buff[0]->toString().c_str());
                //                    m_macro_analisys_buff.erase(m_macro_analisys_buff.begin());
                //                }

                PragmaEval(pragma, m_macro_analisys_buff, temp);
                continue;
            }
        }

        // TODO(cleanup): m_expected/m_unexpected unused — commented out, see task 1785675437901
        // // Обработка команды проверка следующего термина @__PRAGMA_EXPECTED__
        // if (m_expected) {
        //     for (int i = 0; i < m_expected->size(); i++) {
        //         if (m_macro_analisys_buff[0]->getText().compare(m_expected->at(i).second->getText()) == 0) {
        //             m_expected.reset();
        //             break;
        //         }
        //     }
        //     if (m_expected) {
        //         std::string msg;
        //         for (int i = 0; i < m_expected->size(); i++) {
        //             if (!msg.empty()) {
        //                 msg += ", ";
        //             }
        //             msg += "'";
        //             msg += m_expected->at(i).second->getText();
        //             msg += "'";
        //         }
        //         m_ctx.diag().report(Severity::Error, m_macro_analisys_buff[0]->m_mapperRange, "Term {} expected!", msg);
        //     }
        // }

        // // Обработка команды проверка следующего термина @__PRAGMA_UNEXPECTED__
        // if (m_unexpected) {
        //     for (int i = 0; i < m_unexpected->size(); i++) {
        //         if (m_macro_analisys_buff[0]->getText().compare(m_unexpected->at(i).second->getText()) == 0) {
        //             m_ctx.diag().report(Severity::Error, m_macro_analisys_buff[0]->m_mapperRange, "Term '{}' unexpected!",
        //             m_macro_analisys_buff[0]->getText());
        //         }
        //     }
        //     m_unexpected.reset();
        // }

        if (m_no_macro) {
            m_no_macro = false;
            break;
        }

        switch (ExpandTermMacro(*this)) {
        case ExpandMacroResult::Continue:;
            continue;
        case ExpandMacroResult::Goto:;
            goto go_parse_string;
        default:
            break;
        };
        //        if (m_macro) {
        //
        //            // Макрос должне начинаться всегда с термина
        //            if (!(m_macro_analisys_buff[0]->getTermID() == TermID::MACRO || m_macro_analisys_buff[0]->getTermID() == TermID::NAME)) {
        //                break;
        //            }
        //
        //            TermPtr macro_done = nullptr;
        //
        //            // Итератор для списка макросов, один из которых может соответствовать текущему буферу (по первому термину буфера)
        //            Macro::iterator found = m_macro->map::find(Macro::toMacroHash(m_macro_analisys_buff[0]));
        //
        //            if (found == m_macro->end()) {
        //
        //                //                // Если макрос не найден - ошибка
        //                //                if(isMacro(m_macro_analisys_buff[0]->getText())) {
        //                //                    throw ParserError("Macro '%s' not found!", m_macro_analisys_buff[0]->toString().c_str());
        //                //                }
        //
        //                break;
        //            }
        //
        //            macro_done.reset();
        //            // Перебрать все макросы и сравнить с буфером
        //            for (auto iter = found->second.begin(); iter != found->second.end(); ++iter) {
        //
        //                if (Macro::IdentityMacro(m_macro_analisys_buff, *iter)) {
        //
        //                    if (macro_done) {
        //                        throw ParserError("Macro duplication %s and '%s'!", macro_done->toString().c_str(), (*iter)->toString().c_str());
        //                    }
        //                    macro_done = *iter;
        //                }
        //            }
        //
        //            ASSERT(found != m_macro->end());
        //
        //            if (macro_done) {
        //
        //                counter++;
        //                if (counter > 100) {
        //                    throw ParserError("Macro expansion '%s' stack overflow?", macro_done->toString().c_str());
        //                }
        //
        //                //                compare_macro = true; // Раскрывать макросы в теле раскрытого макроса
        //
        //                ASSERT(m_macro_analisys_buff.size() >= macro_done->m_block.size());
        //                ASSERT(macro_done->m_right);
        //
        //                Macro::MacroArgsType macro_args;
        //                size_t size_remove = Macro::ExtractArgs(m_macro_analisys_buff, macro_done, macro_args);
        //
        //                //                LOG_TEST_DUMP("buffer '%s' DumpArgs: %s", Macro::Dump(m_macro_analisys_buff).c_str(),
        //                Macro::Dump(macro_args).c_str());
        //
        //
        //                ASSERT(size_remove);
        //                ASSERT(size_remove <= m_macro_analisys_buff.size());
        //
        //                std::string temp = "";
        //                for (auto &elem : m_macro_analisys_buff) {
        //                    if (!temp.empty()) {
        //                        temp += " ";
        //                    }
        //                    temp += elem->getText();
        //                    temp += ":";
        //                    temp += toString(elem->m_id);
        //                }
        //                //                LOG_TEST("From: %s (remove %d)", temp.c_str(), (int) size_remove);
        //
        //                m_macro_analisys_buff.erase(m_macro_analisys_buff.begin(), m_macro_analisys_buff.begin() + size_remove);
        //
        //                if (macro_done->m_right->getTermID() == TermID::MACRO_STR) {
        //
        //                    std::string macro_str = Macro::ExpandString(macro_done, macro_args);
        //                    lexer->source_string = std::make_shared<std::string>(Macro::ExpandString(macro_done, macro_args));
        //                    lexer->m_macro_iss = new std::istringstream(*lexer->source_string);
        //                    lexer->m_macro_loc = *lexer->m_loc; // save
        //                    lexer->m_loc->initialize();
        //                    lexer->yypush_buffer_state(lexer->yy_create_buffer(lexer->m_macro_iss, lexer->source_string->size()));
        //
        //                    //                    if(lexer->m_data.size() > 100) {
        //                    //                        throw ParserError("Macro expansion '%s' stack overflow?", macro_done->toString().c_str());
        //                    //                    }
        //                    //
        //                    //                    std::string macro_str = MacroBuffer::ExpandString(macro_done, macro_args);
        //                    //                    lexer->source_string = std::make_shared<std::string>(MacroBuffer::ExpandString(macro_done, macro_args));
        //                    //                    lexer->m_data.push_back({lexer->source_string, new std::istringstream(*lexer->source_string),
        //                    *lexer->m_loc});
        //                    //                    lexer->m_loc->initialize();
        //                    //                    lexer->yypush_buffer_state(lexer->yy_create_buffer(lexer->m_data[lexer->m_data.size() - 1].iss,
        //                    lexer->source_string->size()));
        //
        //                    m_is_lexer_complete = false;
        //                    goto go_parse_string;
        //
        //                } else {
        //
        //                    ASSERT(macro_done->m_right);
        //                    BlockType macro_block = Macro::ExpandMacros(macro_done, macro_args);
        //                    m_macro_analisys_buff.insert(m_macro_analisys_buff.begin(), macro_block.begin(), macro_block.end());
        //
        //                    std::string temp = "";
        //                    for (auto &elem : m_macro_analisys_buff) {
        //                        if (!temp.empty()) {
        //                            temp += " ";
        //                        }
        //                        temp += elem->getText();
        //                        temp += ":";
        //                        temp += toString(elem->m_id);
        //                    }
        //                    //                    LOG_TEST("To: %s", temp.c_str());
        //                }
        //
        //                continue;
        //
        //            } else {
        //                //                if(m_macro_analisys_buff[0]->getTermID() == TermID::MACRO) { // || found != m_macro->end()
        //
        //                throw ParserError("Macro mapping '%s' not found!\nThe following macro mapping are available:\n%s",
        //                        m_macro_analisys_buff[0]->toString().c_str(),
        //                        m_macro->GetMacroMaping(Macro::toMacroHash(m_macro_analisys_buff[0]), "\n").c_str());
        //
        //                //                }
        //            }
        //        }

        break;
    }

    // LOG_DEBUG("LexerToken count %d", (int) m_prep_buff.size());

    if (!m_macro_analisys_buff.empty()) {

        //        if (m_macro_analisys_buff[0]->m_id == TermID::END) {
        //            *yylval = nullptr;
        //            return parser::token_type::END;
        //        }

        //        if (m_macro_analisys_buff.at(0)->m_id) {
        //        LOG_DEBUG("%d  %s", (int)m_prep_buff.at(0)->m_lexer_type, m_prep_buff.at(0)->getText().c_str());

        TermPtr current = m_macro_analisys_buff.at(0);
        result = current->m_lexer_type;

        //        LOG_TEST("Token (%d=%s): '%s'", result, toString(current->m_id), current->getText().c_str());

        if (m_postlex) {

            m_postlex->push_back(current->getText());

            // Раскрыть последовательность токенов, т.к. они собираются в термин в лексере, а не парсере
            if (current->getTermID() == TermID::MACRO_SEQ || current->getTermID() == TermID::MACRO_DEL) {
                for (int i = 0; i < current->m_block.size(); i++) {
                    m_postlex->push_back(current->m_block[i]->getText());
                }
                m_postlex->push_back(current->getText());
            }
        }
        //        }

        *yylval = std::move(current);
        m_macro_analisys_buff.erase(m_macro_analisys_buff.begin());
        return result;
    }

    *yylval = nullptr;

    return parser::token_type::END;
}

///*
// *
// * Parser - парсинг для строк, а модули в AST как отдельные термины
// * FileParser - парсинг для файлов, а модули - поиск и загрузка файлов + раскрытие в AST
// *
// */
// parser::token_type FileParser::ExpandPredefMacro(TermPtr &term) {
//
//    if (term && term->m_id == TermID::MACRO) {
//
//        std::string_view text(term->getText());
//        const TermID str_type = TermID::STRWIDE;
//        const parser::token_type str_token = parser::token_type::STRWIDE;
//
//        if (text.compare("@__FILE_TIMESTAMP__") == 0) {
//
//            term->m_id = str_type;
//            if (!m_file_time.empty()) {
//                term->getText() = m_file_time;
//            } else {
//                term->getText() = "??? ??? ?? ??:??:?? ????";
//            }
//            term->m_lexer_type = str_token;
//            return term->m_lexer_type;
//
//        } else if (text.compare("@__FILE_MD5__") == 0) {
//
//            term->m_id = str_type;
//            if (!m_file_md5.empty()) {
//                term->getText() = m_file_md5;
//            } else {
//                term->getText() = "?????????????????????????????????";
//            }
//            term->m_lexer_type = str_token;
//            return term->m_lexer_type;
//
//        } else if (text.compare("@\\\\") == 0) {
//
//            //            if (m_rt) {
//            //                term->getText() = m_rt->m_exec_dir;
//            //            }
//            term->m_id = TermID::NAME;
//            term->m_lexer_type = parser::token_type::NAME;
//            return term->m_lexer_type;
//
//        } else if (text.compare("$\\\\") == 0) {
//
//            term->getText() = "FIX MODULE NAME"; //GetCurrentModule();
//            term->m_id = TermID::MODULE;
//            term->m_lexer_type = parser::token_type::MODULE;
//            return term->m_lexer_type;
//
//        }
//    }
//
//    return Parser::ExpandPredefMacro(term);
//}
//