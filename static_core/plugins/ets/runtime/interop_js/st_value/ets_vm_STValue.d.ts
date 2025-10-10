/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

export declare enum SType {
    BOOLEAN,
    BYTE,
    CHAR,
    SHORT,
    INT,
    LONG,
    FLOAT,
    DOUBLE,
}

export declare class STValue {
    static findClass(desc: string): STValue
    static findNamespace(desc: string): STValue
    static findEnum(desc: string): STValue

    static wrapByte(b: number): STValue
    static wrapChar(b: number): STValue
    static wrapShort(b: number): STValue
    static wrapInt(b: number): STValue
    static wrapLong(l: number): STValue  // may lose precision if abs > 2^53
    static wrapLong(l: bigint): STValue
    static wrapFloat(b: number): STValue
    static wrapNumber(b: number): STValue
    static wrapBigInt(b: bigint): STValue
    static wrapString(b: string): STValue

    static getNull(): STValue
    static getUndefined(): STValue

    static newFixedArrayPrimitive(len: number, elementType: SType): STValue
    static newFixedArrayReference(len: number, elementType: STValue, initialElement: STValue): STValue

    classInstantiate(ctorSignature: string, args: Array<STValue>): STValue

    namespaceGetVariable(name: string, variableType: SType): STValue
    namespaceSetVariable(name: string, val: STValue, variableType: SType): void

    classGetSuperClass(): STValue

    classGetStaticField(name: string, fieldType: SType): STValue
    classSetStaticField(name: string, val: STValue, fieldType: SType): void

    objectGetProperty(name: string, propType: SType): STValue
    objectSetProperty(name: string, val: STValue, propType: SType): void

    fixedArrayGet(idx: number, elementType: SType): STValue
    fixedArraySet(idx: number, val: STValue, elementType: SType): void
    fixedArrayGetLength(): number

    enumGetIndexByName(name: string): number
    enumGetNameByIndex(idx: number): string
    enumGetValueByName(name: string, valueType: SType): STValue
    enumGetValueByIndex(idx: number, valueType: SType): STValue

    functionalObjectInvoke(args: Array<STValue>): STValue

    moduleInvokeFunction(name: string, signature: string, args: STValue[]): STValue

    namespaceInvokeFunction(name: string, signature: string, args: STValue[]): STValue
    objectInvokeMethod(name: string, signature: string, args: Array<STValue>): STValue
    classInvokeStaticMethod(name: string, signature: string, args: Array<STValue>): STValue

    objectInstanceOf(typeArg: STValue): boolean
    objectGetType(): STValue
    typeIsAssignableFrom(from_type: STValue, to_type: STValue): boolean
    isNull(): boolean
    isUndefined(): boolean
    isEqualTo(other: STValue): boolean
    isStrictlyEqualTo(other: STValue): boolean
    
    isNumber(): boolean
    isFloat(): boolean
    isLong(): boolean
    isInt(): boolean
    isShort(): boolean
    isChar(): boolean
    isByte(): boolean
    isBoolean(): boolean
    isBigInt(): boolean
    isString(): boolean

    toNumber(): number
    toString(): string
    toBigInt(): bigint
    toBoolean(): boolean
}