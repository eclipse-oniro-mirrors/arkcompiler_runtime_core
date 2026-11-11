/**
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package std.debug;

/**
 * Represents an identifier.
 *
 * Supports chaining through getField() for namespace traversal,
 * e.g. A.B.c = 1234 → get("A").getField("B").getField("c").setValue(wrap(1234)).
 *
 * invoke() and call() return DebugProxy to support chained operations.
 */
export interface DebugProxy {
    /** Gets the current value. */
    value(): Any
    /** Assigns a value; the argument must implement the DebugProxy interface (ValueProxy or VariableProxy subclass). */
    setValue(v: DebugProxy): void

    /** Invokes the current value as a function, returns DebugProxy to support chained operations. */
    invoke(...args: FixedArray<Any>): DebugProxy

    /**
     * Gets a member value, returns a new DebugProxy for chained access.
     * Supports static field lookup on Class and instance field lookup on Object.
     */
    getField(field: string): DebugProxy

    /** Calls a member method on the current value, returns DebugProxy to support chained operations. */
    call(method: string, ...args: FixedArray<Any>): DebugProxy
}

export final class ValueProxy implements DebugProxy {
    private val_: Any

    constructor(val: Any) {
        this.val_ = val
    }

    override value(): Any {
        return this.val_
    }

    override setValue(v: DebugProxy): void {
        throw new Error("cannot assign to a literal")
    }

    override invoke(...args: FixedArray<Any>): DebugProxy {
        let obj = this.val_
        if (obj == null) {
            throw new Error("literal value is null, cannot invoke")
        }
        if (!(obj instanceof Function)) {
            throw new Error("literal value is not callable")
        }
        return DebuggerAPI.invokeFunction(obj.name, obj, args)
    }

    override getField(field: string): DebugProxy {
        let obj = this.val_
        if (obj == null) {
            return new ValueProxy(null)
        }
        return DebuggerAPI.getFieldOnValue(obj as Object, "literal", field)
    }

    override call(method: string, ...args: FixedArray<Any>): DebugProxy {
        let obj = this.val_
        if (obj == null) {
            throw new Error("cannot call method '" + method + "' on literal (value is null)")
        }
        return DebuggerAPI.callOnValue(obj as Object, "literal", method, args)
    }
}

/**
 * Instance method reference proxy: wraps a "method on an object" as a callable
 * function value, supporting `f(a.b)` style function value arguments. At compile
 * time, "field access" and "method reference" share the same syntax; at runtime,
 * getField first searches fields, then falls back to method reference lookup.
 * Implements Function (unsafeCall + name) so the value can be called directly.
 */
export final class MethodRefProxy extends VariableProxy implements Function {
    private obj_: Object
    private field_: string

    constructor(obj: Object, field: string) {
        super()
        this.obj_ = obj
        this.field_ = field
    }

    protected override getDisplayName(): string {
        return "method " + this.field_
    }

    override value(): Any {
        return this
    }

    override setValue(v: DebugProxy): void {
        throw new Error("cannot assign to a method reference")
    }

    unsafeCall(...r: FixedArray<Any>): Any {
        let cls = Class.of(this.obj_)
        return DebuggerAPI.callInstanceMember(cls, this.obj_, this.field_, "method ref", r).value()
    }

    get name(): string {
        return this.field_
    }
}

/** Static method reference proxy: MyClass.method as a function value (symmetric to MethodRefProxy). */
export final class StaticMethodRefProxy extends VariableProxy implements Function {
    private cls_: Class
    private field_: string

    constructor(cls: Class, field: string) {
        super()
        this.cls_ = cls
        this.field_ = field
    }

    protected override getDisplayName(): string {
        return "static method " + this.field_
    }

    override value(): Any {
        return this
    }

    override setValue(v: DebugProxy): void {
        throw new Error("cannot assign to a static method reference")
    }

    unsafeCall(...r: FixedArray<Any>): Any {
        return DebuggerAPI.callStaticMember(this.cls_, this.field_, "method ref", r).value()
    }

    get name(): string {
        return this.field_
    }
}

/**
 * Abstract base class providing unified invoke/getField/call implementations for
 * all proxies that return Object. Subclasses only need to implement value()/setValue()
 * and getDisplayName() to reuse all member operations. invoke()/call() wrap return
 * values in ValueProxy to support chained operations. Field assignment is done
 * via the getField(f).setValue(v) chain.
 */
export abstract class VariableProxy implements DebugProxy {
    abstract value(): Any
    abstract setValue(v: DebugProxy): void
    protected abstract getDisplayName(): string

    override invoke(...args: FixedArray<Any>): DebugProxy {
        let val = this.value()
        if (val == null) {
            throw new Error("'" + this.getDisplayName() + "' is null, cannot invoke")
        }
        if (!(val instanceof Function)) {
            throw new Error("'" + this.getDisplayName() + "' is not callable")
        }
        return DebuggerAPI.invokeFunction(this.getDisplayName(), val, args)
    }

     override getField(field: string): DebugProxy {
        let val = this.value()
        if (val == null) {
            throw new Error("cannot get field '" + field + "' on '" + this.getDisplayName() + "' (value is null)")
        }
        return DebuggerAPI.getFieldOnValue(val as Object, this.getDisplayName(), field)
    }

    override call(method: string, ...args: FixedArray<Any>): DebugProxy {
        let val = this.value()
        if (val == null) {
            throw new Error("cannot call method '" + method + "' on '" + this.getDisplayName() + "' (value is null)")
        }
        return DebuggerAPI.callOnValue(val as Object, this.getDisplayName(), method, args)
    }
}

/**
 * Proxy for a getter property. ETS compiles getters/setters as methods named "%%get-<prop>"
 * and "%%set-<prop>" with GETTER_SETTER flags. The reflection API (getInstanceField) does not
 * expose them as fields, so DebugProxy.getField must bridge the gap. This proxy supports both
 * read (value()→getter invoke) and write (setValue()→setter invoke), completing the ETS
 * property semantics in the evaluate layer.
 */
export final class GetterProxy extends VariableProxy {
    private obj_: Object
    private propName_: string
    private getter_: reflect.InstanceMethod
    private setter_: reflect.InstanceMethod | undefined

    constructor(obj: Object, propName: string, getter: reflect.InstanceMethod,
        setter: reflect.InstanceMethod | undefined) {
        super()
        this.obj_ = obj
        this.propName_ = propName
        this.getter_ = getter
        this.setter_ = setter
    }

    protected override getDisplayName(): string {
        return "getter " + this.propName_
    }

    override value(): Any {
        return this.getter_.invoke(this.obj_, [])
    }

    override setValue(v: DebugProxy): void {
        let setter = this.setter_
        if (setter == undefined) {
            throw new Error("cannot set getter '" + this.propName_ + "' (no setter found)")
        }
        let val = DebuggerAPI.unwrap(v)
        val = DebuggerAPI.convertArgToParamType(val, setter.getParameterTypes()[0])
        setter.invoke(this.obj_, [val])
    }
}

/** Static counterpart of GetterProxy — same semantics for static getter/setter properties. */
export final class StaticGetterProxy extends VariableProxy {
    private propName_: string
    private getter_: reflect.StaticMethod
    private setter_: reflect.StaticMethod | undefined

    constructor(propName: string, getter: reflect.StaticMethod, setter: reflect.StaticMethod | undefined) {
        super()
        this.propName_ = propName
        this.getter_ = getter
        this.setter_ = setter
    }

    protected override getDisplayName(): string {
        return "static getter " + this.propName_
    }

    override value(): Any {
        return this.getter_.invoke([])
    }

    override setValue(v: DebugProxy): void {
        let setter = this.setter_
        if (setter == undefined) {
            throw new Error("cannot set static getter '" + this.propName_ + "' (no setter found)")
        }
        let val = DebuggerAPI.unwrap(v)
        val = DebuggerAPI.convertArgToParamType(val, setter.getParameterTypes()[0])
        setter.invoke([val])
    }
}

/**
 * Proxy for an subscript access on Record/Map/JsonElement types. When getField
 * doesn't find a named field, the object may support $_get/$_set subscript
 * access. This proxy reads via $_get(key) and writes via $_set(key, value),
 * enabling both 'obj['a'] reads and 'obj['a'] = v' writes through the
 * debugger's getField/setValue proxy chain.
 */
export final class SubscriptProxy extends VariableProxy {
    private obj_: Object
    private key_: string

    constructor(obj: Object, key: string) {
        super()
        this.obj_ = obj
        this.key_ = key
    }

    protected override getDisplayName(): string {
        return "subscript " + this.key_
    }

    override value(): Any {
        return DebuggerAPI.subscriptGet(this.obj_, this.key_)
    }

    override setValue(v: DebugProxy): void {
        DebuggerAPI.subscriptSet(this.obj_, this.key_, DebuggerAPI.unwrap(v))
    }
}

/**
 * Proxy for an instance field. Stores the owning object and the field descriptor.
 * value()/setValue() read/write the field value; getField() further chains into the field's value.
 */
export final class InstanceFieldProxy extends VariableProxy {
    private obj_: Object
    private field_: reflect.InstanceField

    constructor(obj: Object, field: reflect.InstanceField) {
        super()
        this.obj_ = obj
        this.field_ = field
    }

    override value(): Any {
        return this.field_.getValue(this.obj_)
    }

