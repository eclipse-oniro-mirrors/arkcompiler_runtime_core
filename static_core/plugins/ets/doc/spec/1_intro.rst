..
    Copyright (c) 2021-2025 Huawei Device Co., Ltd.
    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

.. _Introduction:

Introduction
############

This document presents complete information on the new common-purpose,
multiparadigm programming language called |LANG|.

|

.. _Common Description:

Overall Description
*******************

The |LANG| language combines and supports features that have already proven
helpful and powerful in many well-known programming languages.

|LANG| supports imperative, object-oriented, functional, and generic
programming paradigms, and combines them safely and consistently.

At the same time, |LANG| does not support features that allow software
developers to write dangerous, unsafe, or inefficient code. In particular,
the language uses the strong static typing principle. Object types are
determined by their declarations, and no dynamic type change is allowed.
The semantic correctness is checked at compile time.

|LANG| is designed as a part of the modern language manifold. To provide an
efficient and safely executable code, the language takes flexibility and
power from |TS| and its predecessor |JS|, and the static
typing principle from Java and Kotlin. The overall design keeps the |LANG|
syntax style similar to that of those languages, and some of its important
constructs are almost identical to theirs on purpose.

In other words, there is a significant *common subset* of features of |LANG|
on the one hand, and of |TS|, |JS|, Java, and Kotlin on the other.
Consequently, the |LANG| style and constructs are no puzzle for the |TS| and
Java users who can intuitively sense the meaning of most constructs of the new
language even if not understand them completely.

.. index::
   construct
   syntax
   common subset

This stylistic and semantic similarity permits smoothly migrating the
applications originally written in |TS|, Java, or Kotlin to |LANG|.

Like its predecessors, |LANG| is a relatively high-level language. It means
that the language provides no access to low-level machine representations.
As a high-level language, |LANG| supports automatic storage management, i.e.,
all dynamically created objects are deallocated automatically soon
after they are no longer available, and deallocating them explicitly is not
required.

|LANG| is not merely a language, but rather a comprehensive software
development ecosystem that facilitates the creation of software solutions
in various application domains.

The |LANG| ecosystem includes the language along with its compiler,
accompanying documents, guidelines, tutorials, the standard library
(see :ref:`Standard Library`), and a set of additional tools that perform
transition from other languages (currently, |TS| and Java) to |LANG|
automatically or semi-automatically.

The |LANG| language as a whole is characterized by the following:

-  **Object orientation**

   The |LANG| language supports the *object-oriented programming* (OOP) approach
   based on classes and interfaces. The major notions of this approach are as
   follows:

   -  Classes with single inheritance,
   -  Interfaces as abstractions to be implemented by classes, and
   -  Methods (class instance or interface methods) with overriding and dynamic
      dispatch mechanisms.

   Object orientation is common in many if not all modern programming languages.
   It enables powerful, flexible, safe, clear, and adequate software design.

.. index::
   object
   object orientation
   object-oriented
   OOP (object-oriented programming)
   inheritance
   overriding
   abstraction
   dynamically dispatched overriding

-  **Modularity**

   The |LANG| language supports the *component programming* approach. It
   presumes that software is designed and implemented as a composition
   of *compilation units*. A compilation unit is typically represented as
   a *module* or a *package*.

   A *module* in |LANG| is a standalone, independently compiled unit that
   combines various programming resources (types, classes, functions, and so
   on). A module can communicate with other modules by exporting all or some
   of its resources to, or importing from other modules.

   This feature provides a high level of software development process and
   software maintainability, supports flexible module reuse and efficient
   version control.

.. index::
   modularity
   compilation unit
   component programming
   maintainability
   module
   package

-  **Genericity**

   Some program entities in |LANG| can be *type-parameterized*. It means that
   an entity can represent a very high-level (abstract) concept. Providing more
   concrete type information constitutes the instantiation of an entity for a
   particular use case.

   A classical illustration is the notion of a list that represents the
   ‘idea’ of an abstract data structure. An abstract notion can be turned
   into a concrete list by providing additional information (i.e., type of
   list elements).

   A similar feature (*generics* or *templates*) supported by many programming
   languages enables making programs and program structures more generic and
   reusable, and serves as a basis of the generic programming paradigm.

