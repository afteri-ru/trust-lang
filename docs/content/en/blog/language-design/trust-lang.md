---
slug: trust-lang
title: A new programming language
date: 2022-02-18
tags: [trust-lang, language-design, programming-languages, syntax]
---


![Featured image](/en/blog/langs.jpeg)


{{% pageinfo %}}

Attention!!!

This article contains a description of the syntax of the previous version of NewLang.

The current version of the language syntax can be viewed [here](/en/docs/).

{{% /pageinfo %}}


More than a year ago I began publishing articles describing the features of a new programming language.
Much water has flowed under the bridge since then, many ideas were tested, in the end everything changed radically several times,
and now I present to the readers a description of the pre-final version of the language and its features.

This article is intended first of all for testing the main concepts of the new programming language, and also for getting feedback from Habr readers.
After all, according to the observation Habr is a chamber of wisdom, an unspoiled outside view helps a lot in developing new ideas.

This project was without its own name for a very long time and in the publications it was called simply and abstractly "the new language".
But after several articles, the temporary name "the new language" gradually turned into the proper name _TrustLang_,
which I decided to keep in the end (which once again confirms the saying that there is nothing more permanent than something temporary).

**TrustLang** is a high-level programming language in which one can combine standard algorithmic constructs
with declarative programming and tensor computations for machine learning tasks.

The main feature of the language is a light, logical and non-contradictory syntax, which is based not on the use of reserved keywords,
but on a strict system of grammar rules using punctuation marks (the list of which also includes the operators of the language).
The main properties and features of the language:

