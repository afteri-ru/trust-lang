%require "3.8"

%language "c++"
%skeleton "lalr1.cc"

%define api.namespace {trust}
%define api.parser.class {ParserAST}

%define api.value.type {const trust::Lexeme*}
%define parse.error detailed

%parse-param { const trust::LexemSequence& ts }
%parse-param { std::size_t& pos }
%parse-param { trust::TermSequence& out }
%parse-param { std::string& err }

%lex-param { const trust::LexemSequence& ts }
%lex-param { std::size_t& pos }

%code requires {
  #include <vector>
  #include <memory>
  #include "diag/location.hpp"
  namespace trust {
    class Lexeme;
    using LexemSequence = std::vector<Lexeme>;
    class Term;
    using TermPtr = std::shared_ptr<Term>;
    using TermSequence = std::vector<TermPtr>;
  } // namespace trust
}

%code {
  #include "parser.hpp"
  #include "parser/term.hpp"
  namespace trust {
    int yylex(ParserAST::semantic_type* yylval, const LexemSequence& ts, std::size_t& pos);
  } // namespace trust
  // #define YYDEBUG 1
}

@@HEADER@@
@@TOKENS@@

%start ast

%% /*** Grammar Rules ***/


/* Разделитель */
separator: ';'             
        {
            $$ = $1;
        }
    | separator  ';'
        {
            $$ = $1;
        }


ns_part:  NAME
            {
                $$ = $1;
            }
        | MACRO_NAMESPACE   NAME
            {
                $$ = $2;
                $$->term->text.insert(0, $1->term->text);
                //$$->term->kind = LexemeKind::STATIC;
            }
        | ns_part  NAMESPACE  NAME   
            {
                $$ = $3;
                $$->term->text.insert(0, $2->term->text);
                $$->term->text.insert(0, $1->term->text);
                //$$->term->kind = LexemeKind::STATIC;
                // У переменных m_namespace заполняется в AstExpandNamespace
            }
        
ns_start:  NAMESPACE
            {
                $$ = $1;
            }
/*        | MACRO_NAMESPACE
            {
                $$ = $1;
                $$->term->kind = LexemeKind::NAMESPACE;
            }
*/

name_no_attr:  ns_part
            {
                $$ = $1;
                
            }
        | ns_start
            {
                $$ = $1;
                //$$->term->kind = LexemeKind::STATIC;
                // У переменных m_namespace заполняется в AstExpandNamespace
            }
        | ns_start  ns_part
            {
                $$ = $2;
                $$->term->text.insert(0, $1->term->text);
                //$$->term->kind = LexemeKind::STATIC;
                
                // У переменных m_namespace заполняется в AstExpandNamespace
            }

attr:  ATTRIBUTE
            {
                $$ = $1;
            }
    | attr  ATTRIBUTE
            {
                $$ = $1;
                //$$->m_attr.push_back(ProcessAttribute(driver, $2));
            }


name_attr:  name_no_attr
            {
                $$ = $1;
                
            }
    |  attr  name_no_attr
            {
                $$ = $2;
                //$$->m_attr.push_back(ProcessAttribute(driver, $1));
                //$$->m_attr.insert($$->m_attr.begin(), $1->m_attr.begin(), $1->m_attr.end());
                
            }

        
name:   name_attr
            {
                $$ = $1;
                
            }
        | ns_part  NAMESPACE  type_class
            {
                $$ = $3;
                $$->term->text.insert(0, $2->term->text);
                $$->term->text.insert(0, $1->term->text);
                //$$->term->kind = LexemeKind::STATIC;
                
                // У переменных m_namespace заполняется в AstExpandNamespace
            }
        | ns_start  ns_part  NAMESPACE  type_class
            {
                $$ = $4;
                $$->term->text.insert(0, $3->term->text);
                $$->term->text.insert(0, $2->term->text);
                $$->term->text.insert(0, $1->term->text);
                //$$->term->kind = LexemeKind::STATIC;
                // У переменных m_namespace заполняется в AstExpandNamespace
            } 
        |  LOCAL
            {
                $$ = $1;
                
            }
        | '$'
            {
                $$ = $1;
                $$->term->kind = LexemeKind::LOCAL;
            }
        |  MODULE
            {
                $$ = $1;
//                $$ = driver.CheckModuleTerm($1);
            }
        |  '\\'
            {
                $$ = $1;
                $$->term->kind = LexemeKind::MODULE;
//                $$ = driver.CheckModuleTerm($1);
            }
        |  MODULE  ns_start  ns_part
            {
                $$ = $3;
                $$->term->text.insert(0, $2->term->text);
                $$->term->text.insert(0, $1->term->text);
            }
        |  native
            {
                $$ = $1;
                
            }
        |  PARENT  /* $$ - rval */
            {
                $$ = $1;
                $$->term->kind = LexemeKind::NAME;
                
            }
        |  NEWLANG  /* \\ - rval */
            {
                $$ = $1;
            }
        | MACRO
            {
                $$ = $1;
                
            }
        | '@'
            {
                $$ = $1;
                $$->term->kind = LexemeKind::MACRO;
            }
        | MACRO_ARGUMENT
            {
                $$ = $1;
                
            }
        | MACRO_ARGPOS
            {
                $$ = $1;
                
            }
        | MACRO_ARGNAME
            {
                $$ = $1;
                
            }
        | MANGLED
            {
                $$ = $1;
            }
        

/* Фиксированная размерность тензоров для использования в типах данных */
type_dim: rval_var
        {
            $$ = $1;
        }
    | NAME  '='  rval_var
        { // torch поддерживает именованные диапазоны, но пока незнаю, нужны ли они?
            $$ = $3;
            //$$->term->SetName($1->getText());
        }
    |  ELLIPSIS
        {
            // Произвольное количество элементов
            $$ = $1; 
        }

type_dims: type_dim
        {
            //$$ = $1->term->AppendBlock($1->term, LexemeKind::SEQUENCE);
        }
    | type_dims  ','  type_dim
        {
            //$$ = $1->term->AppendBlock($3->term, LexemeKind::SEQUENCE);
        }

type_class:  ':'  name_no_attr
            {
                $$ = $2;
                $$->term->text.insert(0, ":");
                $$->term->kind = LexemeKind::TYPE;
                
            }
        | ':'  '~'  name_no_attr
            {
                $$ = $3;
                $$->term->text.insert(0, ":");
                $$->term->kind = LexemeKind::TYPE;
                
            }
        | ':'  OPERATOR_DUCK  name_no_attr
            {
                $$ = $3;
                $$->term->text.insert(0, ":");
                $$->term->kind = LexemeKind::TYPE;
                
            }

ptr: '&' 
        {
            $$ = $1;
        }
    | OPERATOR_AND
        {
            $$ = $1;
        }
    | OPERATOR_PTR
        {
            $$ = $1;
        }
    

