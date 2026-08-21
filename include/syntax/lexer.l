/*** Flex Declarations and Options ***/


/* enable c++ scanner class generation */
%option c++

%option 8bit

/* change the name of the scanner class. results in "ExampleFlexLexer" */
%option prefix="NewLang"

/* the manual says "somewhat more optimized" */
%option batch

/* enable scanner to generate debug output. disable this for release
 * versions. */
%option debug
%option warn

/* no support for include files is planned */
%option yywrap nounput 

/* enables the use of start condition stacks */
%option stack


%{ /*** C/C++ Declarations ***/

#include "syntax/lexer.h"

#include "syntax/types.h"
#include "syntax/term.h"

using namespace trust;

/* import the parser's token type into a local typedef */
typedef trust::parser::token token;
typedef trust::parser::token_type token_type;

/* By default yylex returns int, we use token_type. Unfortunately yyterminate
 * by default returns 0, which is not of token_type. */
#define yyterminate() return token::END

/* This disables inclusion of unistd.h, which is not available under Visual C++
 * on Win32. The C++ scanner uses STL streams instead. */
#define YY_NO_UNISTD_H

/* Отслеживает абсолютную позицию в исходном тексте после каждого токена.
 * Выполняется перед действием каждого правила, после того, как yytext и yyleng установлены. */
#define YY_USER_ACTION m_current_pos += yyleng;

/* Создаёт Term с позицией в исходном коде */
/* Важно: m_current_pos - 0-based (используется и как индекс в исходный текст).
 * Диапазон для source-map/диагностики должен быть 1-based (getText/line_column/CPP
 * используют 1-based; cpp-сторона строится как size+1), поэтому здесь +1. */
#define YY_MKRANGE(len) \
    trust::MapperRange(m_srcIdx, m_current_pos - yyleng + 1, m_current_pos - yyleng + (len) + 1)

#define YY_TOKEN(tok) \
    do { \
        *yylval = Term::Create(TermID::tok, token::tok, tokenText().data(), yyleng, YY_MKRANGE(yyleng)); \
        return token::tok; \
    } while (0)

#define YY_TOKEN_ONLY(tok) \
    do { \
        *yylval = Term::Create(TermID::UNKNOWN, token::tok, tokenText().data(), yyleng, YY_MKRANGE(yyleng)); \
        return token::tok; \
    } while (0)

/* Создаёт токен из указателя на данные (ptr, len) - для извлечения из исходного текста.
 * Диапазон строится от m_content_begin (0-based индекс в исходник) до конца содержимого.
 * Для source-map диапазон должен быть 1-based → +1 к begin и end. */
#define YY_TOKEN_SRC(tok, ptr, len) \
    do { \
        trust::MapperRange _rng(m_srcIdx, m_content_begin + 1, m_content_begin + (len) + 1); \
        *yylval = Term::Create(TermID::tok, token::tok, (ptr), (len), _rng); \
        return token::tok; \
    } while (0)

%}

alpha   [A-Za-z]
U1      [\x80-\xbf]
U2      [\xc2-\xdf]
U3      [\xe0-\xef]
U4      [\xf0-\xf4]
ualpha  {alpha}|{U2}{U1}|{U3}{U1}{U1}|{U4}{U1}{U1}{U1}

/* запрет использования U+202A, U+202B, U+202C, U+202D, U+202E, U+2066, U+2067, U+2068, U+2069, U+061C, U+200E и U+200F */


/*
sign        ::=  "+" | "-"
infinity    ::=  "Infinity" | "inf"
nan         ::=  "nan"
digit       ::=  <a Unicode decimal digit, i.e. characters in Unicode general category Nd>
digitpart   ::=  digit (["_"] digit)*
number      ::=  [digitpart] "." digitpart | digitpart ["."]
exponent    ::=  ("e" | "E") ["+" | "-"] digitpart
floatnumber ::=  number [exponent]
floatvalue  ::=  [sign] (floatnumber | infinity | nan)
*/

digit   [0-9]
/* In numbers, it is allowed to separate digits using a handwriting symbol... */
integer {digit}([_]?{digit}+)*
name    ({ualpha}|[_])({ualpha}|{digit}|[_])*
/* macro   @({ualpha}|[_])?({name})? */

number1 -?{digit}+\.{digit}+([eE][-+]?{digit}+)?
number2 (-?{digit}+\.([eE][-+]?)?)?-?{digit}+
number  ({number1}|{number2})

/* number  [-+]?{integer}[.]{integer}([eE][-+]?[0-9]+)? */

/* field   \.({name}|{digit})+ */

/* local   $({ualpha}|[_])?({name}|{digit})? */

