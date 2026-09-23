---
slug: prog-styles-same-time
title: Can declarative and imperative styles of writing programs be used simultaneously?
date: 2021-07-26
tags: [programming, language-design]
---

![Featured image](/en/blog/programming/matrix.jpeg)

{{% pageinfo %}}

Attention!!!

This article contains a description of the syntax of the previous version of NewLang.

The current version of the language syntax can be viewed [here](/en/docs/).

{{% /pageinfo %}}


When developing his own programming language, the author must decide what properties his language should have,
because in the end it is precisely this that will determine the purpose of the language and the features of its use.
Moreover, some properties influence the final concept of the language and its syntax so strongly
that subsequently their change is impossible in principle, or the new syntax turns out to be very tangled and incomprehensible.
Probably because of this many properties of programming languages are considered and studied as mutually exclusive.

Until recently I believed that the imperative and declarative paradigms of writing programs are antagonists and mutually exclusive notions.
After all, the choice of the style of writing code is initially determined by the author of the language at the stage of its design and influences all subsequent aspects.

But now I think that this is not true. The imperative and declarative styles of programming are not mutually exclusive,
but the rules of syntax implemented in the language push one to write programs in only one of these paradigms!

In other words, a developer is forced to use only the imperative or only the declarative paradigm not because he does not know how or does not want to write code differently, but because
all more or less mainstream programming languages are oriented toward the use of only one paradigm.
And the choice of only one paradigm of writing code is a limitation imposed on programmers by the creators of the language.
After all, if they initially focused only on one concept of code development, then they developed the syntax of the language in accordance with that paradigm.

This article is a reflection on the compatibility of the declarative and imperative paradigms of programming and the possibility of using them simultaneously within one programming language.

## What is "Declarative programming"?
First I wanted to figure out whether purely declarative programming languages exist?
Ones that can do entirely without imperative constructs?