.. index::
   abstract concept
   abstract notion
   abstract data structure
   genericity
   type parameterized entity
   compile-time feature
   program entity
   generic
   template

-  **Multitargeting**

   |LANG| provides an efficient application development solution for a wide
   range of devices. The developer-friendly |LANG| ecosystem is a
   *cross-platform development* providing a uniform programming environment
   for many popular platforms. It can generate optimized applications
   capable of operating under the limitations of lightweight devices, or
   realizing the full potential of any specific-target hardware.

.. index::
   multitargeting
   cross-platform development
.. index::
   high-level language
   low-level representation
   storage management
   dynamically created object
   deallocation
   migration
   automatic transition
   semi-automatic transition

|

.. _Lexical and Syntactic Notation:

Lexical and Syntactic Notation
******************************

This section introduces the notation known as *context-free grammar*. The
notation is used throughout this specification to define the lexical and
syntactic structure of a program.

.. index::
   context-free grammar
   lexical structure
   syntactic structure

The |LANG| lexical notation defines a set of rules, or productions that specify
the structure of the elementary language  parts called *tokens*. All tokens are
defined in :ref:`Lexical Elements`. The set of tokens (identifiers, keywords,
numbers/numeric literals, operator signs, delimiters), special characters
(white spaces and line separators), and comments comprises the language’s
*alphabet*.

.. index::
   lexical notation
   production
   token
   lexical element
   identifier
   keyword
   number
   numeric literal
   operator sign
   line separator
   delimiter
   special character
   white space
   comment

The tokens defined by the lexical grammar are terminal symbols of syntactic
notation. Syntactic notation defines a set of productions starting from the
goal symbol *compilationUnit* (see :ref:`Compilation Units`). It is a sentence
that consists of a single distinguished nonterminal, and describes how
sequences of tokens can form syntactically correct programs.

.. index::
   production
   nonterminal
   lexical grammar
   syntactic notation
   goal symbol
   compilation unit
   module
   nonterminal

Lexical and syntactic grammars are defined as a range of productions, and each
production is comprised of the following:

- Abstract symbol (*nonterminal*) as its left-hand side,
- Sequence of one or more *nonterminal* and *terminal* symbols as its
  *right-hand side*,
- Character '``:``' as a separator between the left- and
  right-hand sides, and
- Character '``;``' as the end marker.

.. index::
   lexical grammar
   syntactic grammar
   abstract symbol
   nonterminal symbol
   terminal symbol
   character
   separator
   end marker

A grammar starts from the goal symbol and specifies the language, i.e., the set
of possible sequences of terminal symbols that can result from repeatedly
replacing any nonterminal in the left-hand-side sequence for a right-hand side
of the production.

.. index::
   goal symbol
   nonterminal
   terminal symbol
   sequence
   production

Grammars can use the following additional symbols (sometimes called
*metasymbols*) in the right-hand side of a grammar production along
with terminal and nonterminal symbols:

-  Vertical line '``|``' to specify alternatives.

-  Question mark '``?``' to specify an optional occurrence (zero- or one-time)
   of the preceding terminal or nonterminal.

-  Asterisk '``*``' to mark a *terminal* or *nonterminal* that can occur zero
   or more times.

-  Parentheses '``(``' and '``)``' to enclose any sequence of terminals and/or
   nonterminals marked with the metasymbols '``?``' or '``*``'.

.. index::
   terminal
   terminal symbol
   nonterminal
   goal symbol
   metasymbol
   grammar production

The metasymbols specify the structuring rules for terminal and nonterminal
sequences. However, they are not part of terminal symbol sequences that
comprise the resultant program text.

The example below represents a production that specifies a list of expressions:

.. code-block:: abnf

    expressionList:
      expression (',' expression)* ','?
      ;

This production introduces the following structure defined by the
nonterminal *expressionList*. The expression list must consist of a
sequence of *expressions* separated by the terminal symbol ‘``,``’. The
sequence must have at least one *expression*. The list is optionally
terminated by the terminal symbol ‘``,``’.