    override setValue(v: DebugProxy): void {
        let val = DebuggerAPI.unwrap(v)
        // The native setValue for number/Double fields rejects Int boxes
        // (Double.IsAssignableFrom(Int) → false).  Read the field's current
        // value to determine its box class, and widen numeric arguments to
        // match, mirroring the ETS compile-time widening semantics.
        let existing = this.field_.getValue(this.obj_)
        if (existing != null && val != null) {
            val = DebuggerAPI.widenNumericBox(val as Object, existing as Object)
        }
        this.field_.setValue(this.obj_, val)
    }

    protected override getDisplayName(): string {
        return "instance field"
    }
}


/**
 * Proxy for a static field. Receives the pre-resolved field handle at
 * construction (resolved by the DebuggerAPI lookup paths).
 */
export final class StaticFieldProxy extends VariableProxy {
    private fieldName_: string
    private field_: reflect.StaticField

    constructor(fieldName: string, field: reflect.StaticField) {
        super()
        this.fieldName_ = fieldName
        this.field_ = field
    }

    override value(): Any {
        return this.field_.getValue()
    }

    override setValue(v: DebugProxy): void {
        let val = DebuggerAPI.unwrap(v)
        let existing = this.field_.getValue()
        if (existing != null && val != null) {
            val = DebuggerAPI.widenNumericBox(val as Object, existing as Object)
        }
        this.field_.setValue(val)
    }

    protected override getDisplayName(): string {
        return "static field " + this.fieldName_
    }
}

/** Represents a local variable */
export final class LocalVariable extends VariableProxy {
    private thread: int
    private frame: int
    private name: string
    private regNumber: long
    private typeSignature: string

    constructor(thread: int, frame: int, name: string, regNumber: long, typeSignature: string) {
        super()
        this.thread = thread
        this.frame = frame
        this.name = name
        this.regNumber = regNumber
        this.typeSignature = typeSignature
    }

    protected override getDisplayName(): string {
        return this.name
    }

    override value(): Any {
        return DebuggerAPI.getLocalValue(this.thread, this.frame, this.regNumber, this.typeSignature)
    }

    override setValue(v: DebugProxy): void {
        DebuggerAPI.setLocalValue(
            this.thread, this.frame, this.regNumber, this.typeSignature, DebuggerAPI.unwrap(v) as Object)
    }
}

/** Represents `this` expression */
export final class This extends VariableProxy {
    private thisObj: Object | null

    constructor(thisObj: Object | null) {
        super()
        this.thisObj = thisObj
    }

    protected override getDisplayName(): string {
        return "this"
    }

    override value(): Any {
        return this.thisObj
    }

    override setValue(v: DebugProxy): void {
        throw new Error("cannot reassign 'this'")
    }
}

/**
 * Represents a variable captured from an enclosing scope by a lambda or local class.
 *
 * In ETS, when a lambda or local class references a variable declared in an outer
 * scope, the compiler's CapturedVariables lowering phase transforms each captured
 * reference into a field access on a compiler-generated capture context object.
 * The capture object holds the values of all variables captured from the enclosing
 * scope, and each captured variable becomes an instance field of that object.
 *
 * This proxy wraps the capture context object (`captureObj`) and the reflective
 * `InstanceField` descriptor (`field`) that corresponds to the captured variable,
 * enabling the debugger to read and write the variable's value during evaluation.
 */
export final class CapturedVariable extends VariableProxy {
    private name: string
    // The capture context object is always an object reference or null (when the
    // capture context is unavailable, e.g. the scope has been exited). It is never
    // a primitive value, so `Object | null` is more precise than `Any`. This also
    // matches the parameter type of `reflect.InstanceField.getValue(thisObj: Object)`.
    private captureObj: Object | null
    private field: reflect.InstanceField

    constructor(name: string, captureObj: Object | null, field: reflect.InstanceField) {
        super()
        this.name = name
        this.captureObj = captureObj
        this.field = field
    }

    protected override getDisplayName(): string {
        return "captured variable " + this.name
    }

    override value(): Any {
        let obj: Object | null = this.captureObj
        if (obj == null) {
            throw new Error("cannot access captured variable '" + this.name + "' (capture object is null)")
        }
        return this.field.getValue(obj)
    }

    override setValue(v: DebugProxy): void {
        let obj: Object | null = this.captureObj
        if (obj == null) {
            throw new Error("cannot set captured variable '" + this.name + "' (capture object is null)")
        }
        this.field.setValue(obj, DebuggerAPI.unwrap(v))
    }
}

/** Represents a global variable */
export final class GlobalVariable extends VariableProxy {
    private name: string

    constructor(name: string) {
        super()
        this.name = name
    }

    protected override getDisplayName(): string {
        return "global variable " + this.name
    }

    override value(): Any {
        return DebuggerAPI.getGlobalVariable(this.name)
    }

    override setValue(v: DebugProxy): void {
        DebuggerAPI.setGlobalVariable(this.name, DebuggerAPI.unwrap(v) as Object)
    }
}

/** Represents a global function */
export final class GlobalFunction implements DebugProxy {
    private method_: reflect.StaticMethod

    constructor(method: reflect.StaticMethod) {
        this.method_ = method
    }

    override value(): Any {
        throw new TypeError("cannot get value of a function")
    }

    override setValue(v: DebugProxy): void {
        throw new TypeError("cannot reassign global function")
    }

    override invoke(...args: FixedArray<Any>): DebugProxy {
        return new ValueProxy(this.method_.invoke(args))
    }

    override getField(field: string): DebugProxy {
        throw new TypeError("cannot get field on a function")
    }

    override call(method: string, ...args: FixedArray<Any>): DebugProxy {
        throw new TypeError("cannot call method on a function")
    }
}

/** Represents a global class / namespace */
export final class GlobalClass implements DebugProxy {
    private cls_: Class

    constructor(cls: Class) {
        this.cls_ = cls
    }

    override value(): Any {
        throw new TypeError("cannot get value of a class")
    }

    override setValue(v: DebugProxy): void {
        throw new TypeError("cannot set value of a class")
    }

    override invoke(...args: FixedArray<Any>): DebugProxy {
        throw new TypeError("cannot invoke a class")
    }

    override getField(field: string): DebugProxy {
        return DebuggerAPI.getFieldOnValue(this.cls_, this.cls_.getName(), field)
    }

    override call(method: string, ...args: FixedArray<Any>): DebugProxy {
        let staticMethod = this.cls_.getStaticMethod(method)
        if (staticMethod == undefined) {
            throw new Error("static method '" + method + "' not found on class '" + this.cls_.getName() + "'")
        }
        return new ValueProxy(staticMethod.invoke(args))
    }
}

export final class DebuggerAPI {
    /**
     * Value carrier of the session variable store (GC root): holds an Object[], storing
     * [name, value, readonlyMark] triplets in slots. readonlyMark is null (let) or the
     * name string itself (const), avoiding boxing allocations under lock. null means not
     * yet initialized, lazily created by native. Only defineVariable/get/set maintain it
     * via static field access; other reads/writes are prohibited.
     */
    private static sessionStore: Any = null

    public static native getLocalBoolean(regNumber: long): boolean

    public static native getLocalByte(regNumber: long): byte

    public static native getLocalShort(regNumber: long): short

    public static native getLocalChar(regNumber: long): char

    public static native getLocalInt(regNumber: long): int

    public static native getLocalFloat(regNumber: long): float

    public static native getLocalDouble(regNumber: long): double

    public static native getLocalLong(regNumber: long): long

    public static native getLocalObject(regNumber: long): Object

    public static native setLocalBoolean(regNumber: long, value: boolean): void

    public static native setLocalByte(regNumber: long, value: byte): void

    public static native setLocalShort(regNumber: long, value: short): void

    public static native setLocalChar(regNumber: long, value: char): void

    public static native setLocalInt(regNumber: long, value: int): void

    public static native setLocalFloat(regNumber: long, value: float): void

    public static native setLocalDouble(regNumber: long, value: double): void

    public static native setLocalLong(regNumber: long, value: long): void

    public static native setLocalObject(regNumber: long, value: Any): void

    /**
     * Gets the `this` object of the current stack frame.
     * @param thread Thread ID
     * @param frame Frame depth (0 = current frame, 1 = caller frame)
     * @returns The `this` object, or undefined if the current method is static
     */
    static native getThis(thread: int, frame: int): Object

    /**
     * Gets the value of a variable by name from the stack frame.
     * @param thread Thread ID
     * @param frame Frame depth
     * @param name Variable name
     * @returns The variable value; throws if the variable does not exist
     */
    static native get(thread: int, frame: int, name: string): Any

    /**
     * Gets the value of a variable by name from the stack frame, returning null
     * (without throwing) if the variable does not exist. Used for optional chaining
     * (a?.b) so that null correctly short-circuits subsequent property access.
     * @param thread Thread ID
     * @param frame Frame depth
     * @param name Variable name
     * @returns The variable value, or null if the variable does not exist
     */
    static native getOptional(thread: int, frame: int, name: string): Any

    /**
     * Sets the value of a variable by name in the stack frame, automatically
     * performing type conversion based on the variable's type signature.
     * @param thread Thread ID
     * @param frame Frame depth
     * @param name Variable name
     * @param value The value to assign
     */
    static native set(thread: int, frame: int, name: string, value: Any): void