/* The first digit character is allowed in module names because they are filenames... */
/* The module name uses only lowercase letters of the Latin alphabet.
   The first character cannot be an underscore, but this check occurs at the level 
   of parsing the syntax tree and checking for the presence of the file. */
module_name \\({ualpha}|[_]|{digit})+
module      \\?{module_name}+

mangled_name    ({alpha}|[_$]|{digit})+
mangled _${mangled_name}?$_{mangled_name}+

%x state_COMMENT
%x state_STRWIDE
%x state_STRCHAR
%x state_DOCUMENT
%x state_EMBED
%x state_MACRO_STR
%x state_REFLECTION
%x state_RAW_STRING
%x state_RAW_BODY


%% /*** Regular Expressions Part ***/

[ \t\r\n]+  /* skip - иначе flex ECHO выводит пробельные символы в yyout (std::cout) и ломает LSP-фрейминг */

"/*"        { yy_push_state(state_COMMENT); }

<state_COMMENT>{
  "/*"      { yy_push_state(state_COMMENT); }
  "*/"      { yy_pop_state(); }
  <<EOF>>   { LexerError("Unterminated /* */ comment"); }
  (.|\n)    /* skip */
}


"///<"[^\n]* {
    *yylval = Term::Create(TermID::DOCUMENT, token::DOCUMENT_INLINE, tokenText().data(), yyleng, YY_MKRANGE(yyleng));
    return token::DOCUMENT_INLINE;
}

"///"[^\n]* {
    *yylval = Term::Create(TermID::DOCUMENT, token::DOCUMENT, tokenText().data(), yyleng, YY_MKRANGE(yyleng));
    return token::DOCUMENT;
}

"@##"           YY_TOKEN(MACRO_CONCAT);
"@#'"           YY_TOKEN(MACRO_TOSTR);
"@#\""          YY_TOKEN(MACRO_TOSTR);
"@#"            YY_TOKEN(MACRO_TOSTR);

"@$"{name}      YY_TOKEN(MACRO_ARGNAME);
"@$"[0-9]+("."[1-9])?  YY_TOKEN(MACRO_ARGPOS);

"@$..."[1-9]?   YY_TOKEN(MACRO_ARGUMENT);
"@$*"("."[1-9])? YY_TOKEN(MACRO_ARGUMENT);
"@$#"("."[1-9])? YY_TOKEN(MACRO_ARGCOUNT);

"/**/"     /* skip */;

"/**"      {
                yy_push_state(state_DOCUMENT);
                m_content_begin = m_current_pos - yyleng;
            }

<state_DOCUMENT>{

"/*"            { LexerError("Document comments cannot be nested!"); }

\n              /* skip */

"*/"            {
                    yy_pop_state();
                    auto src = m_ctx.source().source(m_srcIdx);
                    int len = m_current_pos - m_content_begin;
                    YY_TOKEN_SRC(DOCUMENT, src.data() + m_content_begin, len);
                }

<<EOF>>         { LexerError("Unterminated /** */ doc comment"); }

.               /* skip */

} /* End of state_DOCUMENT state */

"##<"[^\n]* {
    *yylval = Term::Create(TermID::DOCUMENT, token::DOCUMENT_INLINE, tokenText().data(), yyleng, YY_MKRANGE(yyleng));
    return token::DOCUMENT_INLINE;
}

"##"[^\n]* {
    *yylval = Term::Create(TermID::DOCUMENT, token::DOCUMENT, tokenText().data(), yyleng, YY_MKRANGE(yyleng));
    return token::DOCUMENT;
}

"#"[^\n]* { /* skip */ }

"{%"         {
                yy_push_state(state_EMBED);
                m_content_begin = m_current_pos;
            }

<state_EMBED>{

.                   /* skip */

\n              /* skip */

"%}"            {
                    yy_pop_state();
                    auto src = m_ctx.source().source(m_srcIdx);
                    int len = m_current_pos - yyleng - m_content_begin;
                    YY_TOKEN_SRC(EMBED, src.data() + m_content_begin, len);
                }

<<EOF>>         { LexerError("Unterminated source code {% %}"); }

} /* End of state_EMBED state */


"@@@@"      {
                m_macro_del++; 
                YY_TOKEN(MACRO_DEL);
            }

"@@@"       {
                yy_push_state(state_MACRO_STR);
                m_content_begin = m_current_pos;
            }

"@@"        { 
                if (!m_braceStack.empty()) {
                    braceMismatchError(TermID::END, m_braceStack.back());
                }
                m_macro_count++; 
                YY_TOKEN(MACRO_SEQ);
            }