type_name:  type_class
            {
                $$ = $1;
            }
        |  type_class   '['  type_dims   ']'
            {
                $$ = $1;
                //$$->term->m_dims = $2;
                //$$->term->m_dims->SetArgs(*$$type_dims);
            }
        | ':'  ptr  NAME
            {
                // Для функций, возвращаюющих ссылки
                $$ = $3;
                $$->term->text.insert(0, ":");
                //$$->MakeRef($ptr);
                
            }
        | ':'  ptr  NAME   '['  type_dims   ']'
            {
                // Для функций, возвращаюющих ссылки
                $$ = $3;
                $$->term->text.insert(0, ":");
                //$$->term->m_dims = $type_dims;
                //$$->term->m_dims->SetArgs(*$$type_dims);
                //$$->MakeRef($ptr);
                
            }

        | ':'  '*'  NAME
            {
                // Для функций, возвращаюющих ссылки
                $$ = $3;
                $$->term->text.insert(0, ":");
                //$$->MakeRef($2);
                
            }
        | ':'  '*'  NAME   '['  type_dims   ']'
            {
                // Для функций, возвращаюющих ссылки
                $$ = $3;
                $$->term->text.insert(0, ":");
                //$$->term->m_dims = $type_dims;
                //$$->term->m_dims->SetArgs(*$$type_dims);
                //$$->MakeRef($2);
                
            }

type_call: type_name   call
            {
                $$ = $1;
                $$->term->SetArgs(*$2);
                $$->term->kind = LexemeKind::TYPE;
                
            }
        
type_item:  type_name
            {
                $$ = $1;
                $$->term->kind = LexemeKind::TYPE;
                
            }
        | type_call
            {
                $$ = $1;
            }
/*        | ':'  eval
            {
                // Если тип еще не определён и/или его ненужно проверять во время компиляции, то имя типа можно взять в кавычки.
                $$ = $2;
                $$->term->kind = LexemeKind::TYPENAME;
                $$->term->text.insert(0, ":");
            } */

types:  type_name
            {
                $$ = $1;
            }
        | ':' set
            {
                $$ = $2;
                $$->term->kind = LexemeKind::TYPE;
            }
        

digits_literal: INTEGER
            {
                $$ = $1;
                //$$->term->SetType(nullptr);
            }
        | NUMBER
            {
                $$ = $1;
                //$$->term->SetType(nullptr);
            }
        | COMPLEX
            {
                $$ = $1;
                //$$->term->SetType(nullptr);
            }
        | RATIONAL
            {
                $$ = $1;
                //$$->term->SetType(nullptr);
            }
        | MACRO_ARGCOUNT
            {
                $$ = $1;
            }
        
digits:  digits_literal
            {
                $$ = $1;
            }
        | digits_literal  type_item
            {
                $$ = $1;
                //$$->term->SetType(*$type_item);
            }

        
        
range_val:  rval_range
        {
            $$ = $1;  
        }
/*    | '('  arithmetic  ')'
        {
            $$ = $2;
        } */

        
range: range_val  RANGE  range_val
        {
            $$ = $2;
            //$$->term->push_back($1, "start");
            //$$->term->push_back($3, "stop");
        }
    | range_val  RANGE  range_val  RANGE  range_val
        {
            $$ = $2;
            //$$->term->push_back($1, "start");
            //$$->term->push_back($3, "stop");
            //$$->term->push_back($5, "step");
        }
        
        
        
name_to_concat:  MACRO_ARGUMENT  
        {
            $$ = $1;
        }
    |  MACRO_ARGNAME
        {
            $$ = $1;
        }
    |  NAME
        {
            $$ = $1;
        }

strwide: STRWIDE
            {
                $$ = $1;
                //$$->term->SetType(nullptr);
            }
        | strwide  STRWIDE
            {
                $$ = $1;
                $$->term->text.append($2->term->text);
            }

strchar: STRCHAR
            {
                $$ = $1;
                //$$->term->SetType(nullptr);
            }
        | strchar  STRCHAR
            {
                $$ = $1;
                $$->term->text.append($2->term->text);
            }

strtype: strwide
            {
                $$ = $1;
            }
        | strchar
            {
                $$ = $1;
            }
        |  MACRO_TOSTR   name_to_concat
            {            
                $$ = $1;
                $$->term->AppendRight(*$2); 
            }
        |  name_to_concat  MACRO_CONCAT  name_to_concat
           {            
                $$ = $2;
                $$->term->AppendLeft(*$1); 
                $$->term->AppendRight(*$3); 
            }
    

string: strtype
        {
            $$ = $1;
        }
    | strtype  call
        {
            $$ = $1;
            $$->term->SetArgs(*$2);
        }
   

doc_before: DOC_BEFORE 
            {
                $$ = $1;
            }    
        | doc_before  DOC_BEFORE 
            {
                $$ = $1;
                $$->term->AppendRight(*$2);
            }    
    
doc_after: DOC_AFTER
            {
                $$ = $1;
            }    
        | doc_after  DOC_AFTER
            {
                $$ = $1;
                $$->term->AppendRight(*$2);
            }    

        
arg_name: name 
        {
            $$ = $1;
            
        }
    | strtype 
        {
            $$ = $1;
        }
    | '.'  NAME
        {
            $$ = $2; 
            
        }
        
/* Допустимые <имена> объеков */
assign_name:  name
                {
                    $$ = $1;
                    
                }
           | ARGUMENT  /* $123 */
                {
                    $$ = $1;
                    
                }
           
field_name:  NAME
            {
                $$ = $1; 
                
            }
        |  NAME  call
            {
                $$ = $1; 
                $$->term->SetArgs(*$call);
                
            }
        |  NAME  types
            {
                $$ = $1; 
                //$$->term->SetType(*$types);
                
            }
        |  NAME  call  types
            {
                $$ = $1; 
                //$$->term->SetArgs(*$call);
                //$$->term->SetType(*$types);
                
            }

field:  '.'  field_name
            {
                //$field_name->m_id = LexemeKind::FIELD;
                $$ = $1; 
                //$$->Last()->AppendRight(*$field_name);
            }
        | '.'  take  field_name
            {
                //$field_name->m_id = LexemeKind::FIELD;
                //$field_name->m_is_take = true;
                //$field_name->m_is_const = $take->m_is_const;

                $$ = $1; 
                //$$->Last()->AppendRight(*$field_name);
            }
        
        
native:  '%'  ns_part
            {
                $$ = $2;
                $$->term->text.insert(0, $1->term->text);
                $$->term->kind = LexemeKind::NATIVE;
            }

        | '%'  ns_start  ns_part 
            {
                $$ = $3;
                $$->term->text.insert(0, $2->term->text);
                $$->term->text.insert(0, $1->term->text);
                $$->term->kind = LexemeKind::NATIVE;
            }
        | '%'  '.'  NAME
            {
                $$ = $NAME; 
                $$->term->text.insert(0, $2->term->text);
                $$->term->text.insert(0, $1->term->text);
                $$->term->kind = LexemeKind::NATIVE;
            }
        
lval_obj: assign_name
            {
                $$ = $1;
            }
        |  assign_name  '['  args  ']'
            {   
                $$ = $1; 
                //$2->m_id = LexemeKind::INDEX;
                //$2->SetArgs(*$args);
                //$$->Last()->AppendRight(*$2);
            }
        | field 
            {
                $$ = $1; 
            }
        |  lval_obj  field
            {
                $$ = $1; 
                //$$->Last()->AppendRight(*$field);
            }        
        
        
