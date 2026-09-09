---
slug: logic-prog
title: The problem of logical programming languages
date: 2021-01-09
tags: [theory, programming, programming-languages]
---

![Featured image](/en/blog/langs.jpeg)

Some time ago I wrote about ["International programming in natural languages"](/en/blog/language-design/inter-prog/),
in which I tried to present a worthy goal for an abstract programming language,
trying to try on it the role of a link between the world of programmers with computers and non-programmers.

But as a result it turned out that this is not needed in principle, since "non-programmers" simply do not need to learn to write programs.
And if such a desire does arise sometimes, then ordinary formalized programming languages are quite enough,
of which there are probably already more than ten thousand.

And users, both programmers and non-programmers, simply want to solve the tasks that arise before them.
And although the tasks are completely different, if the way (algorithm) to solve it is known, then choosing a language to solve it will not be difficult at all.

With the exception of one class of tasks. Tasks whose solution cannot be described in the form of an algorithm.
But one can specify certain criteria that the sought solution must satisfy.
I mean logical programming languages and [Prolog](https://en.wikipedia.org/wiki/Prolog), as the most striking representative of this class.

I also remember a recollection from my youth, when I managed to get a floppy disk with this language.
Oh, with what enthusiasm my eyes burned when it seemed to me that, just a little more and a system with a knowledge base would be created,
from which it would be possible to get the cherished answer 42 to any question.

So why did this not happen? What is the problem of Prolog, and indeed of any system / programming language
whose purpose is to analyze facts and search for answers to questions?

This problem is called the ["Combinatorial explosion"](https://en.wikipedia.org/wiki/Combinatorial_explosion) — an exponential (or greater) dependence of the running time of the algorithm on the amount of input data.
And there are at least two solutions to this problem.

## Approaches to writing programs
Before moving on to the particulars, we should say a couple of words about programming paradigms.
Usually two different styles of writing programs are opposed to each other: [imperative](https://en.wikipedia.org/wiki/Imperative_programming) and [declarative](https://en.wikipedia.org/wiki/Declarative_programming).

Imperative — this is the classic variant of writing a program, in which the programmer himself specifies the steps of the algorithm to obtain the final result.
And the program text itself consists of a sequence of commands that read, store and process data or call other commands.

Declarative — in this style the programmer describes the conditions of the task and the rules for obtaining the required result,
but it is not required to describe in detail all the steps of the algorithm, leaving them to the discretion of the computer.

It is precisely to the declarative style that the Prolog language belongs, as well as all the other logical programming languages.
The structured query language (SQL) should also be attributed to the declarative style of writing programs.

And the problem called the ["combinatorial explosion"](https://en.wikipedia.org/wiki/Combinatorial_explosion) has the strongest negative effect precisely on such languages.
After all, in the imperative approach the programmer himself is responsible for the sequence of commands executed, and if he programmed an algorithm of a complete enumeration of all possible solutions, then he is his own evil Pinocchio.

It is a different matter with programming in the declarative style. Although the developer can specify the constraints that should be applied when searching for a solution,
this is possible only in the case when the algorithm for solving the task is known.
But if the algorithm for solving it is known, then it is easier to use the imperative style, precisely by implementing this algorithm!

Therefore the main use of programming languages in the declarative style is to abandon the need to describe a clear algorithm for finding a solution,
leaving this to the computer. For the computer, the simplest "brute-force" solution is a complete enumeration of possible variants.

It is precisely in this case that the exponential growth of the running time of the algorithm begins.
And starting from a certain threshold, the waiting time for an answer becomes unacceptable for real use.
This is what the "Combinatorial explosion" means — a sharp ("explosive") growth of the running time of the algorithm with an increase in the size of the input data.

## The problem of searching for solutions
In the Prolog language this problem was solved through the use of the backtracking and cut mechanism.
Sometimes they also specified the "red" and "green" cut of solutions.
But in any case, these were **algorithmic** mechanisms for limiting the size of the tree of possible solutions,
and the necessity of their application still remains on the programmer.

But to implement them correctly, one needs to know the algorithm for solving, which again returns us to the statement that
if the algorithm is known, then it is more convenient to program it in the imperative style.

And if the full algorithm for solving the task is not known (or does not fit, for example because of the large time for its operation),
then as a result all that remains is either to increase the performance of the system in order to shorten the running time of the algorithm,
or to look for another solution, including by reducing the computational complexity of searching for solutions, for example, by excluding obviously unsuitable data,
so as to reduce the possible combinations for their enumeration.

### Performance scaling
Increasing performance is also different and does not work in all cases.
Vertical scaling of the performance of one node of the computing environment has its natural limit.
And even a multiple increase in the speed of the computer can only push back the threshold of the user's patience while waiting for the result,
but is unable to fundamentally solve the problem itself.

It is a different matter with horizontal scaling, in which the execution of the algorithm is launched on separate nodes that solve the same task in parallel.
Such a way of scaling already makes it possible to significantly shorten the time of obtaining the final result for complex computational tasks.
And although this method is a "head-on" solution, the successes in the field of data science prove the success of such an approach.

Of course, horizontal scaling also has its pitfalls.
First of all, the algorithm itself must allow the possibility of parallel execution independently of other nodes.
Automation of the management of tasks, of the computing nodes themselves, and of the whole system as a whole is also required.

Here the paradigm of functional programming can partially help, which limits the result of a function's computation to only the input parameters
and the result of executing other functions, but the result itself does not depend on the state of the system or other external data.

### Searching for a generalized solution
The second way to solve the problem of the combinatorial explosion is to reduce the computational complexity of the solution.
This does not mean choosing another algorithm or solving the task in symbolic form.
If such a thing is possible, then everything will again immediately reduce to the imperative style of programming.

I mean the possibility of searching for the solution algorithm itself.
More precisely, not exactly the algorithm, but the possibility of applying various selection methods to the input data in order to exclude the need for their complete enumeration.
In essence, this reduces to the application of various methods and mechanisms for processing input data taking into account various regularities.

This is possible both by algorithmic methods (backtracking and pruning in Prolog) and with the use of machine learning,
which copes very well with finding various regularities.

Naturally, such a method is not suitable for all classes of tasks. It is not suitable for identifying **ALL possible solutions**.
But where this is not required, such methods of reducing computational complexity have a right to exist.

For example, it is not required to search for all possible medicines for a particular disease; **one** is enough, taking into account certain constraints, that is guaranteed to work.

Moreover, even when particular solutions are found, there is always a chance that with their help it will be possible to see regularities that are not obvious at first glance,
which will help to show new ways of algorithmically reducing the computational complexity of the main task.

## The domain of unsolvable tasks
> How do you think, is it really possible to create a logical programming language that would itself be able to automate the search for solutions for tasks of such classes? Or at least have in its arsenal built-in mechanisms for automating such activity?


### Poll results from the original publication
```
- 57.47%    Ha-ha! What the author wants is called artificial intelligence (50 votes)
- 27.59%    Searching for solutions cannot be automated (24 votes)
- 19.54%    This already exists and everything was invented long ago (17 votes)
- 9.2%      Such a language is not needed, since neural networks can do everything (8 votes)
- 3.45%     I wrote my own variant in the comments (3 votes)
```
87 users voted. 62 users abstained.