    /**
     * Defines (or redefines) a session variable within the debug session, for use by
     * evaluate's let/const declarations. Session variables are stored by name in a
     * process-level session store; name resolution priority is lower than frame variables
     * and the frame's module fields (frame variables shadow session variables, consistent
     * with the JS debugger REPL scope model). Values are boxed into the store using their
     * runtime actual type; null/undefined are both allowed. The declareType type annotation
     * is ignored in v1. Redeclaration unconditionally overwrites (including readonly mark
     * updates).
     * @param thread Thread ID (kept for signature style, unused in implementation)
     * @param frame Stack frame depth (same as above)
     * @param name Variable name
     * @param value Initial value (Any: null/undefined allowed)
     * @param readonly true for const declarations; subsequent set calls will reject assignment
     */
    static native defineVariable(thread: int, frame: int, name: string, value: Any, readonly: boolean): void

    /**
     * Reads the value of a register in the specified stack frame, automatically
     * converting the type based on typeSignature.
     */
    static native getLocalValue(thread: int, frame: int, regNumber: long, typeSignature: string): Object

    /**
     * Sets the value of a register in the specified stack frame, automatically
     * converting the type based on typeSignature.
     */
    static native setLocalValue(thread: int, frame: int, regNumber: long, typeSignature: string, value: Any): void

    /**
     * Sets an array element by variable name. Automatically performs numeric widening
     * based on the array type signature from debug info, and dispatches to the
     * resizable Array / FixedArray / ValueArray write paths.
     * @param thread Thread ID
     * @param frame Frame depth
     * @param name Array variable name
     * @param index Element index
     * @param value The value to write
     */
    static native setElement(thread: int, frame: int, name: string, index: int, value: Any): void

    /**
     * Runtime type cast (castAs), semantically aligned with checkcast: resolves the
     * target class by name at runtime, checks whether the value's actual type is a
     * subtype of the target, returns the original value if so, or throws otherwise.
     * The evaluate compilation context cannot resolve user-defined types, so
     * `obj as MyObj` performs the check at runtime.
     * @param thread Thread ID
     * @param frame Frame depth
     * @param value The value to cast
     * @param className Target class name (source-level simple name, e.g. "MyObj")
     */
    static native castAs(thread: int, frame: int, value: Any, className: string): Object

    /**
     * Resolves a Class object by name at runtime, for scenarios requiring runtime
     * class resolution such as newInstance. Shares the same class name resolution
     * logic (ResolveClassByName) as castAs, but does not check IsInitialized
     * (the new operation itself triggers class initialization).
     * @param thread Thread ID
     * @param frame Frame depth
     * @param className Source-level simple class name (e.g. "MyObj")
     * @returns The resolved Class object; throws if not found
     */
    static native resolveClass(thread: int, frame: int, className: string): Class

    /**
     * Runtime instanceof check: resolves the Class by name and checks whether the
     * value's actual type is a subtype of className. Shares class name resolution
     * logic (ResolveClassByName) with castAs, covering user-defined types that the
     * evaluate compilation context cannot resolve.
     * @param thread Thread ID
     * @param frame Frame depth
     * @param value The value to check
     * @param className Source-level simple class name (e.g. "MyObj")
     * @returns true if value is an instance of className
     */
    static native instanceofOp(thread: int, frame: int, value: Any, className: string): boolean

    /**
     * Gets the value of a global variable.
     */
    static native getGlobalVariable(name: string): Object

    /**
     * Sets the value of a global variable.
     */
    static native setGlobalVariable(name: string, value: Any): void
    static native typeofValue(value: Any): string

    /**
     * Create a String instance from an arbitrary value, bypassing the
     * reflection Constructor.createInstance path that cannot invoke native
     * constructors (String::<ctor> is implemented via initobj, not as a
     * regular method).  Returns null if the value cannot be converted.
     */
    static native createStringInstance(value: Any): String

    /**
     * Resolves top-level functions matching name within the frame's module
     * (compiled as static methods of the module class), returning reflect.StaticMethod[]
     * for overload resolution by the caller.
     */
    static native findModuleFunctions(thread: int, frame: int, name: string): FixedArray<reflect.StaticMethod>

    /**
     * Packs rest arguments into an array after numeric widening according to the
     * rest element signature from debug info (consistent with the compiler's
     * restArgsLowering compile-time boxing). Returns Object[] for invoke.
     */
    static native packRestArgs(args: FixedArray<Any>, startIdx: int, forceDouble: boolean): Object

    /**
     * Extract the underlying value from an enum (BaseEnum subclass) object.
     * BaseEnum<T> has a private "value" field; this reads it at the runtime level
     * and returns a boxed primitive (Int, Long, Double, etc.) or String.
     * Returns null if the object is not an enum or the value field cannot be read.
     */
    static native getEnumValue(enumObj: Any): Object

    /**
     * Classification of builtin subscriptable types, used by the miss-path dispatch of
     * callInstanceMember/getField: 1 = native array (FixedArray/ValueArray — subscript is
     * Ldarr instruction semantics; the class has no methods, invisible to method reflection),
     * 2 = tuple (std.core.Tuple implementation, read via the interface method unsafeGet),
     * 0 = neither.
     */
    static native isBuiltinIndexable(obj: Any): int

    /**
     * Reads a native array element and boxes it (with bounds check; throws RangeError on
     * out-of-bounds) — the evaluate-side equivalent of CheckLoadArrayOp + Ldarr. Guarded by
     * isBuiltinIndexable == 1.
     */
    static native getElementBuiltin(receiver: Any, index: Any): Object

    /**
     * Native array length — the evaluate-side equivalent of the Lenarr instruction (the
     * class has no length field/getter). Guarded by isBuiltinIndexable == 1.
     */
    static native getBuiltinLength(receiver: Any): int

    /**
     * Subscript write on a value receiver (the '$_set' miss-path dispatch for the builtin
     * subscriptable family): native arrays (Starr semantics) and tuples ($N fields) are
     * invisible to method reflection, so they are dispatched via this native; the logic is
     * identical to setElement for an identifier receiver.
     */
    static native setElementBuiltin(receiver: Any, index: Any, value: Any): void

    /**
     * Copies a builtin subscriptable value (native array/tuple) into a resizable
     * std.core.Array for spread expansion iteration in evaluate; a resizable input returns
     * the original object. Replaces the legacy `as Array<Object>` bridge (that cast would
     * always CCE on non-resizable receivers).
     */
    static native toResizableArray(receiver: Any): Array<Object>

    /** Checks whether the object's class hierarchy has a $_get method (subscript-readable). */
    static native hasSubscriptGet(obj: Any): boolean

    /** Invokes $_get(key) on obj, returning the value (or null if not found). */
    static native subscriptGet(obj: Any, key: Any): Any

    /** Invokes $_set(key, value) on obj. */
    static native subscriptSet(obj: Any, key: Any, value: Any): void

    /**
     * Wraps a literal value as a ValueProxy, bridging literals into the Proxy system.
     * Used for scenarios requiring a Proxy type argument such as setValue(v: DebugProxy).
     */
    static wrap(value: Any): DebugProxy {
        return new ValueProxy(value)
    }

    /**
     * Shared field lookup logic for ValueProxy.getField and VariableProxy.getField.
     * Resolves static fields, static getters, static method refs, instance fields
     * (superclass walk), instance getters (superclass walk), method refs (superclass
     * walk), and the native-array length intrinsic. The caller handles the null-value
     * case (return null proxy or throw) before calling this method.
     */
static getFieldOnValue(obj: Object, displayName: string, field: string): DebugProxy {
        if (obj instanceof Class) {
            let cls = obj
            let staticField = DebuggerAPI.getStaticField(cls, field)
            if (staticField != undefined) {
                return new StaticFieldProxy(field, staticField)
            }
            let staticGetter: reflect.StaticMethod | undefined = undefined
            for (let m of DebuggerAPI.getStaticMethods(cls)) {
                let mname = m.getName()
                let propName = mname.startsWith("%%get-") ? mname.substring("%%get-".length) : ""
                if (m.isGetter() && propName == field) {
                    staticGetter = m
                    break
                }
            }
            if (staticGetter != undefined) {
                let staticSetter: reflect.StaticMethod | undefined = undefined
                for (let m of DebuggerAPI.getStaticMethods(cls)) {
                    let mname = m.getName()
                    let propName = mname.startsWith("%%set-") ? mname.substring("%%set-".length) : ""
                    if (m.isSetter() && propName == field) {
                        staticSetter = m
                        break
                    }
                }
                return new StaticGetterProxy(field, staticGetter, staticSetter)
            }
            for (let m of DebuggerAPI.getStaticMethods(cls)) {
                if (m.getName() == field) {
                    return new StaticMethodRefProxy(cls, field)
                }
            }
            throw new Error("field '" + field + "' not found on '" + displayName + "'")
        }
        let objClass = Class.of(obj)
        let fieldClass: Class | undefined = objClass
        while (fieldClass != undefined) {
            let instField = DebuggerAPI.getInstanceField(fieldClass, field)
            if (instField != undefined) {
                return new InstanceFieldProxy(obj, instField)
            }
            fieldClass = fieldClass.getSuper()
        }
        let getter: reflect.InstanceMethod | undefined = undefined
        let getterClass: Class | undefined = objClass
        while (getterClass != undefined) {
            for (let m of DebuggerAPI.getInstanceMethods(getterClass)) {
                let mname = m.getName()
                let propName = mname.startsWith("%%get-") ? mname.substring("%%get-".length) : ""
                if (m.isGetter() && propName == field) {
                    getter = m
                    break
                }
            }
            if (getter != undefined) { break }
            getterClass = getterClass.getSuper()
        }
        if (getter != undefined) {
            let setter: reflect.InstanceMethod | undefined = undefined
            let setterClass: Class | undefined = objClass
            while (setterClass != undefined) {
                for (let m of DebuggerAPI.getInstanceMethods(setterClass)) {
                    let mname = m.getName()
                    let propName = mname.startsWith("%%set-") ? mname.substring("%%set-".length) : ""
                    if (m.isSetter() && propName == field) {
                        setter = m
                        break
                    }
                }
                if (setter != undefined) { break }
                setterClass = setterClass.getSuper()
            }
            return new GetterProxy(obj, field, getter, setter)
        }
        let methodFound = false
        let methodSearchCls: Class | undefined = objClass
        while (methodSearchCls != undefined) {
            let methods = DebuggerAPI.getInstanceMethods(methodSearchCls)
            for (let i = 0; i < methods.length; i++) {
                if (methods[i].getName() == field) {
                    methodFound = true
                    break
                }
            }
            if (methodFound) { break }
            methodSearchCls = methodSearchCls.getSuper()
        }
        if (methodFound) {
            return new MethodRefProxy(obj, field)
        }
        if (field == "length" && DebuggerAPI.isBuiltinIndexable(obj) == 1) {
            return new ValueProxy(new Int(DebuggerAPI.getBuiltinLength(obj)))
        }
        // Subscript fallback: when the field lookup fails, the object may be a
        // Record/Map/JsonElement etc. that supports $_get/$_set subscript access.
        // The frontend transformer obj['a'] (string-literal key) into getField("a"),
        // but for index-signature types this is a map lookup, not a field access.
        // Return a SubscriptProxy so both reads (value() -> $_get) and writes
        // (setValue() -> $_set) work through the proxy chain.
        if (DebuggerAPI.hasSubscriptGet(obj)) {
            return new SubscriptProxy(obj, field)
        }
        throw new Error("field '" + field + "' not found on '" + displayName + "'")
    }