- The ability to work both in interpreter and compiler mode.
- Dynamic and static typing with the ability to specify types explicitly.
- Static typing is conditionally strict (there is no automatic type casting, but conversion between some data types is allowed, for example, an integer can be automatically converted to a real one, but not vice versa)
- Automatic memory management.
- OOP in the form of explicit class inheritance and [duck](https://en.wikipedia.org/wiki/Duck_typing) typing.
- At the syntax level, support for several types of functions (ordinary and [pure functions without side effects](https://en.wikipedia.org/wiki/Pure_function)).
- Optional and named function parameters.
- Inserts of code in the implementation language (C/C++) are possible.
- Simple integration with already existing software libraries (including the import of native variables and functions from C/C++).

## Why is TrustLang needed?
All modern programming languages undergo constant development (read: complication) of the syntax as new versions are released.
This is a kind of payment for the appearance of new capabilities and is perceived by users as a natural phenomenon.

But at the same time it is also a serious problem, because with the release of new versions new keywords and syntactic constructs are added,
which inevitably raises the entry threshold for new users. Another consequence of this process is the constant increase in the complexity of development
and the labor intensity of maintaining already created software products, when old code is refined using already-new standards.

In TrustLang the complexity of language constructs is naturally limited through the division of the language syntax into two parts,
which simplifies its study and use. _The base syntax_ — for writing programs in the object-oriented (imperative)
and declarative styles, which is based not on reserved keywords, but on strict grammar rules,
and _the native syntax_ — when an implementation-language construct needs to be used directly.

Another inconvenience of modern mainstream languages is that most of them were created before the beginning of the era of machine learning,
therefore tensor computations in them are implemented as separate libraries, and not built into the base syntax of the language and the system of base types.
In _TrustLang_, tensor computations are available "out of the box" (the [libtorch](https://pytorch.org/) library is used),
and arithmetic data types are scalars (tensors of zero dimension).

### The base syntax
The base syntax of _TrustLang_ is simple and logical due to the fact that it is built exclusively on grammar rules
and does not use any reserved keywords, while all letter-symbol sequences are considered identifiers
in which any non-ASCII characters can be used.

> The idealized goal of abandoning keywords is to bring the reading of the source text of a program closer to the reading of ordinary text through the use of punctuation marks when describing the logic of the algorithm.

> Of course, a human can single out the key control words of the language with a _comma_ and a _slash_ or take into account the formatting of the program _comma_
> in order to understand syntactic constructs on their basis _comma_, although in ordinary reading we are used to relying precisely on the semantics
> of punctuation marks _full stop_ we can of course write punctuation marks in ordinary text _full stop_ but agree _comma_ that then
> _open bracket_ for example _comma_ such a text _close bracket_ will be very inconvenient to read _full stop_

The names of the built-in types or the names of the service functions of the system library are determined by the concrete implementation of the language,
therefore they are not reserved keywords and can be redefined if necessary, for example,
to create one's own domain-oriented dialect ([DSL - domain-specific language](https://en.wikipedia.org/wiki/Domain-specific_language)), if the need arises.
But the very structure of the program and the logic of the executed algorithm will still remain understandable to everyone who is familiar with the rules of the base TrustLang syntax.

**Example of a Hello world! script in TrustLang**
~~~bash
    #!./nlc --eval 
    # Definition of the hello function
    hello(str) := { 
    printf := @import('printf(format:Format, ...):Int');  # Import of a standard C function
    printf('%s\n', $str);  # Calling a C function with type checking of the arguments against the format string
    };
    hello('Hello, world!'); # Call the function
~~~
Output: Hello, world!

### The native syntax
The native syntax is the ability to insert source code in the implementation language into the text of a TrustLang program.
Currently this is C/C++, which makes it possible to use any capabilities of this powerful programming language.

The processing of the native syntax occurs at the stage of compiling the application,
and the interaction between the base and the native syntaxes occurs through the joint use of identifiers,
which is completely transparent to the user and obeys unified grammar rules.

### A few more examples:
Any sequence of computations returns the result of the execution of the last statement.
Therefore the execution of one command or a sequence of commands always returns some result,
and the return operator from a function is optional, since the result will be the value of the last computed expression.

**Creating variables**
~~~bash
    scalar := 42
    42
~~~
~~~bash
    tensor := [1,2,3,4,5,]  # The type of the tensor is inferred automatically
    [1, 2, 3, 4, 5,]:Char
~~~
~~~bash
    str := '$1 string'
    $1 string
~~~

**Arithmetic operations**
~~~bash
    tensor * 2
    [2, 4, 6, 8, 10,]:Short
~~~
~~~bash
    tensor * 20
    [20, 40, 60, 80, 100,]:Short
~~~
~~~bash
    tensor * 0.5
    [0.5, 1, 1.5, 2, 2.5,]:Double
~~~
~~~bash
    tensor / 2 # The result of division is a floating-point number
    [0.5, 1, 1.5, 2, 2.5,]:Double
~~~
~~~bash
    tensor // 2 # Integer division without a remainder
    [0, 1, 1, 2, 2,]:Char</source>
~~~
~~~bash
    tensor % 2 # Integer remainder of division
    [1, 0, 1, 0, 1,]:Char</source>
~~~

**String operations**
~~~bash
    str = 'string concatenation ' ++ str;
    string concatenation $1 string
~~~
~~~bash
    str('string as template');
    string concatenation string as template string
~~~

### Tensor conversion
In the era of machine learning, tensors are the main elements of computations,
therefore a separate syntactic construct is used to convert _data_ into tensors,
consisting of double square brackets **[[** _data_ **]]**. More about the features of type conversion can be read further.

~~~bash
    tstr := [["Тест"]]   # Create a tensor from a wide character string
    [1058, 1077, 1089, 1090,]:Int
~~~

~~~bash
    t2 := [[ "Тест" ]]:Int[2,2] # The same, but a two-dimensional tensor
    [
        [1058, 1077,], [1089, 1090,],
    ]:Int</source>
~~~

~~~bash
    StrWide(tstr) # Convert the tensor back to a string
    Тест
~~~

~~~bash
    Double(t2)    # Change the data type of the tensor
    [
        [1058, 1077,], [1089, 1090,],
    ]:Double
~~~

~~~bash
    t3 := [[ t2 ]]:Char[4] # Convert the data type of the tensor and its dimensionality
    [34, 53, 65, 66,]:Char
~~~


## TrustLang syntax:
When developing the syntax, I tried to adhere to already established rules, so as not to create multiple meanings
that depend on the context. ~~And at the same time "to embrace the boundless"~~

### Basics

- Statements are separated by a semicolon ";".
- Indentation and line breaks are ignored (I very much wanted to have the possibility of automatic code formatting).
- Multiline comments in the source code correspond to the C/C++ style and must be placed between the characters /* and */. Nesting of multiline comments is not supported.
- Single-line comments start with the "#" character up to the line break, which corresponds to comments in the Python and Bash style.
- A sequence of executable commands that must be executed as a whole is enclosed in curly braces "{}".
- Extended-syntax program inserts in the implementation language are enclosed in curly braces with a percent sign **%{** _/* any C/C++ code can be here */_ **%}**.

### Creating objects and assigning new values
TrustLang uses three different operators to create objects and assign new values to them.   
The operator "**::=**" is used only to create new objects, and if an object with such a name already exists, an error is generated.  
The operator "**:=**" is used for the same purposes, but if an object with such a name already exists, no error occurs,
and the new value is assigned to the already existing object.  
And the last operator "**=**" is applied only to assign a value to already existing objects, and if an object with the specified name is absent, an error also occurs.

Using three different operators for creating/changing objects makes it possible to control such operations more flexibly and to detect logical errors in the code at an earlier stage.

~~~cpp
    var ::= 1.0; # Create a new variable var without specifying a type
    var = 100; # Assign a new value to an already existing variable
    printf := @import('printf(format:Format, ...):Int'); /* Create a new or redefine
    the object printf, which will be
    the result of executing the global function @import */
~~~

### Object identifiers and modifiers
As identifiers one can use letters, digits and underscores in any combinations, provided
that the first character of the identifier is not a digit.

TrustLang has the possibility of specifying the scope and lifetime of an object using a modifier — a special character before
the variable name. This may seem a bit similar to Hungarian notation, but unlike it,
the modifier has nothing to do with the type of the object and is not part of the identifier name.
Moreover, strictly defined characters are used as modifiers, whose purpose is determined in advance.

So, the character "**$**" at the beginning of a name denotes a local variable whose lifetime is limited by the current scope, and when it ends the local variable is destroyed.
The character "**@**" denotes a global variable, and the object itself retains its state even after leaving the current scope.
The names of data types are denoted in the same way, for example when creating new types, and the colon character "**:**" is used as the modifier.

The semantics of accessing function arguments is very similar to working with arguments in bash scripts, where **$1** or **$arg** is the ordinal number
or the name of the argument (access occurs to local variables in the current scope).

The use of modifiers is mandatory only in two cases:
    - When creating a new data type, since types are always created in the global scope, and their symbolic names must be unique
    - When accessing TrustLang objects inside program inserts of code in the implementation language, since they are used as markers when searching for TrustLang identifiers in C/C++ code.

In the remaining cases, it is not mandatory to specify their modifiers to access variables.
And if no modifier is specified when accessing an object, then first a local variable is searched, and then a global one with the same name.
Moreover, the local variable will shadow the global one.

One should also keep in mind that the compiler can generate code for direct access to local objects already at compile time,
whereas for accessing global objects, or if the scope modifier is absent, the compiler is forced to embed a runtime call of the object search function in the symbol table every time.


## Type system
Since the type system of the language is dynamic, an explicit type indication does not affect the size of a variable and is only a kind of logical restriction on the possibility of assigning a value of another type to the variable.

Type information is used when checking their compatibility, when a value of another type is assigned to an existing object.
Such an operation is possible only when the types are compatible with each other and allow automatic casting.
This is true both during the parsing/compilation of the source text and during execution in interpreter and/or compiled file modes.

### Arithmetic types:
Arithmetic data types are tensors — arrays of numbers of one type with an arbitrary number of dimensions and the same column size in each.
A single number is also a tensor of zero size.

Only signed integers are supported, since there is no particular need for unsigned numbers, and many problems with them can be found out of the blue.

**Problems of unsigned numbers (from the internet)**
> First, the subtraction of two unsigned numbers, for example 3 and 5. 3 minus 5 equals 4294967294,
> since -2 cannot be represented as an unsigned number. Second, unexpected behavior can occur when mixing integer
> values with and without a sign. C++ can freely convert signed and unsigned numbers,
> but does not check the range to make sure you are not overflowing your data type.
> 
> In C++ there are still a few cases where one can (or must) use unsigned numbers.
> First, unsigned numbers are preferable when working with bits. Second, the use of unsigned numbers related to array indexing.

But this is my case, since an index can be negative and even not a number, but a range or an ellipsis.
P.S. And even knowing this, I still recently managed to catch a bug with negative indices of dictionaries!

The names of the built-in arithmetic types speak for themselves: Char, Short, Int, Long, Float, Double, ComplexFloat, ComplexDouble.
The boolean type Bool is a separate type, which can take only the values 0 or 1 (false/true respectively),
and depending on the operation performed can be attributed to integer types or not be part of them.

_(this approach to interpreting the boolean data type was taken from the Torch library)_
~~~cpp
// Treat bool as a distinct "category," to be consistent with type promotion
// rules (e.g. `bool_tensor + 5 -> int64_tensor`). If `5` was in the same
// category as `bool_tensor`, we would not promote. Differing categories
// implies `bool_tensor += 5` is disallowed.
//
// NB: numpy distinguishes "unsigned" as a category to get the desired
// `bool_tensor + 5 -> int64_tensor` behavior. We don't, because:
// * We don't want the performance hit of checking the runtime sign of Scalars.
// * `uint8_tensor + 5 -> int64_tensor` would be undesirable.
~~~

In the future it is planned to add number classes for long arithmetic and fractions, for which the type names BigNum, Currency and Fraction are reserved.

Access to the elements of a tensor occurs by an integer index that starts at 0.
For a multidimensional tensor, the indices of an element are listed in square brackets separated by commas.
Access to elements through a negative index is supported, which is handled exactly the same way
as in Python (-1 the last element, -2 the penultimate one, etc.).

A tensor literal in the program text is written in square brackets with a mandatory trailing comma, i.e. [1, 2,] is a literal one-dimensional tensor of two numbers.
After the closing bracket, the type of the tensor can be specified explicitly. If the type is not specified, then it is inferred automatically on the basis of the specified data and the minimally possible byte size is chosen,
which allows storing all values without loss of precision.

Examples:
~~~bash
    $var_char := 123; # The type Char is inferred automatically
    $var_short := 1000; # The type Short is inferred automatically
    $var_bool := [0, 1, 0, 1,]; # A tensor of 4 elements. The type Bool is inferred automatically
    $tensor[10,10]:Int := 1; # A tensor Int of size 2x2 initialized with 1
    $scalar := $tensor[5,5]; # Assign to a scalar the value of the specified tensor element
~~~


### String data types:
Two types of strings are supported: StrWide — character (wide) and StrChar — byte. The difference between them lies in the type of the single element.
For character strings the single element is the wide character wchar_t, and for a byte string the single element is one byte (more precisely char, i.e. a signed byte).
Character string literals in the source text are written in "double quotes", and byte strings in 'single quotes'.

The number of elements of a character string is returned in wide characters, and the size of a byte string in bytes, therefore access to a string element by index occurs respectively either to a character or to a byte.

An important point. Any variable can be addressed in the same way as a function (by writing parentheses after its name).
The result of this operation will be the creation of a copy/clone of the object. Moreover, some types (dictionaries, classes and character strings) can be used as a template
when creating a copy of an object with modified properties, if the new and/or changed values are specified in parentheses, as arguments in function calls.
So, if when creating a copy one specifies a set of new data in parentheses, then the resulting copy will contain the already changed data.

For example:
~~~bash
$template := "${name} $1"; # An ordinary string
$result := $template("template", name = "String"); # result = "String template"
~~~

### Composite data types:
#### Dictionary
A dictionary is a set of data of an arbitrary type with access to individual elements by an integer index or by the name of the element (if present)
(it resembles both a tuple and a structure at the same time). Dictionaries differ from tensors in that they are only one-dimensional arrays,
but each element can contain an arbitrary number of elements of any type, including other dictionaries.

Access to the elements of dictionaries occurs by the name of the element, which is written through a dot after the name of the variable, or by an integer index.
The index also starts at 0 and, like tensors, can also be negative.

A literal of the "dictionary" type in the program text is written in parentheses with a mandatory trailing comma,
i.e. (,) is an empty dictionary, (1, 2= "2", name=3,).

### Enumeration
An enumeration is not a separate data type, but an ordinary dictionary in which all elements have unique names and an integer value
that is specified explicitly during the definition or computed automatically (one more than the previous element).
For enumerations, the value type is specified immediately after the closing bracket through a colon (ONE=1, TWO=, THREE=): Int.

#### Classes
A class (implemented partially) is a data type with the help of which one of the principles of OOP is implemented — inheritance.
When creating an instance of a class, a new variable is created that retains information about its parent
and inherits properties and methods from it. The "class" data type is similar to a dictionary,
but all properties must have names (although access to the properties of a class by index is also possible).
A literal of the "Class" type in the program text is written in parentheses without a trailing comma, i.e. () is an empty class, (1, 2= "2", name=3).

For now the remaining details of classes are not fully implemented, therefore I will not describe them, since in the final version the syntax of classes and the definition of their methods may change.

### Functions
The TrustLang syntax supports several types of functions (and in the future also class methods): ordinary functions, pure functions and simple pure functions.

Default arguments are supported for all types of functions. When creating a function, its arguments are specified as in Python,
i.e. first come the mandatory arguments, then the arguments with default values,
where the name of the argument is separated from its default value by the equals sign "=".
If a function allows handling an arbitrary number of arguments, then an ellipsis is specified last in the parameter list.

#### Ordinary function
An ordinary function — such functions are precisely ordinary functions in the C/C++ understanding.
Inside them one can write absolutely any code, including condition checks, loops, calls of other functions, etc.

Inside an ordinary function one can access local and global objects, and they can contain inserts in the implementation language, for example to call functions from external libraries.

Inserts in the implementation language are formatted as **%{**     **%}** and can contain any C/C++ text,
and directly from it one can access local and global TrustLang objects in the same way as in ordinary syntax,
by specifying the corresponding modifier as the first character of the name (**$** for local objects and **@** for global ones).

Technically, such a program insert is simply transferred by the transpiler directly into the source text of the generated file,
and all TrustLang identifiers are decorated in a special way (special markers are added for their identification),
after which the source text is fed to the input of an ordinary C++ compiler.
For local objects the transpiler can generate code for direct access to the object at compile time,
while for working with global objects it is forced to use runtime calls of the search function in the symbol table.

For example:
~~~cpp
print(str) := { 
    %{ 
        printf("%s", static_cast<const char *>($str)); /* A direct call of a C function */ 
    %} 
};
~~~


#### Pure functions
A pure function is also an ordinary function, only in the sense that functional programming puts into it.
The creation of a pure function occurs with the help of the operator ":-". A pure function has no access to the context and global objects,
therefore it can process only the data that were passed to it as arguments.

Program inserts in the implementation language inside pure functions are not forbidden and can be used, for example, for debugging.
But this is done at the developer's own risk. It is he who is responsible for their "purity", for example when calling functions from external libraries.

~~~bash
    Sum(arg1, arg2) :- {$arg1+$arg2;}; # Return the sum of the arguments
~~~

Since the language has no operator for returning data from the current execution block (an analog of the return <data> operator),
the returned value of a function / code block is always the result of the execution of the last operation.

#### Simple pure functions
Simple pure functions are a separate class of pure functions that are intended only for computing a logical result
(i.e. they are predicates) and are distinguished by a simplified notation form.
The body of a simple pure function consists of a sequence of statements separated by commas and ending,
like any expression, with a semicolon. All the statements of a simple pure function are always reduced to a boolean value,
and the final result of the function is computed by one of the possible logical operations:  _AND_, _OR_ and _exclusive OR_.

For example:
~~~bash
    func_and(arg1, arg2) &&= arg1==3, arg2 > 0;  # A simple pure function, logical AND
    func_or(arg1, arg2) ||= arg1==3, arg2 > 0; # A simple pure function, logical OR
    func_xor(arg1, arg2) ^^= arg1==3, arg2 > 0;  # A simple pure function, exclusive OR
~~~

### Special data types:
#### None
None (the empty type) — contains no value (more precisely it has a single value None) and is compatible with any other data type.
It is indicated in the program text as a single underscore "_". Uninitialized variables have the value None,
and an attempt to read from such a variable raises an error.

The type of a variable can be specified explicitly or inferred automatically from the assigned value.
A new value can be assigned to an already initialized variable only for a compatible type, since implicit type conversions are not allowed.

~~~bash
    $var := _; # Create a variable with the value None
    $var2 := var; # Error!!! Cannot read the uninitialized variable var
    $var = 1000; # The variable will have the type Short (the minimum size for storing the value)
    $var = 0,5; # Error!!! Short ← Float are incompatible
    $var = _; # Clear the value of the variable
    $var = 0,5; # Now it is possible, since None is compatible with any type
~~~

#### Range (Range)
A range (implemented partially) is a special data type that is an approximate analog of the "generator" type in Python.
A range can be addressed as an iterator and it will alternately yield elements in the specified interval with a given step.
A range in the program text is specified as two or three elements separated by two dots, for example 1..5 — a range from one to five with the default step 1.
As the parameters of a range one can specify not only literals, but also variable names.
For example, 0,1.._$stop_..0,1 — a range from the value 0.1 to the value of $stop with step 0.1.

A range for integers can be used as an index of tensors (more precisely, of any objects that allow access to their elements by index,
i.e. tensors, dictionaries and text strings). In fact, this behavior is analogous to slice in the Python language and array[1:5]
in Python means the same as array[1..5] in TrustLang.

As an index of tensors one can also specify an arbitrary number of dimensions with the help of an ellipsis, i.e.
~~~bash 
    $tensor[…, 0] = 0; # Zero out all the first elements in each dimension.
~~~

#### Iterators
Iterators (under development) are the most complex and ambiguous data type for working with the elements of collections.
For working with iterators the symbols "!" and "?" are reserved, but the iterators themselves are not yet implemented.


### Type conversion
#### Explicit type casting
Despite the dynamic typing of the language, if the type of a variable is specified explicitly, then automatic type casting is not performed,
and to assign a value of an incompatible type to the variable an explicit conversion is required.

Since the symbolic names of types relate to implementation details, an explicit conversion to a concrete data type is performed by calling a function with the system name,
i.e. Bool(), StrWide(), Long, etc. Moreover, for tensors such a conversion changes only the data type, but the dimensionality of the tensor does not change.

To convert any data type to a string, one can also use the string concatenation operator, which converts any data type to a string representation.
But since there are two string types (byte and wide strings), the string type is determined by the first argument in the concatenation operator, i.e.
~~~bash
    "" ++ 123 # "123" - a wide character string
    '' ++ 123 # '123' - a byte string
~~~
    Or convert any value to a string with the help of a template string:
~~~bash
    val := 12345;
    "$1"(val) # Will be the string "12345"
~~~

#### Tensor comprehensions
In the era of machine learning, tensors are the main element of computations, therefore a separate syntactic construct is used to convert _data_ into tensors,
consisting of double square brackets **[[** _data_ **]]**. In fact, it is an operator and a runtime function depending on the expression specified between the double square brackets.

To convert any variable into a tensor (taking into account the admissibility of such a conversion), it is enough to specify it between double square brackets.
The expression **[[** _varibale_ **]]** converts the variable _varibale_ into a one-dimensional tensor with automatic inference of the data type.
To convert into a one-dimensional tensor of a concrete type, the expression **[[** _varibale_ **]]**:_Type_ is used, where _Type_ is any of the arithmetic types.

If it is required to convert a variable not into a one-dimensional tensor, but into a tensor of a concrete type and a given dimensionality,
then this is done with the expression [[ varibale  ]]:Type[2,2], which will return a tensor with dimensionality 2x2 and the type Type of the elements.

Inside the double square brackets there can be not only any expression, but also a literal or a range.
In this case, they are also expanded into a tensor by the same rules.  
In the future I plan to add the possibility of specifying several values at once separated by commas to combine them into one tensor.

Examples:
```python
>[[(1,2,3)]]  # A tensor from a dictionary
[1, 2, 3,]:Char

>[['first second']]  # A byte string into a tensor
[102, 105, 114, 115, 116, 32, 115, 101, 99, 111, 110, 100,]:Char

> [[(first='first', space=32, second='second')]]  # We get a tensor from a dictionary with the same data
[102, 105, 114, 115, 116, 32, 115, 101, 99, 111, 110, 100,]:Char

>[[ 0 ... ]]:Double[10,2]   # A tensor of the given format with zeros
[
  [0, 0,], [0, 0,], [0, 0,], [0, 0,], [0, 0,], [0, 0,], [0, 0,], [0, 0,], [0, 0,], [0, 0,],
]:Double

>[[ rand() ... ]]: Int[3,2]  # A tensor with random data
[
  [1804289383, 846930886,], [1681692777, 1714636915,], [1957747793, 424238335,],
]:Int

>[[ 0..10 ]]: Int[5,2]  # A tensor from a range
[
  [0, 1,], [2, 3,], [4, 5,], [6, 7,], [8, 9,],
]:Int

>[[ 0..0.99..0.1 ]]  # Or even like this
[0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9,]:Double</source>
```

## Operators and control constructs
#### Operators:

All operators have a paired operator with value assignment.
- **+** and += addition of arithmetic data types
- - and -= subtraction of arithmetic data types
- / and /= division (the result is a floating-point number)
- // and //= integer division rounded toward the smaller number (as in Python)
- * and *= multiplication
- \*\* and \*\*=  exponentiation (it is also used to repeat text strings)
- ++ and ++= string concatenation with automatic casting of the arguments to the string type (the increment character is deliberately used instead of a single plus in order to explicitly separate string concatenation and arithmetic addition operators)

#### Comparison operators:

- <, >, <=, >=  the classical ones for comparing scalars
- ==, != comparison operators with automatic casting of compatible types for any objects
- ===, !== the exact comparison operator for any objects (no automatic type casting is performed)s


### Type checks (under development):
The class name check "~" — somewhat similar to the instanceof operator in Java.
The left operand must be the object being checked, and the right operand the name of the type, a string literal or a string-type object with the class name.
The result of the operation will be true if the right operand contains the name of the class of the object being checked or it is present in the inheritance hierarchy of the class being checked.
~~~bash
    name := "class"; # A string with the class name
    var ~ :class; 
    var ~ "class";
    var ~ name; 

    (field1="value", field2=2, field3="33",) ~~ (); # True (i.e. the left operand is a dictionary)
    (field1="value", field2=2, field3="33",) ~~ (field1=_); # Also true (since the field field1 is present in the left operand)
~~~


Duck typing "~~" — an approximate analog of the isinstance() function in Python, which for simple types compares the compatibility of the type of the left operand with respect to the right one, and for dictionaries and classes the left operand is checked for the presence of all the field names present in the right operand. i.e.
~~~bash
    (field1="value", field2=2, field3="33",) ~~ (); # True (i.e. the left operand is a dictionary)
    (field1="value", field2=2, field3="33",) ~~ (field1=_); # Also true (since the field field1 is present in the left operand)
    (field1="value", field2=2, field3="33",) ~~ (not_found=_); # False, since the field not_found is absent in the left operand
~~~

Strict duck typing "~~~" — for simple types the identity of types is compared without taking compatibility into account,
and for composite types all properties are compared strictly. For this operation, the empty type is compatible only with another empty type!

## Control constructs (under development)
### Conditional operator
As the condition check operator, a syntactic construct is used that in meaning corresponds to the term "follows",
i.e. a dash and an angle bracket "->". Such a notation of the conditional operator is very similar to a mathematical one and is easily combined into sequences for checking multiple conditions of the "else if" kind.

In the general case, the conditional operator has the form:
~~~bash
    condition -> action;
    or  
    (condition) -> {action};
    or  
    (condition1 || condition2) -> {action} -> {otherwise action};
~~~

Or the extended "else if" variant, written with indentation for clarity:
~~~bash
    (condition1) -> {action1}
        (condition2) -> {action2}
        (condition3) -> {action3}
        -> {otherwise_action};
~~~

### Loop operators (planned)
_Loop operators are still under development, since they are tightly connected with iterators._

For now I plan to use the constructs: (condition) **<-->** {loop body};

Or like this: (condition) **->>** {loop body};

And although I do not like the syntax very much, I decided not to rack my brains over it for now and plan to try several variants of the loop notation.

#### Operators for interrupting the command execution flow (implemented partially)
The operator for interrupting the command execution flow and returning from the current function, i.e. the closest analog of the return operator,
is the operator of two minus symbols "--". But unlike the classical return, the return operator does not return a value,
since the value from any function or code block is always returned and it is the result of the execution of the very last operation
(or None, if there is no such operation).

I have not yet figured out how to format the operator for interrupting the execution flow in case of an error (when it is executed, an exception will be generated),
therefore, if you have suggestions, write in the comments to the article (and about the formatting of loops too).


### Error handling (planned)
At the very beginning of the work I focused on the classical variant of exception handling,
which in ordinary programming languages is usually formatted with the keywords _try_ .. _catch_ .. _finally_ with various variations.
But under the conditions of strict restrictions on the syntax of the language, and the impossibility of using keywords,
combining characters to indicate different types of blocks in exception handling would be an extremely dubious undertaking.
After all, the main goal of developing TrustLang is the simplicity and understandability of the code, and here from the very beginning combinations of brackets, arrows,
bars and other similar symbols might appear.

And here a very simple thought came to mind. There is no need to repeat the logic of error handling from classical programming languages!
After all, the main goal of such syntactic constructs is to single out a section of code where an error may occur,
and to catch and handle the correct type of exception.
After all, classical programming languages were initially rigidly tied to the machine representation of data in the computer's RAM,
and the data type played a fundamentally important role for them.
But this is not a limitation for languages with dynamic typing!

Therefore, the approach to exception handling is planned as follows: The program code that may lead to an error is enclosed in double curly braces
**\{\{** <i>any code or a call of a single function</i>  **\}\}**,
and the result of the execution of such a code block is assigned to a variable. After that the returned value is analyzed and the type of the exception can be handled by an ordinary conditional operator.

It is probably easier to show this with an example:
~~~bash
    $error := {{ # the beginning of a try block
        call_or_exception1(); 
        call_or_exception2(); 
    }}; # the end of a try block
    # Ordinary conditional operators instead of typed catch blocks
    ($error ~ :type1)->{ error handling code 1}
    ($error ~ :type2)->{ error handling code 2};
~~~

The most surprising thing is that with such an approach the semantics of the _try_ **…** _finally_ blocks is also significantly simplified, as they become completely unnecessary.

The source code in Java:
```java
    try {
        try {
            throw new Exception("a");
        } finally {
            throw new IOException("b");
        }
    } catch (IOException ex) {
        System.err.println(ex.getMessage());
    } catch (Exception ex) {
        System.err.println(ex.getMessage());
    }
```

Its equivalent in TrustLang:
```bash
$catch := {{  
    $finally := {{  
	Error1("1");  
    }};
    Error2("2"); 	# The line will be executed even if the exception Error1 occurs
    $finally;		# Error1 will be returned if there is no Error2
}}
($catch ~ :Error1) -> printf("%s", $catch)
($catch ~ :Error2) -> printf("%s", $catch);
```

## How to try all this?
Currently the build of the project is implemented only for Linux, and if besides the text description you want to experiment live on your machine, you will have to build the interpreter from the sources yourself.

Since the current variant is intended first of all for working out the concept, some of the described capabilities are not yet implemented (algorithmic constructs, class inheritance, iterators, some operations, etc.).

But you can play with creating variables, calling functions and performing arithmetic operations on data, in order to evaluate the rule-based syntax, and maybe suggest your own thoughts and refinements for its improvement.

## The nlc utility (TrustLangCompiler)
At the present time nlc supports work only in interpreter mode (despite the name).
For testing and simple checking a compiler is not needed, although at first I did make exactly that.
But the labor intensity of constantly reworking it for the new syntax turned out to be very high, therefore for the time of the initial debugging of the language constructs a strong-willed decision was made to limit ourselves to the interpreter,
as a simpler and faster way of checking various hypotheses, and to postpone the development of a real compiler (in the form of a transpiler to the C++ language)
until the final working out of the syntax.

## Plans for the future
Naturally, one article and a few small examples do not give exhaustive information about the capabilities of the language.
Moreover, the capabilities themselves have not yet been fully revealed.
After all, the current version is rather a test platform for checking the declared concepts and the base syntax.

For now some of the declared capabilities and very important wants remain unimplemented.
But the main approach can already be tested now, and I will be grateful for any feedback and suggestions.

If we talk about plans (naturally, in future versions something may be added or the order of their implementation may change), but at the present moment the roadmap of TrustLang development seems to me as follows:
- Finish the standard control constructs, error handling and iterators.
- Refine the type system taking into account multiple class inheritance.
- Implement long arithmetic and fractions.
- Make some kind of logical game (tic-tac-toe, sudoku or something similar) with an algorithmic choice of the next move and its computation using machine learning.
- Write many different examples to evaluate the syntax.
- Refine the syntax taking into account the experience gained and the feedback.
- Restore the operability of the compiler for generating executable files.
- Do another big code cleanup.
- Rework and document the resulting semantics of the language taking into account all the capabilities and release the first full-featured version of TrustLang.

