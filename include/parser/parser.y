%require "3.8"

%language "c++"
%skeleton "lalr1.cc"

%define api.namespace {trust}
%define api.parser.class {ParserAST}

%define api.value.type {const trust::Lexeme*}
%define parse.error detailed

%parse-param { const trust::LexemeSequence& ts }
%parse-param { std::size_t& pos }
%parse-param { trust::AstNodeSequence& out }
%parse-param { std::string& err }

%lex-param { const trust::LexemeSequence& ts }
%lex-param { std::size_t& pos }

%code requires {
  #include "parser/token_info.hpp"
}

%code {
  #include "parser/parser.hpp"
  #include "gencpp/ast.hpp"
  namespace trust {
    int yylex(ParserAST::semantic_type* yylval, const LexemeSequence& ts, std::size_t& pos);
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
digits_literal: INTEGER
              {                
                  out.push_back(make_int_literal_node(0, $1->kind, std::string($1->data(), $1->size()), $1->pos));
                  $$ = $1;
              }
              | NUMBER
              {
                  out.push_back(make_int_literal_node(0, $1->kind, std::string($1->data(), $1->size()), $1->pos));
                  $$ = $1;
              }
              | COMPLEX
              {
                  out.push_back(make_int_literal_node(0, $1->kind, std::string($1->data(), $1->size()), $1->pos));
                  $$ = $1;
              }
              | RATIONAL
              {
                  out.push_back(make_int_literal_node(0, $1->kind, std::string($1->data(), $1->size()), $1->pos));
                  $$ = $1;
              }

string_literal: STRWIDE
              {
                  out.push_back(make_string_literal_node(std::string($1->data(), $1->size()), $1->kind, $1->pos));
                  $$ = $1;
              }
              | STRCHAR
              {
                  out.push_back(make_string_literal_node(std::string($1->data(), $1->size()), $1->kind, $1->pos));
                  $$ = $1;
              }

/* stmt — либо литерал, либо разделитель */
stmt: digits_literal
    | string_literal
    | separator

/* Последовательность операторов */
sequence: stmt
        | sequence separator

ast: /* empty */
   | sequence

%% /*** Additional Code ***/