    /**
     * Shared function invocation with ClassCastError retry, used by ValueProxy.invoke
     * and VariableProxy.invoke.
     */
    static invokeFunction(displayName: string, fn: Function, args: FixedArray<Any>): DebugProxy {
        DebuggerAPI.validateArgCount(displayName, args, fn)
        try {
            return new ValueProxy(fn.unsafeCall(...args))
        } catch (e) {
            if (e instanceof ClassCastError) {
                return new ValueProxy(fn.unsafeCall(...DebuggerAPI.widenAllNumericArgsToDouble(args)))
            }
            throw e
        }
    }

    /**
     * Shared member call dispatch, used by ValueProxy.call and VariableProxy.call.
     * If val is a Class, dispatches to callStaticMember; otherwise to callInstanceMember.
     */
    static callOnValue(val: Object, displayName: string, method: string, args: FixedArray<Any>): DebugProxy {
        if (val instanceof Class) {
            let cls = val
            return DebuggerAPI.callStaticMember(cls, method, cls.getName(), args)
        }
        let cls = Class.of(val)
        return DebuggerAPI.callInstanceMember(cls, val, method, displayName, args)
    }

    /**
     * Invokes an instance member, supporting:
     * 1. Method invocation with overload resolution aligned to compiler semantics:
     *    tries in declaration order, the first whose "arity is within [mandatory
     *    param count, total param count (rest is unbounded)] and all args are
     *    assignable" wins; omitted optional args are filled with undefined (callee
     *    evaluates default values, same as defaultParametersLowering), excess args
     *    for rest methods are packed into an array (same as restArgsLowering);
     * 2. Function-typed field invocation (getField + invoke).
     * ETS getters are compiled as methods rather than fields, so getField falls
     * back to getter method lookup.
     */
    static callInstanceMember(cls: Class, obj: Object, method: string, displayName: string,
                              args: FixedArray<Any>): DebugProxy {
        // 1. Resolution: score by argument compatibility (exact > numeric widening > subtype),
        //    select the unique highest score. Note: the runtime method table is in fixed
        //    physical order (unrelated to source declaration order), while the ETS compiler's
        //    prevails resolution is based on declaration order — the runtime cannot reproduce
        //    the compile-time choice, so type-matching scoring is used as the closest
        //    approximation; ties throw ambiguous.
        // getInstanceMethods only returns methods declared on the current class,
        // so walk the superclass chain to include inherited methods.
        let m: reflect.Method | undefined
        let methodSearchCls: Class | undefined = cls
        while (methodSearchCls != undefined) {
            m = DebuggerAPI.resolveMethod(DebuggerAPI.getInstanceMethods(methodSearchCls), method, args, displayName)
            if (m != undefined) {
                break
            }
            methodSearchCls = methodSearchCls.getSuper()
        }
        if (m != undefined) {
            let im = m as reflect.InstanceMethod
            let shaped = DebuggerAPI.shapeArgs(args, im, false)
            try {
                return new ValueProxy(
                    im.invoke(obj, DebuggerAPI.convertArgsToParamTypes(shaped, im.getParameterTypes())))
            } catch (e) {
                if (DebuggerAPI.hasRestParam(im)) {
                    let shaped2 = DebuggerAPI.shapeArgs(args, im, true)
                    return new ValueProxy(
                        im.invoke(obj, DebuggerAPI.convertArgsToParamTypes(shaped2, im.getParameterTypes())))
                }
                throw e
            }
        }

        // No overload matched — distinguish "method exists but args mismatch" from
        // "method does not exist at all" so that the user gets a precise error.
        let methodExists = false
        let errSearchCls: Class | undefined = cls
        while (errSearchCls != undefined) {
            let errMethods = DebuggerAPI.getInstanceMethods(errSearchCls)
            for (let i = 0; i < errMethods.length; i++) {
                if (errMethods[i].getName() == method) {
                    methodExists = true
                    break
                }
            }
            if (methodExists) { break }
            errSearchCls = errSearchCls.getSuper()
        }
        if (methodExists) {
            throw new Error("no matching overload for method '" + method + "' with "
                + args.length + " argument(s) on '" + displayName + "'")
        }

        // Builtin indexable kinds whose subscript semantics are invisible to
        // method reflection: native arrays (FixedArray/ValueArray — subscript is
        // Ldarr, the class has no methods at all) and tuples (elements live in
        // the $N fields behind the interface's unsafeGet). '$_' cannot start a
        // user identifier, so this never collides with a user-defined method.
        if (args.length == 1 && method == "$_get") {
            let kind = DebuggerAPI.isBuiltinIndexable(obj)
            if (kind == 1) {
                return new ValueProxy(DebuggerAPI.getElementBuiltin(obj, args[0] as Object))
            }
            if (kind == 2) {
                // The Tuple interface mandates unsafeGet(index) — a regular
                // reflection-visible method (with its own bounds check).
                return DebuggerAPI.callInstanceMember(cls, obj, "unsafeGet", "tuple", args)
            }
        }

        // Same builtin-indexable kinds on the write side: native arrays
        // (Starr semantics) and tuples ($N fields) have no '$_set' method.
        if (args.length == 2 && method == "$_set") {
            let kind = DebuggerAPI.isBuiltinIndexable(obj)
            if (kind == 1 || kind == 2) {
                DebuggerAPI.setElementBuiltin(obj, args[0] as Object, args[1] as Object)
                return new ValueProxy(undefined)
            }
        }

        // 2. Function-typed field fallback: if the field value is callable, invoke it.
        let fieldSearchCls: Class | undefined = cls
        while (fieldSearchCls != undefined) {
            let instField = DebuggerAPI.getInstanceField(fieldSearchCls, method)
            if (instField != undefined) {
                let fval = instField.getValue(obj)
                if (fval instanceof Function) {
                    DebuggerAPI.validateArgCount(method, args, fval)
                    try {
                        return new ValueProxy(fval.unsafeCall(...args))
                    } catch (e) {
                        if (e instanceof ClassCastError) {
                            return new ValueProxy(fval.unsafeCall(...DebuggerAPI.widenAllNumericArgsToDouble(args)))
                        }
                        throw e
                    }
                }
                throw new Error("field '" + method + "' on '" + displayName + "' is not callable")
            }
            fieldSearchCls = fieldSearchCls.getSuper()
        }
        throw new Error("method or callable field '" + method + "' not found on '" + displayName + "'")
    }

    /** Invokes a static member, symmetric to callInstanceMember (static method / static function field / static getter). */
    static callStaticMember(cls: Class, method: string, displayName: string, args: FixedArray<Any>): DebugProxy {
        // 1. Resolution: same as callInstanceMember (scoring-based).
        let m = DebuggerAPI.resolveMethod(DebuggerAPI.getStaticMethods(cls), method, args, displayName)
        if (m != undefined) {
            let sm = m as reflect.StaticMethod
            let shaped = DebuggerAPI.shapeArgs(args, sm, false)
            try {
                return new ValueProxy(
                    sm.invoke(DebuggerAPI.convertArgsToParamTypes(shaped, sm.getParameterTypes())))
            } catch (e) {
                if (DebuggerAPI.hasRestParam(sm)) {
                    let shaped2 = DebuggerAPI.shapeArgs(args, sm, true)
                    return new ValueProxy(
                        sm.invoke(DebuggerAPI.convertArgsToParamTypes(shaped2, sm.getParameterTypes())))
                }
                throw e
            }
        }

        // 2. Static function field fallback.
        let staticField = DebuggerAPI.getStaticField(cls, method)
        if (staticField != undefined) {
            let fval = staticField.getValue()
            if (fval instanceof Function) {
                DebuggerAPI.validateArgCount(method, args, fval)
                try {
                    return new ValueProxy(fval.unsafeCall(...args))
                } catch (e) {
                    if (e instanceof ClassCastError) {
                        return new ValueProxy(fval.unsafeCall(...DebuggerAPI.widenAllNumericArgsToDouble(args)))
                    }
                    throw e
                }
            }
            throw new Error("static field '" + method + "' on '" + displayName + "' is not callable")
        }
        throw new Error("method or callable field '" + method + "' not found on '" + displayName + "'")
    }

