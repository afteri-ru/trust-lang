%{ /*** C/C++ Declarations ***/

//#include "pch.h"

#include "syntax/term.h"
#include "syntax/parser.h"
#include "syntax/lexer.h"

/* this "connects" the bison parser in the driver to the flex scanner class
 * object. it defines the yylex() function call to pull the next token from the
 * current lexer object of the driver context. */
#undef yylex
#define yylex driver.GetNextToken
    //driver.lexer->lex

#define YYDEBUG 1
    
%}

/*** yacc/bison Declarations ***/

%require "3.6"

/* add debug output code to generated parser. disable this for release
 * versions. */
%debug 

/* start symbol is named "start" */
%start start

/* write out a header file containing the token defines */
%defines

//%no-lines

/* use newer C++ skeleton file */
%skeleton "lalr1.cc"

/* namespace to enclose parser in */
%define api.prefix {trust}

/* verbose error messages */
/* %define parse.error verbose */
%define parse.error detailed

/* set the parser's class identifier */


/* The driver is passed by reference to the parser and to the scanner. This
 * provides a simple but effective pure interface, not relying on global
 * variables. */
%parse-param { class Parser& driver }


/*** BEGIN EXAMPLE - Change the example grammar's tokens below ***/
%define api.value.type {TermPtr}

/*
 * Проблема перегоузки функций разными типами аргументов специфична только для компилируемых языков со статической типизацией
 * Для языков с динамической типизацией, перегразука функций не требуется, т.к. типы аргументов могут быть произвольными
 * Но перегрузка фунций в языке со статической типизацие может еще использоваться и для замены одной реализациина функции 
 * на другую, если типы аргументов различаются, что эквивалентно замене (добавлению) новой функции для другого типа аргументов.
 * 
 * Как сделать замену одной реализации фунции на другую для языка с динамиеческой типизацией без перегрузки функций?
 * 1. Сохранять указатель на предыдущую функцию в новой реализации, тогда  нужны локальные статические переменные и/или деструкторы.
 * 2. Управлять именами функций средствами языка (не нужны локальные статические переменные и деструкторы, 
 * но нужна лексическая контструкция чтобы обращаться к предыдущей реализации (что эквавалентно досутпу к родительсокму классу),
 * а весь список функций можно вытащить итератором)
 * 
 * Связанный вопрос - пересечения имен у переменный и функций и их уникальность в одной области видимости.
 * Таготею к подходу в Эсперанто, где по струкутре слова можно понять часть речи и нет двойных смыслов
 * 
 * Для глобальных объектов - имена уникальны, но есть возможность добавлять несколько варинатов реализации одного и того же термина 
 * (новый термин заменяет старый, но в новом термине остается возможност вызывать предыдущий вариант реализации).
 * 
 * Для локальных объектов - имена <перекрываются>, т.е. объекты не "заменяются", а "перекрываются".
 * Это получается из-за того, что к локальным объектам кроме имени можно обратить по <индексу>, а к глобальным только по имени.
 * Локальный объект добаляется в начало списка, а его имя ищется последовательно (потом можно будет прикрутить хешмап)
 * 
 * 
 * все метод классов, кроме статических - вирутальные (могут быть переопределены в наследниках)
 * Класс с чистым вирутальным методом (None) создать можно, но вызвать метод нельзя (будет ошибка)
 * Интерфейсы ненужны, т.к. есть утиная типизация
 * 
 * Ссылка на статические поля класса
 * Ссылка на статические методы класса
 * Ссылка на поля класса
 * Ссылка на методы класса
 * Конструкторы
 * Дестурктор
 * 
 * Derived2::*Derived2_mfp
 * 
 * 
 * https://docs.microsoft.com/ru-ru/cpp/build/reference/decorated-names?view=msvc-160
 * Формат внутреннего имени C++
 * Внутреннее имя функции C++ содержит следующие сведения.
 * - Имя функции.
 * - Класс, членом которого является функция, если это функция-член. Это может быть класс, в который входит содержащий функцию класс, и т. д.
 * - Пространство имен, которой принадлежит функция, если она входит в пространство имен.
 * - Типы параметров функции.
 * - Соглашение о вызовах.
 * - Тип значения, возвращаемого функцией.
 * Имена функций и классов кодируются во внутреннем имени. Остальная часть внутреннего имени — это код, 
 * который имеет смысл только для компилятора и компоновщика. 
 * 
 * 
 *   :class.var_class (static) := "";
 *   :class.var_class (static) := 1; # публичное поле объекта
 *   :class._var_class (static) := 1; # защиенное поле объекта
 *   :class.__var_class (static) := 1; # приватное поле объекта
 *   :class.__var_class__ (static) := 1; # Системное 
 * 
 *   var := 1; # публичное поле объекта
 *   _var := 1; # защиенное поле объекта
 *   __var := 1; # приватное поле объекта
 *   __var__ := 1; # системное поле
 * 
 */

/* Для разрешения циклической зависимости: term.h -> parser.yy.h -> TermPtr,
 * включаем term_types.h в сгенерированный bison заголовок через %code requires.
 * term_types.h определяет TermPtr, TermID */
%code requires {
    #include "syntax/term_types.h"
}

/* Макрос TERM() теперь pass-through, т.к. api.value.type — TermPtr */
%code {
    using namespace trust;
    inline static TermPtr& TERM(TermPtr &v) __attribute__((unused));
    inline static TermPtr& TERM(TermPtr &v) { return v; }
    inline static TermPtr& TERM(const TermPtr &v) __attribute__((unused));
    inline static TermPtr& TERM(const TermPtr &v) { return const_cast<TermPtr&>(v); }
}

%token                  INTEGER		"Integer"
%token                  NUMBER		"Float"
%token                  COMPLEX		"Complex"
%token                  RATIONAL	"Rational"
%token           	STRCHAR		"StrChar"
%token           	STRWIDE		"StrWide"
%token           	TEMPLATE	"Template"
%token           	REFLECTION      "Reflection"

%token			NAME
%token			LOCAL
%token			MODULE
%token			NATIVE
%token			SYMBOL
%token			MANGLED

%token			ATTRIBUTE
%token			ATTR_COMPLETE
%token			UNKNOWN		"Token ONLY UNKNOWN"
%token			OPERATOR_DIV
%token			OPERATOR_AND
%token			OPERATOR_PTR
%token			OPERATOR_ANGLE_EQ
%token			OPERATOR_DUCK

%token			ESCAPE

 %token			TRUSTLANG	"\\\\"
%token			PARENT		"$$"
%token			ARGS		"$*"

%token			MACRO           "Macro"
%token			MACRO_SEQ
%token			MACRO_STR       "Macro str"
%token			MACRO_DEL       "Macro del"
%token			MACRO_CONCAT    "Macro concatenation"
%token			MACRO_TOSTR     "Macro to string"
%token			MACRO_ARGUMENT  "Macro argument"
%token			MACRO_ARGCOUNT  "Macro args count"
%token			MACRO_ARGPOS    "Macro argument pos"
%token			MACRO_ARGNAME   "Macro argument name"



%token			LBRACE
%token			RBRACE

%token			MACRO_NAMESPACE

%token			CREATE_TYPE             "::="
%token			CREATE_NAME             ":="
%token			APPEND                  "[]="
%token			SWAP