take:   TAKE_CONST  /*  *^  */
        {
            $$ = $1;
            $$->term->kind = LexemeKind::TAKE;
            //$$->m_is_const = true;
        }
    | '*' 
        {
            $$ = $1;
            $$->term->kind = LexemeKind::TAKE;
            //$$->m_is_const = false;
        }  
    
/* Допустимые lvalue объекты */
lval_var:  lval_obj
            {
                $$ = $1; 
            }
        |  take  rval_name
            {
                $$ = $2;
                //$$->m_is_take = true;
                //$$->m_is_const = $take->m_is_const;
            } 
        |  type_item
            {   
                $$ = $type_item; 
            }
        |  type_item  type_item
            {   
                $$ = $1; 
                //$$->term->SetType(*$2);
            }
        |  name  type_item
            {   
                $$ = $1; 
                //$$->term->SetType(*$type_item);
            }

/* Допустимые lvalue объекты */
lval_call:  take  call
            {
                $$ = $2;
                //$$->m_is_take = true;
                //$$->m_is_const = $take->m_is_const;
            } 
        |  name  call
            {   
                $$ = $name; 
                $$->term->SetArgs(*$call);
            }
        |  name  call  types
            {   
                $$ = $name; 
                $$->term->SetArgs(*$call);
                //$$->term->SetType(*$types);
            }

        

lval: lval_var
        {       
            $$ = $1;
        }
    | lval_call
        {       
            $$ = $1;
        }
        
rval_name: lval
            {
                $$ = $1; 
            }
/*        | with_op  '('  lval  ')'
            {
                $$ = $1; 
                $$->Last()->Append($lval);
            } */
        | ARGS /* $* и @* - rval */
            {
                $$ = $1;
                
            }

        
rval_range: rval_name
            {
                $$ = $1;
            }
        | digits
            {
                $$ = $1;
            }
        |  string
            {
                $$ = $1;
            }
            
eval:  EVAL 
        {
            $$ = $1;
        }
    |  EVAL  call
        {
            $$ = $1;
            $$->term->SetArgs(*$call);
        }
    
rval_var:  rval_range
            {
                $$ = $1;
            }
        |  collection
            {
                $$ = $1;
            }
        |  range
            {
                $$ = $1;
            }
        |  eval 
            {   
                $$ = $1;
            }
        
        
        
rval:   rval_var
            {
                $$ = $1;
            }
        |  assign_lval
            {
                $$ = $1;
            }


iter:  '?'
            {
                $$=$1;
                $$->term->kind = LexemeKind::ITERATOR;
            }
        | '!'
            {
                $$=$1;
                $$->term->kind = LexemeKind::ITERATOR;
            }
        | ITERATOR  /* !! ?? */
            {
                $$=$1;
            }

iter_call:  iter  '('  ')'
            {
                $$ = $1;
                $$->term->m_is_call = true;
            }
        | iter  '('  args   ')'
            {
                $$ = $1;
                $$->term->SetArgs(*$args);
            }

        
iter_all:  ITERATOR_QQ  /* !?  ?! */
            {
                $$=$1;
                $$->term->kind = LexemeKind::ITERATOR;
            }
        | iter
            {
                $$=$1;
            }
        | iter_call
            {
                $$=$1;
            }

       

/*
 * Порядок аргументов проверяется не на уровне парсера, а при анализе объекта, поэтому 
 * в парсере именованные и не именованные аргуметы могут идти в любом порядке и в любом месте.
 * 
 * Но различаются аругменты с левой и правой стороны от оператора присвоения!
 * С левой стороны в скобках указывается прототип функции, где у каждого аргумента должно быть имя, может быть указан тип данных 
 * и признак ссылки, а последним оператором может быть многоточие (т.е. произвольное кол-во аргументов).
 * С правой стороны в скобках происходит вызов функции (для функции, которая возвращает ссылку, перед именем "&" не ставится),
 * а перед аргументами может стоять многоточие или два (т.е. операторы раскрытия словаря).
 * 
 * <Но все это анализирутся тоже после парсера на уровне компилятора/интерпретатора!>
 * 
 */

    

/* Аргументом может быть что угодно */
arg: arg_name  '='
        {  // Именованный аргумент
            $$ = $2;
            $$->term->m_name_or_class.swap($1->term->text);
            $$->term->kind = LexemeKind::EMPTY;
        }
/*    | arg_name  type_item  '='
        { // Именованный аргумент
            $$ = $3;
            //$$->term->SetType(*$type_item);
            $$->m_name_or_class.swap($1->term->text);
            $$->term->SetTermID(LexemeKind::EMPTY);
        } */
    | arg_name  '='  logical
        { // Именованный аргумент
            $$ = $3;
            //$$->term->SetName($1->getText());
        }
    | ptr   arg_name  '='  logical
        { // Именованный аргумент
            $$ = $logical;
            //$$->term->SetName($arg_name->getText());
            //$$->MakeRef($ptr);
        }
    | name  type_item  '='  logical
        { // Именованный аргумент
            $$ = $4;
            //$$->term->SetType(*$type_item);
            //$$->term->SetName($1->getText());
        }
/*    | name  type_list  '='  logical
        { // Именованный аргумент
            $$ = $4;
            //$$->term->SetName($1->getText());
            //$$->term->SetType(*$type_list);
        } */
    | ptr  name  type_item  '='  logical
        { // Именованный аргумент
            $$ = $logical;
            //$$->term->SetType(*$type_item);
            //$$->term->SetName($name->getText());
            //$$->MakeRef($ptr);
        }
/*    | ptr  name  type_list  '='  logical
        { // Именованный аргумент
            $$ = $logical;
            //$$->term->SetName($name->getText());
            //$$->term->SetType(*$type_list);
            //$$->MakeRef($ptr);
        } */
    | arg_name  '='  ptr  logical
        { // Именованный аргумент
            $$ = $4;
            //$$->term->SetName($1->getText());
            //$$->MakeRef($ptr);
        }
    | name  type_item  '='  ptr  logical
        { // Именованный аргумент
            $$ = $5;
            //$$->term->SetType(*$type_item);
            //$$->term->SetName($1->getText());
            //$$->MakeRef($ptr);
        }
/*    | name  type_list  '='  ptr  logical
        { // Именованный аргумент
            $$ = $5;
            //$$->term->SetName($1->getText());
            //$$->term->SetType(*$type_list);
            //$$->MakeRef($ptr);
        } */
/*    | arg_name  '='  take  logical
        { // Именованный аргумент
            $$ = $take;
            $//logical->SetName($1->getText());
            $$->term->SetArgs(*$$logical);
        }
    | name  type_item  '='  take  logical
        { // Именованный аргумент
            $$ = $take;
            $logical->SetType(*$type_item);
            //$logical->SetName($1->getText());
            $$->term->SetArgs(*$$logical);
        }
    | name  type_list  '='  take  logical
        { // Именованный аргумент
            $$ = $take;
            Term::ListToVector($type_list, $logical->m_type_allowed);
            //$logical->SetName($1->getText());
            $$->term->SetArgs(*$$logical);
        } */ 
    | logical
        {
            // сюда попадают и именованные аргументы как операция присвоения значения
            $$ = $1;
        }
    | ptr  logical
        {
            $$ = $2;  
            //$$->MakeRef($ptr);
        }   
