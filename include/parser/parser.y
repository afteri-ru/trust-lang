%require "3.8"

%language "c++"
%skeleton "lalr1.cc"

%define api.namespace {trust}
%define api.parser.class {ParserAST}

%define api.value.type {trust::TokenPtr}
/* %define api.value.automove */
%define parse.error detailed

%parse-param { trust::TokenSequence& ts }
%parse-param { std::size_t& pos }
%parse-param { trust::TokenSequence& out }
%parse-param { trust::DiagnosticEngine* diag }

%lex-param { trust::TokenSequence& ts }
%lex-param { std::size_t& pos }

%code requires {
  #include "diag/diag.hpp"
  #include "parser/token_info.hpp"
}

%code {
  #include "parser/parser.hpp"
  namespace trust {
    int yylex(ParserAST::semantic_type* yylval, TokenSequence& ts, std::size_t& pos);
  } // namespace trust
}

@@HEADER@@
@@TOKENS@@

%start ast

%% /*** Grammar Rules ***/

/* Разделитель — поглощается, не попадает в AST */
separator: SEMICOLON
         | separator SEMICOLON

/* Литералы — создаём конкретные AST-ноды с заполненным source */
digits_literal: INTEGER { $$ = $1;}
              | NUMBER { $$ = $1;}
              | COMPLEX { $$ = $1;}
              | RATIONAL { $$ = $1;}

string_literal: STRWIDE { $$ = $1;}
              | STRCHAR { $$ = $1;}


/* stmt — либо литерал, либо разделитель */
stmt: digits_literal { $$ = $1;}
    | string_literal { $$ = $1;}

/* Последовательность операторов */
sequence: stmt 
        { 
            $$ = TokenInfo::make(ParserToken::Kind::sequence, "", {$1->range.begin, $1->range.begin});
            $$->m_sequence.push_back(std::move($stmt));
        }
        | sequence separator stmt 
        { 
            $$ = $1;
            $$->m_sequence.push_back(std::move($stmt));
        }

ast: /* empty */
   | sequence { out.push_back(std::move($sequence));}
   /* | sequence separator { out.push_back(std::move($sequence));} */

%% /*** Additional Code ***/