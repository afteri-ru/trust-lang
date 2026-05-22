%require "3.8"

%language "c++"
%skeleton "lalr1.cc"

%define api.namespace {trust}
%define api.parser.class {ParserAST}

%define api.value.type {trust::TokenPtr}
/* %define api.value.automove */
%define parse.error detailed

%parse-param { trust::ParserContext& pc }

%lex-param { trust::ParserContext& pc }

%code requires {
  #include "diag/diag.hpp"
  #include "ast/token_info.hpp"
  #include "ast/attr_parser.hpp"
  #include "diag/context.hpp"
  #include "utils/error.hpp"
  #include "parser/parser.hpp"
}

%code {
  #include "parser/parser.hpp"
  namespace trust {
    int yylex(ParserAST::semantic_type* yylval, ParserContext& pc);
  } // namespace trust
}

@@HEADER@@
@@TOKENS@@

%start ast

%% /*** Grammar Rules ***/

/* Атрибут — разбирает содержимое @[...]@ */
attr: ATTR {
    EXPECT($1 != nullptr);
    EXPECT(!$1->m_sequence.empty());
    auto parsed = parse_attr(pc.ctx.attrs(), $1->m_sequence, pc.ctx.diag());
    if (parsed.has_value()) {
        pc.pending_attrs.push_back(parsed->m_id);
    }
    $$ = std::move($1);
}

/* Несколько атрибутов перед токеном */
attr_groups: /* empty */
           | attr_groups attr

/* Разделитель — поглощается, не попадает в AST */
separator: SEMICOLON
         | separator SEMICOLON

/* Литералы — создаём конкретные AST-ноды с заполненным source */
literal: INTEGER { $$ = $1;}
       | NUMBER { $$ = $1;}
       | COMPLEX { $$ = $1;}
       | RATIONAL { $$ = $1;}
       | STRWIDE { $$ = $1;}
       | STRCHAR { $$ = $1;}


/* Инструкция */
stmt: attr_groups literal {
        $$ = $literal;
        for (auto id : pc.pending_attrs) $$->add_attr(id);
        pc.pending_attrs.clear();
    }
    | attr_groups SEMICOLON {
        $$ = TokenInfo::make(ParserToken::Kind::END, "", {});
        if (!pc.pending_attrs.empty()) {
            pc.ctx.diag().report(MapperRange{}, Severity::Warning, "attribute(s) before ';' have no target");
            pc.pending_attrs.clear();
        }
    }

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
   | sequence { pc.out.push_back(std::move($sequence));}
   /* | sequence separator { out.push_back(std::move($sequence));} */

%% /*** Additional Code ***/