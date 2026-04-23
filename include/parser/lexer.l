%top{
    #include "parser/lexer.hpp"
}

%option reentrant noyywrap nounput noinput
%option nodefault
%option stack
%option extra-type="trust::Lexer *"

%{
#include "parser/token.hpp"
#include <stdexcept>

  #ifndef YY_FATAL_ERROR
  #define YY_FATAL_ERROR(msg) throw trust::SyntaxError(msg, trust::SourceLoc::invalid())
  #endif

  #ifndef YY_INPUT
  #define YY_INPUT(buf, result, max_size) \
    (result) = trust::Lexer::fill_buffer(yyextra, (buf), (max_size))
  #endif

  #define TK(tor, ...)  trust::Lexer::token(yyextra, static_cast<int>(trust::ParserToken::Kind::tor), yyleng __VA_OPT__(,) __VA_ARGS__);\
                        return static_cast<int>(trust::ParserToken::Kind::tor)
  #define ADV(n, ...)   trust::Lexer::advance(yyextra, n __VA_OPT__(,) __VA_ARGS__);
  #define LEXER_ERROR(msg) trust::Lexer::error(yyextra, msg);
%}

/*
 * NOTE: Comments like [TK(NAME)] following lexer rules are NOT dead code.
 * They are used by the token generator for consistency checks.
 * DO NOT remove them when modifying lexer rules.
 */

%x state_COMMENT
%x state_EMBED
%x state_EVAL
%x state_MACRO_STR
%x state_STRWIDE
%x state_STRCHAR
%x state_STRWIDE_RAW
%x state_STRCHAR_RAW
%x state_DOC_BEFORE
%x state_ATTRIBUTE


alpha   [A-Za-z]
U1      [\x80-\xbf]
U2      [\xc2-\xdf]
U3      [\xe0-\xef]
U4      [\xf0-\xf4]
ualpha  {alpha}|{U2}{U1}|{U3}{U1}{U1}|{U4}{U1}{U1}{U1}

digit   [0-9]
integer {digit}([_]?{digit}+)*
name    ({ualpha}|[_])({ualpha}|{digit}|[_])*

number1 -?{digit}+\.{digit}+([eE][-+]?{digit}+)?
number2 (-?{digit}+\.([eE][-+]?)?)?-?{digit}+
number  ({number1}|{number2})

module_name \\({ualpha}|[_]|{digit})+
module      \\?{module_name}+

mangled_name    ({alpha}|[_$]|{digit})+
mangled _${mangled_name}?$_{mangled_name}+


%%

"/*"        { yy_push_state(state_COMMENT, yyscanner); ADV(yyleng); }

<state_COMMENT>{
  "/*"      { yy_push_state(state_COMMENT, yyscanner); ADV(yyleng); }
  "*/"      { yy_pop_state(yyscanner); ADV(yyleng); }
  <<EOF>>   LEXER_ERROR("lex: unterminated /* */ comment");
  (.|\n)    ADV(yyleng);
}

"///<"[^\n]*   TK(DOC_AFTER);
"///"[^\n]*    TK(DOC_BEFORE);

"@##"           TK(MACRO_CONCAT);
"@#'"           TK(MACRO_TOSTR);
"@#\""          TK(MACRO_TOSTR);
"@#"            TK(MACRO_TOSTR);

"@$"{name}      TK(MACRO_ARGNAME);
"@$"[0-9]+      TK(MACRO_ARGPOS);

"@$..."         TK(MACRO_ARGUMENT);
"@$*"           TK(MACRO_ARGUMENT);
"@$#"           TK(MACRO_ARGCOUNT);

"/**/"     ADV(yyleng);
"/**"      { yy_push_state(state_DOC_BEFORE, yyscanner); ADV(yyleng, true);} /* TK(DOC_BEFORE) - do not remove checks */

<state_DOC_BEFORE>{
  "*/"      { yy_pop_state(yyscanner); return TK(DOC_BEFORE, 2, 2); }
  "/*"      LEXER_ERROR("Document comments cannot be nested!");
  <<EOF>>   LEXER_ERROR("lex: unterminated /** */ doc comment");
  (.|\n)    ADV(yyleng);
}

"##<"[^\n]* TK(DOC_AFTER);
"##"[^\n]*  TK(DOC_BEFORE);
"#"[^\n]*  ADV(yyleng);

"{%"       { yy_push_state(state_EMBED, yyscanner); ADV(yyleng, true); } /* TK(EMBED) - do not remove checks */

<state_EMBED>{
  "%}"      { yy_pop_state(yyscanner); return TK(EMBED, 2,2); }
  <<EOF>>   LEXER_ERROR("lex: unterminated source code {% %}");
  (.|\n)    ADV(yyleng);
}