<state_MACRO_STR>{

\n              /* skip */

.               /* skip */

"@@@"           {
                    yy_pop_state();
                    auto src = m_ctx.source().source(m_srcIdx);
                    int len = m_current_pos - yyleng - m_content_begin;
                    YY_TOKEN_SRC(MACRO_STR, src.data() + m_content_begin, len);
                }

<<EOF>>         { LexerError("Unterminated macro str @@@ ... @@@"); }

} /* End of state_MACRO_STR state */


R["]     {
                yy_push_state(state_RAW_STRING);
                m_raw_closing = ")\"";
            }

R[']     {
                yy_push_state(state_RAW_STRING);
                m_raw_closing = ")'";
            }

<state_RAW_STRING>{
  [A-Za-z0-9_]+  {
                     m_raw_closing.insert(1, yytext, yyleng);
                 }
  "("        {
                 yy_push_state(state_RAW_BODY);
                 m_content_begin = m_current_pos;
             }
  ")"        { LexerError("Raw string delimiter must be followed by '('"); }
  \n         { LexerError("Newline in raw string delimiter"); }
  .          { LexerError("Invalid character in raw string delimiter"); }
  <<EOF>>    { LexerError("Unterminated raw string delimiter"); }
}

<state_RAW_BODY>{

  \n              ; /* skip - flex пропускает, YY_USER_ACTION обновляет позицию */

  [^)\n]+         ; /* skip - flex пропускает, YY_USER_ACTION обновляет позицию */

  ")"        {
                 /* Проверяем closing delimiter напрямую в исходном тексте через смещение.
                  * m_raw_closing хранит полный закрывающий маркер: ) + разделитель + кавычка.
                  * Например для R"foo(...)foo" m_raw_closing = ")foo\"" (5 символов). */
                 auto src = m_ctx.source().source(m_srcIdx);
                 int closing_len = static_cast<int>(m_raw_closing.size());
                 int pos = m_current_pos - yyleng; /* позиция символа ')' в исходнике */
                 bool match = (pos + closing_len <= static_cast<int>(src.size()) &&
                               src.compare(pos, closing_len, m_raw_closing) == 0);
                 if (match) {
                     /* Синхронизируем буфер flex - потребляем оставшиеся символы closing delimiter */
                     for (int i = 1; i < closing_len; ++i) {
                         yyinput();
                     }
                     yy_pop_state(); /* RAW_BODY */
                     yy_pop_state(); /* RAW_STRING */
                     int body_len = (m_current_pos - yyleng) - m_content_begin;
                     YY_TOKEN_SRC(STRWIDE, src.data() + m_content_begin, body_len);
                 }
                 /* Не совпало - flex сам продолжит сканирование после ')'.
                  * Символы не нужно возвращать или сохранять: YY_USER_ACTION уже обновил
                  * m_current_pos, а flex продолжит с позиции после ')'. */
             }

  <<EOF>>    { LexerError("Unterminated raw string"); }

} /* End of state_RAW_BODY state */


\"      { /* Start state state_STRWIDE */
            BEGIN(state_STRWIDE);
            m_content_begin = m_current_pos;
}
            

<state_STRWIDE>{

\"      { /* saw closing quote - all done */
            BEGIN(INITIAL);
            auto src = m_ctx.source().source(m_srcIdx);
            int len = m_current_pos - yyleng - m_content_begin;
            YY_TOKEN_SRC(STRWIDE, src.data() + m_content_begin, len);
        }

\n      {
            /* generate error message */
            LexerError("Unterminated string constant");
        }

\\(.|\n)    /* skip - escape sequences preserved as-is in source */
[^\\\"\n]+ /* skip */

} /* End of state_STRWIDE state*/


'      { /* Start state state_STRCHAR */
            BEGIN(state_STRCHAR);
            m_content_begin = m_current_pos;
}

<state_STRCHAR>{

'      { /* saw closing quote - all done */
            BEGIN(INITIAL);
            auto src = m_ctx.source().source(m_srcIdx);
            int len = m_current_pos - yyleng - m_content_begin;
            YY_TOKEN_SRC(STRCHAR, src.data() + m_content_begin, len);
        }

\n      {
            /* generate error message */
            LexerError("Unterminated string constant");
        }

\\(.|\n)    /* skip - escape sequences preserved as-is in source */
[^\\'\n]+   /* skip */

} /* End of state_STRCHAR state*/


