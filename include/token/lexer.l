%top{
    namespace trust { namespace internal { struct LexerContext; } }
}

%option reentrant noyywrap nounput noinput
%option stack
%option extra-type="trust::internal::LexerContext *"

%{
  #include "flex.gen.h"
  #include "parser.hpp"
  #include <stdexcept>

  // YY_FATAL_ERROR вызывается flex для внутренних ошибок (выделение памяти),
  // где yyextra/yyscanner недоступны — используем пустую позицию.
  #ifndef YY_FATAL_ERROR
  #define YY_FATAL_ERROR(msg) throw trust::ParserError(msg, trust::SourceLoc{})
  #endif

  // LEXER_ERROR — для ошибок в правилах, где есть доступ к yyscanner и позиции.
  #define LEXER_ERROR(msg)                                                                       \
    throw trust::ParserError(msg, trust::internal::LexerContext::GetCurrentPos(yyextra))

  #ifndef YY_INPUT
  #define YY_INPUT(buf, result, max_size)                                                           \
    do {                                                                                            \
      (result) = static_cast<int>(::trust::internal::LexerContext::ReadInput((yyextra), (buf), (max_size))); \
    } while (0)
  #endif

  #define TK(tor)     { ::trust::internal::LexerContext::MakeLexeme((yyextra), (yytext), (yyleng), ::trust::LexemeKind::tor ); return tor; }
  #define ADV(n)      { ::trust::internal::LexerContext::AdvancePos((yyextra), (n)); }
  #define STR_BEG(k)  { ::trust::internal::LexerContext::StrBegin((yyextra), (k), (yyleng)); }
  #define STR_END(cl, otrim, ctrim) { ::trust::internal::LexerContext::StrEnd((yyextra), (cl), (otrim), (ctrim)); }
%}

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

"/*"        { ADV(yyleng); yy_push_state(state_COMMENT, yyscanner); }