"@@@@"      TK(MACRO_DEL);
"@@@"       { yy_push_state(state_MACRO_STR, yyscanner); ADV(yyleng, true); } /* TK(MACRO_STR) - do not remove checks */
"@@"        TK(MACRO_SEQ);

<state_MACRO_STR>{
  "@@@"     { yy_pop_state(yyscanner); return TK(MACRO_STR, 3, 3); }
  [^@\n]+   ADV(yyleng);
  @\n       ADV(yyleng);
  @@\n      ADV(yyleng);
  @         ADV(yyleng);
  @@        ADV(yyleng);
  \n        ADV(yyleng);
  <<EOF>>   LEXER_ERROR("lex: unterminated macro str @@@ ... @@@");
}

\"        { yy_push_state(state_STRWIDE, yyscanner); ADV(yyleng, true); } /* TK(STRWIDE) - do not remove checks */

<state_STRWIDE>{
  \"        { yy_pop_state(yyscanner); return TK(STRWIDE, 1, 1); }
  \\(.|\n)  ADV(yyleng);
  [^\\\n\"]+ ADV(yyleng);
  \n        LEXER_ERROR("Unterminated string constant");
  .         ADV(yyleng);
  <<EOF>>   LEXER_ERROR("lex: unterminated \" string");
}

'         { yy_push_state(state_STRCHAR, yyscanner); ADV(yyleng, true); } /* TK(STRCHAR) - do not remove checks */

<state_STRCHAR>{
  '         { yy_pop_state(yyscanner); return TK(STRCHAR, 1, 1); }
  \\(.|\n)  ADV(yyleng);
  [^\\\n']+ ADV(yyleng);
  \n        LEXER_ERROR("Unterminated string constant");
  .         ADV(yyleng);
}

"r\""     { yy_push_state(state_STRWIDE_RAW, yyscanner); ADV(yyleng, true); } /* TK(STRWIDE_RAW) - do not remove checks */

<state_STRWIDE_RAW>{
  \"        { yy_pop_state(yyscanner); return TK(STRWIDE_RAW, 1, 1); }
  [^\\\n\"]+ ADV(yyleng);
  \n        LEXER_ERROR("Unterminated raw string constant");
  .         ADV(yyleng);
  <<EOF>>   LEXER_ERROR("lex: unterminated r\" string");
}

"r'"      { yy_push_state(state_STRCHAR_RAW, yyscanner); ADV(yyleng, true); } /* TK(STRCHAR_RAW) - do not remove checks */

<state_STRCHAR_RAW>{
  '         { yy_pop_state(yyscanner); return TK(STRCHAR_RAW, 1, 1); }
  \n        LEXER_ERROR("Unterminated raw char constant");
  .         ADV(yyleng);
  <<EOF>>   LEXER_ERROR("lex: unterminated r' string");
}

"`"       { yy_push_state(state_EVAL, yyscanner); ADV(yyleng, true); } /* TK(EVAL) - do not remove checks */

<state_EVAL>{
  "`"       { yy_pop_state(yyscanner); return TK(EVAL, 1, 1); }
  \\(.|\n)  ADV(yyleng);
  [^\\\n`]+ ADV(yyleng);
  \n        LEXER_ERROR("Unterminated eval string");
  .         ADV(yyleng);
  <<EOF>>   LEXER_ERROR("lex: unterminated eval string");
}

-?{number}[-+]{number}j  TK(COMPLEX);
-?{number}[-+]{number}i  TK(COMPLEX);
-?{number}j[-+]{number}  TK(COMPLEX);
-?{number}i[-+]{number}  TK(COMPLEX);

-?{number}j     TK(COMPLEX);
-?{number}i     TK(COMPLEX);

-?{integer}\\-?{integer} TK(RATIONAL);
-?{integer}     TK(INTEGER);
{number}        TK(NUMBER);

"$"[0-9]+"^"?   TK(ARGUMENT);

{mangled}   TK(MANGLED);
${name}"^"? TK(LOCAL);
"%"{name}"^"?   TK(NATIVE);
@{name}"^"? TK(MACRO);
{name}"^"?  TK(NAME);
{module}    TK(MODULE);

"$..."      TK(LOCAL);  /* For statement macros */
"..."       TK(ELLIPSIS);
".."        TK(RANGE);
"<->"       TK(REPEAT);
"-->"       TK(FOLLOW);
"~>"        TK(MATCHING);
"~~>"       TK(MATCHING);
"~~~>"      TK(MATCHING);
"==>"       TK(MATCHING);
"===>"      TK(MATCHING);

"::="       TK(CREATE_NEW);
":="        TK(CREATE_USE);
"[]="       TK(APPEND);
"::-"       TK(PURE_NEW);
":-"        TK(PURE_USE);
":=:"       TK(SWAP);

"::"        TK(NAMESPACE);
"$*"        TK(ARGS);
"$^"        TK(ARGS);
"$$"        TK(PARENT);
"\\$"       TK(NEWLANG);
"\\\\"      TK(MODULE);

"@::"       TK(MACRO_NAMESPACE);
"@$$"       TK(MACRO_NAMESPACE);
"$::"       TK(MACRO_NAMESPACE);

"$\\\\"     TK(MACRO);

"@\\\\"       TK(ESCAPE);

"@["        { yy_push_state(state_ATTRIBUTE, yyscanner); ADV(yyleng); yyextra->content_start = yyextra->offset; } /* TK(ATTRIBUTE) - do not remove checks */

<state_ATTRIBUTE>{
  "]@"       { yy_pop_state(yyscanner); return TK(ATTRIBUTE, 2, 2); }
  <<EOF>>    LEXER_ERROR("lex: unterminated @[ ... ]@ attribute");
  (.|\n)     ADV(yyleng);
}

"{@"        TK(MACRO_EXPR_BEGIN);
"@}"        TK(MACRO_EXPR_END);

"++"        TK(INT_PLUS);
"--"        TK(INT_MINUS);
"-+"        TK(INT_REPEAT);
"+-"        TK(INT_REPEAT);

"{*"        TK(TRY_ALL_BEGIN);
"*}"        TK(TRY_ALL_END);
"{+"        TK(TRY_PLUS_BEGIN);
"+}"        TK(TRY_PLUS_END);
"{-"        TK(TRY_MINUS_BEGIN);
"-}"        TK(TRY_MINUS_END);

"//"        TK(OPERATOR_DIV);
"+="        TK(OP_MATH);
"-="        TK(OP_MATH);
"*="        TK(OP_MATH);
"/="        TK(OP_MATH);
"//="       TK(OP_MATH);
"%="        TK(OP_MATH);
"~="        TK(OP_COMPARE);

"||"        TK(OP_LOGICAL);
"^^"        TK(OP_LOGICAL);
"&&"        TK(OPERATOR_AND);

"&"[0-9]+"^"?   TK(OPERATOR_PTR);
"&^"        TK(OPERATOR_PTR);
"&&^"       TK(OPERATOR_PTR);
"&*"        TK(OPERATOR_PTR);
"&*^"       TK(OPERATOR_PTR);
"&?"        TK(OPERATOR_PTR);
"&?^"       TK(OPERATOR_PTR);

"*^"        TK(TAKE_CONST);
"**"        TK(WITH);

".>."        TK(OP_BITWISE);
".<."        TK(OP_BITWISE);
".>>."       TK(OP_BITWISE);
".&."        TK(OP_BITWISE);
".|."        TK(OP_BITWISE);
".^."        TK(OP_BITWISE);
".~."        TK(OP_BITWISE);

"!~~~"      TK(OP_COMPARE);
"~~~"       TK(OPERATOR_DUCK);
"!~~"       TK(OP_COMPARE);
"~~"        TK(OPERATOR_DUCK);
"==="       TK(OP_COMPARE);
"=="        TK(OP_COMPARE);
"!=="       TK(OP_COMPARE);
"!="        TK(OP_COMPARE);
"=/="       TK(OP_COMPARE);
"<="        TK(OP_COMPARE);
">="        TK(OPERATOR_ANGLE_EQ);

"[*]"       TK(AWAIT);
"[++]"      TK(YIELD);
"[++"       TK(YIELD_BEGIN);
"++]"       TK(YIELD_END);
"[**]"      TK(WHEN_ALL);
"[***]"     TK(WHEN_ANY);

"!!"        TK(ITERATOR);
"??"        TK(ITERATOR);
"?!"        TK(ITERATOR_QQ);
"!?"        TK(ITERATOR_QQ);

[ \t\n]+   ADV(yyleng);

"("         TK(LPAREN);
")"         TK(RPAREN);
"{"         TK(LBRACE);
"}"         TK(RBRACE);
"["         TK(LBRACKET);
"]"         TK(RBRACKET);
"<"         TK(LANGLE);
">"         TK(RANGLE);
";"         TK(SEMICOLON);
","         TK(COMMA);
":"         TK(COLON);
"."         TK(DOT);
"="         TK(ASSIGN);
"+"         TK(PLUS);
"-"         TK(MINUS);
"*"         TK(STAR);
"/"         TK(SLASH);
"%"         TK(PERCENT);
"&"         TK(AMPERSAND);
"|"         TK(PIPE);
"^"         TK(CARET);
"~"         TK(TILDE);
"!"         TK(BANG);
"?"         TK(QUESTION);
"@"         TK(AT);
"$"         TK(DOLLAR);
"#"         TK(HASH);

.           { LEXER_ERROR("lex: unrecognized character"); }

<<EOF>>   return 0;

%%