All grammar rules are presented in the Grammar section (see
:ref:`Grammar Summary`) of this Specification.

.. index::
   structuring rule
   sequence
   terminal symbol
   expression
   grammar rule

|

Terms and Definitions
*********************

This section contains the alphabetical list of important terms found in the
Specification, and their |LANG|-specific definitions. Such definitions are
not generic and can differ significantly from the definitions of the same terms
as used in other languages, application areas, or industries.

.. glossary::
   :sorted:

   expression
     -- a formula for calculating values. An expression has the syntactic
     form that is a composition of operators and parentheses, where
     parentheses are used to change the order of calculation. The default order
     of calculation is determined by operator preferences.

   operator (in programming languages)
     -- the term can have several meanings as follows:

     (1) a token that denotes the action to be performed on a value (addition,
     subtraction, comparison, etc.).

     (2) a syntactic construct that denotes an elementary calculation within
     an expression. An operator normally consists of an operator sign and
     one or more operands.

     In unary operators that have a single operand, the operator sign can be
     placed either in front of or after an operand (*prefix* and *postfix*
     unary operator respectively).

     If both operands are available, then the operator sign can be placed
     between the two (*infix* binary operator). A conditional operator with
     three operands is called *ternary*.

     Some operators have special notations. For example, an indexing operator
     has a conventional form like a[i] while formally being a binary operator.

     Some languages treat operators as *syntactic sugar*, i.e., a conventional
     version of a more common construct or *function call*. Therefore,
     an operator like ``a+b`` is conceptually handled as the call ``+(a,b)``,
     where the operator sign plays the role of a function name, and the operands
     are function call arguments.

   operation sign
     -- a language token that signifies an operator and conventionally
     denotes a usual mathematical operator, e.g., '``+``' for addition,
     '``/``' for division, etc. However, some languages allow using
     identifiers to denote operators, and/or arbitrarily combining characters
     that are not tokens in the alphabet of that language (i.e., operator
     signs).

   operand
     -- an argument of an operation. Syntactically, operands have the form of
     simple or qualified identifiers that refer to variables or members of
     structured objects. In turn, operands can be operators whose preferences
     ('priorities') are higher than the preference of a given operator.

   operation
     -- an informal notion that signifies an action or a process of operator
     evaluation.

   metasymbol
     -- additional symbols '``|``', '``?``', '``*``', '``(``', and '``)``' that
     can be used along with terminal and nonterminal symbols in the right-hand
     side of a grammar production.

   goal symbol
     -- a sentence that consists of a single distinguished nonterminal
     (*compilationUnit*). The *goal symbol* describes how sequences of
     tokens can form syntactically correct programs.

   token
     -- an elementary part of a programming language: identifier, keyword,
     operator and punctuator, or literal. Tokens are lexical input elements
     that form the vocabulary of a language, and can act as terminal symbols
     of the language's syntactic grammar.

   tokenization
     -- finding the longest sequence of characters that forms a valid token
     (i.e., *establishing* a token) in the process of codebase reading by the
     machine.

   punctuator
     -- a token that serves to separate, complete, or otherwise organize
     program elements and parts: commas, semicolons, parentheses, square
     brackets, etc.

   literal
     -- a representation of a value type.

   comment
     -- a piece of text, insignificant for the syntactic grammar, that is
     added to a stream in order to document and compliment source code.

   generic type
     -- a named type (class or interface) that has type parameters.

   generic
     -- see *generic type*.

   non-generic type
     -- a named type (class or interface) that has no type parameters.

   non-generic
     -- see *non-generic type*.

   type reference
     -- references that refer to named types by specifying their type names
     and type arguments, where applicable, to be substituted for type
     parameters of the named type.

   nullable type
     -- a variable declared to have the value ``null``, or ``type T | null``
     that can hold values of type ``T`` and its derived types.

   nullish value
     -- a reference which is null or undefined.

   simple name
     -- a name that consists of a single identifier.

   qualified name
     -- a name that consists of a sequence of identifiers separated with the
     token ‘``.``’.

   scope of a name
     -- a region of program code within which an entity---as declared by
     that name---can be accessed or referred to by its simple name without
     any qualification.

   function declaration
     -- a declaration that specifies names, signatures, and bodies when
     introducing a named function.

   terminal symbol
     -- a syntactically invariable token (i.e., a syntactic notation defined
     directly by an invariable form of the lexical grammar that defines a
     set of productions starting from the :term:`goal symbol`).

   terminal
     -- see *terminal symbol*.

   nonterminal symbol
     -- a syntactically variable token that results from the successive
     application of production rules.

   context-free grammar
      -- grammar in which the left-hand side of each production rule consists
      of only a single nonterminal symbol.

   nonterminal
     -- see *nonterminal symbol*.

   keyword
     -- one of *reserved words* that have their meanings permanently predefined
     in the language.

   variable
     -- see *variable declaration*.

   variable declaration
     -- a declaration that introduces a new named variable to which a modifiable
     initial value can be assigned.

   constant
     -- see *constant declaration*.

   constant declaration
     -- declaration that introduces a new variable to which an immutable
     initial value can be assigned only once at the time of instantiation.

   grammar
     -- set of rules that describe what possible sequences of terminal and
     nonterminal symbols a programming language interprets as correct.

     Grammar is a range of productions. Each production comprises an
     abstract symbol (nonterminal) as its left-hand side, and a sequence
     of nonterminal and terminal symbols as its right-hand side.
     Each production contains the characters ‘``:``’ as a separator between the
     left- and right-hand sides, and ‘``;``’ as the end marker.

   production
     -- a sequence of terminal and nonterminal symbols that a programming
     language interprets as correct.

   white space
     -- lexical input elements that separates tokens from one another in order
     to improve the source code readability and avoid ambiguities.

   widening conversion
     -- a conversion that causes no loss of information about the overall
     magnitude of a numeric value.

   narrowing conversion
     -- a conversion that can cause a loss information about the overall
     magnitude of a numeric value, and potentially a loss of precision
     and range.

   function types conversion
     -- a conversion of one function type to another.

   casting conversion
     -- a conversion of an operand of a cast expression to an explicitly
     specified type.

   method
     -- an ordered 3-tuple consisting of type parameters, argument types, and
     return types.

   abstract declaration
     -- an ordinary interface method declaration that specifies the method’s name
     and signature.

   overloading
     -- a lanaguage feature that allows using a single name to call several
     functions (in the general sense, i.e., including methods and constructors)
     with different signatures and different bodies.


   package level scope
      -- a name that is declared on the package level, and is accessible
      throughout the entire package and in other packages if exported.

   module level scope
     -- a name that is applicable to separate modules only, and is accessible
     throughout the entire module and in other packages if exported.

   class level scope
     -- a name that is declared inside a class, and is accessible inside the
     class and sometimes outside that class by means of an access modifier, or
     via a derived class).

   interface level scope
     -- a name that is declared inside an interface, and is accessible inside
     and outside the interface.

   function type parameter scope
     -- a scope of a type parameter name in a function declaration.
     It is identical to that entire declaration.

   method scope
     -- a scope of a name declared immediately inside the body of a method
     (function) declaration. Method scope is identical to the body of that
     method (function) declaration from the place of declaration and up to
     the end of the body.

   function scope
     -- same as *method scope*.

   type parameter scope
     -- a name of a type parameter declared in a class or an interface. Type
     parameter scope is identical to the entire declaration (except static
     member declarations).

   static member
     -- a class member that is not related to a particular class instance.
     A static member can be used across an entire program by using
     a qualified name notation (qualification is the name of a class).

   linearization
     -- de-nesting of all nested types in a union type to present them in
     the form of a flat line that includes no more union types.

   fit into (v.)
     -- belong, or be implicitly convertible to an entity (see
     :ref:`Widening Numeric Conversions`).

   match (v.)
     -- correspond to an entity.

   own (adj.)
     -- of a member textually declared in a class, interface, type, etc., as
     opposed to members inherited from base class (superclass), base interfaces
     (superinterface), base type (supertype), etc.

   supercomponent (base component, parent component)
     -- a component from which another component is derived.

   subcomponent (derived component, child component)
     -- a component produced by, inherited from, and dependent from another
     component.

.. raw:: pdf

   PageBreak