<state_COMMENT>{
  "/*"      { ADV(yyleng); yy_push_state(state_COMMENT, yyscanner); }
  "*/"      { ADV(yyleng); yy_pop_state(yyscanner); }
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
"/**"      { STR_BEG(trust::LexemeKind::DOC_BEFORE); yy_push_state(state_DOC_BEFORE, yyscanner); }

<state_DOC_BEFORE>{
  "*/"      { STR_END(yyleng, 0, 0); yy_pop_state(yyscanner); }
  "/*"      LEXER_ERROR("Document comments cannot be nested!");
  <<EOF>>   LEXER_ERROR("lex: unterminated /** */ doc comment");
  (.|\n)    ADV(yyleng);
}

"##<"[^\n]* TK(DOC_AFTER);
"##"[^\n]*  TK(DOC_BEFORE);
"#"[^\n]*  ADV(yyleng);

"{%"       { STR_BEG(trust::LexemeKind::EMBED); yy_push_state(state_EMBED, yyscanner); }

<state_EMBED>{
  "%}"      { STR_END(yyleng, 0, 0); yy_pop_state(yyscanner); }
  <<EOF>>   LEXER_ERROR("lex: unterminated source code {% %}");
  (.|\n)    ADV(yyleng);
}

"@@@@"      TK(MACRO_DEL);
"@@@"       { STR_BEG(trust::LexemeKind::MACRO_STR); yy_push_state(state_MACRO_STR, yyscanner); }
"@@"        TK(MACRO_SEQ);

<state_MACRO_STR>{
  "@@@"     { STR_END(yyleng, 0, 0); yy_pop_state(yyscanner); }
  [^@\n]+   ADV(yyleng);
  @\n       ADV(yyleng);
  @@\n      ADV(yyleng);
  @         ADV(yyleng);
  @@        ADV(yyleng);
  \n        ADV(yyleng);
  .         ADV(yyleng);
  <<EOF>>   LEXER_ERROR("lex: unterminated macro str @@@ ... @@@");
}

\"        { STR_BEG(trust::LexemeKind::STRWIDE); yy_push_state(state_STRWIDE, yyscanner); }

<state_STRWIDE>{
  \"        { STR_END(yyleng, 1, 1); yy_pop_state(yyscanner); }
  \\(.|\n)  ADV(yyleng);
  [^\\\n\"]+ ADV(yyleng);
  \n        LEXER_ERROR("Unterminated string constant");
  .         ADV(yyleng);
  <<EOF>>   LEXER_ERROR("lex: unterminated \" string");
}

'         { STR_BEG(trust::LexemeKind::STRCHAR); yy_push_state(state_STRCHAR, yyscanner); }

<state_STRCHAR>{
  '         { STR_END(yyleng, 1, 1); yy_pop_state(yyscanner); }
  \\(.|\n)  ADV(yyleng);
  [^\\\n']+ ADV(yyleng);
  \n        LEXER_ERROR("Unterminated string constant");
  .         ADV(yyleng);
}

"r\""     { STR_BEG(trust::LexemeKind::STRWIDE_RAW); yy_push_state(state_STRWIDE_RAW, yyscanner); }

<state_STRWIDE_RAW>{
  \"        { STR_END(yyleng, 2, 1); yy_pop_state(yyscanner); }
  \\(.|\n)  ADV(yyleng);
  [^\\\n\"]+ ADV(yyleng);
  \n        LEXER_ERROR("Unterminated raw string constant");
  .         ADV(yyleng);
  <<EOF>>   LEXER_ERROR("lex: unterminated r\" string");
}

"r'"      { STR_BEG(trust::LexemeKind::STRCHAR_RAW); yy_push_state(state_STRCHAR_RAW, yyscanner); }

<state_STRCHAR_RAW>{
  '         { STR_END(yyleng, 2, 1); yy_pop_state(yyscanner); }
  \n        LEXER_ERROR("Unterminated raw char constant");
  .         ADV(yyleng);
  <<EOF>>   LEXER_ERROR("lex: unterminated r' string");
}

"`"       { STR_BEG(trust::LexemeKind::EVAL); yy_push_state(state_EVAL, yyscanner); }

<state_EVAL>{
  "`"       { STR_END(yyleng, 1, 1); yy_pop_state(yyscanner); }
  \\(.|\n)  ADV(yyleng);
  [^\\\n\"]+ ADV(yyleng);
  \n        LEXER_ERROR("Unterminated eval string");
  .         ADV(yyleng);
  <<EOF>>   LEXER_ERROR("lex: unterminated eval string");
}

     /*   "-1j-0.2"   "1.333+0.e10j"    */

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

        /* Creating and assigning */
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

"@\\"       TK(ESCAPE);

"@["        { STR_BEG(trust::LexemeKind::ATTRIBUTE); yy_push_state(state_ATTRIBUTE, yyscanner); }

<state_ATTRIBUTE>{
  "]@"       { STR_END(yyleng, 0, 0); yy_pop_state(yyscanner); }
  <<EOF>>    LEXER_ERROR("lex: unterminated @[ ... ]@ attribute");
  (.|\n)     ADV(yyleng);
}

        /* AST */
"{@"        TK(MACRO_EXPR_BEGIN);
"@}"        TK(MACRO_EXPR_END);

        /* Interruptions */
"++"        TK(INT_PLUS);
"--"        TK(INT_MINUS);
"-+"        TK(INT_REPEAT);
"+-"        TK(INT_REPEAT);

        /* Blocks */
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

    /* Links and dereference */
"&"[0-9]+"^"?   TK(OPERATOR_PTR);
"&^"        TK(OPERATOR_PTR);
"&&^"       TK(OPERATOR_PTR);
"&*"        TK(OPERATOR_PTR);
"&*^"       TK(OPERATOR_PTR);
"&?"        TK(OPERATOR_PTR);
"&?^"       TK(OPERATOR_PTR);

"*^"        TK(TAKE_CONST);
"**"        TK(WITH);

    /* Bitwise operators */
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
"!~"        TK(OP_COMPARE);
"==="       TK(OP_COMPARE);
"=="        TK(OP_COMPARE);
"!=="       TK(OP_COMPARE);
"!="        TK(OP_COMPARE);
"=/="       TK(OP_COMPARE);
"<="        TK(OP_COMPARE);
">="        TK(OPERATOR_ANGLE_EQ);

    /* Async  */
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

.           { ::trust::internal::LexerContext::MakeLexeme((yyextra), (yytext), (yyleng), static_cast<trust::LexemeKind>(*yytext)); return *yytext; }

<<EOF>>   return 0;

%%