/*    | take  logical
        {
            $$ = $1;  
            $$->term->SetArgs(*$2);
        } */  
    |  ELLIPSIS
        {
            // Раскрыть элементы словаря в последовательность не именованных параметров
            $$ = $1; 
        }
    |  ELLIPSIS  logical
        {
            // Раскрыть элементы словаря в последовательность не именованных параметров
            $$ = $1; 
            $$->term->AppendRight(*$2);
        }
    |  ELLIPSIS  ELLIPSIS  logical
       {            
            // Раскрыть элементы словаря в последовательность ИМЕНОВАННЫХ параметров
            $$ = $2;
            $$->term->AppendLeft(*$1); 
            $$->term->AppendRight(*$3); 
        }
    |  ELLIPSIS  logical  ELLIPSIS
       {            
            // Заполнить данные значением
            $$ = $1;
            $$->term->kind = LexemeKind::FILLING;
            $$->term->AppendRight(*$2); 
        }
    |  ESCAPE /* for pragma terms */
       {            
            $$ = $1;
        }
    /*    |  operator
       {            
            $$ = $1;
       }
    |  op_factor
       {            
            $$ = $1;
       } */

args:   arg
            {
                $$ = $1;
                if ($$->term) {
                    auto wrapper = std::make_shared<Term>();
                    wrapper->kind = LexemeKind::SEQUENCE;
                    wrapper->m_block.push_back($$->term);
                    $$->term = wrapper;
                }
            }
        | args  ','  arg
            {
                $$ = $1;
                if ($1->term && $arg->term) {
                    $1->term->m_block.push_back($arg->term);
                }
            }
        
        
call:  '('  ')'
            {   
                $$ = $1;
                $$->term->kind = LexemeKind::END;
            }
        | '('  args   ')'
            {
                $$ = $2;
            }
        
        
array: '['  args  ','  ']'
            {
                $$ = $1;
                $$->term->text.clear();
                $$->term->kind = LexemeKind::TENSOR;
                $$->term->SetArgs(*$args);
            }
        | '['  args  ','  ']'  type_item
            {
                $$ = $1;
                $$->term->text.clear();
                $$->term->kind = LexemeKind::TENSOR;
                $$->term->SetArgs(*$args);
                //$$->term->SetType(*$type_item);
            }
        | '['  ','  ']'  type_item
            {
                // Не инициализированый тензор должен быть с конкретным типом 
                $$ = $1;
                $$->term->text.clear();
                $$->term->kind = LexemeKind::TENSOR;
                //$$->term->SetType(*$type_item);
            }

            
dictionary: '('  ','  ')'
            {
                $$ = $1;
                $$->term->text.clear();
                $$->term->kind = LexemeKind::DICT;
            }
        | '('  args  ','  ')'
            {
                $$ = $1;
                $$->term->text.clear();
                $$->term->kind = LexemeKind::DICT;
                $$->term->SetArgs(*$2);
            }


class:  dictionary
            {
                $$ = $1;
            }
        | dictionary   type_class
            {
                $$ = $1;
                //$$->term->SetType(*$type_class);
            }

set_no_type: '<'  ','  '>'
            {
                $$ = $1;
                $$->term->text.clear();
                $$->term->kind = LexemeKind::SET;
            }
        | '<'  args  ','  '>'
            {
                $$ = $1;
                $$->term->text.clear();
                $$->term->kind = LexemeKind::SET;
                $$->term->SetArgs(*$2);
            }

set:  set_no_type
            {
                $$ = $1;
            }
        | set_no_type  ':'  NAME
            {
                $$ = $1;
                //$$->term->m_name_or_class = $NAME->text;
            }
        
collection: array 
            {
                $$ = $1;
            }
        | class
            {
                $$ = $1;
            }
        | set
            {
                $$ = $1;
            }
        
class_props: assign_lval
            {
                $$ = $1;
            }
        | class_props   separator   assign_lval
            {
                $$ = $1;
                $$->term->AppendRight(*$3);
            }

class_item:  type_call
            {
                $$ = $1;
            }
        | name  call
            {
                $$ = $1;
                $$->term->SetArgs(*$call);
            }

class_base: class_item
            {
                $$ = $1;
            }
        | class_base   ','   class_item
            {
                $$ = $1;
                $$->term->AppendRight(*$3);
            }


class_def:  class_base  '{'  '}'
            {
                $$ = $class_base;
                $$->term->kind = LexemeKind::CLASS;
            }
        | class_base '{' class_props  separator  '}'
            {
                $$ = $class_base;
                $$->term->kind = LexemeKind::CLASS;
                //$class_props->RightToBlock($$->m_block);
            }
        | class_base '{' doc_after  '}'
            {
                $$ = $class_base;
                $$->term->kind = LexemeKind::CLASS;
                //$doc_after->RightToBlock($$->m_docs);
            }
        | class_base '{' doc_after  class_props  separator  '}'
            {
                $$ = $class_base;
                $$->term->kind = LexemeKind::CLASS;
                //$class_props->RightToBlock($$->m_block);
                //$doc_after->RightToBlock($$->m_docs);
            }
        

        
assign_op: CREATE_USE /* := */
            {
                $$ = $1;
            }
        | CREATE_NEW /* ::= */
            {
                $$ = $1;
            }
        | APPEND /* []= */
            {
                $$ = $1;
            }
        | PURE_USE /* :- */
            {
                $$ = $1;
            }
        | PURE_NEW /* ::- */
            {
                $$ = $1;
            }
        
    
assign_expr:  body
                {
                    $$ = $1;  
                }
            |  ptr  body
                {
                    $$ = $2;  
                    //$$->MakeRef($ptr);
                }
            | ELLIPSIS  rval
                {
                    $$ = $1;  
                    $$->term->AppendRight(*$rval); 
                }
            | class_def
                {
                    $$ = $1;  
                }
            |  MACRO_SEQ
                {
                    $$ = $1;  
                }
            |  MACRO_STR
                {
                    $$ = $1;
                }
            |  native  ELLIPSIS
                {
                    $$ = $1;
                    $$->term->AppendRight(*$2);
                }
            |  lambda
                {
                    $$ = $1;  
                }
            |  lambda '('  ')'
                {
                    $$ = $1;  
                    //$$->term->SetArgs(nullptr);
                }
            |  lambda  '('  args  ')'
                {
                    $$ = $1;  
                    $$->term->SetArgs(*$args);
                }
/*            | template_call
                {
                    $$ = $1;  
                }
*/           


