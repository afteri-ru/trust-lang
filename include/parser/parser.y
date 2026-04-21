%require "3.8"

%language "c++"
%skeleton "lalr1.cc"

%define api.namespace {trust}
%define api.parser.class {ParserAST}

%define api.value.type {const trust::Lexeme*}
%define parse.error detailed

%parse-param { const trust::LexemeSequence& ts }
%parse-param { std::size_t& pos }
%parse-param { trust::AstNodes& out }
%parse-param { std::string& err }

%lex-param { const trust::LexemeSequence& ts }
%lex-param { std::size_t& pos }

%code requires {
  #include <vector>
  #include <memory>
  #include <string>
  #include "diag/location.hpp"
  #include "parser/token.hpp"
  namespace trust {
    struct AstNodeBase;
    using AstNodePtr = std::unique_ptr<AstNodeBase>;
    using AstNodes = std::vector<AstNodePtr>;
  } // namespace trust
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
separator: ';'
         | separator ';'

/* Литералы — создаём конкретные AST-ноды с заполненным source */
digits_literal: INTEGER
              {
                  auto node = std::make_unique<IntLiteral>(0);
                  node->set_source($1->kind, std::string($1->data(), $1->size()), $1->pos);
                  out.push_back(std::move(node));
                  $$ = $1;
              }
              | NUMBER
              {
                  auto node = std::make_unique<IntLiteral>(0);
                  node->set_source($1->kind, std::string($1->data(), $1->size()), $1->pos);
                  out.push_back(std::move(node));
                  $$ = $1;
              }
              | COMPLEX
              {
                  auto node = std::make_unique<IntLiteral>(0);
                  node->set_source($1->kind, std::string($1->data(), $1->size()), $1->pos);
                  out.push_back(std::move(node));
                  $$ = $1;
              }
              | RATIONAL
              {
                  auto node = std::make_unique<IntLiteral>(0);
                  node->set_source($1->kind, std::string($1->data(), $1->size()), $1->pos);
                  out.push_back(std::move(node));
                  $$ = $1;
              }

string_literal: STRWIDE
              {
                  auto node = std::make_unique<StringLiteral>(std::string($1->data(), $1->size()));
                  node->set_source($1->kind, std::string($1->data(), $1->size()), $1->pos);
                  out.push_back(std::move(node));
                  $$ = $1;
              }
              | STRWIDE STRWIDE
              {
                  std::string text($1->data(), $1->size());
                  text.append($2->data(), $2->size());
                  auto node = std::make_unique<StringLiteral>(text);
                  node->set_source($1->kind, text, $1->pos);
                  out.push_back(std::move(node));
                  $$ = $1;
              }
              | STRCHAR
              {
                  auto node = std::make_unique<StringLiteral>(std::string($1->data(), $1->size()));
                  node->set_source($1->kind, std::string($1->data(), $1->size()), $1->pos);
                  out.push_back(std::move(node));
                  $$ = $1;
              }
              | STRCHAR STRCHAR
              {
                  std::string text($1->data(), $1->size());
                  text.append($2->data(), $2->size());
                  auto node = std::make_unique<StringLiteral>(text);
                  node->set_source($1->kind, text, $1->pos);
                  out.push_back(std::move(node));
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