%token			INT_PLUS
%token			INT_MINUS
%token			INT_REPEAT
%token			TRY_PLUS_BEGIN
%token			TRY_PLUS_END
%token			TRY_MINUS_BEGIN
%token			TRY_MINUS_END
%token			TRY_ALL_BEGIN
%token			TRY_ALL_END


%token			FOLLOW
%token			MATCHING
%token			REPEAT
%token			WITH
%token			TAKE
%token			TAKE_CONST

%token			ARGUMENT


%token			RANGE           ".."
%token			ELLIPSIS        "..."
%token			NAMESPACE       "::"

%token			END         0	"end of file"

%token                  EMBED
%token			ITERATOR
%token			ITERATOR_QQ
%token			FUNCTION
%token			COROUTINE

%token			AWAIT
%token			YIELD
%token			YIELD_BEGIN
%token			YIELD_END
%token			WHEN_ALL
%token			WHEN_ANY


%token			OP_LOGICAL
%token			OP_MATH
%token			OP_COMPARE
%token			OP_BITWISE
%token			DOC_BEFORE
%token			DOC_AFTER

%% /*** Grammar Rules ***/


/* Разделитель */
separator: ';' | separator  ';'

/* Незнаю, нужны ли теперь символы? Раньше планировалось с их помощью расширять синтаксис языковых конструкций, 
 * но это относится к парсеру и не может изменяться динамически в зависимости от наличия существующий объектов и определений.
 * Если относится к символам как к идентификаторам на других языках, то опять же это лучше делать на уровне лексера и парсера,
 * чтобы еще при обработке исходников вместо создания неопределнных последовательностьей возникала ошибка времени компиляции,
 * а не передача отдельных символов как не распознанных терминалов.
 */
symbols: SYMBOL
            {
                $$ = $1;
            }
        | symbols  SYMBOL
            {
                $$ = $1; 
                TERM($$)->AppendText(TERM($2)->getText());
            }

        

ns_part:  NAME
            {
                $$ = $1;
            }
        | MACRO_NAMESPACE   NAME
            {
                $$ = $2;
                TERM($$)->AppendLeft(TERM($1));
                TERM($$)->m_id = TermID::STATIC;
            }
        | ns_part  NAMESPACE  NAME   
            {
                $$ = $3;
                TERM($$)->AppendLeft(TERM($2));
                TERM($2)->AppendLeft(TERM($1));
                TERM($$)->m_id = TermID::STATIC;
                // У переменных namespace собирается в m_text через FinalizeAndTest
            }
        
ns_start:  NAMESPACE
            {
                $$ = $1;
            }
/*        | MACRO_NAMESPACE
            {
                $$ = $1;
                $$->m_id = TermID::TermID::NAMESPACE;
            }
*/

name_no_attr:  ns_part
            {
                $$ = $1;
            }
        | ns_start
            {
                $$ = $1;
                TERM($$)->m_id = TermID::STATIC;
                // У переменных namespace собирается в m_text через FinalizeAndTest
            }
        | ns_start  ns_part
            {
                $$ = $2;
                TERM($$)->AppendLeft(TERM($1));
                TERM($$)->m_id = TermID::STATIC;
                // У переменных namespace собирается в m_text через FinalizeAndTest
            }

attr_elem:  ATTRIBUTE  rval  ATTR_COMPLETE
            {
                $$ = $1;
                TERM($$)->m_attr.push_back(TERM($2));
            }

attr:  attr_elem
            {
                $$ = $1;
            }
    | attr  attr_elem
            {
                $$ = $1;
                TERM($$)->m_attr.insert(TERM($$)->m_attr.end(), TERM($2)->m_attr.begin(), TERM($2)->m_attr.end());
            }

name_attr:  name_no_attr
            {
                $$ = $1;
                TERM($$)->FinalizeAndTest(TERM($$)->m_id);
            }
    |  attr  name_no_attr
            {
                $$ = $2;
                TERM($$)->m_attr.insert(TERM($$)->m_attr.begin(), TERM($1)->m_attr.begin(), TERM($1)->m_attr.end());
                TERM($$)->FinalizeAndTest(TERM($$)->m_id);
            }

name:   name_attr
            {
                $$ = $1;
            }
        | ns_part  NAMESPACE  type_class
            {
                $$ = $3;
                TERM($$)->AppendLeft(TERM($2));
                TERM($2)->AppendLeft(TERM($1));
                TERM($$)->FinalizeAndTest(TermID::STATIC);
                // У переменных namespace собирается в m_text через FinalizeAndTest
            }
        | ns_start  ns_part  NAMESPACE  type_class
            {
                $$ = $4;
                TERM($$)->AppendLeft(TERM($3));
                TERM($3)->AppendLeft(TERM($2));
                TERM($2)->AppendLeft(TERM($1));
                TERM($$)->FinalizeAndTest(TermID::STATIC);
                // У переменных namespace собирается в m_text через FinalizeAndTest
            } 
        |  LOCAL
            {
                $$ = $1;
            }
        | '$'
            {
                $$ = $1;
                TERM($$)->m_id = TermID::LOCAL;
            }
        |  MODULE
            {
                $$ = $1;
            }
        |  '\\'
            {
                $$ = $1;
                TERM($$)->m_id = TermID::MODULE;
            }
        |  MODULE  ns_start  ns_part
            {
                $$ = $1;
                TERM($3)->AppendLeft(TERM($2));
                TERM($3)->FinalizeAndTest(TermID::STATIC);
                TERM($$)->AppendRight(TERM($3));
            }
        |  native
            {
                $$ = $1;
            }
        |  PARENT  /* $$ - rval */
            {
                $$ = $1;
                TERM($$)->m_id = TermID::NAME;
            }
        |  TRUSTLANG  /* \\ - rval */
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
                TERM($$)->m_id = TermID::MACRO;
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
            // Именованная размерность: оборачиваем значение в ARGUMENT-терм (имя в m_left),
            // как это делается для именованных аргументов (см. arg_name '=' rval_var).
            $$ = Term::Create(TermID::ARGUMENT, "", trust::MapperRange{TERM($1)->m_mapperRange.begin, TERM($3)->m_mapperRange.end});
            TERM($$)->m_left = TERM($1);
            TERM($$)->m_right = TERM($3);
        }
    |  ELLIPSIS
        {
            // Произвольное количество элементов
            $$ = $1; 
        }

type_dims: type_dim
        {
            $$ = Term::Create(TermID::ARGS, "", TERM($1)->m_mapperRange);
            // ARGUMENT-терм сам несёт имя размерности в m_left — имя в ArgsPair не дублируется
            TERM($$)->push_back(TERM($1));
        }
    | type_dims  ','  type_dim
        {
            $$ = $1;
            TERM($$)->push_back(TERM($3));
        }

type_class:  ':'  name_no_attr
            {
                $$ = $2;
                TERM($$)->AppendLeft(TERM($1));
                TERM($$)->FinalizeAndTest(TermID::TYPE);
            }
        | ':'  '~'  name_no_attr
            {
                $$ = $3;
                TERM($$)->AppendLeft(TERM($2));
                TERM($2)->AppendLeft(TERM($1));
                TERM($$)->FinalizeAndTest(TermID::TYPE);
            }
        | ':'  OPERATOR_DUCK  name_no_attr
            {
                $$ = $3;
                TERM($$)->AppendLeft(TERM($2));
                TERM($2)->AppendLeft(TERM($1));
                TERM($$)->FinalizeAndTest(TermID::TYPE);
            }