assign_item:  lval
                {
                    $$ = $1;
                }
            | ptr   lval
                {
                    $$ = $2;
                    //$$->MakeRef($ptr);
                }
            | ptr  call  logical
                {
                    //$ptr->SetArgs(*$call);
                    $$ = $3;  
                    //$$->MakeRef($ptr);
                }   
            |  MACRO_SEQ
                {
                    $$ = $1;
                }
            
assign_items: assign_item
                {
                    $$ = $1;
                }
            |  assign_items  ','  assign_item
                {
                    $$ = $1;
                    $$->term->AppendLeft(*$3);
                }

/*
 * Для применения в определениях классов и в качестве rval
 */            
assign_lval:  lval  assign_op  assign_expr
            {
                $$ = $2;  
                $$->term->AppendLeft(*$1); 
                $$->term->AppendRight(*$3); 
            }
        | lval  '='  assign_expr
            {
                //$$ = Term::Create(LexemeKind::ASSIGN, "=", token::SYMBOL, 1, & @$);
                $$->term->AppendLeft(*$1); 
                $$->term->AppendRight(*$3); 
            }

assign_seq:  assign_items  assign_op  assign_expr
            {
                $$ = $2;  
                $$->term->AppendLeft(*$1); 
                $$->term->AppendRight(*$3); 

                //if($$->isMacro()){
                //    $$ = ProcessMacro(driver, $$);
                //}
            }
        | assign_items  '='  assign_expr
            {
                //$$ = Term::Create(LexemeKind::ASSIGN, "=", token::SYMBOL, 1, & @$);
                $$->term->AppendLeft(*$1); 
                $$->term->AppendRight(*$3); 

                //if($$->isMacro()){
                //    $$ = ProcessMacro(driver, $$);
               // }
            }
        | MACRO_DEL
            {
                //$$ = ProcessMacro(driver, $$);
            }

        
block:  '{'  '}'
            {
                $$ = $1; 
                $$->term->kind = LexemeKind::BLOCK;
            }
        | '{'  sequence  '}'
            {
                $1->term->kind = LexemeKind::BLOCK;
                //$$ = $1->term->AppendBlock($sequence, LexemeKind::BLOCK, true);
            }
        | '{'  sequence  separator  '}'
            {
                $1->term->kind = LexemeKind::BLOCK;
                //$$ = $1->term->AppendBlock($sequence, LexemeKind::BLOCK, true);
            }
        |  '{'  doc_after  '}'
            {
                $$ = $1; 
                $$->term->kind = LexemeKind::BLOCK;
                //$doc_after->RightToBlock($$->m_docs);
            }
        | '{'  doc_after  sequence  '}'
            {
                $1->term->kind = LexemeKind::BLOCK;
                //$$ = $1->term->AppendBlock($sequence, LexemeKind::BLOCK, true);
                //$doc_after->RightToBlock($$->m_docs);
            }
        | '{'  doc_after  sequence  separator  '}'
            {
                $1->term->kind = LexemeKind::BLOCK;
                //$$ = $1->term->AppendBlock($sequence, LexemeKind::BLOCK, true);
                //$doc_after->RightToBlock($$->m_docs);
            }

block_any: block
            {
                $$ = $1;
            }
        |  try_any
            {
                $$ = $1;
            }
        |  WITH  try_any
            {
                $$ = $2;
                $$->term->AppendLeft(*$1); 
            }

block_all: block_any
            {
                $$ = $1;
            }
        | ns_part  block_any
            {
                $$ = $2;
                //$$->m_namespace = $1;
            }
        |  ns_part  NAMESPACE  block_any
            {
                $$ = $3;
                //$$->m_namespace = $2;
                //$$->m_namespace->text.insert(0, $1->term->text);
            }
        |  ns_start  ns_part  NAMESPACE  block_any
            {
                $$ = $4;
                //$$->m_namespace = $3;
                //$$->m_namespace->text.insert(0, $2->term->text);
                //$$->m_namespace->text.insert(0, $1->term->text);
            }
        |  ns_start  ns_part  block_any
            {
                $$ = $3;
                //$$->m_namespace = $2;
                //$$->m_namespace->text.insert(0, $1->term->text);
            } 
        |  ns_start  block_any
            {
                $$ = $2;
                //$$->m_namespace = $1;
            } 
        

block_type: block_all
            {
                $$ = $1;
            }
        | block_all  types
            {
                $$ = $1;
                //$$->term->SetType(*$types);
            }
        
body:  condition
            {
                $$ = $1;
            }
        |  block_type
            {
                $$ = $1;
            }
        |  doc_before  block_type
            {
                $$ = $block_type;
                //$doc_before->RightToBlock($$->m_docs);
            } 
        |  exit
            {
                $$ = $1;
            }
        

body_else: ','  '['  ELLIPSIS  ']'  FOLLOW  body
            {
                $$ = $FOLLOW; 
                $$->term->AppendLeft(*$ELLIPSIS); 
                $$->term->AppendRight(*$body); 
            }


try_all: TRY_ALL_BEGIN  TRY_ALL_END
            {
                //$1->term->kind = LexemeKind::BLOCK_TRY;
                $$ = $1; 
            }
        | TRY_ALL_BEGIN  sequence  TRY_ALL_END
            {
                //$1->term->kind = LexemeKind::BLOCK_TRY;
                //$$ = $1->term->AppendBlock($sequence, LexemeKind::BLOCK_TRY, true);
            }
        | TRY_ALL_BEGIN  sequence  separator  TRY_ALL_END
            {
                //$1->term->kind = LexemeKind::BLOCK_TRY;
                //$$ = $1->term->AppendBlock($sequence, LexemeKind::BLOCK_TRY, true);
            }

try_plus: TRY_PLUS_BEGIN  TRY_PLUS_END
            {
                //$1->term->kind = LexemeKind::BLOCK_PLUS;
                $$ = $1; 
            }
        | TRY_PLUS_BEGIN  sequence  TRY_PLUS_END
            {
                //$1->term->kind = LexemeKind::BLOCK_PLUS;
                //$$ = $1->term->AppendBlock($sequence, LexemeKind::BLOCK_PLUS, true);
            }
        | TRY_PLUS_BEGIN  sequence  separator  TRY_PLUS_END
            {
                //$1->term->kind = LexemeKind::BLOCK_PLUS;
                //$$ = $1->term->AppendBlock($sequence, LexemeKind::BLOCK_PLUS, true);
            }
        
try_minus: TRY_MINUS_BEGIN  TRY_MINUS_END
            {
                //$1->term->kind = LexemeKind::BLOCK_MINUS;
                $$ = $1; 
            }
        | TRY_MINUS_BEGIN  sequence  TRY_MINUS_END
            {
                //$1->term->kind = LexemeKind::BLOCK_MINUS;
                //$$ = $1->term->AppendBlock($sequence, LexemeKind::BLOCK_MINUS, true);
            }
        | TRY_MINUS_BEGIN  sequence  separator  TRY_MINUS_END
            {
                //$1->term->kind = LexemeKind::BLOCK_MINUS;
                //$$ = $1->term->AppendBlock($sequence, LexemeKind::BLOCK_MINUS, true);
            }

