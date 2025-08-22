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

.. _Interfaces:

Interfaces
##########

.. meta:
    frontend_status: Done

An interface declaration declares an *interface type*, i.e., a reference
type that:

-  Includes properties and methods as its members;
-  Has no instance variables (fields);
-  Usually declares one or more methods;
-  Allows otherwise unrelated classes to provide implementations for the
   methods, and so implement the interface.

.. index::
   interface declaration
   interface type
   reference type
   instance variable
   field
   property
   method
   member
   implementation
   class
   interface

Creating an instance of interface type is not possible.

An interface can be declared *direct extension* of one or more other
interfaces. If so, the interface inherits all members from the interfaces
it extends. Inherited members can be optionally overridden or hidden.

A class can be declared to *directly implement* one or more interfaces. Any
instance of a class implements all methods specified by its interface(s).
A class implements all interfaces that its direct superclasses and direct
superinterfaces implement. Interface inheritance allows objects to
support common behaviors without sharing a superclass.

.. index::
   interface
   interface type
   creation
   instantiation
   direct extension
   inheritance
   inherited member
   extension
   superinterface
   direct implementation
   superclass
   object
   overriding
   hiding
   overridden member
   hidden member
   shadowing

The value of a variable declared *interface type* can be a reference to any
instance of a class that implements the specified interface. However, it is not
enough for a class to implement all methods of an interface. A class or one of
its superclasses must be actually declared to implement an interface.
Otherwise, the class is not considered to implement the interface.

The rules of subtyping are discussed in detail in
:ref:`Subtyping for Classes and Interfaces`.

.. index::
   value
   variable
   interface type
   interface
   class
   superclass
   declaration
   instance
   reference
   method
   implementation
   assignability
   Object

|

.. _Interface Declarations:

Interface Declarations
**********************

.. meta:
    frontend_status: Done

*Interface declaration* specifies a new named reference type.

The syntax of *interface declarations* is presented below:


.. index::
   interface declaration
   reference type
   syntax

.. code-block:: abnf

    interfaceDeclaration:
        'interface' identifier typeParameters?
        interfaceExtendsClause? '{' interfaceMember* '}'
        ;

    interfaceExtendsClause:
        'extends' interfaceTypeList
        ;

    interfaceTypeList:
        typeReference (',' typeReference)*
        ;

The *identifier* in an interface declaration specifies the interface name.

An interface declaration with ``typeParameters`` introduces a new generic
interface (see :ref:`Generics`).

The scope of an interface declaration is defined in :ref:`Scopes`.

.. The interface declaration shadowing is specified in :ref:`Shadowing by Parameter`.

.. index::
   identifier
   interface declaration
   interface name
   class name
   generic interface
   generic declaration
   shadowing
   scope

|

.. _Superinterfaces and Subinterfaces:

Superinterfaces and Subinterfaces
*********************************

.. meta:
    frontend_status: Done

An interface declared with an ``extends`` clause extends all other named
interfaces, and thus inherits all their members. Such other named interfaces
are *direct superinterfaces* of a declared interface. A class that *implements*
the declared interface also implements all interfaces that the interface
*extends*.

.. index::
   superinterface
   subinterface
   extends clause
   direct superinterface
   implementation
   declared interface
   interface
   inheritance

A :index:`compile-time error` occurs if:

-  `typeReference`` in the ``extends`` clause refers directly to, or is an
   alias of non-interface type.
-  Interface type named by ``typeReference`` is not :ref:`Accessible`.
-  Type arguments (see :ref:`Type Arguments`) of ``typeReference`` denote a
   parameterized type that is not well-formed (see
   :ref:`Generic Instantiations`).
-  The ``extends`` graph has a cycle.

.. index::
   extends clause
   alias
   non-interface type
   interface declaration
   interface type
   access
   accessibility
   scope
   type argument
   parameterized type
   generic instantiation
   extends graph
   well-formed parameterized type

If an interface declaration (possibly generic) ``I`` <``F``:sub:`1` ``,...,
F``:sub:`n`> (:math:`n\geq{}0`) contains an ``extends`` clause, then the
*direct superinterfaces* of the interface type ``I`` <``F``:sub:`1` ``,...,
F``:sub:`n`> are the types given in the ``extends`` clause of the declaration
of ``I``.

All *direct superinterfaces* of the parameterized interface type ``I``
<``T``:sub:`1` ``,..., T``:sub:`n`> are types ``J``
<``U``:sub:`1`:math:`\theta{}` ``,..., U``:sub:`k`:math:`\theta{}`>, if:

-  ``T``:sub:`i` (:math:`1\leq{}i\leq{}n`) is the type of a generic interface
   declaration ``I`` <``F``:sub:`1` ``,..., F``:sub:`n`> (:math:`n > 0`);
-  ``J`` <``U``:sub:`1` ``,..., U``:sub:`k`> is a direct superinterface of
   ``I`` <``F``:sub:`1` ``,..., F``:sub:`n`>; and
-  :math:`\theta{}` is a substitution
   [``F``:sub:`1` ``:= T``:sub:`1` ``,..., F``:sub:`n` ``:= T``:sub:`n`].

.. index::
   interface declaration
   generic
   generic declaration
   extends clause
   interface type
   declaration
   direct superinterface
   parameterized interface
   substitution
   superinterface

The transitive closure of the direct superinterface relationship results in
the *superinterface* relationship.

Interface *I* is a *subinterface* of *K* wherever *K* is a superinterface of *I*.
Interface *K* is a superinterface of *I* if:

-  *I* is a direct subinterface of *K*; or
-  *K* is a superinterface of some interface *J* of which *I* is, in turn,
   a subinterface.

.. index::
   transitive closure
   direct superinterface
   superinterface
   direct subinterface
   interface
   subinterface

There is no single interface to which all interfaces are extensions (unlike
class ``Object`` to which every class is an extension).

A :index:`compile-time error` occurs if an interface depends on itself.

If superinterfaces have default implementations (see
:ref:`Default Interface Method Declarations`) for some method ``m``, then
the following occurs:

.. index::
   interface
   extension
   Object
   class
   superinterface
   implementation

- Method ``m`` with an override-compatible signature (see
  :ref:`Override-Compatible Signatures`) declared within the current interface
  overrides all other ``m`` methods inherited from superinterfaces; or
- All methods inherited from superinterfaces refer to the same implementation,
  and this default implementation is the current interface method; or
- One method ``m`` in some superinterface overrides all other methods from
  other superinterfaces.

Otherwise, a :index:`compile-time error` occurs.

.. code-block:: typescript
   :linenos:

    interface I1 { foo () {} }
    interface I2 { foo () {} }
    interface II1 extends I1, I2 {
       foo () {} // foo() from II1 overrides both foo() from I1 and foo() from I2
    }
    interface II2 extends I1, I2 {
       // Compile-time error as foo() from I1 and foo() from I2 have different implementations
    }
    interface I3 extends I1 {}
    interface I4 extends I1 {}
    interface II3 extends I3, I4 {
       // OK, as foo() from I3 and foo() from I4 refer to the same implementation
    }

    class Base {}
    class Derived extends Base {}

    interface II1 {
        foo (p: Base) {}
    }
    interface II2 {
        foo (p: Derived) {}
    }
    interface II3 extends II1, II2 {}
        // foo() from II1 overrides foo() from II2

.. index::
   interface
   object
   class
   method
   extension
   implementation
   override-compatible signature

|

.. _Interface Members:

Interface Members
*****************

.. meta:
    frontend_status: Done

An *interface declaration* can contain *interface members*, i.e., its
properties (see :ref:`Interface Properties`) and methods (see
:ref:`Interface Method Declarations`).

The syntax of *interface member* is presented below:

.. code-block:: abnf

    interfaceMember
        : annotationUsage?
        ( interfaceProperty
        | interfaceMethodDeclaration
        | overloadInterfaceMethodDeclaration
        )
        ;

The scope of declaration of a member *m* that the interface type ``I``
declares or inherits is specified in :ref:`Scopes`.

The usage of annotations is discussed in :ref:`Using Annotations`.

.. index::
   interface
   interface member
   interface type
   property
   method
   syntax
   interface declaration
   method declaration
   scope
   inheritance
   annotation

*Interface members* include:

-  Members declared explicitly in the interface declaration;
-  Members inherited from a direct superinterface (see
   :ref:`Superinterfaces and Subinterfaces`).

A :index:`compile-time error` occurs if the method explicitly declared by the
interface has the same name as the ``Object``'s ``public`` method.

.. code-block:: typescript
   :linenos:

    interface I {
        toString (p: number): void // Compile-time error
        toString(): string { return "some string" } // Compile-time error
    }

.. index::
   interface
   interface member
   inheritance
   interface declaration
   direct superinterface
   Object
   public method

An interface inherits all members of the interfaces it extends
(see :ref:`Interface Inheritance`).

A name in a declaration scope must be unique, i.e., the names of properties and
methods of an interface type must not be the same (see
:ref:`Interface Declarations`).

.. index::
   inheritance
   interface
   property
   method
   declaration scope
   interface type
   interface declaration
   scope

|

.. _Interface Properties:

Interface Properties
********************

.. meta:
    frontend_status: Done

*Interface property* can be defined in the form of a field or an *accessor*
(a getter or a setter).

The syntax of *interface property* is presented below:

.. code-block:: abnf

    interfaceProperty:
        'readonly'? identifier '?'? ':' type
        | 'get' identifier '(' ')' returnType
        | 'set' identifier '(' parameter ')'
        ;

.. index::
   interface
   property
   field
   accessor
   getter
   setter
   interface property
   syntax

An interface property is a *required property* (see
:ref:`Required Interface Properties`) if it is one of the following:

- Explicit *accessor*, i.e., a getter or a setter; or
- Form of a field that has no '``?``'.

Otherwise, it is an *optional property* (see :ref:`Optional Interface Properties`).

If '``?``' is used after the name of the property, then the property type is
semantically equivalent to ``type | undefined``.

.. code-block:: typescript
   :linenos:

    interface I {
        property?: Type
    }
    // is the same as
    interface I {
        property: Type | undefined
    }

.. index::
   interface property
   interface
   property
   required property
   optional property
   accessor
   getter
   setter
   field
   property type
   semantic equivalent

|

.. _Required Interface Properties:

Required Interface Properties
=============================

.. meta:
    frontend_status: Done

A *required property* defined in the form of a field implicitly
defines the following:

-  Getter, if the property is marked as ``readonly``;
-  Otherwise, both a getter and a setter with the same name.

A type annotation for the field defines return type for the getter
and type of parameter for the setter.

As a result, the following declarations have the same effect:

.. index::
   property
   interface
   required property
   interface property
   field
   accessor
   readonly
   getter
   setter
   property
   type annotation
   parameter
   return type

.. code-block:: typescript
   :linenos:

    interface Style {
        color: string
    }
    // is the same as
    interface Style {
        get color(): string
        set color(s: string)
    }

**Note.** A *required property* defined in a form of accessors does not
define any additional entities in the interface.

A class that implements an interface with properties can also use a field or
an accessor notation (see :ref:`Implementing Required Interface Properties`,
:ref:`Implementing Optional Interface Properties`).

.. index::
   string
   implementation
   required property
   accessor
   interface
   interface property
   optional property
   field
   notation
   property
   entity
   class

|

.. _Optional Interface Properties:

Optional Interface Properties
=============================

.. meta:
    frontend_status: Done

An *optional property* can be defined in two forms:

-  Short form ``identifier '?' ':' T``; or
-  Explicit form ``identifier ':' T | undefined``.


In both cases, ``identifier`` has effective type ``T | undefined``.

The *optional property* implicitly defines the following:

.. index::
   optional property
   interface property
   identifier

-  A getter (if the property is marked as ``readonly``);
-  Otherwise, both a getter and a setter with the same name.

Accessors have implicitly defined bodies, in this aspect they are similar to
:ref:`Default Interface Method Declarations`.
However, |LANG| does not support explicitly defined accessors with bodies.

The following declaration:

.. code-block:: typescript
   :linenos:

    interface I {
        num?: number
    }
    
-- implicitly declares two accessors:
    
.. code-block:: typescript
   :linenos:

    interface I {
        get num(): number | undefined { return undefined }
        set num(x: number | undefined) { throw new InvalidStoreAccessError }
    }

If the default setter is not overridden in a class that implements the interface,
``InvalidStoreAccessError`` is thrown at attempt to set value of an optional
property. See also :ref:`Implementing Optional Interface Properties`.

.. index::
   getter
   setter
   implementation
   value
   optional property
   readonly
   accessor
   body
   interface property

|

.. _Interface Method Declarations:

Interface Method Declarations
*****************************

.. meta:
    frontend_status: Done

An ordinary *interface method declaration* specifies the method name and
signature, and is called *abstract*. Its implicit accessibility is ``public``.

An interface method can have a body (see :ref:`Default Interface Method Declarations`)
as an experimental feature.

.. index::
   interface
   interface method declaration
   method name
   method signature
   method
   declaration
   signature
   interface method
   method body
   abstract declaration

The syntax of *interface method declaration* is presented below:

.. code-block:: abnf

    interfaceMethodDeclaration:
        identifier signature
        | interfaceDefaultMethodDeclaration
        ;


.. index::
   interface method declaration
   declaration
   syntax

|

.. _Interface Method Overload Signatures:

Interface Method Overload Signatures
====================================

.. meta:
    frontend_status: Partly

|LANG| allows specifying several interface methods with a single name.
It is a special case of :ref:`Declarations with Overload Signatures`.

An *implementation body* cannot be defined in the interface, but must
be defined in a class that implements this interface.

.. index::
   interface
   method
   interface method
   overload signature
   name
   implementation

The order of textual declarations of overloaded signatures affects the manner
a type check is performed at the call site, starting from the signature declared
first, and then proceeding in the proper sequence.

The use of *overload signatures* is represented by the example below:

.. code-block-meta:

.. code-block:: typescript
   :linenos:

    interface I {
        foo(): number                        // 1st signature
        foo(p: string): string               // 2nd signature
        foo(p1: string, p2?: number): number // 3rd signature       
    }

    function demo(i: I) {
       i.foo()                     // ok, call matches the 1st signature
       i.foo("aa")                 // ok, call matches the 2nd signature
       let n: number = i.foo("aa") // compile-time error, as the 2nd signature returns string
       n = i.foo("aa", undefined)  // ok, call matches the 3rd signature
    }

.. index::
   implementation body
   overload signatures
   declaration
   call site
   signature
   string

|

.. _Interface Inheritance:

Interface Inheritance
*********************

.. meta:
    frontend_status: Done

Interface *I* inherits all properties and methods from its direct
superinterfaces. Semantic checks are described in
:ref:`Overriding and Overload Signatures in Interfaces`.

**Note**. The semantic rules of methods apply to properties because any
interface property implicitly defines a getter, a setter, or both.

Private methods defined in superinterfaces are not accessible (see
:ref:`Accessible`) in the interface body.

.. index::
   inheritance
   interface
   interface inheritance
   direct superinterface
   overriding
   overload signature
   method
   superinterface
   semantic check
   private method
   property
   getter
   setter
   access
   accessibility
   interface body

A :index:`compile-time error` occurs if interface *I* declares a ``private``
method *m* with a signature compatible with the instance method :math:`m'`
(see :ref:`Override-Compatible Signatures`) that has any access modifier in the
superinterface of *I*.

.. index::
   interface
   declaration
   method
   private method
   compatibility
   instance method
   override-compatible signature
   access
   access modifier
   superinterface
   private method
   signature

.. raw:: pdf

   PageBreak
