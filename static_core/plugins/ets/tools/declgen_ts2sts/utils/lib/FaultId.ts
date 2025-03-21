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

export enum FaultID {
  AnyType,
  SymbolType,
  ObjectLiteralNoContextType,
  ArrayLiteralNoContextType,
  ComputedPropertyName,
  LiteralAsPropertyName,
  TypeQuery,
  IsOperator,
  DestructuringParameter,
  YieldExpression,
  InterfaceMerging,
  EnumMerging,
  InterfaceExtendsClass,
  IndexMember,
  WithStatement,
  ThrowStatement,
  IndexedAccessType,
  UnknownType,
  ForInStatement,
  InOperator,
  ImportFromPath,
  FunctionExpression,
  IntersectionType,
  ObjectTypeLiteral,
  CommaOperator,
  LimitedReturnTypeInference,
  LambdaWithTypeParameters,
  ClassExpression,
  DestructuringAssignment,
  DestructuringDeclaration,
  VarDeclaration,
  CatchWithUnsupportedType,
  DeleteOperator,
  DeclWithDuplicateName,
  UnaryArithmNotNumber,
  ConstructorType,
  ConstructorIface,
  ConstructorFuncs,
  CallSignature,
  TypeAssertion,
  PrivateIdentifier,
  LocalFunction,
  ConditionalType,
  MappedType,
  NamespaceAsObject,
  ClassAsObject,
  NonDeclarationInNamespace,
  GeneratorFunction,
  FunctionContainsThis,
  PropertyAccessByIndex,
  JsxElement,
  EnumMemberNonConstInit,
  ImplementsClass,
  MethodReassignment,
  MultipleStaticBlocks,
  ThisType,
  IntefaceExtendDifProps,
  StructuralIdentity,
  DefaultImport,
  ExportAssignment,
  ImportAssignment,
  GenericCallNoTypeArgs,
  ParameterProperties,
  InstanceofUnsupported,
  ShorthandAmbientModuleDecl,
  WildcardsInModuleName,
  UMDModuleDefinition,
  NewTarget,
  DefiniteAssignment,
  Prototype,
  GlobalThis,
  UtilityType,
  PropertyDeclOnFunction,
  FunctionApplyCall,
  FunctionBind,
  ConstAssertion,
  ImportAssertion,
  SpreadOperator,
  LimitedStdLibApi,
  ErrorSuppression,
  StrictDiagnostic,
  ImportAfterStatement,
  EsObjectType,
  NumbericLiteral,
  ConditionalTypes,
  EnumWithMixedType,
  IndexAccessType,
  Module,
  ExportNamespace,
  TypeLiteral,
  KeyofType,
  AnyToJSValue,
  UnknownToJSValue,
  SymbolToJSValue,
  ESObjectToJSValue,
  IntersectionTypeJSValue,
  TypeDeclaration,
  DuplicatedDeclaration,
  ObjectParametersToJSValue,
  WrapperToPrimitive,
  TypeTransform,
  StringLiteralType,
  StringTypeAlias,
  ObjectBindingParams,
  ReflectQuery,
  VarDeclarationAssignment,
  LiteralType,
  LimitImport,
  NoInitializer,
  DuplicatedEnum,
  // this should always be last enum
  LAST_ID
}