    /**
     * Identifier-form call: first searches by variable name (local/parameter/module
     * field function value, preserving zero-regression for f(x) variable calls),
     * then falls back to resolving top-level functions in the frame's module (static
     * method set, overload resolution by argument types).
     * typeArgs: comma-separated generic type parameter names (e.g. "String" or "double"),
     * empty string means no generic type parameters.
     * Runtime validation: throws when reference type parameters are incompatible with
     * boxed primitive arguments (e.g. ident<String>(42)); primitive type parameters
     * allow numeric widening (e.g. ident<double>(42)).
     */
    static callFunction(thread: int, frame: int, name: string, typeArgs: string,
                        ...args: FixedArray<Any>): DebugProxy {
        // Validate generic type arguments against actual argument types.
        if (typeArgs != "") {
            DebuggerAPI.validateTypeArgs(name, typeArgs, args)
        }
        // callFunction itself is an ETS frame: the internal intrinsics
        // (get/findModuleFunctions) count from the stack top (this frame), so the
        // target frame depth needs +1 to align with the frame semantics of direct
        // intrinsic calls (transformer-generated get/set).
        let targetFrame = frame + 1
        let fn: Any = null
        try {
            fn = DebuggerAPI.get(thread, targetFrame, name)
        } catch (e) {
            fn = null
        }
        if (fn != null) {
            // Recursive local lambdas are compiled as Box<FunctionType>: the
            // compiler wraps the lambda in a std.core.Box so the closure can
            // capture a mutable reference. Unwrap the Box's 'value' field to
            // recover the actual callable object before instanceof checks.
            let fnCls0 = Class.of(fn as object)
            if (fnCls0.getName() == "std.core.Box") {
                let boxField = DebuggerAPI.getInstanceField(fnCls0, "value")
                if (boxField != undefined) {
                    let inner = boxField.getValue(fn as Object)
                    if (inner != null) {
                        fn = inner
                    }
                }
            }
            if (fn instanceof Function) {
                DebuggerAPI.validateArgCount(name, args, fn)
                try {
                    return new ValueProxy(fn.unsafeCall(...args))
                } catch (e) {
                    if (e instanceof ClassCastError) {
                        return new ValueProxy(fn.unsafeCall(...DebuggerAPI.widenAllNumericArgsToDouble(args)))
                    }
                    throw e
                }
            }
            // If fn is a Class, treat ClassName(args) as new ClassName(args).
            // In JS/TS, `BigInt(1)` creates a BigInt — it's a constructor call
            // even without the `new` keyword.  ETS classes behave similarly.
            if (fn instanceof Class) {
                let cls = fn
                return DebuggerAPI.constructInstance(cls, name, args)
            }
            throw new Error("'" + name + "' is not callable")
        }
        let methods = DebuggerAPI.findModuleFunctions(thread, targetFrame, name)
        if (methods.length > 0) {
            return new ValueProxy(DebuggerAPI.invokeModuleFunctions(methods, name, args))
        }
        // Fallback: try 'this' instance method (e.g. `add(1,2)` in a method
        // body is really `this.add(1,2)` — the compiler inserts the implicit
        // receiver, but the evaluate AST transformer only emits callFunction).
        let thisObj = DebuggerAPI.getThis(thread, targetFrame)
        let thisCls = Class.of(thisObj)
        return DebuggerAPI.callInstanceMember(
            thisCls, thisObj, name, thisCls.getName(), args)
    }

    /** Construct an instance from a Class object (e.g. BigInt(1) → new BigInt(1)).
     *  Shared by callFunction (when fn is a Class) and the new-expression path. */
    private static constructInstance(cls: Class, className: string, args: FixedArray<Any>): DebugProxy {
        // String has native constructors — use the dedicated intrinsic.
        if (className == "String") {
            if (args.length > 0) {
                return new ValueProxy(DebuggerAPI.createStringInstance(args[0] as Object))
            }
            return new ValueProxy("")
        }
        return new ValueProxy(DebuggerAPI.invokeConstructor(cls, className, args))
    }

    /** Module function overload resolution + invocation (same rules as callStaticMember:
     *  declaration order + assignability + arity range). */
    private static invokeModuleFunctions(methods: FixedArray<reflect.StaticMethod>, name: string,
                                         args: FixedArray<Any>): Any {
        let m = DebuggerAPI.resolveMethod(methods, name, args, "function '" + name + "'")
        if (m != undefined) {
            let sm = m as reflect.StaticMethod
            let shaped = DebuggerAPI.shapeArgs(args, sm, false)
            try {
                return sm.invoke(DebuggerAPI.convertArgsToParamTypes(shaped, sm.getParameterTypes()))
            } catch (e) {
                if (DebuggerAPI.hasRestParam(sm)) {
                    let shaped2 = DebuggerAPI.shapeArgs(args, sm, true)
                    return sm.invoke(DebuggerAPI.convertArgsToParamTypes(shaped2, sm.getParameterTypes()))
                }
                throw e
            }
        }
        throw new Error("no overload of function '" + name + "' accepts the given arguments")
    }

    /**
     * super.foo(args) call: resolves and invokes an instance method targeting the
     * superclass of the current frame's this (spec Step 1: super → superclass of
     * the class that contains the call). Resolution and argument shaping reuse
     * the complete rules from callInstanceMember.
     */
    static callSuper(thread: int, frame: int, method: string, ...args: FixedArray<Any>): DebugProxy {
        // Same as callFunction: this method is an ETS frame, so getThis target frame depth needs +1.
        let thisObj = DebuggerAPI.getThis(thread, frame + 1)
        let cls = Class.of(thisObj)
        let superCls = cls.getSuper()
        if (superCls == undefined) {
            throw new Error("no superclass for super call '" + method + "'")
        }
        return DebuggerAPI.callInstanceMember(superCls, thisObj, method, "super", args)
    }

    /**
     * Runtime implementation of `new X(args)`: resolves Class by name → gets
     * constructor list → overload resolution → argument shaping →
     * Constructor.createInstance. Resolution and shaping reuse the complete rules
     * from callInstanceMember.
     */
    static newInstance(thread: int, frame: int, className: string, ...args: FixedArray<Any>): Object {
        // String has native constructors implemented via initobj that
        // cannot be invoked through the reflection API (Constructor.createInstance).
        // Use the dedicated C++ intrinsic to create String instances directly.
        if (className == "String") {
            if (args.length > 0) {
                return DebuggerAPI.createStringInstance(args[0] as Object)
            }
            return ""
        }
        let cls = DebuggerAPI.resolveClass(thread, frame, className)
        return DebuggerAPI.invokeConstructor(cls, className, args)
    }

    /**
     * Shared constructor resolution + invocation: resolveConstructor → shapeArgs →
     * createInstance with ClassCastError retry. Used by constructInstance and newInstance.
     */
    private static invokeConstructor(cls: Class, className: string, args: FixedArray<Any>): Object {
        let ctors = cls.getConstructors()
        if (ctors.length == 0) {
            throw new Error("no constructor found for class '" + className + "'")
        }
        let m = DebuggerAPI.resolveConstructor(ctors, args, "class '" + className + "'")
        if (m != undefined) {
            let ctor = m
            let shaped = DebuggerAPI.shapeArgs(args, ctor, false)
            try {
                return ctor.createInstance(
                    DebuggerAPI.convertArgsToParamTypes(shaped, ctor.getParameterTypes()))
            } catch (e) {
                if (e instanceof ClassCastError) {
                    let widened = DebuggerAPI.widenAllNumericArgsToDouble(args)
                    return ctor.createInstance(
                        DebuggerAPI.convertArgsToParamTypes(widened, ctor.getParameterTypes()))
                }
                throw e
            }
        }
        let argTypes = ""
        for (let i = 0; i < args.length; i++) {
            if (i > 0) { argTypes += ", " }
            if (args[i] == null) {
                argTypes += "null"
            } else {
                argTypes += Class.of(args[i] as Object).getName()
            }
        }
        let ctorSigs = ""
        for (let i = 0; i < ctors.length; i++) {
            let c = ctors[i]
            let ptypes = c.getParameterTypes()
            ctorSigs += "("
            for (let j = 0; j < ptypes.length; j++) {
                if (j > 0) { ctorSigs += "," }
                ctorSigs += ptypes[j].getName()
            }
            ctorSigs += ")"
            if (i < ctors.length - 1) { ctorSigs += " | " }
        }
        throw new Error("no matching constructor for class '" + className + "'"
            + " with args [" + argTypes + "]; available: " + ctorSigs)
    }