try_any:  try_plus 
            {
                $$ = $1;
            }
        | try_minus
            {
                $$ = $1;
            }
        | try_all
            {
                $$ = $1;
            }

       
/* 
 * lvalue - объект в памяти, которому может быть присовено значение (может быть ссылкой и/или константой)
 * rvalue - объект, которому <НЕ> может быть присвоено значение (литерал, итератор, вызов функции)
 * Все lvalue могут быть преобразованы в rvalue. 
 * eval - rvalue или операция с rvalue. Возвращает результат выполнения <ОДНОЙ операции !!!!!!!>
 * 
 * Операции присвоения используют lvalue, многоточие или определение функций
 * Алгоритмы используют eval или блок кода (у matching)
 */
        
/*
 * <arithmetic> -> <arithmetic> + <addition> | <arithmetic> - <addition> | <addition>
 * <addition> -> <addition> * <factor> | <addition> / <factor> | <factor>
 * <factor> -> vars | ( <expr> )
 */

operator: '~'
            {
                $$ = $1;
                $$->term->kind = LexemeKind::OP_COMPARE;
            }
        | '>'
            {
                $$ = $1;
                $$->term->kind = LexemeKind::OP_COMPARE;
            }
        | '<'
            {
                $$ = $1;
                $$->term->kind = LexemeKind::OP_COMPARE;
            }
        |  OPERATOR_AND
            {
                $$ = $1;
                $$->term->kind = LexemeKind::OP_LOGICAL;
            }
        |  OPERATOR_ANGLE_EQ
            {
                $$ = $1;
                $$->term->kind = LexemeKind::OP_COMPARE;
            }
        |  OPERATOR_DUCK
            {
                $$ = $1;
                $$->term->kind = LexemeKind::OP_COMPARE;
            }
        |  OP_MATH
            {
                $$ = $1;
            }
        |  OP_LOGICAL
            {
                $$ = $1;
            }
        |  OP_BITWISE
            {
                $$ = $1;
            }
        |  OP_COMPARE
            {
                $$ = $1;
            }
        


arithmetic:  arithmetic '+' addition
                { 
                    $$ = $2;
                    $$->term->kind = LexemeKind::OP_MATH;
                    $$->term->AppendLeft(*$1);
                    $$->term->AppendRight(*$3);
                }
            | arithmetic '-'  addition
                { 
                    $$ = $2;
                    $$->term->kind = LexemeKind::OP_MATH;
                    $$->term->AppendLeft(*$1);
                    $$->term->AppendRight(*$3); 
                }
            |  addition   digits
                {
                    //if($digits->begin() != '-') {
                        //throw trust::ParserError(msg, trust::internal::LexerContext::GetCurrentPos(yyextra))
                        //NL_PARSER($digits, "Missing operator between '%s' and '%s'", $addition->begin(), $digits->text.begin());
                    //}
                    //@todo location
                    //$$ = Term::Create(LexemeKind::OP_MATH, $2->term->text.c_str(), token::OP_MATH, 1, & @$);
                    //$$->term->AppendLeft(*$1); 
                    //$2->term->text = $2->term->text.substr(1);
                    //$$->term->AppendRight(*$2); 
                }
            | addition
                { 
                    $$ = $1; 
                }


op_factor: '*'
            {
                $$ = $1;
            }
        |  '/'
            {
                $$ = $1;
            }
        |  OPERATOR_DIV
            {
                $$ = $1;
            }
        |  '%'
            {
                $$ = $1;
            }
        
addition:  addition  op_factor  factor
                { 
                    //if($1->getTermID() == LexemeKind::INTEGER && $op_factor->text.compare("/")==0 && $3->term->text.compare("1")==0) {
                        //NL_PARSER($op_factor, "Do not use division by one (e.g. 123/1), "
                          //      "as this operation does not make sense, but it is easy to "
                            //    "confuse it with the notation of a rational literal (123\\1).");
                    //}
    
                    $$ = $op_factor;
                    $$->term->kind = LexemeKind::OP_MATH;
                    $$->term->AppendLeft(*$1); 
                    $$->term->AppendRight(*$3); 
                }
        |  factor
                { 
                    $$ = $1; 
                }    

factor:   rval_var
            {
                $$ = $1; 
            }
        | '-'  factor
            { 
                //$$ = Term::Create(LexemeKind::OP_MATH, "-", token::OP_MATH, 1,  & @$);
                //$$->term->AppendRight(*$2); 
            }
        | '('  logical  ')'
            {
                $$ = $2; 
            }


symbolyc: MACRO_EXPR_BEGIN  arithmetic  MACRO_EXPR_END   assign_op   MACRO_EXPR_BEGIN  sequence  MACRO_EXPR_END
            {
                $$ = $assign_op;
                //$$->term->kind = LexemeKind::SYM_RULE;
                $$->term->AppendLeft(*$arithmetic); 
                $$->term->AppendRight(*$sequence); 
            }
        | MACRO_EXPR_BEGIN  arithmetic  MACRO_EXPR_END   assign_op   MACRO_EXPR_BEGIN  sequence  separator  MACRO_EXPR_END
            {
                $$ = $assign_op;
                //$$->term->kind = LexemeKind::SYM_RULE;
                $$->term->AppendLeft(*$arithmetic); 
                $$->term->AppendRight(*$sequence); 
            }
        | MACRO_EXPR_BEGIN  arithmetic  separator MACRO_EXPR_END   assign_op   MACRO_EXPR_BEGIN  sequence  MACRO_EXPR_END
            {
                $$ = $assign_op;
                //$$->term->kind = LexemeKind::SYM_RULE;
                $$->term->AppendLeft(*$arithmetic); 
                $$->term->AppendRight(*$sequence); 
            }
        | MACRO_EXPR_BEGIN  arithmetic  separator MACRO_EXPR_END   assign_op   MACRO_EXPR_BEGIN  sequence  separator  MACRO_EXPR_END
            {
                $$ = $assign_op;
                //$$->term->kind = LexemeKind::SYM_RULE;
                $$->term->AppendLeft(*$arithmetic); 
                $$->term->AppendRight(*$sequence); 
            }


embed: EMBED
            {
                $$ = $1;
            }
        | embed  EMBED
            {
                $$ = $1;
                $$->term->text.append($2->term->text);
            }
        
condition: embed
            {
                $$ = $1;
            }
        | logical
            {
                $$ = $1;
            }

        
logical:  arithmetic
            {
                $$ = $1;
            }
        |  logical  operator  arithmetic
            {
                $$ = $2;
                $$->term->AppendLeft(*$1); 
                $$->term->AppendRight(*$3); 
            }
        |  arithmetic  iter_all
            {
                $$ = $2;
                //$$->Last()->AppendLeft(*$1); 
            }
        |  logical  operator  arithmetic   iter_all
            {
                $$ = $2;
                $$->term->AppendLeft(*$1); 
                //$iter_all->Last()->AppendLeft(*$arithmetic); 
                //$$->term->AppendRight(*$iter_all); 
            }
        
        

match_cond: '['   condition   ']' 
            {
                $$ = $2;
            }

