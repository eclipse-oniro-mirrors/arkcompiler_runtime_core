/*
 * Copyright (c) 2022-2025 Huawei Device Co., Ltd.
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

import * as ts from 'typescript';
export const JSValue: string = 'Any';
export const ESObject: string = 'ESObject';
export const ArrayType: string = 'Array';
export const KitPrefix: string[] = [];
export const BuiltInType: string[] = ['Symbol'];
export const ETSKeyword: string[] = ['Class', 'ESObject', 'MethodType'];
export const UtilityTypes = new Map<string, string>([
    ['Capitalize', JSValue],
    ['Uncapitalize', JSValue],
    ['Uppercase', JSValue],
    ['Lowercase', JSValue],
    ['ConstructorParameters', ArrayType],
    ['Exclude', JSValue],
    ['Extract', JSValue],
    ['NonNullable', JSValue],
    ['InstanceType', JSValue],
    ['NoInfer', JSValue],
    ['Omit', JSValue],
    ['OmitThisParameter', JSValue],
    ['Parameters', ArrayType],
    ['Pick', JSValue],
    ['ReturnType', JSValue],
    ['ThisParameterType', JSValue],
    ['ThisType', JSValue],
]);
export const SpecificTypes = [
    ts.SyntaxKind.AnyKeyword,
    ts.SyntaxKind.ConditionalType,
    ts.SyntaxKind.ConstructorType,
    ts.SyntaxKind.IndexedAccessType,
    ts.SyntaxKind.IndexSignature,
    ts.SyntaxKind.IntersectionType,
    ts.SyntaxKind.MappedType,
    ts.SyntaxKind.ObjectLiteralExpression,
    ts.SyntaxKind.SymbolKeyword,
    ts.SyntaxKind.TemplateLiteralType,
    ts.SyntaxKind.UnknownKeyword
];
export const FINAL_CLASS: string[] = [
    'ArgumentOutOfRangeException',
    'ArrayIndexOutOfBoundsError',
    'ArrayStoreError',
    'ArrayType',
    'ArrayValue',
    'ArithmeticError',
    'AtomicFlag',
    'Boolean',
    'BooleanType',
    'BooleanValue',
    'Byte',
    'ByteType',
    'ByteValue',
    'CallBodyDefault',
    'CallBodyFunction',
    'CallBodyMethod',
    'CallBodyErasedFunction',
    'Char',
    'CharType',
    'CharValue',
    'Class',
    'ClassCastError',
    'ClassType',
    'ClassValue',
    'Chrono',
    'Console',
    'Coroutine',
    'CoroutineExtras',
    'DebuggerAPI',
    'Double',
    'DoubleType',
    'DoubleValue',
    'EnumConstant',
    'EnumType',
    'Field',
    'FieldCreator',
    'FinalizableWeakRef',
    'FinalizationRegistry',
    'Float',
    'FloatType',
    'FloatValue',
    'GC',
    'IllegalArgumentError',
    'InterfaceType',
    'Int',
    'IntIntValue',
    'IntType',
    'JSError',
    'JSRuntime',
    'JSValue',
    'LambdaType',
    'LambdaValue',
    'Logger',
    'Long',
    'LongType',
    'LongValue',
    'Method',
    'MethodCreator',
    'MethodType',
    'NegativeArraySizeError',
    'NoDataException',
    'NullType',
    'NullValue',
    'Parameter',
    'ParameterCreator',
    'Promise',
    'Runtime',
    'RuntimeException',
    'Short',
    'ShortType',
    'ShortValue',
    'String',
    'StringBuilder',
    'StringIndexOutOfBoundsError',
    'StringType',
    'StringValue',
    'TupleType',
    'Types',
    'UnionCase',
    'UnionType',
    'UnsupportedOperationException',
    'UndefinedType',
    'UndefinedValue',
    'Void',
    'VoidType',
    'VoidValue',
    'WeakRef'
];
export const InvalidFuncParaNames: string[] = ['this'];
export const LIMIT_DECORATOR: string[] = ['Sendable', 'Concurrent'];