    /**
     * Constructor overload resolution: symmetric to resolveMethod logic, with the
     * constructor name fixed as "constructor". Scores by argument compatibility,
     * selects the unique highest score.
     */
    private static resolveConstructor(ctors: FixedArray<reflect.Constructor>, args: FixedArray<Any>,
                                      displayName: string): reflect.Constructor | undefined {
        let best: reflect.Constructor | undefined = undefined
        let bestScore = -1
        let tie = false
        for (let i = 0; i < ctors.length; i++) {
            let m = ctors[i]
            let nParams = m.getParametersNum()
            let isRest = DebuggerAPI.hasRestParam(m)
            let mandatory = DebuggerAPI.getMandatoryArgsNum(m)
            if (!(mandatory <= args.length && (isRest || args.length <= nParams))) { continue }
            let score = DebuggerAPI.argsMatchScore(args, m.getParameterTypes(), isRest)
            if (score < 0) { continue }
            if (score > bestScore) {
                best = m
                bestScore = score
                tie = false
            } else if (score == bestScore) {
                tie = true
            }
        }
        if (best != undefined && !tie) {
            return best
        }
        throw new Error("ambiguous overload or no matching constructor for " + displayName)
    }

    /**
     * Resolution: iterates the method set (in runtime enumeration order, unrelated
     * to source declaration order — the ETS compiler's prevails resolution is based
     * on declaration order, which the runtime cannot reproduce), scoring by argument
     * compatibility:
     *   exact match (arg class == param class, including primitive params and their
     *   corresponding boxed classes) > numeric widening > subtype
     * Selects the unique highest score; ties or all-incompatible throw an error.
     */
    private static resolveMethod(methods: FixedArray<reflect.Method>, method: string, args: FixedArray<Any>,
                                 displayName: string): reflect.Method | undefined {
        let best: reflect.Method | undefined = undefined
        let bestScore = -1
        let tie = false
        for (let i = 0; i < methods.length; i++) {
            let m = methods[i]
            if (m.getName() != method) { continue }
            let nParams = m.getParametersNum()
            let isRest = DebuggerAPI.hasRestParam(m)
            let mandatory = DebuggerAPI.getMandatoryArgsNum(m)
            if (!(mandatory <= args.length && (isRest || args.length <= nParams))) { continue }
            let score = DebuggerAPI.argsMatchScore(args, m.getParameterTypes(), isRest)
            if (score < 0) { continue }
            if (score > bestScore) {
                best = m
                bestScore = score
                tie = false
            } else if (score == bestScore) {
                tie = true
            }
        }
        if (best != undefined && !tie) {
            return best
        }
        if (tie) {
            throw new Error("ambiguous overload for method '" + method + "' on '" + displayName + "'")
        }
        return undefined
    }

    /** Per-argument scoring: exact=3, numeric widening=2, subtype=1, incompatible=-1.
     *  Rest excess args are not scored. */
    private static argsMatchScore(args: FixedArray<Any>, paramTypes: FixedArray<Class>, isRest: boolean): int {
        let regular = isRest ? paramTypes.length - 1 : paramTypes.length
        let score = 0
        for (let i = 0; i < args.length; i++) {
            if (i < regular) {
                let s = DebuggerAPI.argMatchScore(args[i], paramTypes[i])
                if (s < 0) { return -1 }
                score += s
            } else if (!isRest) {
                return -1
            }
        }
        return score
    }

    private static argMatchScore(arg: Any, pt: Class): int {
        if (arg == null) {
            return pt.isPrimitive() ? -1 : 1
        }
        let argClass = Class.of(arg as Object)
        if (argClass == pt || DebuggerAPI.boxMatchesPrimitive(argClass, pt)) {
            return 3
        }
        if (argClass.isSubtypeOf(pt)) {
            return 1
        }
        if (DebuggerAPI.isWideningTarget(pt) && DebuggerAPI.isNumericBox(arg)) {
            return 2
        }
        return -1
    }

    /** Exact match between primitive parameter class (f64/i32/...) and its boxed class (Double/Int/...). */
    private static boxMatchesPrimitive(argClass: Class, pt: Class): boolean {
        let ptName = pt.getName()
        if (ptName == "f64") { return argClass == Class.of(0.0) }
        if (ptName == "f32") { return argClass == Class.of(0.0 as Float) }
        if (ptName == "i64") { return argClass == Class.of(0 as Long) }
        if (ptName == "i32") { return argClass == Class.of(0) }
        if (ptName == "i8") { return argClass == Class.of(0 as Byte) }
        if (ptName == "i16") { return argClass == Class.of(0 as Short) }
        if (ptName == "u16") { return argClass == Class.of(c'a') }
        if (ptName == "u1") { return argClass == Class.of(true) }
        return false
    }

    /** Whether the target parameter type is a valid ETS compile-time numeric widening target
     *  (byte/short/char require explicit cast, not included). */
    private static isWideningTarget(pt: Class): boolean {
        let ptName = pt.getName()
        if (pt == Class.of(0.0) || ptName == "f64") { return true }
        if (pt == Class.of(0.0 as Float) || ptName == "f32") { return true }
        if (pt == Class.of(0 as Long) || ptName == "i64") { return true }
        if (pt == Class.of(0) || ptName == "i32") { return true }
        return false
    }

    /** Validate that generic type arguments are compatible with actual argument types.
     *  Uses ETS widening rules: Byte→Short→Int→Long→Float→Double, Char→Int.
     *  e.g. ident<String>(42) → Error (Int is not assignable to String)
     *       ident<double>(42) → OK    (Int widens to Double)
     *       ident<Char>(42)   → Error (Int does NOT widen to Char)
     *       ident<Boolean>(42)→ Error (Int is not assignable to Boolean) */
    private static validateTypeArgs(name: string, typeArgs: string, args: FixedArray<Any>): void {
        let typeArgList = typeArgs.split(",")
        let count = typeArgList.length < args.length ? typeArgList.length : args.length
        for (let i = 0; i < count; i++) {
            let ta = typeArgList[i]
            if (args[i] == null) {
                continue  // null is assignable to any reference type
            }
            let argClass = Class.of(args[i] as Object)
            let targetClass = DebuggerAPI.resolvePrimitiveClass(ta)
            if (targetClass != undefined) {
                // Type arg is a primitive/boxed type — check widening compatibility
                if (!DebuggerAPI.canWidenTo(argClass, targetClass)) {
                    throw new Error("type argument '" + ta + "' is not compatible with argument of type '"
                        + argClass.getName() + "'")
                }
            } else {
                // Type arg is a reference type (String, Object, MyClass, etc.)
                // Boxed primitives are not assignable to reference types
                if (DebuggerAPI.isBoxedPrimitive(argClass)) {
                    throw new Error("type argument '" + ta + "' is not compatible with argument of type '"
                        + argClass.getName() + "'")
                }
            }
        }
    }
 
    /** Resolve a primitive/boxed type name to its Class object, or undefined if not a primitive type. */
    private static resolvePrimitiveClass(name: string): Class | undefined {
        if (name == "int" || name == "Int") { return Class.of(0) }
        if (name == "double" || name == "Double" || name == "number" || name == "Number") { return Class.of(0.0) }
        if (name == "long" || name == "Long") { return Class.of(0 as Long) }
        if (name == "float" || name == "Float") { return Class.of(0.0 as Float) }
        if (name == "short" || name == "Short") { return Class.of(0 as Short) }
        if (name == "byte" || name == "Byte") { return Class.of(0 as Byte) }
        if (name == "char" || name == "Char") { return Class.of(c'a') }
        if (name == "boolean" || name == "Boolean") { return Class.of(true) }
        return undefined
    }
 
    /** Check if argClass can widen to targetClass per ETS widening rules.
     *  Widening chain: Byte→Short→Int→Long→Float→Double, Char→Int.
     *  Exact match is also allowed. */
    private static canWidenTo(argClass: Class, targetClass: Class): boolean {
        if (argClass == targetClass) {
            return true  // exact match
        }
        // Double accepts Byte, Short, Int, Long, Float
        if (targetClass == Class.of(0.0)) {
            return argClass == Class.of(0) || argClass == Class.of(0 as Long)
                || argClass == Class.of(0 as Short) || argClass == Class.of(0 as Byte)
                || argClass == Class.of(0.0 as Float)
        }
        // Float accepts Byte, Short, Int, Long
        if (targetClass == Class.of(0.0 as Float)) {
            return argClass == Class.of(0) || argClass == Class.of(0 as Long)
                || argClass == Class.of(0 as Short) || argClass == Class.of(0 as Byte)
        }
        // Long accepts Byte, Short, Int
        if (targetClass == Class.of(0 as Long)) {
            return argClass == Class.of(0) || argClass == Class.of(0 as Short)
                || argClass == Class.of(0 as Byte)
        }
        // Int accepts Byte, Short, Char
        if (targetClass == Class.of(0)) {
            return argClass == Class.of(0 as Short) || argClass == Class.of(0 as Byte)
                || argClass == Class.of(c'a')
        }
        // Short accepts Byte
        if (targetClass == Class.of(0 as Short)) {
            return argClass == Class.of(0 as Byte)
        }
        return false
    }
 
    /** Check if a Class is a boxed primitive wrapper (Int, Double, Long, …). */
    private static isBoxedPrimitive(cls: Class): boolean {
        return cls == Class.of(0) || cls == Class.of(0.0) || cls == Class.of(0 as Long)
            || cls == Class.of(0 as Byte) || cls == Class.of(0 as Short)
            || cls == Class.of(0.0 as Float) || cls == Class.of(c'a')
            || cls == Class.of(true)
    }