if_then:  match_cond  FOLLOW  body
            {
                $$=$2;
                $$->term->AppendLeft(*$1); 
                $$->term->AppendRight(*$3); 
            }


if_list: if_then
            {
                //$$=Term::Create(LexemeKind::FOLLOW, $1->term->text.c_str(), token::FOLLOW, 1, & @$);
                //$$->m_block.push_back($if_then);
            }
        | if_list  ','  if_then
            {
                $$ = $1; 
                //$$->m_block.push_back($if_then);
            }
        
follow: if_list
            {
                $$ = $1; 
            }
        | if_list  body_else
            {
                $$ = $1; 
                //$$->m_block.push_back($body_else);
                
            }
   
repeat: body  REPEAT  match_cond
            {
                $$=$2;
                $$->term->kind = LexemeKind::DOWHILE;
                $$->term->AppendLeft(*$body); 
                $$->term->AppendRight(*$match_cond); 
            }
        | match_cond  REPEAT  body
            {
                $$=$2;
                $$->term->kind = LexemeKind::WHILE;
                $$->term->AppendLeft(*$match_cond); 
                $$->term->AppendRight(*$body); 
            }
        | match_cond  REPEAT  body  body_else
            {
                $$=$2;
                $$->term->kind = LexemeKind::WHILE;
                $$->term->AppendLeft(*$match_cond); 
                $$->term->AppendRight(*$body); 
                //$$->m_block.push_back($body_else); 
            }

matches:  rval_range
            {
                $$=$1;
            }
        |  matches  ','  rval_range
            {
                $$ = $1;
                //$$->m_block.push_back($3);
            }        
        
match_item: '[' matches ']' FOLLOW  body
            {
                $$=$FOLLOW;
                $$->term->AppendLeft(*$matches); 
                $$->term->AppendRight(*$body); 
            }

match_items:  match_item  ';'
            {
                //$$ = Term::Create(LexemeKind::MATCHING, $1->term->text.c_str(), token::MATCHING, 1, & @$);
                //$$->m_block.push_back($match_item);
            }
        | match_items  match_item  ';'
            {
                $$ = $1;
                //$$->m_block.push_back($match_item);
            }

match_items_else:  match_items
            {
                $$=$1;
            } 
        |  match_items  '['  ELLIPSIS  ']'  FOLLOW  body
            {
                $$=$1;
                //$FOLLOW->AppendLeft(*$ELLIPSIS); 
                //$FOLLOW->AppendRight(*$body); 
                //$$->m_block.push_back($FOLLOW);
            } 
      
match_body: '{'  match_items_else  '}'
            {
                $$ = $2;
                $$->term->kind = LexemeKind::BLOCK;
            }
        | '{'  match_items_else  separator '}'
            {
                $$ = $2;
                $$->term->kind = LexemeKind::BLOCK;
            }
        | TRY_ALL_BEGIN  match_items_else  TRY_ALL_END
            {
                $$ = $2;
                //$$->term->kind = LexemeKind::BLOCK_TRY;
            }
        | TRY_ALL_BEGIN  match_items_else  separator TRY_ALL_END
            {
                $$ = $2;
                //$$->term->kind = LexemeKind::BLOCK_TRY;
            }
        | TRY_PLUS_BEGIN  match_items_else  TRY_PLUS_END
            {
                $$ = $2;
                //$$->term->kind = LexemeKind::BLOCK_PLUS;
            }
        | TRY_PLUS_BEGIN  match_items_else  separator TRY_PLUS_END
            {
                $$ = $2;
                ///$$->term->kind = LexemeKind::BLOCK_PLUS;
            }
        | TRY_MINUS_BEGIN  match_items_else  TRY_MINUS_END
            {
                $$ = $2;
                //$$->term->kind = LexemeKind::BLOCK_MINUS;
            }
        | TRY_MINUS_BEGIN  match_items_else  separator TRY_MINUS_END
            {
                $$ = $2;
                //$$->term->kind = LexemeKind::BLOCK_MINUS;
            }

        
match:  match_cond   MATCHING  match_body
            {
                $$=$2;
                $$->term->AppendLeft(*$1); 
                $$->term->AppendRight(*$3);
            }
        |  body  MATCHING  match_body
            {
                $$=$2;
                $$->term->AppendLeft(*$1); 
                $$->term->AppendRight(*$3);
            }

interrupt: INT_PLUS 
            {
                $$ = $1;
            }
        | INT_MINUS
            {
                $$ = $1;
            }
        | INT_REPEAT
            {
                $$ = $1;
            }
        

exit_part:  interrupt
        {
            $$ = $1;
        }
    |  interrupt   rval_var   interrupt
        {
            $$ = $1;
            $$->term->AppendRight(*$2); 
        }


exit_prefix: ns_part
        {
            $$ = $1;
            if( $$->term->text.compare("_") != 0 ){
                $$->term->text += "::";
            }
        }

    |  MACRO_NAMESPACE
        {
            $$ = $1;
        }
    |  ns_start
        {
            $$ = $1;
        }
    |  ns_start   ns_part
        {
            $$ = $2;
            $$->term->text.insert(0, $1->term->text);
            $$->term->text += "::";
        }
    | ns_part  NAMESPACE
        {
            $$ = $2;
            $$->term->text.insert(0, $1->term->text);
        }
    |  ns_start   ns_part  NAMESPACE
        {
            $$ = $3;
            $$->term->text.insert(0, $2->term->text);
            $$->term->text.insert(0, $1->term->text);
        }
            
exit:   exit_part
        {
            $$ = $1;
        }
    | exit_prefix  exit_part 
        {
            $$ = $2;
            //$$->m_namespace = $1;
        }


with_op:  WITH
        {
            $$ = $1;
        }

with_arg:  rval_name
        {
            $$ = $1;
        }
    |  name  '='  rval_name
        { // Именованный аргумент
            $$ = $3;
            //$$->term->SetName($1->getText());
        }

with_args: with_arg
        {
            $$ = $1;
        }
    |  with_args  ','  with_arg
        {
            $$ = $1;
            $$->term->AppendRight(*$3); 
        }
        
    
with: with_op  lval
        {
                $$ = $1;
                $$->term->AppendRight(*$2); 
        }
    | with_op  '('  ')'   body
        {
                $$ = $1;
                $$->term->AppendRight(*$4); 
        }
    | with_op  '('  with_args  ')'  body
        {
                $$ = $1; 
                $$->term->SetArgs(*$3);
                $$->term->AppendRight(*$5); 
        }
    |  with_op  '('  with_args  ')'  body  body_else
        {
                $$ = $1; 
                $$->term->SetArgs(*$3);
                $$->term->AppendRight(*$5); 
                //$$->m_block.push_back($body_else); 
        }

await:  AWAIT  rval_var
        {
            $$ = $1; 
            $$->term->AppendRight(*$rval_var);
        }

yield:  YIELD
        {
            $$ = $1; 
        }
    | YIELD_BEGIN   rval_var   YIELD_END
        {
            $$ = $1; 
            $$->term->kind = LexemeKind::YIELD;
            $$->term->AppendRight(*$rval_var);
        }