ptr: '&' 
        {
            $$ = $1;
            // Все операторы ссылок (&, &&, &*, &?, &1) хранятся единообразно как OPERATOR_PTR
            TERM($$)->m_id = TermID::OPERATOR_PTR;
        }
    | OPERATOR_AND
        {
            $$ = $1;
            TERM($$)->m_id = TermID::OPERATOR_PTR;
        }
    | OPERATOR_PTR
        {
            $$ = $1;
            TERM($$)->m_id = TermID::OPERATOR_PTR;
        }
    

type_name:  type_class
            {
                $$ = $1;
            }
        |  type_class   '['  type_dims   ']'
            {
                $$ = $1;
                TERM($$)->m_type = TERM($type_dims);
            }
        | ':'  ptr  NAME
            {
                // Префиксный оператор (&, &&, &*, ...) над типом: ':&Int8'
                $$ = $3;
                TERM($$)->AppendLeft(TERM($1));
                TERM($$)->FinalizeAndTest(TermID::TYPE);
                TERM($ptr)->AppendRight(TERM($$));
                $$ = $ptr;
            }
        | ':'  ptr  NAME   '['  type_dims   ']'
            {
                // Префиксный оператор над типом с размерностями: ':&Int8[2]'
                $$ = $3;
                TERM($$)->AppendLeft(TERM($1));
                TERM($$)->FinalizeAndTest(TermID::TYPE);
                TERM($$)->m_type = TERM($type_dims);
                TERM($ptr)->AppendRight(TERM($$));
                $$ = $ptr;
            }

        | ':'  '*'  NAME
            {
                // Take над типом: ':*type'
                $$ = $3;
                TERM($$)->AppendLeft(TERM($1));
                TERM($$)->FinalizeAndTest(TermID::TYPE);
                TERM($2)->m_id = TermID::TAKE;
                TERM($2)->AppendRight(TERM($$));
                $$ = $2;
            }
        | ':'  '*'  NAME   '['  type_dims   ']'
            {
                // Take над типом с размерностями: ':*type[2]'
                $$ = $3;
                TERM($$)->AppendLeft(TERM($1));
                TERM($$)->FinalizeAndTest(TermID::TYPE);
                TERM($$)->m_type = TERM($type_dims);
                TERM($2)->m_id = TermID::TAKE;
                TERM($2)->AppendRight(TERM($$));
                $$ = $2;
            }

type_call: type_name   call
            {
                $$ = $1;
                TERM($$)->m_args.swap(TERM($2)->m_args);
                if (TERM($$)->getTermID() != TermID::OPERATOR_PTR && TERM($$)->getTermID() != TermID::TAKE)
                    TERM($$)->m_id = TermID::TYPE;
            }
        
type_item:  type_name
            {
                $$ = $1;
                // Узел-оператор (&, *) уже корень типа — не перезаписываем m_id
                if (TERM($$)->getTermID() != TermID::OPERATOR_PTR && TERM($$)->getTermID() != TermID::TAKE)
                    TERM($$)->m_id = TermID::TYPE;
            }
        | type_call
            {
                $$ = $1;
            }

types:  type_name
            {
                $$ = $1;
            }
        | ':' set
            {
                $$ = $2;
                TERM($$)->m_id = TermID::TYPE;
            }
        

digits_literal: INTEGER
            {
                $$ = $1;
                TERM($$)->SetType(nullptr);
            }
        | NUMBER
            {
                $$ = $1;
                TERM($$)->SetType(nullptr);
            }
        | COMPLEX
            {
                $$ = $1;
                TERM($$)->SetType(nullptr);
            }
        | RATIONAL
            {
                $$ = $1;
                TERM($$)->SetType(nullptr);
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
                TERM($$)->SetType(TERM($type_item));
            }

        
        
range_val:  rval_range
        {
            $$ = $1;  
        }

        
range: range_val  RANGE  range_val
        {
            $$ = $2;
            TERM($$)->push_back(TERM($1), "start");
            TERM($$)->push_back(TERM($3), "stop");
        }
    | range_val  RANGE  range_val  RANGE  range_val
        {
            $$ = $2;
            TERM($$)->push_back(TERM($1), "start");
            TERM($$)->push_back(TERM($3), "stop");
            TERM($$)->push_back(TERM($5), "step");
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
                TERM($$)->SetType(nullptr);
            }
        | strwide  STRWIDE
            {
                $$ = $1;
                TERM($$)->getText().append(TERM($2)->getText());
            }

strchar: STRCHAR
            {
                $$ = $1;
                TERM($$)->SetType(nullptr);
            }
        | strchar  STRCHAR
            {
                $$ = $1;
                TERM($$)->getText().append(TERM($2)->getText());
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
                TERM($$)->AppendRight(TERM($2)); 
            }
        |  name_to_concat  MACRO_CONCAT  name_to_concat
           {            
                $$ = $2;
                TERM($$)->AppendLeft(TERM($1)); 
                TERM($$)->AppendRight(TERM($3)); 
            }
    

string: strtype
        {
            $$ = $1;
        }
    | strtype  call
        {
            $$ = $1;
            TERM($$)->m_args.swap(TERM($2)->m_args);
        }
   

doc_before: DOC_BEFORE 
            {
                $$ = $1;
            }    
        | doc_before  DOC_BEFORE 
            {
                $$ = $1;
                TERM($$)->AppendRight(TERM($2));
            }    
    