    /**
     * Argument shaping aligned to the compiler-lowered bytecode shape (regular
     * params + undefined padding + rest array): missing params are filled with
     * undefined (callee evaluates default values), excess rest args are packed
     * via packRestArgs with numeric widening according to the element signature
     * from debug info (Int box → Double box, consistent with the compiler's
     * restArgsLowering compile-time boxing, otherwise callee readback checkcast
     * fails), and an empty array is passed when there are no rest args.
     */
    private static shapeArgs(args: FixedArray<Any>, m: reflect.Method, forceDouble: boolean): FixedArray<Any> {
        let nParams = m.getParametersNum()
        let isRest = DebuggerAPI.hasRestParam(m)
        let regular = isRest ? nParams - 1 : nParams
        let result = new FixedArray<Any>(nParams, undefined)
        for (let i = 0; i < regular; i++) {
            result[i] = i < args.length ? args[i] : undefined
        }
        if (isRest) {
            result[nParams - 1] = DebuggerAPI.packRestArgs(args, regular, forceDouble)
        }
        return result
    }

    /**
     * Numeric argument widening: reflect invoke's parameter checking is exact-typed
     * (CheckReceiverType, Int box passed to a double parameter throws TypeError),
     * while ETS compile-time argument passing allows implicit widening such as
     * int → double — this bridges the evaluate-side semantics. Non-numeric args
     * are returned as-is (exact matching is still checked by the reflect layer).
     * The array is modified in-place (FixedArray elements are writable, and the
     * caller's varargs array is newly created per call, so no side effects).
     */
    static convertArgsToParamTypes(args: FixedArray<Any>, paramTypes: FixedArray<Class>): FixedArray<Any> {
        for (let i = 0; i < args.length; i++) {
            args[i] = DebuggerAPI.convertArgToParamType(args[i], paramTypes[i])
        }
        return args
    }

    public static convertArgToParamType(arg: Any, pt: Class): Any {
        // Only performs ETS compile-time legal implicit widening (byte/short/char/int/long/float → double,
        // integer family → float/long/int, etc.); narrowing directions (Double → float/long/int,
        // Float → long/int, Long → int) require explicit cast at compile time, so they are left
        // as-is for the reflect layer's exact type check to report errors, avoiding silent truncation.
        // Target type determination: primitive parameters return primitive class at the reflect layer
        // (getName() returns the signature name: f64/f32/i64/i32, not the boxed class); boxed class
        // parameters return the boxed class (compared via Class.of).
        let ptName = pt.getName()
        if (pt == Class.of(0.0) || ptName == "f64") {
            if (DebuggerAPI.isNumericBox(arg)) {
                return DebuggerAPI.toDouble(arg as Object)
            }
            return arg
        }
        if (pt == Class.of(0.0 as Float) || ptName == "f32") {
            // Double → float is narrowing, do not convert.
            if (arg instanceof Int || arg instanceof Byte || arg instanceof Short || arg instanceof Char ||
                arg instanceof Long || arg instanceof Float) {
                return DebuggerAPI.toFloat(arg)
            }
            return arg
        }
        if (pt == Class.of(0 as Long) || ptName == "i64") {
            // Float/Double → long is narrowing, do not convert.
            if (arg instanceof Int || arg instanceof Byte || arg instanceof Short || arg instanceof Char ||
                arg instanceof Long) {
                return DebuggerAPI.toLong(arg)
            }
            return arg
        }
        if (pt == Class.of(0) || ptName == "i32") {
            // Long/Float/Double → int is narrowing, do not convert.
            if (arg instanceof Int || arg instanceof Byte || arg instanceof Short || arg instanceof Char) {
                return DebuggerAPI.toInt(arg)
            }
            return arg
        }
        return arg
    }

    private static isNumericBox(v: Any): boolean {
        return v instanceof Int || v instanceof Byte || v instanceof Short || v instanceof Char ||
               v instanceof Long || v instanceof Float || v instanceof Double
    }

    /** Widens all numeric args to Double (lambda parameters typed as `number` expect
     *  Double boxes but integer literals arrive as Int).
     */
    /**
     * Find the name of the primary invoke method of a Function (lambda/closure)
     * by walking the class hierarchy. Returns "invokeN" (non-rest) or "invokeRN"
     * (rest-param), or empty string if not found.
     */
    static findPrimaryInvokeName(fn: Object): string {
        let searchCls: Class | undefined = Class.of(fn)
        while (searchCls != undefined) {
            let methods = DebuggerAPI.getInstanceMethods(searchCls)
            for (let i = 0; i < methods.length; i++) {
                let m = methods[i]
                let mname = m.getName()
                // Rest-param lambda: invokeR0, invokeR1, ...
                if (mname.startsWith("invokeR")) {
                    if (DebuggerAPI.parseArity(mname.substring("invokeR".length)) >= 0) {
                        return mname
                    }
                } else if (mname.startsWith("invoke")) {
                    // Non-rest lambda: invoke0, invoke1, ..., invoke16.
                    // The abstract invokeN is the primary method; final ones
                    // are forwarding bridges added by LambdaN base.
                    if (DebuggerAPI.parseArity(mname.substring("invoke".length)) >= 0 && m.isAbstract()) {
                        return mname
                    }
                }
            }
            searchCls = searchCls.getSuper()
        }
        return ""
    }

    /**
     * Get the expected parameter count of a Function (lambda/closure).
     * Returns -1 if the invoke method cannot be determined (in which case
     * validation is skipped).
     */
    static getFunctionParamCount(fn: Object): int {
        let mname = DebuggerAPI.findPrimaryInvokeName(fn)
        if (mname.length == 0) { return -1 }
        let prefix = mname.startsWith("invokeR") ? "invokeR" : "invoke"
        return DebuggerAPI.parseArity(mname.substring(prefix.length))
    }

    /**
     * Check whether a Function (lambda/closure) has rest parameters.
     */
    static functionHasRestParam(fn: Object): boolean {
        return DebuggerAPI.findPrimaryInvokeName(fn).startsWith("invokeR")
    }
 
    private static parseArity(s: string): int {
        if (s.length == 0) { return -1 }
        let n = 0
        for (let i = 0; i < s.length; i++) {
            let c = s.charCodeAt(i)
            if (c < 48 || c > 57) { return -1 }  // not a digit
            n = n * 10 + (c - 48)
        }
        return n
    }
 
    /**
     * Validate argument count against the function's expected parameter count.
     * For non-rest lambdas: exact match required.
     * For rest-param lambdas: at least paramCount args required.
     * Throws TypeError if the count is wrong; does nothing if the param
     * count cannot be determined (getFunctionParamCount returns -1).
     */
    static validateArgCount(name: string, args: FixedArray<Any>, fn: Object): void {
        let paramCount = DebuggerAPI.getFunctionParamCount(fn)
        if (paramCount < 0) {
            return  // Can't determine param count; skip validation
        }
        let hasRest = DebuggerAPI.functionHasRestParam(fn)
        if (hasRest) {
            if (args.length < paramCount) {
                throw new TypeError("'" + name + "' requires at least " +
                    paramCount.toString() + " argument(s) but " +
                    args.length.toString() + " were provided")
            }
        } else {
            if (args.length != paramCount) {
                throw new TypeError("'" + name + "' expects " +
                    paramCount.toString() + " argument(s) but " +
                    args.length.toString() + " were provided")
            }
        }
    }
 
    static widenAllNumericArgsToDouble(args: FixedArray<Any>): FixedArray<Any> {
        let result = new FixedArray<Any>(args.length, undefined)
        for (let i = 0; i < args.length; i++) {
            if (DebuggerAPI.isNumericBox(args[i])) {
                result[i] = DebuggerAPI.toDouble(args[i] as Object)
            } else {
                result[i] = args[i]
            }
        }
        return result
    }

    static unwrap(v: DebugProxy): Any {
        if (v instanceof ValueProxy) {
            return v.value()
        }
        if (v instanceof VariableProxy) {
            return v.value()
        }
        if (v instanceof GlobalFunction || v instanceof GlobalClass) {
            throw new TypeError("cannot unwrap a global function or class proxy")
        }
        return v
    }

    /**
     * Numerically widens value to match the reference's boxed type, so that
     * field.setValue's IsAssignableFrom check passes. Only converts numeric boxed
     * types; non-numeric or already-matching values are returned as-is.
     */
    public static widenNumericBox(value: Object, reference: Object): Object {
        if (reference instanceof Double) return DebuggerAPI.toDouble(value)
        if (reference instanceof Float) return DebuggerAPI.toFloat(value)
        if (reference instanceof Long) return DebuggerAPI.toLong(value)
        if (reference instanceof Int || reference instanceof Short ||
            reference instanceof Byte || reference instanceof Char) {
            return DebuggerAPI.toInt(value)
        }
        return value
    }

    /**
     * Converts an Object to Double (boxed) by boxed type, for the evaluate
     * transformer's `as double` expression. Implemented in native to keep the
     * stdlib abc small; enum values are unwrapped and retried.
     */
    public static native toDouble(v: Any): Double

    /**
     * Converts an Object to Float (boxed) by boxed type, for the evaluate
     * transformer's `as float` expression. Implemented in native to keep the
     * stdlib abc small; enum values are unwrapped and retried.
     */
    public static native toFloat(v: Any): Float