when_prefix: WHEN_ALL
        {
            $$ = $1; 
        }
    | WHEN_ANY
        {
            $$ = $1; 
        }
        
when:  when_prefix '('  with_args  ')'
        {
                $$ = $1;
                //$$->term->SetArgs(*$with_args);
        }
    
    
lambda_op: '['  ']'
        {   
            //$$ = Term::Create(LexemeKind::COROUTINE, "", token::SYMBOL, 0, & @$);
        }
    | '['  args  ']'
        {   
            //$$ = Term::Create(LexemeKind::COROUTINE, "", token::SYMBOL, 0, & @$);
            //$$->term->SetArgs(*$2);
        }

   
    
lambda:  lambda_op  '('  ')'   block_any
        {
                $$ = $1;
                $$->term->AppendRight(*$4); 
        }
    | lambda_op  '('  args  ')'  block_any
        {
                $$ = $1; 
                $$->term->SetArgs(*$3);
                $$->term->AppendRight(*$5); 
        }
    | lambda_op  '('  ')'  types  block_any
        {
                $$ = $1;
                //$$->term->SetType(*$types);
                //$$->term->AppendRight(*$block_any); 
        }
    | lambda_op  '('  args  ')'  types  block_any
        {
                $$ = $1; 
                //$$->term->SetArgs(*$3);
                //$$->term->SetType(*$types);
                //$$->term->AppendRight(*$block_any); 
        }

/*
template_op: TEMPLATE_BEGIN  TEMPLATE_END
        {   
            //$$ = Term::Create(LexemeKind::TEMPLATE, "", token::SYMBOL, 0, & @$);
        }
    | TEMPLATE_BEGIN  args  TEMPLATE_END
        {   
            //$$ = Term::Create(LexemeKind::TEMPLATE, "", token::SYMBOL, 0, & @$);
            //$$->term->SetArgs(*$2);
        }
    
   
    
template_def:  template_op name_attr '('  ')'   block_any
        {
                $$ = $1;
                $$->term->kind = LexemeKind::TEMPLATE;
                //$$->term->SetName($2->getText());
                //$$->term->SetArgs(nullptr);
                $$->term->AppendRight(*$4); 
        }
    | template_op name_attr '('  args  ')'  block_any
        {
                $$ = $1; 
                $$->term->kind = LexemeKind::TEMPLATE;
                //$$->term->SetName($2->getText());
                $$->term->SetArgs(*$3);
                $$->term->AppendRight(*$5); 
        }
    | template_op  name_attr  '('  ')'  types  block_any
        {
                $$ = $1;
                $$->term->kind = LexemeKind::TEMPLATE;
                //$$->term->SetName($2->getText());
                //$$->term->SetType(*$types);
                //$$->term->AppendRight(*$block_any); 
        }
    | template_op name_attr '('  args  ')'  types  block_any
        {
                $$ = $1; 
                $$->term->kind = LexemeKind::TEMPLATE;
                //$$->term->SetName($2->getText());
                //$$->term->SetArgs(*$3);
                //$$->term->SetType(*$types);
                //$$->term->AppendRight(*$block_any); 
        }


template_call:  name_attr template_op '('  ')'
        {
                $$ = $1;
                $$->term->kind = LexemeKind::TEMPLATE;
                //$$->term->SetArgs(nullptr);
                //$$->term->AppendLeft(*$template_op); 
        }
    | name_attr template_op '('  args  ')'
        {
                $$ = $1; 
                $$->term->kind = LexemeKind::TEMPLATE;
                //$$->term->SetArgs(*$args);
                //$$->term->AppendLeft(*$template_op); 
        }
    | name_attr template_op  '('  ')'
        {
                $$ = $1;
                $$->term->kind = LexemeKind::TEMPLATE;
                //$$->term->SetArgs(nullptr);
                $$->term->AppendLeft(*$template_op); 
        }
    | name_attr template_op '('  args  ')'
        {
                $$ = $1; 
                $$->term->kind = LexemeKind::TEMPLATE;
                $$->term->SetArgs(*$args);
                $$->term->AppendLeft(*$template_op); 
        }

*/    
using_list: exit_prefix
            {
                $$ = $1;
            }
        | using_list  ','  exit_prefix
            {
                $$ = $1;
                $$->term->AppendRight(*$3);
            }
    
ns_using:  ELLIPSIS  '='  using_list
        {
            $$ = $2;
            $$->term->kind = LexemeKind::ASSIGN;
            $$->term->AppendLeft(*$1); 
            $$->term->AppendRight(*$3); 
        }
    
/*  expression - одна операция или результат <ОДНОГО выражения без завершающей точки с запятой !!!!!> */
seq_item: assign_seq
            {
                $$ = $1;
            }
        | doc_before assign_seq
            {
                $$ = $assign_seq;
                //$doc_before->RightToBlock($$->m_docs);
            }
        | follow
            {
                $$ = $1; 
            }
        | match
            {
                $$ = $1; 
            }
        | repeat
            {
                $$ = $1; 
            }
        | body
            {
                //$$ = driver.CheckModuleTerm($1);
            }
        |  with
            {            
                $$ = $1;
            }
        |  ESCAPE /* for pragma terms */
            {            
                $$ = $1;
            }
        | ns_using
            {            
                $$ = $1;
            }
        |  symbolyc
            {            
                $$ = $1;
            }
        |  await
            {            
                $$ = $1;
            }
        |  yield
            {            
                $$ = $1;
            }
        |  when
            {            
                $$ = $1;
            }
        
sequence:  seq_item
            {
                $$ = $1;
                ASSERT($seq_item->term && "Validate input data using ValidateMMBuffer.");
                out.push_back($seq_item->term);
            }
        | seq_item  doc_after
            {
                //$doc_after->RightToBlock($seq_item->m_docs);
                $$ = $1;
                ASSERT($seq_item->term && "Validate input data using ValidateMMBuffer.");
                out.push_back($seq_item->term);
            }
        | sequence  separator  seq_item
            {
                $$ = $3;
                ASSERT($seq_item->term && "Validate input data using ValidateMMBuffer.");
                out.push_back($seq_item->term);
            }
        | sequence  separator  doc_after  seq_item
            {
                //$doc_after->RightToBlock($seq_item->m_docs);
                $$ = $4;
                ASSERT($seq_item->term && "Validate input data using ValidateMMBuffer.");
                out.push_back($seq_item->term);
            }


ast:    END
        | separator // Игнорируются
        | sequence  // Добалвение в выходной поток происходит в правилях грамматики
        | sequence separator
            {
                //driver.AstAddTerm($1);
            }
        | sequence separator  doc_after
            {
                //$doc_after->RightToBlock($1->m_docs);
                //driver.AstAddTerm($1);
            }
        | separator  sequence
            {
                //driver.AstAddTerm($2);
            }
        | separator  sequence separator
            {
                //driver.AstAddTerm($2);
            }
        | separator  sequence separator  doc_after
            {
                //$doc_after->RightToBlock($1->m_docs);
                //driver.AstAddTerm($2);
            }

%% /*** Additional Code ***/