`         { yy_push_state(state_REFLECTION);
            m_content_begin = m_current_pos; }

<state_REFLECTION>{
  `         { yy_pop_state();
              auto src = m_ctx.source().source(m_srcIdx);
              int len = m_current_pos - yyleng - m_content_begin;
              YY_TOKEN_SRC(REFLECTION, src.data() + m_content_begin, len); }
  [^ \t\r\n`]+ /* skip */
  [ \t\r\n]  LexerError("Whitespace not allowed in SYMBOL");
  <<EOF>>    LexerError("Unterminated ` SYMBOL");
}


    /*   "-1j-0.2"   "1.333+0.e10j"    */

-?{number}[-+]{number}j  YY_TOKEN(COMPLEX);
-?{number}[-+]{number}i  YY_TOKEN(COMPLEX);
-?{number}j[-+]{number}  YY_TOKEN(COMPLEX);
-?{number}i[-+]{number}  YY_TOKEN(COMPLEX);

-?{number}j  YY_TOKEN(COMPLEX);
-?{number}i  YY_TOKEN(COMPLEX);

-?{integer}\\-?{integer} YY_TOKEN(RATIONAL);
-?{integer}     YY_TOKEN(INTEGER);
{number}        YY_TOKEN(NUMBER);


"$"[0-9]+"^"?   YY_TOKEN(ARGUMENT);

{mangled}   YY_TOKEN(MANGLED);
{name}"^"?  YY_TOKEN(NAME);
@{name}"^"? YY_TOKEN(MACRO);
${name}"^"? YY_TOKEN(LOCAL);
{module}    YY_TOKEN(MODULE);

"$..."      YY_TOKEN(LOCAL);  /* For statement macros */
"..."       YY_TOKEN(ELLIPSIS);
".."        YY_TOKEN(RANGE);
"<->"       YY_TOKEN_ONLY(REPEAT);
"-->"       YY_TOKEN(FOLLOW);
"~>"        YY_TOKEN(MATCHING);
"~~>"       YY_TOKEN(MATCHING);
"~~~>"      YY_TOKEN(MATCHING);
"==>"       YY_TOKEN(MATCHING);
"===>"      YY_TOKEN(MATCHING);

        /* Creating and assigning */
"::="       YY_TOKEN(CREATE_TYPE);
":="        YY_TOKEN(CREATE_NAME);
"[]="       YY_TOKEN(APPEND);

":=:"       YY_TOKEN(SWAP);

"::"        YY_TOKEN(NAMESPACE);
"$*"        YY_TOKEN(ARGS);
"$^"        YY_TOKEN(ARGS);
"$$"        YY_TOKEN(NAME);
"\\\\"      YY_TOKEN(MODULE);

"\\\\\\"    YY_TOKEN(MODULE);

"@::"       YY_TOKEN(MACRO_CONTEXT);
"@$$"       YY_TOKEN(MACRO_CONTEXT);
"$::"       YY_TOKEN(MACRO_CONTEXT);

"@["        YY_TOKEN(ATTRIBUTE);
"]@"        YY_TOKEN_ONLY(ATTR_COMPLETE);

"@\\,"      YY_TOKEN(COMMA_LEXEME);

        /* AST */
        /* Interruptions */
"++"        YY_TOKEN(INT_PLUS);
"--"        YY_TOKEN(INT_MINUS);
"-+"        YY_TOKEN(INT_REPEAT);
"+-"        YY_TOKEN(INT_REPEAT);

        /* Blocks */
"{*"        m_braceStack.push_back(TermID::BLOCK_TRY); YY_TOKEN_ONLY(TRY_ALL_BEGIN);
"*}"        checkBraceClose(TermID::BLOCK_TRY); YY_TOKEN_ONLY(TRY_ALL_END);
"{+"        m_braceStack.push_back(TermID::BLOCK_PLUS); YY_TOKEN_ONLY(TRY_PLUS_BEGIN);
"+}"        checkBraceClose(TermID::BLOCK_PLUS); YY_TOKEN_ONLY(TRY_PLUS_END);
"{-"        m_braceStack.push_back(TermID::BLOCK_MINUS); YY_TOKEN_ONLY(TRY_MINUS_BEGIN);
"-}"        checkBraceClose(TermID::BLOCK_MINUS); YY_TOKEN_ONLY(TRY_MINUS_END);


"//"        YY_TOKEN_ONLY(OPERATOR_DIV);
"+="        YY_TOKEN(OP_MATH);
"-="        YY_TOKEN(OP_MATH);
"*="        YY_TOKEN(OP_MATH);
"/="        YY_TOKEN(OP_MATH);
"//="       YY_TOKEN(OP_MATH);
"%="        YY_TOKEN(OP_MATH);
"~="        YY_TOKEN(OP_COMPARE);