    /**
     * Converts an Object to Long (boxed) by boxed type, for the evaluate
     * transformer's `as long` expression. Integers widen, floating-point
     * values truncate, Boolean→0/1. Enum values are unwrapped and retried.
     */
    public static native toLong(v: Any): Long

    /**
     * Converts an Object to Byte (boxed) by boxed type, for the evaluate
     * transformer's `as byte` expression. Integers are converted by widening
     * semantics, floating-point by ETS truncation semantics.
     */
    public static native toByte(v: Any): Byte

    /**
     * Converts an Object to Short (boxed) by boxed type, for the evaluate
     * transformer's `as short` expression.
     */
    public static native toShort(v: Any): Short

    /**
     * Converts an Object to Char (boxed) by boxed type, for the evaluate
     * transformer's `as char` expression. Floating-point boxed types have no
     * toChar method, and Double→Char is not a legal ETS conversion, so it is
     * not provided.
     */
    public static native toChar(v: Any): Char

    /**
     * Converts an Object to Boolean (boxed) by boxed type, for the evaluate
     * transformer's `as boolean` expression. Numerics are converted by zero-value
     * semantics (0 → false, non-zero → true).
     */
    public static native toBoolean(v: Any): Boolean

    /**
     * Converts an Object to Int (boxed) by boxed type, for the evaluate
     * transformer's index expressions: in `arr[i]`, get("i") returns Object,
     * which is dispatched via instanceof to Int by this method, then implicitly
     * unboxed as the int index for Array.$_get/$_set.
     */
    public static native toInt(v: Any): Int

    /**
     * Unary negation -a. BigInt uses ETS operator overloading; all other
     * numeric boxes are dispatched to the native numericUnaryOp.
     */
    static neg(a: Object): Object {
        if (a instanceof BigInt) {
            return -a
        }
        return DebuggerAPI.numericUnaryOp(a, 16)  // NEG
    }

    /**
     * Addition or string concatenation a + b. If either operand is a String, performs string concatenation;
     * otherwise delegates to `numericAdd()` for numeric addition.
     *
     * Why `Object` instead of `Any`: the evaluate expression AST transformer
     * (expressionASTTransformer.cpp) bridges all operands via `EnsureObject()` — an `as Object` cast —
     * before calling this method. `Object` is non-nullable and guarantees the operands are reference
     * types (boxed primitives), so `instanceof` dispatch for type-specific arithmetic works reliably.
     * `Any` would also permit unboxed primitives or null, which cannot be used with `instanceof`.
     *
     * Why string processing is required: in ETS/JS semantics, the `+` operator is overloaded —
     * it performs string concatenation when either operand is a String, and numeric addition
     * otherwise. The evaluate transformer maps every `+` operator to this single method, so it
     * must reproduce both branches of the overload at runtime.
     */
    static add(a: Object, b: Object): Object {
        if (a instanceof String || b instanceof String) {
            let sa = (a instanceof String) ? a : a.toString()
            let sb = (b instanceof String) ? b : b.toString()
            return new String(sa + sb)
        }
        if (a instanceof BigInt && b instanceof BigInt) {
            return a + b
        }
        return DebuggerAPI.numericBinaryOp(a, b, 0)  // ADD
    }

    /**
     * BigInt-or-numeric binary op: if both operands are BigInt, applies `op` via
     * ETS operator overloading; otherwise dispatches to the native binary op
     * with the given opcode. Used by sub/mul/div/mod/pow.
     */
    static bigIntOrNumeric(a: Object, b: Object, op: int, opcode: int): Object {
        if (a instanceof BigInt && b instanceof BigInt) {
            if (op == 1) { return a - b }
            if (op == 2) { return a * b }
            if (op == 3) { return a / b }
            if (op == 4) { return a % b }
            return a ** b
        }
        return DebuggerAPI.numericBinaryOp(a, b, opcode)
    }

    /** Subtraction a - b. */
    static sub(a: Object, b: Object): Object {
        return DebuggerAPI.bigIntOrNumeric(a, b, 1, 1)  // SUB
    }

    /** Multiplication a * b. */
    static mul(a: Object, b: Object): Object {
        return DebuggerAPI.bigIntOrNumeric(a, b, 2, 2)  // MUL
    }

    /** Division a / b. */
    static div(a: Object, b: Object): Object {
        return DebuggerAPI.bigIntOrNumeric(a, b, 3, 3)  // DIV
    }

    /** Modulo a % b. */
    static mod(a: Object, b: Object): Object {
        return DebuggerAPI.bigIntOrNumeric(a, b, 4, 4)  // MOD
    }

    /** Exponentiation a ** b. */
    static pow(a: Object, b: Object): Object {
        return DebuggerAPI.bigIntOrNumeric(a, b, 5, 5)  // POW
    }

    /**
     * Left shift a << b. Result type depends only on the left operand:
     * long/double → long, byte/short/int/float/char → int.
     */
    static shl(a: Object, b: Object): Object {
        return DebuggerAPI.numericBinaryOp(a, b, 6)  // SHL
    }

    /**
     * Right shift a >> b. Result type depends only on the left operand:
     * long/double → long, byte/short/int/float/char → int.
     */
    static shr(a: Object, b: Object): Object {
        return DebuggerAPI.numericBinaryOp(a, b, 7)  // SHR
    }

    /**
     * Comparison op (lt/gt/le/ge): String and BigInt use ETS operator
     * overloading; numerics are dispatched to the native binary op.
     * `op` selects the comparison (9=LT, 10=GT, 11=LE, 12=GE) and is also
     * passed as the native opcode.
     */
    static compareOp(a: Object, b: Object, op: int): Object {
        if (a instanceof String && b instanceof String) {
            if (op == 9) { return a < b }
            if (op == 10) { return a > b }
            if (op == 11) { return a <= b }
            return a >= b
        }
        if (a instanceof BigInt && b instanceof BigInt) {
            if (op == 9) { return a < b }
            if (op == 10) { return a > b }
            if (op == 11) { return a <= b }
            return a >= b
        }
        return DebuggerAPI.numericBinaryOp(a, b, op)
    }

    /** Less-than comparison a < b. */
    static lt(a: Object, b: Object): Object {
        return DebuggerAPI.compareOp(a, b, 9)  // LT
    }

    /** Greater-than comparison a > b. */
    static gt(a: Object, b: Object): Object {
        return DebuggerAPI.compareOp(a, b, 10)  // GT
    }

    /** Less-than-or-equal comparison a <= b. */
    static le(a: Object, b: Object): Object {
        return DebuggerAPI.compareOp(a, b, 11)  // LE
    }

    /** Greater-than-or-equal comparison a >= b. */
    static ge(a: Object, b: Object): Object {
        return DebuggerAPI.compareOp(a, b, 12)  // GE
    }

    /**
     * Bitwise AND a & b. Boolean&Boolean→Boolean; otherwise dispatched to the
     * native binary op which widens to Long or Int.
     */
    static bitAnd(a: Object, b: Object): Object {
        return DebuggerAPI.numericBinaryOp(a, b, 13)  // BIT_AND
    }

    /**
     * Bitwise OR a | b. Boolean|Boolean→Boolean; otherwise dispatched to the
     * native binary op which widens to Long or Int.
     */
    static bitOr(a: Object, b: Object): Object {
        return DebuggerAPI.numericBinaryOp(a, b, 14)  // BIT_OR
    }

    /**
     * Bitwise XOR a ^ b. Boolean^Boolean→Boolean; otherwise dispatched to the
     * native binary op which widens to Long or Int.
     */
    static bitXor(a: Object, b: Object): Object {
        return DebuggerAPI.numericBinaryOp(a, b, 15)  // BIT_XOR
    }

    /**
     * Unsigned right shift a >>> b. Result type depends only on the left operand:
     * long/double → long, byte/short/int/float/char → int.
     */
    static ushr(a: Object, b: Object): Object {
        return DebuggerAPI.numericBinaryOp(a, b, 8)  // USHR
    }

    /**
     * Unary plus +a. BigInt uses ETS operator overloading; all other numeric
     * boxes are dispatched to the native numericUnaryOp.
     */
    static pos(a: Object): Object {
        if (a instanceof BigInt) {
            return +a
        }
        return DebuggerAPI.numericUnaryOp(a, 17)  // POS
    }

    /**
     * Bitwise NOT ~a. Boolean→!Boolean; all other numeric boxes are dispatched
     * to the native numericUnaryOp.
     */
    static bitNot(a: Object): Object {
        return DebuggerAPI.numericUnaryOp(a, 18)  // BIT_NOT
    }

    // Debugger-private reflection lookup that includes non-public members:
    // unlike the public Class.getInstanceField/getInstanceMethods (which filter
    // out private/protected members), the debugger must inspect all members
    // regardless of access modifier. Kept private: they expose private state
    // and must not be visible or callable outside this class; the proxies
    // receive pre-resolved reflect handles instead.
    private static native getInstanceField(cls: Class, name: string): reflect.InstanceField | undefined

    private static native getStaticField(cls: Class, name: string): reflect.StaticField | undefined

    private static native getInstanceMethods(cls: Class): FixedArray<reflect.InstanceMethod>

    private static native getStaticMethods(cls: Class): FixedArray<reflect.StaticMethod>

    private static native numericBinaryOp(a: Any, b: Any, op: int): Object
    private static native numericUnaryOp(a: Any, op: int): Object
    private static native getMandatoryArgsNum(m: reflect.Method): int
    private static native hasRestParam(m: reflect.Method): boolean
}