doc_after: DOC_AFTER
            {
                $$ = $1;
            }    
        | doc_after  DOC_AFTER
            {
                $$ = $1;
                TERM($$)->AppendRight(TERM($2));
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
            |  symbols
                {
                    $$ = $1;  
                }
            |  ns_part  symbols
                {
                    $$ = $2;
                    TERM($$)->AppendLeft(TERM($1));
                    TERM($$)->FinalizeAndTest(TermID::STATIC);
                }
            |  ns_start   ns_part   symbols
                {
                    $$ = $3;
                    TERM($$)->AppendLeft(TERM($2));
                    TERM($2)->AppendLeft(TERM($1));
                    TERM($$)->FinalizeAndTest(TermID::STATIC);
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
                TERM($$)->m_args.swap(TERM($call)->m_args);
            }
        |  NAME  types
            {
                $$ = $1; 
                TERM($$)->SetType(TERM($types));
            }
        |  NAME  call  types
            {
                $$ = $1; 
                TERM($$)->m_args.swap(TERM($call)->m_args);
                TERM($$)->SetType(TERM($types));
            }

field:  '.'  field_name
            {
                TERM($field_name)->m_id = TermID::FIELD;
                $$ = $1; 
                TERM($$)->Last()->AppendRight(TERM($field_name));
            }
        | '.'  take  field_name
            {
                // Оператор take (* / *^) остаётся в AST как узел RefTakeExpr:
                // терм take становится корнем, field_name — его правым операндом.
                // Признак иммутабельности (*^) живёт на самом take (текст "*^"),
                // имя field_name не модифицируется.
                TERM($field_name)->m_id = TermID::FIELD;
                TERM($take)->AppendRight(TERM($field_name));

                $$ = $1; 
                TERM($$)->Last()->AppendRight(TERM($take));
            }
        
        
native:  '%'  ns_part
            {
                $$ = $2;
                TERM($$)->AppendLeft(TERM($1));
                TERM($$)->FinalizeAndTest(TermID::NATIVE);
            }

        | '%'  ns_start  ns_part 
            {
                $$ = $3;
                TERM($$)->AppendLeft(TERM($2));
                TERM($2)->AppendLeft(TERM($1));
                TERM($$)->FinalizeAndTest(TermID::NATIVE);
            }
        | '%'  '.'  NAME
            {
                $$ = TERM($NAME);
                TERM($$)->AppendLeft(TERM($2));
                TERM($2)->AppendLeft(TERM($1));
                TERM($$)->FinalizeAndTest(TermID::NATIVE);
            }
        
lval_obj: assign_name
            {
                $$ = $1;
            }
        |  assign_name  '['  args  ']'
            {   
                $$ = $1; 
                TERM($2)->m_id = TermID::INDEX;
                TERM($2)->m_args.swap(TERM($args)->m_args);
                TERM($$)->Last()->AppendRight(TERM($2));
            }
        | field 
            {
                $$ = $1; 
            }
        |  lval_obj  field
            {
                $$ = $1; 
                TERM($$)->Last()->AppendRight(TERM($field));
            }        
        
        
take:   TAKE_CONST  /*  *^  */
        {
            $$ = $1;
            TERM($$)->m_id = TermID::TAKE;
        }
    | '*' 
        {
            $$ = $1;
            TERM($$)->m_id = TermID::TAKE;
        }  
    
/* Допустимые lvalue объекты */
lval_var:  lval_obj
            {
                $$ = $1; 
            }
        |  take  rval_name
            {
                // take (* / *^) — оператор; остаётся в AST как RefTakeExpr над rval_name.
                $$ = $take;
                TERM($$)->AppendRight(TERM($rval_name));
            } 
        |  type_item
            {   
                $$ = $type_item; 
            }
        |  type_item  type_item
            {   
                $$ = $1; 
                TERM($$)->SetType(TERM($2));
            }
        |  name  type_item
            {   
                $$ = $1; 
                TERM($$)->SetType(TERM($type_item));
            }

/* Допустимые lvalue объекты */
lval_call:  take  call
            {
                // take (* / *^) — оператор; остаётся в AST как RefTakeExpr над call.
                $$ = $take;
                TERM($$)->AppendRight(TERM($call));
            } 
        |  name  call
            {   
                $$ = $name; 
                TERM($$)->m_args.swap(TERM($call)->m_args);
            }
        |  name  call  types
            {   
                $$ = $name; 
                TERM($$)->m_args.swap(TERM($call)->m_args);
                TERM($$)->SetType(TERM($types));
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
            
eval:  REFLECTION 
        {
            $$ = $1;
        }
    |  REFLECTION  call
        {
            $$ = $1;
            TERM($$)->m_args.swap(TERM($2)->m_args);
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
                $$ = $1;
                TERM($$)->m_id = TermID::ITERATOR;
            }
        | '!'
            {
                $$ = $1;
                TERM($$)->m_id = TermID::ITERATOR;
            }
        | ITERATOR  /* !! ?? */
            {
                $$ = $1;
            }

        iter_call:  iter  '('  ')'
            {
                $$ = $1;
                TERM($$)->m_args.emplace();
            }
        | iter  '('  args   ')'
            {
                $$ = $1;
                TERM($$)->m_args.swap(TERM($args)->m_args);
            }

        
iter_all:  ITERATOR_QQ  /* !?  ?! */
            {
                $$ = $1;
                TERM($$)->m_id = TermID::ITERATOR;
            }
        | iter
            {
                $$ = $1;
            }
        | iter_call
            {
                $$ = $1;
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

/* Аргументом может быть что угодно — единый список (arg-list := Expression (',' Expression)*).
 * Именованные аргументы (name=value) строят ARGUMENT-терм (m_left=имя, m_right=значение).
 * Это отдельный узел-обёртка (аналог Python ast.keyword / C# ArgumentSyntax), а не ASSIGN:
 * присваивание — действие, именованный аргумент — декларативная метка + выражение. */

named_rhs: '=' logical
        { // Правая часть именованного аргумента со значением
            $$ = $2;
        }
    | '=' ptr logical
        { // Правая часть именованного аргумента со ссылкой
            $$ = $ptr;
            TERM($$)->AppendRight(TERM($3));
        }

arg: ptr arg_name named_rhs
        { // Именованный аргумент со ссылкой
            // Грамматически допустимо любое значение справа (литерал, вызов и т.п.);
            // проверка «нельзя ссылку на литерал/повторную ссылку» выполняется в анализаторе.
            $$ = Term::Create(TermID::ARGUMENT, "", trust::MapperRange{TERM($ptr)->m_mapperRange.begin, TERM($3)->m_mapperRange.end});
            TERM($$)->m_left = TERM($arg_name);
            TERM($$)->m_right = TERM($ptr);
            TERM($ptr)->AppendRight(TERM($3));
        }
    | ptr name type_item named_rhs
        { // Именованный аргумент со ссылкой и типом
            $$ = Term::Create(TermID::ARGUMENT, "", trust::MapperRange{TERM($ptr)->m_mapperRange.begin, TERM($4)->m_mapperRange.end});
            TERM($$)->m_left = TERM($name);
            TERM($$)->m_right = TERM($ptr);
            TERM($ptr)->AppendRight(TERM($4));
            TERM($4)->SetType(TERM($type_item));
        }
    | arg_name named_rhs
        { // Именованный аргумент
            $$ = Term::Create(TermID::ARGUMENT, "", trust::MapperRange{TERM($1)->m_mapperRange.begin, TERM($2)->m_mapperRange.end});
            TERM($$)->m_left = TERM($1);
            TERM($$)->m_right = TERM($2);
        }
    | name type_item named_rhs
        { // Именованный аргумент с типом
            $$ = Term::Create(TermID::ARGUMENT, "", trust::MapperRange{TERM($1)->m_mapperRange.begin, TERM($3)->m_mapperRange.end});
            TERM($$)->m_left = TERM($1);
            TERM($$)->m_right = TERM($3);
            TERM($$)->m_right->SetType(TERM($type_item));
        }
    | logical
        {
            $$ = $1;
        }
    | ptr  logical
        {
            $$ = $ptr;
            TERM($$)->AppendRight(TERM($2));
        }
    |  ELLIPSIS
        {
            $$ = $1;
        }
    |  ELLIPSIS  logical
        {
            $$ = $1;
            TERM($$)->AppendRight(TERM($2));
        }
    |  ELLIPSIS  ELLIPSIS  logical
        {
            $$ = $2;
            TERM($$)->AppendLeft(TERM($1));
            TERM($$)->AppendRight(TERM($3));
        }
    |  ELLIPSIS  logical  ELLIPSIS
        {
            $$ = $1;
            TERM($$)->m_id = TermID::FILLING;
            TERM($$)->AppendRight(TERM($2));
        }
    |  ESCAPE
        {
            $$ = $1;
        }

args:   arg
            {
                $$ = Term::Create(TermID::ARGS, "", TERM($1)->m_mapperRange);
                std::string argName1 = (TERM($1)->getTermID() == TermID::ARGUMENT && TERM($1)->m_left) ? std::string(TERM($1)->m_left->getText()) : "";
                TERM($$)->push_back(TERM($1), argName1);
            }
    | args  ','  arg
            {
                $$ = $1;
                std::string argName3 = (TERM($3)->getTermID() == TermID::ARGUMENT && TERM($3)->m_left) ? std::string(TERM($3)->m_left->getText()) : "";
                TERM($$)->push_back(TERM($3), argName3);
            }
        
call:  '('  ')'
            {   
                $$ = $1;
                TERM($$)->m_id = TermID::END;
                TERM($$)->m_args.emplace();
            }
        | '('  args   ')'
            {
                $$ = $2;
            }
        
        
array: '['  args  ','  ']'
            {
                $$ = $1;
                TERM($$)->getText().clear();
                TERM($$)->m_id = TermID::TENSOR;
                TERM($$)->m_args.swap(TERM($args)->m_args);
            }
        | '['  args  ','  ']'  type_item
            {
                $$ = $1;
                TERM($$)->getText().clear();
                TERM($$)->m_id = TermID::TENSOR;
                TERM($$)->m_args.swap(TERM($args)->m_args);
                TERM($$)->SetType(TERM($type_item));
            }
        | '['  ','  ']'  type_item
            {
                $$ = $1;
                TERM($$)->getText().clear();
                TERM($$)->m_id = TermID::TENSOR;
                TERM($$)->SetType(TERM($type_item));
            }

            
dictionary: '('  ','  ')'
            {
                $$ = $1;
                TERM($$)->getText().clear();
                TERM($$)->m_id = TermID::DICT;
            }
        | '('  args  ','  ')'
            {
                $$ = $1;
                TERM($$)->getText().clear();
                TERM($$)->m_id = TermID::DICT;
                TERM($$)->m_args.swap(TERM($2)->m_args);
            }


class:  dictionary
            {
                $$ = $1;
            }
        | dictionary   type_class
            {
                $$ = $1;
                TERM($$)->SetType(TERM($type_class));
                TERM($$)->GetType()->FinalizeAndTest(TermID::TYPE);
            }

set_no_type: '<'  ','  '>'
            {
                $$ = $1;
                TERM($$)->getText().clear();
                TERM($$)->m_id = TermID::SET;
            }
        | '<'  args  ','  '>'
            {
                $$ = $1;
                TERM($$)->getText().clear();
                TERM($$)->m_id = TermID::SET;
                TERM($$)->m_args.swap(TERM($2)->m_args);
            }

set:  set_no_type
            {
                $$ = $1;
            }
        | set_no_type  ':'  NAME
            {
                // Имя множества (например, < :Error, >) собирается из string_view
                // фрагментов (':' и NAME) через AppendLeft, как в type_class.
                // FinalizeAndTest формирует итоговый текст ":Error^" в m_text.
                // Иммутабельность сохраняется в самом имени; '^' срезается
                // только при конвертации в AstNode (termToAst).
                // В AST (SET → EnumLiteral) имя доступно через getText().
                $$ = $1;
                TERM($$)->AppendLeft(TERM($3));
                TERM($3)->AppendLeft(TERM($2));
                TERM($$)->FinalizeAndTest(TermID::SET);
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
                TERM($$)->AppendRight(TERM($3));
            }

class_item:  type_call
            {
                $$ = $1;
            }
        | name  call
            {
                $$ = $1;
                TERM($$)->m_args.swap(TERM($call)->m_args);
            }

class_base: class_item
            {
                $$ = $1;
            }
        | class_base   ','   class_item
            {
                $$ = $1;
                TERM($$)->AppendRight(TERM($3));
            }


class_def:  class_base  LBRACE  RBRACE
            {
                $$ = $class_base;
                TERM($$)->m_id = TermID::CLASS;
            }
        | class_base LBRACE class_props  separator  RBRACE
            {
                $$ = $class_base;
                TERM($$)->m_id = TermID::CLASS;
                TERM($class_props)->RightToBlock(TERM($$)->m_block);
            }
        | class_base LBRACE doc_after  RBRACE
            {
                $$ = $class_base;
                TERM($$)->m_id = TermID::CLASS;
                TERM($doc_after)->RightToBlock(TERM($$)->m_docs);
            }
        | class_base LBRACE doc_after  class_props  separator  RBRACE
            {
                $$ = $class_base;
                TERM($$)->m_id = TermID::CLASS;
                TERM($class_props)->RightToBlock(TERM($$)->m_block);
                TERM($doc_after)->RightToBlock(TERM($$)->m_docs);
            }
        

        
assign_op: CREATE_NAME /* := */
            {
                $$ = $1;
            }
        | CREATE_TYPE /* ::= */
            {
                $$ = $1;
            }
        | APPEND /* []= */
            {
                $$ = $1;
            }
        
    
assign_expr:  body
                {
                    $$ = $1;  
                }
            |  ptr  body
                {
                    $$ = $ptr;
                    TERM($$)->AppendRight(TERM($2));
                }
            | ELLIPSIS  rval
                {
                    $$ = $1;  
                    TERM($$)->AppendRight(TERM($rval)); 
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
                    TERM($$)->AppendRight(TERM($2));
                }
            |  lambda
                {
                    $$ = $1;  
                }
            |  lambda '('  ')'
                {
                    $$ = $1;  
                    TERM($$)->m_args.emplace();
                }
            |  lambda  '('  args  ')'
                {
                    $$ = $1;  
                    TERM($$)->m_args.swap(TERM($args)->m_args);
                }
           


assign_item:  lval
                {
                    $$ = $1;
                }
            | ptr   lval
                {
                    $$ = $ptr;
                    TERM($$)->AppendRight(TERM($2));
                }
            | ptr  call  logical
                {
                    TERM($ptr)->m_args.swap(TERM($call)->m_args);
                    $$ = $ptr;
                    TERM($$)->AppendRight(TERM($3));
                }   
            |  MACRO_SEQ
                {
                    $$ = $1;
                }
            
/*
 * Для применения в определениях классов и в качестве rval
 */            
assign_lval:  lval  assign_op  assign_expr
            {
                $$ = $2;  
                TERM($$)->AppendLeft(TERM($1)); 
                TERM($$)->AppendRight(TERM($3)); 
            }
        | lval  '='  assign_expr
            {
                $$ = Term::Create(TermID::ASSIGN, "=", trust::MapperRange{TERM($1)->m_mapperRange.begin, TERM($3)->m_mapperRange.end}, token::SYMBOL);
                TERM($$)->AppendLeft(TERM($1)); 
                TERM($$)->AppendRight(TERM($3)); 
            }

assign_items: assign_item
                {
                    $$ = $1;
                }
            |  assign_items  ','  assign_item
                {
                    TERM($1)->FinalizeAndTest(TERM($1)->m_id);
                    TERM($3)->FinalizeAndTest(TERM($3)->m_id);

                    $$ = $1;
                    TERM($$)->AppendLeft(TERM($3));
                }

assign_seq:  assign_items  assign_op  assign_expr
            {
                $$ = $2;  
                TERM($$)->AppendLeft(TERM($1)); 
                TERM($$)->AppendRight(TERM($3)); 

                if(TERM($$)->isMacro()){
                    $$ = ProcessMacro(driver, TERM($$));
                }
            }
        | assign_items  '='  assign_expr
            {
                $$ = Term::Create(TermID::ASSIGN, "=", trust::MapperRange{TERM($1)->m_mapperRange.begin, TERM($3)->m_mapperRange.end}, token::SYMBOL);
                TERM($$)->AppendLeft(TERM($1)); 
                TERM($$)->AppendRight(TERM($3)); 

                if(TERM($$)->isMacro()){
                    $$ = ProcessMacro(driver, TERM($$));
                }
            }
        | MACRO_DEL
            {
                $$ = ProcessMacro(driver, TERM($1));
            }

        
block:  LBRACE  RBRACE
            {
                $$ = $1; 
                TERM($$)->m_id = TermID::BLOCK;
            }
        | LBRACE  sequence  RBRACE
            {
                TERM($1)->m_id = TermID::BLOCK;
                $$ = TERM($1)->AppendBlock(TERM($sequence), TermID::BLOCK, true);
            }
        | LBRACE  sequence  separator  RBRACE
            {
                TERM($1)->m_id = TermID::BLOCK;
                $$ = TERM($1)->AppendBlock(TERM($sequence), TermID::BLOCK, true);
            }
        |  LBRACE  doc_after  RBRACE
            {
                $$ = $1; 
                TERM($$)->m_id = TermID::BLOCK;
                TERM($doc_after)->RightToBlock(TERM($$)->m_docs);
            }
        | LBRACE  doc_after  sequence  RBRACE
            {
                TERM($1)->m_id = TermID::BLOCK;
                $$ = TERM($1)->AppendBlock(TERM($sequence), TermID::BLOCK, true);
                TERM($doc_after)->RightToBlock(TERM($$)->m_docs);
            }
        | LBRACE  doc_after  sequence  separator  RBRACE
            {
                TERM($1)->m_id = TermID::BLOCK;
                $$ = TERM($1)->AppendBlock(TERM($sequence), TermID::BLOCK, true);
                TERM($doc_after)->RightToBlock(TERM($$)->m_docs);
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
                TERM($$)->AppendLeft(TERM($1)); 
            }

block_all: block_any
            {
                $$ = $1;
            }
        | ns_part  block_any
            {
                $$ = $2;
                TERM($1)->FinalizeAndTest(TERM($1)->m_id);
                TERM($$)->getText() = std::move(TERM($1)->getText());
            }
        |  ns_part  NAMESPACE  block_any
            {
                $$ = $3;
                TERM($2)->AppendLeft(TERM($1));
                TERM($2)->FinalizeAndTest(TERM($2)->m_id);
                TERM($$)->getText() = std::move(TERM($2)->getText());
            }
        |  ns_start  ns_part  NAMESPACE  block_any
            {
                $$ = $4;
                TERM($3)->AppendLeft(TERM($2));
                TERM($2)->AppendLeft(TERM($1));
                TERM($3)->FinalizeAndTest(TERM($3)->m_id);
                TERM($$)->getText() = std::move(TERM($3)->getText());
            }
        |  ns_start  ns_part  block_any
            {
                $$ = $3;
                TERM($2)->AppendLeft(TERM($1));
                TERM($2)->FinalizeAndTest(TERM($2)->m_id);
                TERM($$)->getText() = std::move(TERM($2)->getText());
            } 
        |  ns_start  block_any
            {
                $$ = $2;
                TERM($1)->FinalizeAndTest(TERM($1)->m_id);
                TERM($$)->getText() = std::move(TERM($1)->getText());
            } 
        

block_type: block_all
            {
                $$ = $1;
            }
        | block_all  types
            {
                $$ = $1;
                TERM($$)->SetType(TERM($types));
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
                TERM($doc_before)->RightToBlock(TERM($$)->m_docs);
            } 
        |  exit
            {
                $$ = $1;
            }
        

body_else: ','  '['  ELLIPSIS  ']'  FOLLOW  body
            {
                $$ = $FOLLOW; 
                TERM($$)->AppendLeft(TERM($ELLIPSIS)); 
                TERM($$)->AppendRight(TERM($body)); 
            }


try_all: TRY_ALL_BEGIN  TRY_ALL_END
            {
                TERM($1)->m_id = TermID::BLOCK_TRY;
                $$ = $1; 
            }
        | TRY_ALL_BEGIN  sequence  TRY_ALL_END
            {
                TERM($1)->m_id = TermID::BLOCK_TRY;
                $$ = TERM($1)->AppendBlock(TERM($sequence), TermID::BLOCK_TRY, true);
            }
        | TRY_ALL_BEGIN  sequence  separator  TRY_ALL_END
            {
                TERM($1)->m_id = TermID::BLOCK_TRY;
                $$ = TERM($1)->AppendBlock(TERM($sequence), TermID::BLOCK_TRY, true);
            }

try_plus: TRY_PLUS_BEGIN  TRY_PLUS_END
            {
                TERM($1)->m_id = TermID::BLOCK_PLUS;
                $$ = $1; 
            }
        | TRY_PLUS_BEGIN  sequence  TRY_PLUS_END
            {
                TERM($1)->m_id = TermID::BLOCK_PLUS;
                $$ = TERM($1)->AppendBlock(TERM($sequence), TermID::BLOCK_PLUS, true);
            }
        | TRY_PLUS_BEGIN  sequence  separator  TRY_PLUS_END
            {
                TERM($1)->m_id = TermID::BLOCK_PLUS;
                $$ = TERM($1)->AppendBlock(TERM($sequence), TermID::BLOCK_PLUS, true);
            }
        
try_minus: TRY_MINUS_BEGIN  TRY_MINUS_END
            {
                TERM($1)->m_id = TermID::BLOCK_MINUS;
                $$ = $1; 
            }
        | TRY_MINUS_BEGIN  sequence  TRY_MINUS_END
            {
                TERM($1)->m_id = TermID::BLOCK_MINUS;
                $$ = TERM($1)->AppendBlock(TERM($sequence), TermID::BLOCK_MINUS, true);
            }
        | TRY_MINUS_BEGIN  sequence  separator  TRY_MINUS_END
            {
                TERM($1)->m_id = TermID::BLOCK_MINUS;
                $$ = TERM($1)->AppendBlock(TERM($sequence), TermID::BLOCK_MINUS, true);
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
                TERM($$)->m_id = TermID::OP_COMPARE;
            }
        | '>'
            {
                $$ = $1;
                TERM($$)->m_id = TermID::OP_COMPARE;
            }
        | '<'
            {
                $$ = $1;
                TERM($$)->m_id = TermID::OP_COMPARE;
            }
        |  OPERATOR_AND
            {
                $$ = $1;
                TERM($$)->m_id = TermID::OP_LOGICAL;
            }
        |  OPERATOR_ANGLE_EQ
            {
                $$ = $1;
                TERM($$)->m_id = TermID::OP_COMPARE;
            }
        |  OPERATOR_DUCK
            {
                $$ = $1;
                TERM($$)->m_id = TermID::OP_COMPARE;
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
                    TERM($$)->m_id = TermID::OP_MATH;
                    TERM($$)->AppendLeft(TERM($1));
                    TERM($$)->AppendRight(TERM($3));
                }
            | arithmetic '-'  addition
                { 
                    $$ = $2;
                    TERM($$)->m_id = TermID::OP_MATH;
                    TERM($$)->AppendLeft(TERM($1));
                    TERM($$)->AppendRight(TERM($3)); 
                }
            |  addition   digits
                {
                    if(TERM($digits)->getText()[0] != '-') {
                        driver.m_ctx.diag().report(Severity::Error, TERM($digits)->m_mapperRange, "Missing operator between '{}' and '{}'",
                                                   TERM($addition)->getText(), TERM($digits)->getText());
                    }
                    $$ = Term::Create(TermID::OP_MATH, std::string(TERM($2)->getText().data(), 1), trust::MapperRange{TERM($1)->m_mapperRange.begin, TERM($2)->m_mapperRange.end}, token::OP_MATH);
                    TERM($$)->AppendLeft(TERM($1)); 
                    TERM($2)->getText() = TERM($2)->getText().substr(1);
                    TERM($$)->AppendRight(TERM($2)); 
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
                    if(TERM($1)->getTermID() == TermID::INTEGER && TERM($op_factor)->getText().compare("/")==0 && TERM($3)->getText().compare("1")==0) {
                        driver.m_ctx.diag().report(Severity::Error, TERM($op_factor)->m_mapperRange,
                                                   "Do not use division by one (e.g. 123/1), as this operation does not make sense, but it is easy to confuse it with the notation of a rational literal (123\\1).");
                    }
    
                    $$ = $op_factor;
                    TERM($$)->m_id = TermID::OP_MATH;
                    TERM($$)->AppendLeft(TERM($1)); 
                    TERM($$)->AppendRight(TERM($3)); 
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
                $$ = Term::Create(TermID::OP_MATH, "-", trust::MapperRange{TERM($1)->m_mapperRange.begin, TERM($2)->m_mapperRange.end}, token::OP_MATH);
                TERM($$)->AppendRight(TERM($2)); 
            }
        | '('  logical  ')'
            {
                $$ = $2; 
            }



embed: EMBED
            {
                $$ = $1;
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
                TERM($$)->AppendLeft(TERM($1)); 
                TERM($$)->AppendRight(TERM($3)); 
            }
        |  arithmetic  iter_all
            {
                $$ = $2;
                TERM($$)->Last()->AppendLeft(TERM($1)); 
            }
        |  logical  operator  arithmetic   iter_all
            {
                $$ = $2;
                TERM($$)->AppendLeft(TERM($1)); 
                TERM($iter_all)->Last()->AppendLeft(TERM($arithmetic)); 
                TERM($$)->AppendRight(TERM($iter_all)); 
            }
        
        

match_cond: '['   condition   ']' 
            {
                $$ = $2;
            }

if_then:  match_cond  FOLLOW  body
            {
                // branch: m_left = условие, m_right = тело
                $$=$2;
                TERM($$)->AppendLeft(TERM($1)); 
                TERM($$)->AppendRight(TERM($3)); 
            }

else_body: ','  '['  ELLIPSIS  ']'  FOLLOW  body
            {
                // else: тело else (маркер ELLIPSIS в AST не нужен — else ложится в m_right)
                $$ = $body;
            }

follow: if_then
            {
                // [cond] --> body : m_left=cond, m_block=[body]
                $$=Term::Create(TermID::FOLLOW, "if", TERM($1)->m_mapperRange, token::FOLLOW);
                TERM($$)->m_left = TERM($1)->m_left;
                TERM($$)->m_block.push_back(TERM($1)->m_right);
            }
        | follow  ','  if_then
            {
                // else-if: branch (m_left=cond, m_right=body) в m_block
                $$ = $1;
                TERM($$)->m_block.push_back(TERM($3));
            }
        | follow  else_body
            {
                // else: тело else в m_right
                $$ = $1;
                TERM($$)->m_right = TERM($2);
            }

repeat: body  REPEAT  match_cond
            {
                $$=$2;
                TERM($$)->m_id = TermID::DOWHILE;
                TERM($$)->m_left = TERM($match_cond);
                TERM($$)->m_block.push_back(TERM($body));
            }
        | match_cond  REPEAT  body
            {
                $$=$2;
                TERM($$)->m_id = TermID::WHILE;
                TERM($$)->m_left = TERM($match_cond);
                TERM($$)->m_block.push_back(TERM($body));
            }
        | match_cond  REPEAT  body  else_body
            {
                $$=$2;
                TERM($$)->m_id = TermID::WHILE;
                TERM($$)->m_left = TERM($match_cond);
                TERM($$)->m_block.push_back(TERM($body));
                TERM($$)->m_right = TERM($else_body);
            }

matches:  rval_range
            {
                $$=$1;
            }
        |  matches  ','  rval_range
            {
                $$ = $1;
                TERM($$)->m_block.push_back(TERM($3));
            }        
        
match_item: '[' matches ']' FOLLOW  body
            {
                $$=$FOLLOW;
                TERM($$)->AppendLeft(TERM($matches)); 
                TERM($$)->AppendRight(TERM($body)); 
            }

match_items:  match_item  ';'
            {
                // ';' — разделитель условия, не входит в выполняемое выражение; берём range самого match_item.
                $$ = Term::Create(TermID::MATCHING, std::string(TERM($1)->getText().data(), 1), TERM($1)->m_mapperRange, token::MATCHING);
                TERM($$)->m_block.push_back(TERM($match_item));
            }
        | match_items  match_item  ';'
            {
                $$ = $1;
                TERM($$)->m_block.push_back(TERM($match_item));
            }

match_items_else:  match_items
            {
                $$=$1;
            } 
        |  match_items  '['  ELLIPSIS  ']'  FOLLOW  body
            {
                $$=$1;
                TERM($FOLLOW)->AppendLeft(TERM($ELLIPSIS)); 
                TERM($FOLLOW)->AppendRight(TERM($body)); 
                TERM($$)->m_block.push_back(TERM($FOLLOW));
            } 
      
match_body: LBRACE  match_items_else  RBRACE
            {
                $$ = $2;
                TERM($$)->m_id = TermID::BLOCK;
            }
        | LBRACE  match_items_else  separator RBRACE
            {
                $$ = $2;
                TERM($$)->m_id = TermID::BLOCK;
            }
        | TRY_ALL_BEGIN  match_items_else  TRY_ALL_END
            {
                $$ = $2;
                TERM($$)->m_id = TermID::BLOCK_TRY;
            }
        | TRY_ALL_BEGIN  match_items_else  separator TRY_ALL_END
            {
                $$ = $2;
                TERM($$)->m_id = TermID::BLOCK_TRY;
            }
        | TRY_PLUS_BEGIN  match_items_else  TRY_PLUS_END
            {
                $$ = $2;
                TERM($$)->m_id = TermID::BLOCK_PLUS;
            }
        | TRY_PLUS_BEGIN  match_items_else  separator TRY_PLUS_END
            {
                $$ = $2;
                TERM($$)->m_id = TermID::BLOCK_PLUS;
            }
        | TRY_MINUS_BEGIN  match_items_else  TRY_MINUS_END
            {
                $$ = $2;
                TERM($$)->m_id = TermID::BLOCK_MINUS;
            }
        | TRY_MINUS_BEGIN  match_items_else  separator TRY_MINUS_END
            {
                $$ = $2;
                TERM($$)->m_id = TermID::BLOCK_MINUS;
            }

        
match:  match_cond   MATCHING  match_body
            {
                $$=$2;
                TERM($$)->AppendLeft(TERM($1)); 
                TERM($$)->AppendRight(TERM($3));
            }
        |  body  MATCHING  match_body
            {
                $$=$2;
                TERM($$)->AppendLeft(TERM($1)); 
                TERM($$)->AppendRight(TERM($3));
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
            TERM($$)->AppendRight(TERM($2)); 
        }


exit_prefix: ns_part
        {
            $$ = $1;
            TERM($$)->FinalizeAndTest(TERM($$)->m_id);
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
            TERM($$)->AppendLeft(TERM($1));
            TERM($$)->FinalizeAndTest(TERM($$)->m_id);
        }
    | ns_part  NAMESPACE
        {
            $$ = $2;
            TERM($$)->AppendLeft(TERM($1));
            TERM($$)->FinalizeAndTest(TERM($$)->m_id);
        }
    |  ns_start   ns_part  NAMESPACE
        {
            $$ = $3;
            TERM($$)->AppendLeft(TERM($2));
            TERM($2)->AppendLeft(TERM($1));
            TERM($$)->FinalizeAndTest(TERM($$)->m_id);
        }
            
exit:   exit_part
        {
            $$ = $1;
        }
    | exit_prefix  exit_part 
        {
            $$ = $2;
            TERM($$)->getText() = std::move(TERM($1)->getText());
        }


with_op:  WITH
        {
            $$ = $1;
        }

    
with: with_op  lval
        {
                $$ = $1;
                TERM($$)->AppendRight(TERM($2)); 
        }
    | with_op  '('  ')'   body
        {
                $$ = $1;
                TERM($$)->AppendRight(TERM($4)); 
        }
    | with_op  '('  args  ')'  body
        {
                $$ = $1; 
                TERM($$)->m_args.swap(TERM($3)->m_args);
                TERM($$)->AppendRight(TERM($5)); 
        }
    |  with_op  '('  args  ')'  body  body_else
        {
                $$ = $1; 
                TERM($$)->m_args.swap(TERM($3)->m_args);
                TERM($$)->AppendRight(TERM($5)); 
                TERM($$)->m_block.push_back(TERM($body_else)); 
        }

await:  AWAIT  rval_var
        {
            $$ = $1; 
            TERM($$)->AppendRight(TERM($rval_var));
        }

yield:  YIELD
        {
            $$ = $1; 
        }
    | YIELD_BEGIN   rval_var   YIELD_END
        {
            $$ = $1; 
            TERM($$)->m_id = TermID::YIELD;
            TERM($$)->AppendRight(TERM($rval_var));
        }

when_prefix: WHEN_ALL
        {
            $$ = $1; 
        }
    | WHEN_ANY
        {
            $$ = $1; 
        }
        
when:  when_prefix '('  args  ')'
        {
                $$ = $1;
                TERM($$)->m_args.swap(TERM($args)->m_args);
        }
    
    
lambda_op: '['  ']'
        {   
            $$ = Term::Create(TermID::COROUTINE, "", trust::MapperRange{TERM($1)->m_mapperRange.begin, TERM($2)->m_mapperRange.end});
        }
    | '['  args  ']'
        {   
            $$ = Term::Create(TermID::COROUTINE, "", trust::MapperRange{TERM($1)->m_mapperRange.begin, TERM($3)->m_mapperRange.end});
            TERM($$)->m_args.swap(TERM($2)->m_args);
        }

   
    
lambda:  lambda_op  '('  ')'   block_any
        {
                $$ = $1;
                TERM($$)->AppendRight(TERM($4)); 
        }
    | lambda_op  '('  args  ')'  block_any
        {
                $$ = $1; 
                TERM($$)->m_args.swap(TERM($3)->m_args);
                TERM($$)->AppendRight(TERM($5)); 
        }
    | lambda_op  '('  ')'  types  block_any
        {
                $$ = $1;
                TERM($$)->SetType(TERM($types));
                TERM($$)->AppendRight(TERM($block_any)); 
        }
    | lambda_op  '('  args  ')'  types  block_any
        {
                $$ = $1; 
                TERM($$)->m_args.swap(TERM($3)->m_args);
                TERM($$)->SetType(TERM($types));
                TERM($$)->AppendRight(TERM($block_any)); 
        }


using_list: exit_prefix
            {
                $$ = $1;
            }
        | using_list  ','  exit_prefix
            {
                $$ = $1;
                TERM($$)->AppendRight(TERM($3));
            }
    
ns_using:  ELLIPSIS  '='  using_list
        {
            $$ = $2;
            TERM($$)->m_id = TermID::ASSIGN;
            TERM($$)->AppendLeft(TERM($1)); 
            TERM($$)->AppendRight(TERM($3)); 
        }
    
/*  expression - одна операция или результат <ОДНОГО выражения без завершающей точки с запятой !!!!!> */
seq_item: assign_seq
            {
                $$ = $1;
            }
        | doc_before assign_seq
            {
                $$ = $assign_seq;
                TERM($doc_before)->RightToBlock(TERM($$)->m_docs);
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
                $$ = driver.CheckModuleTerm(TERM($1));
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
            }
        | seq_item  doc_after
            {
                TERM($doc_after)->RightToBlock(TERM($seq_item)->m_docs);
                $$ = $1;  
            }
        | sequence  separator  seq_item
            {
                $$ = TERM($1)->AppendBlock(TERM($seq_item), TermID::SEQUENCE);
            }
        | sequence  separator  doc_after  seq_item
            {
                TERM($doc_after)->RightToBlock(TERM($seq_item)->m_docs);
                $$ = TERM($1)->AppendBlock(TERM($seq_item), TermID::SEQUENCE);
            }
        | sequence EMBED
            {
                // Последовательный {% ... %} блок (без ';' между ними) — отдельный
                // seq_item: каждый блок становится собственным EmbedExpr-узлом со своим
                // range, а не конкатенируется с соседями (иначе блоки склеиваются на
                // одной строке в C++ и теряют корректный диапазон).
                $$ = TERM($1)->AppendBlock(TERM($2), TermID::SEQUENCE);
            }


ast:    END
        | separator
        | sequence
            {
                driver.AstAddTerm($1);
            }
        | sequence separator
            {
                driver.AstAddTerm($1);
            }
        | sequence separator  doc_after
            {
                TERM($doc_after)->RightToBlock(TERM($1)->m_docs);
                driver.AstAddTerm($1);
            }
        | separator  sequence
            {
                driver.AstAddTerm($2);
            }
        | separator  sequence separator
            {
                driver.AstAddTerm($2);
            }
        | separator  sequence separator  doc_after
            {
                TERM($doc_after)->RightToBlock(TERM($1)->m_docs);
                driver.AstAddTerm($2);
            }

start	:   ast

%% /*** Additional Code ***/