"||"        YY_TOKEN(OP_LOGICAL);
"^^"        YY_TOKEN(OP_LOGICAL);
"&&"        YY_TOKEN_ONLY(OPERATOR_AND);

    /* Links and dereference */
"&"[0-9]+"^"?   YY_TOKEN(OPERATOR_PTR);
"&^"        YY_TOKEN_ONLY(OPERATOR_PTR);
"&&^"       YY_TOKEN_ONLY(OPERATOR_PTR);
"&*"        YY_TOKEN_ONLY(OPERATOR_PTR);
"&*^"       YY_TOKEN_ONLY(OPERATOR_PTR);
"&?"        YY_TOKEN_ONLY(OPERATOR_PTR);
"&?^"       YY_TOKEN_ONLY(OPERATOR_PTR);

"*^"        YY_TOKEN_ONLY(TAKE_CONST);
"**"        YY_TOKEN(WITH);


    /* Bitwise operators */
".>."        YY_TOKEN(OP_BITWISE);
".<."        YY_TOKEN(OP_BITWISE);
".>>."        YY_TOKEN(OP_BITWISE);
".&."        YY_TOKEN(OP_BITWISE);
".|."        YY_TOKEN(OP_BITWISE);
".^."        YY_TOKEN(OP_BITWISE);
".~."        YY_TOKEN(OP_BITWISE);

"!~~~"      YY_TOKEN(OP_COMPARE);
"~~~"       YY_TOKEN_ONLY(OPERATOR_DUCK);
"!~~"       YY_TOKEN(OP_COMPARE);
"~~"        YY_TOKEN_ONLY(OPERATOR_DUCK);
"!~"        YY_TOKEN(OP_COMPARE);
"==="       YY_TOKEN(OP_COMPARE);
"=="        YY_TOKEN(OP_COMPARE);
"!=="       YY_TOKEN(OP_COMPARE);
"!="        YY_TOKEN(OP_COMPARE);
"=/="       YY_TOKEN(OP_COMPARE);
"<="        YY_TOKEN(OP_COMPARE);
">="        YY_TOKEN_ONLY(OPERATOR_ANGLE_EQ);

    /* Async  */
"[*]"       YY_TOKEN(AWAIT);
"[++]"      YY_TOKEN(YIELD);
"[++"       YY_TOKEN_ONLY(YIELD_BEGIN);
"++]"       YY_TOKEN_ONLY(YIELD_END);
"[**]"      YY_TOKEN(WHEN_ALL);
"[***]"     YY_TOKEN(WHEN_ANY);

"??"        YY_TOKEN(ITERATOR);

"{"     {
            m_braceStack.push_back(TermID::LBRACE);
            YY_TOKEN(LBRACE);
        }
"}"     {
            checkBraceClose(TermID::LBRACE);
            YY_TOKEN(RBRACE);
        }

"("     YY_TOKEN(LPAREN);
")"     YY_TOKEN(RPAREN);
"["     YY_TOKEN(LBRACKET);
"]"     YY_TOKEN(RBRACKET);
";"     YY_TOKEN(SEMICOLON);
","     YY_TOKEN(COMMA);
"."     YY_TOKEN(DOT);
":"     YY_TOKEN(COLON);
"="     YY_TOKEN(EQ);
"+"     YY_TOKEN(PLUS);
"-"     YY_TOKEN(MINUS);
"*"     YY_TOKEN(STAR);
"/"     YY_TOKEN(SLASH);
"%"     YY_TOKEN(PERCENT);
"&"     YY_TOKEN(AMP);
"|"     YY_TOKEN(PIPE);
"^"     YY_TOKEN(CARET);
"~"     YY_TOKEN(TILDE);
"!"     YY_TOKEN(BANG);
"?"     YY_TOKEN(QUESTION);
"@"     YY_TOKEN(AT);
"$"     YY_TOKEN(DOLLAR);
"<"     YY_TOKEN(LT);
">"     YY_TOKEN(GT);


<<EOF>>     {
    *yylval = Term::Create(TermID::END, "", YY_MKRANGE(0), token::END);
    return token::END;
}

. {
    /* Незнаю, нужны ли нераспознанные символы? Раньше планировалось с их помощью расширять синтаксис языковых конструкций, 
    * но это относится к парсеру и не может изменяться динамически в зависимости от наличия существующий объектов и определений.
    * Если относится к символам как к идентификаторам на других языках, то опять же это лучше делать на уровне лексера и парсера,
    * чтобы еще при обработке исходников вместо создания неопределнных последовательностьей возникала ошибка времени компиляции,
    * а не передача отдельных символов как не распознанных терминалов.
    */        
    LexerError("Unexpected character");
}

%% /*** Additional Code ***/