But I almost stumbled at the very beginning, since even the very definition of "declarative programming" is described on the wiki [as follows](https://en.wikipedia.org/wiki/Declarative_programming):
>Declarative programming is a programming paradigm in which a specification of the solution of a task is given, that is, it is described what the problem is and what the expected result is. The opposite of declarative is imperative programming, which describes, at one or another level of detail, how to solve the task and present the result.

Already in the original definition an opposition of paradigms is laid down, and then comes reasoning about the "direction" of programming.
And although the wiki itself is not an authoritative source of information, even such a draft of an article was able to show the direction of the search for answers to the first question posed.

And one paragraph at the end of the article made me think:
> "Purely declarative" computer languages are often not Turing-complete — since it is theoretically not always possible to generate executable code from a declarative description.
> This sometimes leads to disputes about the correctness of the term

Judging by the presence of such a caveat, the existence of "purely declarative" languages is called into question.
And as soon as I had to think about this thought, it immediately became obvious that whatever declarative programming language existed,
it must have at least one imperative construct that launches the search for a solution!
After all, any synonym of the word "execute" or "run" will by definition be an imperative!

For example, in the Prolog language, which is usually given as an example of a declarative programming language, the "disguised" imperative operator is the question mark.
It seems natural and logical, how could one do without it? But in essence, it is an imperative construct!

The same applies to the SQL language. It too is seemingly declarative, but unexpectedly all its commands are imperative in essence. _SELECT_, _CREATE_, _ALTER_, _DROP_, _INSERT_, _UPDATE_, _DELETE_, etc., and only the descriptions of the conditions of their execution are declarative!

As a result, I never managed to find a purely declarative programming language without imperative operators (maybe the Wiki is right and such a programming language does not exist at all?).

## And what distinguishes declarative programming languages from imperative ones?
According to the definition, programming languages are called declarative because of the possibility of writing certain conditions in a declarative style.
But besides this, declarative languages, as a rule, also have a certain internal mechanism for searching for solutions.
Such a "computer" exists in both SQL and Prolog and in many other declarative languages.

It is interesting whether the internal system of searching for solutions is an obligatory feature of the declarative style of programming, or whether it is only a feature of a specific programming language that does not depend on the declarative style of writing code?

To test these assumptions, one could try to write a classical declarative program in an imperative language that lacks an internal mechanism for searching for solutions.

And at the same time, in this way one could also study the question of whether it is possible to write a program in a declarative style using an ordinary imperative programming language?

For an example, I decided to try to repeat the already classical declarative program in Prolog.

```cpp
    parent("Tom","Jake").
    parent("Janna","Jake").
    parent("Tom","Tim").
    male("Tom").
    male("Tim").
    male("Jake").
    female("Janna").

    brother(X,Y):-parent(Z,X),parent(Z,Y),male(X),male(Y),X\=Y.
    ? brother
```
The result was the following functionally equivalent program in C++, which is as close in style as possible to the declarative prototype:


```cpp
    enum sex_ {
        male,
        female
    };

    struct human;
    typedef std::vector<human *> parent_;

    struct human {
        const char *name;
        sex_ sex;
        parent_ parent;
    };

    human Tom{"Tom", male,{}};
    human Janna{"Janna", female,{}};
    human Jake{"Jake", male,{&Tom, &Janna}};
    human Tim{"Tim", male,{&Tom}};

    std::vector<human *> humans{&Tom, &Janna, &Jake, &Tim};


    auto brothers = [](human * a, human * b) {

        auto intersec = [](parent_ &a, parent_ & b) {
            for (auto elem_a : a) {
                for (auto elem_b : b) {
                    if(elem_a && elem_b && elem_a == elem_b) {
                        return true;
                    }
                }
            }
            return false;
        };

        return a && b && a != b && a->sex == male && b->sex == male && (intersec(a->parent, b->parent));
    };

    for (auto a : humans) {
        for (auto b : humans) {
            if(brothers(a, b)) {
                std::cout << a->name << " and " << b->name << "\n";
            }
        }
    }
```

Of course, the C++ text turns out significantly more verbose than the Prolog variant, but in essence it is almost a verbatim repetition of the declarative style of writing code.
All the more so because one should not forget the reasoning at the beginning of the article about the initial choice of a concept when creating a language
and the "coercion" of programmers to use only one, initially chosen development paradigm.

**Thus, one can assert with a high degree of confidence that theoretically it is possible to write code simultaneously in different styles.**

But what then prevents one from writing, within one programming language, using the imperative and declarative style?
Could it be only the conviction of the creators of languages that the imperative and declarative styles of programming are mutually exclusive?
And only because of this do we get programming languages whose syntax is suitable for using a single paradigm?

_But if this is so, then what prevents one from trying to develop a syntax of a programming language in which one could use both the imperative and declarative styles of programming simultaneously?_

## What prevents combining the imperative and declarative styles of writing within one program?
In any computer program there is always a division of the code into a description of data and into language constructs for processing them, i.e. in fact this is a division **into data** and **into functions**.
It seems to me that the main difficulty that does not allow using different programming styles within one language is the need to separate the described entities into "data" and "control constructs".
After all, this property (the need to separate entities into "data" and "functions") is an integral part of any programming language.

This is not surprising, because at the dawn of the formation of the IT industry, the creators of the first programming languages focused exclusively on the imperative style, because the purpose of any compiler was to convert the source text of a program into machine instructions. And on the examples of the modular, structural and object-oriented approaches, the necessity of formatting executable code into dedicated procedures with their subsequent grouping into modules and classes was shown and proved.

And the declarative style of writing programs began to appear only after the creation of high-level programming languages.
And the main goal of creating these languages shifted to finding a solution for the end user, and not to simplifying the generation of binary files with machine instructions.
If you look carefully at the examples of code given above, you can notice that in them the data definitions and the operators for processing them go intermixed (for example, in C++ this is the definition of lambda functions), which is fundamentally different from the imperative approach.

So maybe the main feature of the declarative style is precisely that it does not separate "data" and "actions on data"?
Or, as an option, one may not specify the actions performed on the data at all (as in some SQL constructs)?

Maybe it is precisely this feature (the possibility of sequential writing of program code in accordance with one's own logical reasoning,
in which "data" and "functions" can be interleaved, as happens in the human thought process),
that does not allow fully realizing the possibility of combining the imperative and declarative styles of programming?

_And if this is so, then one can try to develop a syntax that will support both the definition of data and the declaration of functions within a single flow of language constructs!_


## Testing the hypothesis in a new programming language
To test this assumption, I decided to add to my [new programming language](/en/blog/language-design/unusual-prog/)
(whose syntax allows defining functions in the same flow as the description of the processed data) the missing algorithmic constructs that would make it possible to implement the imperative style of programming,
despite the initial orientation only toward the declarative paradigm.

True, taking into account the initial limitations of the syntax of the new language (the prohibition on the use of operators in the form of reserved keywords),
as the condition check operator a syntactic construct was chosen that in meaning corresponds to the term "follows",
i.e. a dash and an angle bracket "**->**".

As a result, the conditional operator turned out to be practically mathematical, which is easily combined into sequences for implementing the check of multiple conditions of the "else if" kind.
And to combine several logical operators at once and to separate them from the subsequent action, the condition check operators can be enclosed in parentheses.

In the general case, the conditional operator in the new programming language has the form:
~~~cpp
    condition -> action;
    or  
    (condition) -> {action};
    or  
    (condition1 || condition2) -> {action} -> {otherwise action};
~~~

Or an extended variant, written with indentation for clarity:
~~~cpp
    (condition1) -> {action1}
    (condition2) -> {action2}
    (condition3) -> {action3}
    -> {otherwise_action};
~~~

Then the loop operators can also be written practically analogously, only by choosing as the operator not the follow operator,
but follows-with-return (since one needs to specify a cyclic action).

The while loop:
~~~cpp
    (condition) <-> {loop body};
~~~
A counting loop for working with an iterator: 
~~~cpp
    (counter_or_data!)? <-> {loop body};
~~~

In this case, the solution of the test example of the declarative Prolog program can easily be formatted both in the imperative and in the declarative style at the same time!

~~~bash
    m := "male";
    f := "female";
    human @= term(sex=, parent=[,]);
    Tom @= human(sex=m);
    Janna @= human(sex=f);
    Jake @= human(m, [&Tom, &Janna,]);
    Tim @= human(sex=m, parent=[&Tom,]);


    brother(h1, h2) &&= $h1!=$h2, $h1.sex==m, $h2.sex==m, $h1.parent & $h2.parent; 
    // The "&" operator is bitwise "AND" for numbers or the intersection operation for sets


    // Writing the solution search algorithm in the imperative style 
    (h1=human!, h2=human!)? <-> {
        (brother(h1, h2)) -> {
            @print($h1, $h2, "\n");
        }
    };

    // A short notation of the solution search algorithm in the imperative style 
    (h1=human!, h2=human!)? <-> 
        brother($h1, $h2) -> @print($h1, $h2, "\n");

    // Writing the solution search in the declarative style
    brother(human!, human!)?;

~~~

This is roughly what needed to be shown!

### P.S.
I have almost finished the experiments with the syntax of the language.
And now, after adding the missing algorithmic constructs to it (branching and loops), I plan
that the next article about the new programming language will be devoted to a description of its full syntax and the publication of the sources of a compiler prototype so that one can play around with real examples.

### P.P.S
In the comments @gbg gave a very simple answer to one of the questions raised.
And in a more elegant form and without writing code!

> And a few pennies about OOP. Strangely enough:
>
> 1) It does have a declarative part, all these pubic, private, virtual, etc.
> 2) This declarative part, unexpectedly, elegantly allows implementing decomposition and complexity management.
> 3) With overloading, one can play at algebraic thinking, considering the interaction of two objects as a binary operation.
>
> Wow, OOP is a technology proven by practice that combines the declarative with the imperative and seems to allow those who have mastered it to eat tastily and sleep softly!

Original publication

