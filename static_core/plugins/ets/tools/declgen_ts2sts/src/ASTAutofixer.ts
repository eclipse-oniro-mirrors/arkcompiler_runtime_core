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

import { FaultID } from '../utils/lib/FaultId';
import { visitVisitResult } from './utils/ASTHelpers';
import { JSValue, KitPrefix } from '../utils/lib/TypeUtils';

export class Autofixer {
  private readonly typeChecker: ts.TypeChecker;
  private readonly context: ts.TransformationContext;

  constructor(typeChecker: ts.TypeChecker, context: ts.TransformationContext) {
    this.typeChecker = typeChecker;
    this.context = context;
  }

  private readonly autofixes = new Map<ts.SyntaxKind, ts.Visitor[]>([
    [
      ts.SyntaxKind.VariableDeclarationList,
      [this[FaultID.VarDeclaration].bind(this), this[FaultID.VarDeclarationAssignment].bind(this)]
    ],
    [
      ts.SyntaxKind.PropertyDeclaration,
      [this[FaultID.PrivateIdentifier].bind(this), this[FaultID.NoInitializer].bind(this)]
    ],
    [
      ts.SyntaxKind.SourceFile,
      [
        this[FaultID.ImportAfterStatement].bind(this),
        this[FaultID.LimitImport].bind(this),
        this[FaultID.DuplicatedDeclaration].bind(this)
      ]
    ],
    [ts.SyntaxKind.LiteralType, [this[FaultID.NumbericLiteral].bind(this)]],
    [
      ts.SyntaxKind.TypeAliasDeclaration,
      [
        this[FaultID.StringTypeAlias].bind(this),
        this[FaultID.IndexAccessType].bind(this),
        this[FaultID.ConditionalTypes].bind(this)
      ]
    ],
    [ts.SyntaxKind.ModuleDeclaration, [this[FaultID.Module].bind(this)]],
    [
      ts.SyntaxKind.ModuleBlock,
      [this[FaultID.ExportNamespace].bind(this), this[FaultID.DuplicatedDeclaration].bind(this)]
    ],
    [ts.SyntaxKind.TypeOperator, [this[FaultID.KeyofType].bind(this)]],
    [ts.SyntaxKind.TypeLiteral, [this[FaultID.TypeLiteral].bind(this)]],
    [ts.SyntaxKind.AnyKeyword, [this[FaultID.AnyToJSValue].bind(this)]],
    [ts.SyntaxKind.UnknownKeyword, [this[FaultID.UnknownToJSValue].bind(this)]],
    [
      ts.SyntaxKind.Identifier,
      [this[FaultID.ESObjectToJSValue].bind(this), this[FaultID.WrapperToPrimitive].bind(this)]
    ],
    [ts.SyntaxKind.SymbolKeyword, [this[FaultID.SymbolToJSValue].bind(this)]],
    [ts.SyntaxKind.IntersectionType, [this[FaultID.IntersectionTypeJSValue].bind(this)]],
    [ts.SyntaxKind.TypeReference, [this[FaultID.ObjectParametersToJSValue].bind(this)]],
    [ts.SyntaxKind.VariableStatement, [this[FaultID.StringLiteralType].bind(this)]],
    [
      ts.SyntaxKind.FunctionDeclaration,
      [this[FaultID.GeneratorFunction].bind(this), this[FaultID.ObjectBindingParams].bind(this)]
    ],
    [ts.SyntaxKind.TypeQuery, [this[FaultID.ReflectQuery].bind(this)]],
    [ts.SyntaxKind.TypeParameter, [this[FaultID.LiteralType].bind(this)]]
  ]);

  fixNode(node: ts.Node): ts.VisitResult<ts.Node> {
    const autofixes = this.autofixes.get(node.kind);

    if (autofixes === undefined) {
      return node;
    }

    let result: ts.VisitResult<ts.Node> = node;

    for (const autofix of autofixes) {
      result = visitVisitResult(result, autofix);
    }

    return result;
  }

  /**
   * Rule: `arkts-no-var`
   */
  private [FaultID.VarDeclaration](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * Ensure that all variable declarations are using `let` or `const`.
     */

    if (ts.isVariableDeclarationList(node)) {
      const isLetDeclaration = node.flags & ts.NodeFlags.Let;
      const isConstDeclaration = node.flags & ts.NodeFlags.Const;

      if (!isLetDeclaration && !isConstDeclaration) {
        const newFlags = node.flags | ts.NodeFlags.Let;
        return this.context.factory.createVariableDeclarationList(node.declarations, newFlags);
      }
    }

    return node;
  }

  /**
   * Rule: `arkts-no-bigint-binaryExpression`
   */
  private [FaultID.VarDeclarationAssignment](node: ts.Node): ts.VisitResult<ts.Node> {
    /*
     * bigint literal map to number,and binary experssion map to boolean in arkts1.2
     */

    if (ts.isVariableDeclarationList(node)) {
      const isLetDeclaration = node.flags & ts.NodeFlags.Let;
      const isConstDeclaration = node.flags & ts.NodeFlags.Const;

      // update boolean type declaration function
      if (isConstDeclaration || isLetDeclaration) {
        return transformLogicalOperators(
          node,
          this.context,
          isConstDeclaration ? ts.NodeFlags.Const : ts.NodeFlags.Let
        );
      }
    }

    return node;
  }

  /**
   * Rule: `arkts-no-private-identifiers`
   */
  private [FaultID.PrivateIdentifier](node: ts.Node): ts.VisitResult<ts.Node> {
    void this;

    /*
     * Since we can access only public members of the imported dynamic class,
     * there is no need to fix private fields, we just do not emit them
     */

    if (ts.isPropertyDeclaration(node)) {
      if (
        ts.isPrivateIdentifier(node.name) ||
        node.modifiers?.find((v) => {
          return v.kind === ts.SyntaxKind.PrivateKeyword;
        })
      ) {
        return undefined;
      }
    }

    return node;
  }

  /**
   * Rule: `arkts-no-misplaced-imports`
   */
  private [FaultID.ImportAfterStatement](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * This algorithm is very very bad for both memory and performance.
     * Redo later, when implementing other autofixes
     */

    if (ts.isSourceFile(node)) {
      const importDeclarations: ts.Statement[] = [];
      const statements: ts.Statement[] = [];

      for (const stmt of node.statements) {
        if (ts.isImportDeclaration(stmt)) {
          importDeclarations.push(stmt);
        } else {
          statements.push(stmt);
        }
      }

      return this.context.factory.updateSourceFile(node, [...importDeclarations, ...statements]);
    }

    return node;
  }

  /**
   * Rule: `arkts-no-limit-imports`
   */
  private [FaultID.LimitImport](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * Ensure the order of import statements and remove restricted import statements.
     */

    if (ts.isSourceFile(node)) {
      const { importDeclarations, statements } = processSourceFileStatements(node.statements, this.context);
      return this.context.factory.updateSourceFile(node, [...importDeclarations, ...statements]);
    }

    return node;
  }

  /**
   * Rule: `arkts-no-number-literal`
   */
  private [FaultID.NumbericLiteral](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * NumbericLiteral mapped to number in arkts1.2
     */

    if (ts.isLiteralTypeNode(node)) {
      const typeNode = node.literal;
      if (
        (ts.isPrefixUnaryExpression(typeNode) && typeNode.operand.kind === ts.SyntaxKind.NumericLiteral) ||
        ts.isNumericLiteral(typeNode)
      ) {
        return this.context.factory.createKeywordTypeNode(ts.SyntaxKind.NumberKeyword);
      }
    }

    return node;
  }

  /**
   * Rule: `arkts-no-conditional-types`
   */
  private [FaultID.ConditionalTypes](node: ts.Node): ts.VisitResult<ts.Node> {
    void this;

    /**
     * ConditionalTypes mapped to JSValue in arkts1.2
     */

    if (ts.isTypeAliasDeclaration(node)) {
      if (ts.isConditionalTypeNode(node.type)) {
        const newType = ts.factory.createTypeReferenceNode(JSValue, undefined);
        return ts.factory.createTypeAliasDeclaration(node.modifiers, node.name, node.typeParameters, newType);
      }
    }
    return node;
  }

  /**
   * Rule: `arkts-no-indexed-access-type`
   */
  private [FaultID.IndexAccessType](node: ts.Node): ts.VisitResult<ts.Node> {
    void this;

    /**
     * IndexedAccessType mapped to JSValue in arkts1.2
     */

    if (ts.isTypeAliasDeclaration(node) && ts.isIndexedAccessTypeNode(node.type)) {
      const type = this.typeChecker.getTypeAtLocation(node.type);
      const newType = this.typeChecker.typeToTypeNode(type, undefined, ts.NodeBuilderFlags.NoTruncation);
      if (newType) {
        // Create a new type alias declaration node
        return ts.factory.createTypeAliasDeclaration(node.modifiers, node.name, node.typeParameters, newType);
      }
    }

    return node;
  }

  /**
   * Rule: `arkts-no-module`
   */
  private [FaultID.Module](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * Module mapped to namespace in arkts1.2
     */

    if (ts.isModuleDeclaration(node)) {
      const newFlags = node.flags | ts.NodeFlags.Namespace;
      return this.context.factory.createModuleDeclaration(node.modifiers, node.name, node.body, newFlags);
    }

    return node;
  }

  /**
   * `arkts-no-type-literal`
   */
  private [FaultID.TypeLiteral](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * TypeLiteral mapped to JSValue in arkts1.2
     */

    if (ts.isTypeLiteralNode(node)) {
      const createNode = this.context.factory.createIdentifier(JSValue);
      return this.context.factory.createTypeReferenceNode(createNode, undefined);
    }
    return node;
  }

  /**
   * return keyof result in TS function to JSValue
   */
  private [FaultID.KeyofType](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * keyof result in TS function mapped to JSValue in arkts1.2
     */

    if (ts.isTypeOperatorNode(node)) {
      const createNode = this.context.factory.createIdentifier(JSValue);
      return this.context.factory.createTypeReferenceNode(createNode, undefined);
    }
    return node;
  }

  /**
   * Rule: `arkts-no-export-namespace`
   */
  private [FaultID.ExportNamespace](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * In ArkTS 1.2, it is required to explicitly export namespaces in declaration files.
     */

    const statements: ts.Statement[] = [];
    let shouldChange: boolean = true;

    if (ts.isModuleBlock(node)) {
      for (const stmt of node.statements) {
        stmt.forEachChild((child) => {
          if (child.kind === ts.SyntaxKind.ExportKeyword) {
            shouldChange = false;
            return;
          }
        });
        if (!shouldChange) {
          // reset initial value
          shouldChange = true;
          statements.push(stmt);
        } else {
          let newDeclaration = transModuleBlockformation(stmt, this.context, stmt.kind);
          statements.push(newDeclaration);
        }
      }
      return this.context.factory.updateModuleBlock(node, [...statements]);
    }
    return node;
  }

  /**
   * Rule: `arkts-no-intersection-type`
   */
  private [FaultID.IntersectionTypeJSValue](node: ts.Node): ts.VisitResult<ts.Node> {
    /*
     * Intersection type mapped to JSValue in arkts1.2
     */

    if (ts.isIntersectionTypeNode(node)) {
      return this.context.factory.createTypeReferenceNode(JSValue);
    }

    return node;
  }

  /**
   * Rule: `arkts-no-any`
   */
  private [FaultID.AnyToJSValue](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * Keyword 'any' mapped to JSValue in arkts1.2
     */

    if (node.kind === ts.SyntaxKind.AnyKeyword) {
      return this.context.factory.createTypeReferenceNode(JSValue);
    }

    return node;
  }

  /**
   * Rule: `arkts-no-unknown`
   */
  private [FaultID.UnknownToJSValue](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * Keyword 'unknown' mapped to JSValue in arkts1.2
     */

    if (node.kind === ts.SyntaxKind.UnknownKeyword) {
      return this.context.factory.createTypeReferenceNode(JSValue);
    }

    return node;
  }

  /**
   * Rule: `arkts-no-symbol`
   */
  private [FaultID.SymbolToJSValue](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * Keyword 'symbol' mapped to JSValue in arkts1.2
     */

    if (node.kind === ts.SyntaxKind.SymbolKeyword) {
      return this.context.factory.createTypeReferenceNode(JSValue);
    }

    return node;
  }

  /**
   * Rule: `arkts-no-ESObject`
   */
  private [FaultID.ESObjectToJSValue](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * ESObject mapped to JSValue in arkts1.2
     */

    if (ts.isIdentifier(node)) {
      if (node.text !== undefined && node.text === 'ESObject') {
        return this.context.factory.createTypeReferenceNode(JSValue);
      }
    }

    return node;
  }

  /**
   * Rule: `arkts-no-duplicated-declaration`
   */
  private [FaultID.DuplicatedDeclaration](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * Duplicate interface and class declarations will be merged into one.
     */

    if (!ts.isSourceFile(node) && !ts.isModuleBlock(node)) {
      return node;
    }
    const statements: ts.Statement[] = [];
    const interfaceDeclarations: ts.InterfaceDeclaration[] = [];
    const classDeclarations: ts.ClassDeclaration[] = [];
    for (const stmt of node.statements) {
      if (ts.isInterfaceDeclaration(stmt)) {
        interfaceDeclarations.push(stmt);
      } else if (ts.isClassDeclaration(stmt)) {
        classDeclarations.push(stmt);
      } else {
        statements.push(stmt);
      }
    }
    if (ts.isModuleBlock(node)) {
      const moduleBlockNode = this.context.factory.updateModuleBlock(node, [
        ...statements,
        ...findSameInterfaceAndClassList(interfaceDeclarations, this.context, ts.SyntaxKind.InterfaceDeclaration),
        ...findSameInterfaceAndClassList(classDeclarations, this.context, ts.SyntaxKind.ClassDeclaration)
      ]);
      return moduleBlockNode;
    } else {
      return this.context.factory.updateSourceFile(node, [
        ...statements,
        ...findSameInterfaceAndClassList(interfaceDeclarations, this.context, ts.SyntaxKind.InterfaceDeclaration),
        ...findSameInterfaceAndClassList(classDeclarations, this.context, ts.SyntaxKind.ClassDeclaration)
      ]);
    }

    return node;
  }

  /**
   * Rule: `arkts-no-funtion-objec-JSValue`
   */
  private [FaultID.ObjectParametersToJSValue](node: ts.Node): ts.VisitResult<ts.Node> {
    void this;

    /**
     * Object parameters mapped to JSValue in arkts1.2
     */

    if (ts.isTypeReferenceNode(node)) {
      const typeName = node.typeName;
      if (ts.isIdentifier(typeName)) {
        switch (typeName.text) {
          case 'Object':
          case 'Function':
            return ts.factory.createTypeReferenceNode('JSValue', undefined);
          default:
            return node;
        }
      }
    }

    return node;
  }

  /**
   * Rule: `arkts-no-wrapperTypes`
   */
  private [FaultID.WrapperToPrimitive](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * Wrapper types mapped to primitive type in arkts1.2
     */

    if (ts.isIdentifier(node)) {
      if (node.text !== undefined && node.text === 'Wrapper') {
        return ts.factory.createTypeReferenceNode(JSValue, undefined);
      }
      switch (node.text) {
        case 'Number':
          return this.context.factory.createKeywordTypeNode(ts.SyntaxKind.NumberKeyword);
        case 'String':
          return this.context.factory.createKeywordTypeNode(ts.SyntaxKind.StringKeyword);
        case 'Boolean':
          return this.context.factory.createKeywordTypeNode(ts.SyntaxKind.BooleanKeyword);
        case 'BigInt':
          return this.context.factory.createKeywordTypeNode(ts.SyntaxKind.BigIntKeyword);
        default:
          return node;
      }
    }

    return node;
  }

  /**
   * Rule: `arkts-no-string-literal-type`
   */
  private [FaultID.StringLiteralType](node: ts.Node): ts.VisitResult<ts.Node> {
    void this;

    /**
     * StringLiteralType mapped to string in arkts1.2
     */

    if (ts.isVariableStatement(node)) {
      const newDeclarations = node.declarationList.declarations.map((declaration) => {
        if (declaration.type && ts.isLiteralTypeNode(declaration.type)) {
          let isStringLiteral = false;
          declaration.type.forEachChild((child: ts.Node) => {
            if (child.kind === ts.SyntaxKind.StringLiteral) {
              isStringLiteral = true;
              return;
            }
          });
          if (isStringLiteral) {
            return ts.factory.createVariableDeclaration(
              declaration.name,
              declaration.exclamationToken,
              ts.factory.createKeywordTypeNode(ts.SyntaxKind.StringKeyword),
              declaration.initializer
            );
          }
        }
        return declaration;
      });
      const newDeclarationList = ts.factory.createVariableDeclarationList(newDeclarations, node.declarationList.flags);
      return ts.factory.createVariableStatement(node.modifiers, newDeclarationList);
    }
    return node;
  }

  /**
   * Rule: `arkts-no-string-type-alias`
   */
  private [FaultID.StringTypeAlias](node: ts.Node): ts.VisitResult<ts.Node> {
    void this;

    /**
     * StringTypeAlias mapped to string in arkts1.2
     */

    if (ts.isTypeAliasDeclaration(node)) {
      if (ts.isLiteralTypeNode(node.type)) {
        let isStringLiteral = false;
        node.type.forEachChild((child: ts.Node) => {
          if (child.kind === ts.SyntaxKind.StringLiteral) {
            isStringLiteral = true;
            return;
          }
        });
        if (isStringLiteral) {
          return ts.factory.createTypeAliasDeclaration(
            node.modifiers,
            node.name,
            node.typeParameters,
            ts.factory.createKeywordTypeNode(ts.SyntaxKind.StringKeyword)
          );
        }
      }
    }
    return node;
  }

  /**
   * Rule: `arkts-no-generator-function`
   */
  private [FaultID.GeneratorFunction](node: ts.Node): ts.VisitResult<ts.Node> {
    void this;

    /**
     * GeneratorFunction mapped to JSValue in arkts1.2
     */

    if (ts.isFunctionDeclaration(node) && node.type) {
      const returnType = node.type;
      if (
        ts.isTypeReferenceNode(returnType) &&
        ts.isIdentifier(returnType.typeName) &&
        returnType.typeName.text === 'Generator'
      ) {
        const newType = ts.factory.createTypeReferenceNode(JSValue, undefined);
        return ts.factory.updateFunctionDeclaration(
          node,
          node.modifiers,
          undefined,
          node.name,
          node.typeParameters,
          node.parameters,
          newType,
          undefined
        );
      }
    }

    return node;
  }

  /**
   * Rule: `arkts-no-object-bind-params`
   */
  private [FaultID.ObjectBindingParams](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * ObjectBindingParams mapped to JSValue in arkts1.2
     */

    if (ts.isFunctionDeclaration(node)) {
      const parameters = node.parameters;
      const newParamters: ts.ParameterDeclaration[] = [];
      if (parameters !== undefined && parameters.length > 0) {
        parameters.forEach((parameter, index) => {
          if (ts.isObjectBindingPattern(parameter.name)) {
            const typeNode = this.context.factory.createTypeReferenceNode(
              this.context.factory.createIdentifier('JSValue'),
              undefined
            );
            const currentParamter = this.context.factory.createParameterDeclaration(
              undefined,
              undefined,
              this.context.factory.createIdentifier(`args${index}`),
              undefined,
              typeNode
            );
            newParamters.push(currentParamter);
          } else {
            newParamters.push(parameter);
          }
        });

        return this.context.factory.updateFunctionDeclaration(
          node,
          node.modifiers,
          node.asteriskToken,
          node.name,
          node.typeParameters,
          newParamters,
          node.type,
          node.body
        );
      }
    }

    return node;
  }

  /**
   * Rule: `arkts-no-reflect-query`
   */
  private [FaultID.ReflectQuery](node: ts.Node): ts.VisitResult<ts.Node> {
    void this;

    /**
     * ReflectQuery mapped to Reflect in arkts1.2
     */

    if (ts.isTypeQueryNode(node) && ts.isIdentifier(node.exprName)) {
      if (node.exprName.text === 'Reflect') {
        return this.context.factory.createIdentifier('Reflect');
      }
    }

    return node;
  }

  /**
   * Rule: `arkts-no-literal-type`
   */
  private [FaultID.LiteralType](node: ts.Node): ts.VisitResult<ts.Node> {
    /**
     * Union literal type mapped to JSValue in arkts1.2
     */
    if (ts.isTypeParameterDeclaration(node)) {
      const { unionTypeNode, typeNode } = extractTypeNodes(node, this.context);

      if (unionTypeNode === undefined && typeNode !== undefined) {
        return createTypeParameterDeclaration(node, undefined, typeNode, this.context);
      } else if (unionTypeNode !== undefined && typeNode === undefined) {
        return createTypeParameterDeclaration(node, unionTypeNode, undefined, this.context);
      }
    }
    return node;
  }

  /**
   * Rule: `arkts-no-initializer`
   */
  private [FaultID.NoInitializer](node: ts.Node): ts.VisitResult<ts.Node> {
    
    /**
     * For member variables with initial assignment, convert literals to their corresponding data types.
     */

    if (!ts.isPropertyDeclaration(node)) {
      return node;
    }

    const nodeType = inferNodeTypeFromInitializer(node, this.context);

    if (nodeType !== undefined) {
      return this.context.factory.updatePropertyDeclaration(
        node,
        node.modifiers,
        node.name,
        undefined,
        nodeType,
        undefined
      );
    }

    return node;
  }
}

/**
 * Modify the ModuleBlock node to add Export so that it can be referenced by external methods.
 */
function transModuleBlockformation(
  stmt: ts.Statement,
  context: ts.TransformationContext,
  kindType: number
): ts.Statement {
  const exportToken = context.factory.createToken(ts.SyntaxKind.ExportKeyword);
  return updateNodetoAddExport(stmt, context, kindType, exportToken);
}

/**
 * Extract interface members together.
 */
function extractInterfaceMembers(item: ts.InterfaceDeclaration, methods: ts.TypeElement[]): void {
  item.members.forEach((member) => {
    if ((ts.isMethodSignature(member) || ts.isPropertySignature(member)) && member.name) {
      const memberName = (member.name as ts.Identifier).text;
      if (
        !methods.some(
          (m) =>
            isSameFunction(m, member) ||
            (ts.isPropertySignature(m) && ts.isPropertySignature(member) && m.name.getText() === memberName)
        )
      ) {
        methods.push(member);
      }
    }
  });
}

/**
 * Extract class members together.
 */
function extractClassMembers(item: ts.ClassDeclaration, classMethods: ts.ClassElement[]): void {
  item.members.forEach((member) => {
    if ((ts.isMethodDeclaration(member) || ts.isPropertyDeclaration(member)) && member.name) {
      const memberName = (member.name as ts.Identifier).text;
      if (
        !classMethods.some(
          (m) =>
            (ts.isMethodDeclaration(m) && ts.isMethodDeclaration(member) && m.name.getText() === memberName) ||
            (ts.isPropertyDeclaration(m) && ts.isPropertyDeclaration(member) && m.name.getText() === memberName)
        )
      ) {
        classMethods.push(member);
      }
    }
  });
}

/**
 * Merge classes or interfaces with the same name
 */
function createCombinedInterfaceAndClass(
  list: ts.Statement[],
  context: ts.TransformationContext,
  kindType: number
): ts.Statement {
  const methods: ts.TypeElement[] = [];
  const classMethods: ts.ClassElement[] = [];

  list.forEach((item) => {
    if (ts.isInterfaceDeclaration(item)) {
      extractInterfaceMembers(item, methods);
    } else if (ts.isClassDeclaration(item)) {
      extractClassMembers(item, classMethods);
    }
  });

  if (kindType === ts.SyntaxKind.ClassDeclaration) {
    const classNode = list[0] as ts.ClassDeclaration;
    return context.factory.createClassDeclaration(
      classNode.modifiers,
      classNode.name,
      undefined,
      undefined,
      classMethods
    );
  } else {
    const node = list[0] as ts.InterfaceDeclaration;
    return context.factory.createInterfaceDeclaration(node.modifiers, node.name, undefined, undefined, methods);
  }
}

function isSameFunction(m: ts.TypeElement, member: ts.TypeElement): boolean {
  const memberName = (member.name as ts.Identifier).text;
  const mName = (m.name as ts.Identifier).text;

  if (mName !== memberName) {
    return false;
  }
  if (!ts.isMethodSignature(m) || !ts.isMethodSignature(member)) {
    return false;
  }
  if (!compareParameters(m.parameters, member.parameters)) {
    return false;
  }
  if (m.type && member.type) {
    if (!typesAreEqual(m.type, member.type)) {
      return false;
    }
  } else if (!!m.type !== !!member.type) {
    return false;
  }
  if (!compareTypeParameters(m.typeParameters, member.typeParameters)) {
    return false;
  }
  return true;
}

function compareParameters(
  params1: ts.NodeArray<ts.ParameterDeclaration>,
  params2: ts.NodeArray<ts.ParameterDeclaration>
): boolean {
  if (params1.length !== params2.length) {
    return false;
  }
  for (let i = 0; i < params1.length; i++) {
    const p1 = params1[i];
    const p2 = params2[i];
    if (p1.name.getText() !== p2.name.getText()) {
      return false;
    }
    if (!typesAreEqual(p1.type, p2.type)) {
      return false;
    }

    if (p1.questionToken !== p2.questionToken || p1.initializer !== p2.initializer) {
      return false;
    }
  }

  return true;
}

function typesAreEqual(type1: ts.TypeNode | undefined, type2: ts.TypeNode | undefined): boolean {
  if (!type1 || !type2) {
    return type1 === type2;
  }

  return type1.getText() === type2.getText();
}

function compareTypeParameters(
  params1: ts.NodeArray<ts.TypeParameterDeclaration> | undefined,
  params2: ts.NodeArray<ts.TypeParameterDeclaration> | undefined
): boolean {
  if (!params1 && !params2) {
    return true;
  }
  if (!params1 || !params2) {
    return false;
  }
  if (params1.length !== params2.length) {
    return false;
  }

  for (let i = 0; i < params1.length; i++) {
    const p1 = params1[i];
    const p2 = params2[i];

    if (p1.name.getText() !== p2.name.getText()) {
      return false;
    }
  }

  return true;
}

/**
 * Find a list of interfaces or classes with the same name
 */
function findSameInterfaceAndClassList(
  statements: ts.Statement[],
  context: ts.TransformationContext,
  kindType: number
): ts.Statement[] {
  const sameInterfaceMap = new Map<string, { count: number; nodes: ts.Statement[] }>();
  statements.forEach((statement) => {
    let name: string | undefined;
    const isInterfaceDeclaration = ts.isInterfaceDeclaration(statement) && statement.name;
    const isClassDeclaration = ts.isClassDeclaration(statement) && statement.name;
    if (isInterfaceDeclaration || isClassDeclaration) {
      name = statement.name.text;
    }
    if (name) {
      if (!sameInterfaceMap.has(name)) {
        sameInterfaceMap.set(name, { count: 0, nodes: [] });
      }
      const entry = sameInterfaceMap.get(name)!;
      entry.count++;
      entry.nodes.push(statement);
    }
  });
  const uniqueNodes: ts.Statement[] = [];
  const newNodes: ts.Statement[] = [];
  sameInterfaceMap.forEach((entry) => {
    if (entry.count === 1) {
      uniqueNodes.push(...entry.nodes);
    } else {
      // Reorganize interface information
      const newInterface = createCombinedInterfaceAndClass(entry.nodes, context, kindType);
      newNodes.push(newInterface);
    }
  });

  return [...uniqueNodes, ...newNodes];
}

/**
 * Modify the node node and add the export keyword.
 */
function updateNodetoAddExport(
  stmt: ts.Statement,
  context: ts.TransformationContext,
  kindType: number,
  exportToken: ts.Modifier
): ts.Statement {
  switch (kindType) {
    case ts.SyntaxKind.VariableDeclaration:
    case ts.SyntaxKind.VariableStatement:
      return updateVariableOrStatement(stmt, context, exportToken);
    case ts.SyntaxKind.ModuleDeclaration:
      return updateModuleDeclaration(stmt, context, exportToken);
    case ts.SyntaxKind.FunctionDeclaration:
      return updateFunctionDeclaration(stmt, context, exportToken);
    case ts.SyntaxKind.InterfaceDeclaration:
      return updateInterfaceDeclaration(stmt, context, exportToken);
    case ts.SyntaxKind.ClassDeclaration:
      return updateClassDeclaration(stmt, context, exportToken);
    case ts.SyntaxKind.TypeAliasDeclaration:
      return updateTypeAliasDeclaration(stmt, context, exportToken);
    case ts.SyntaxKind.EnumDeclaration:
      return updateEnumDeclaration(stmt, context, exportToken);
    default:
      return stmt;
  }
}

function updateVariableOrStatement(
  stmt: ts.Statement,
  context: ts.TransformationContext,
  exportToken: ts.Modifier
): ts.Statement {
  if (ts.isVariableStatement(stmt)) {
    return context.factory.updateVariableStatement(
      stmt,
      [exportToken, ...(stmt.modifiers || [])],
      stmt.declarationList
    );
  }
  throw new Error('Unsupported statement type');
}

function updateModuleDeclaration(
  stmt: ts.Statement,
  context: ts.TransformationContext,
  exportToken: ts.Modifier
): ts.Statement {
  if (ts.isModuleDeclaration(stmt)) {
    return context.factory.updateModuleDeclaration(
      stmt,
      [exportToken, ...(stmt.modifiers || [])],
      stmt.name,
      stmt.body
    );
  }
  return stmt;
}

function updateFunctionDeclaration(
  stmt: ts.Statement,
  context: ts.TransformationContext,
  exportToken: ts.Modifier
): ts.Statement {
  if (ts.isFunctionDeclaration(stmt)) {
    return context.factory.updateFunctionDeclaration(
      stmt,
      [exportToken, ...(stmt.modifiers || [])],
      stmt.asteriskToken,
      stmt.name,
      stmt.typeParameters,
      stmt.parameters,
      stmt.type,
      stmt.body
    );
  }
  return stmt;
}

function updateInterfaceDeclaration(
  stmt: ts.Statement,
  context: ts.TransformationContext,
  exportToken: ts.Modifier
): ts.Statement {
  if (ts.isInterfaceDeclaration(stmt)) {
    return context.factory.updateInterfaceDeclaration(
      stmt,
      [exportToken, ...(stmt.modifiers || [])],
      stmt.name,
      stmt.typeParameters,
      stmt.heritageClauses,
      stmt.members
    );
  }
  return stmt;
}

function updateClassDeclaration(
  stmt: ts.Statement,
  context: ts.TransformationContext,
  exportToken: ts.Modifier
): ts.Statement {
  if (ts.isClassDeclaration(stmt)) {
    return context.factory.updateClassDeclaration(
      stmt,
      [exportToken, ...(stmt.modifiers || [])],
      stmt.name,
      stmt.typeParameters,
      stmt.heritageClauses,
      stmt.members
    );
  }
  return stmt;
}

function updateTypeAliasDeclaration(
  stmt: ts.Statement,
  context: ts.TransformationContext,
  exportToken: ts.Modifier
): ts.Statement {
  if (ts.isTypeAliasDeclaration(stmt)) {
    return context.factory.updateTypeAliasDeclaration(
      stmt,
      [exportToken, ...(stmt.modifiers || [])],
      stmt.name,
      stmt.typeParameters,
      stmt.type
    );
  }
  return stmt;
}

function updateEnumDeclaration(
  stmt: ts.Statement,
  context: ts.TransformationContext,
  exportToken: ts.Modifier
): ts.Statement {
  if (ts.isEnumDeclaration(stmt)) {
    return context.factory.updateEnumDeclaration(
      stmt,
      [exportToken, ...(stmt.modifiers || [])],
      stmt.name,
      stmt.members
    );
  }
  return stmt;
}

/**
 * Determine whether the type reference needs to be replaced.
 */
function shouldReplaceTypeReference(typeName: ts.EntityName, typeAliasMap: Map<string, string>): boolean {
  if (ts.isIdentifier(typeName)) {
    return typeAliasMap.has(typeName.text);
  } else if (ts.isQualifiedName(typeName)) {
    const firstIdentifier = getFirstIdentifierInQualifiedName(typeName);
    if (firstIdentifier !== undefined) {
      return typeAliasMap.has(firstIdentifier.text);
    }
  }
  return false;
}

/**
 * Replace the type reference
 */
function replaceTypeReference(node: ts.TypeReferenceNode, context: ts.TransformationContext): ts.Node {
  return context.factory.createTypeReferenceNode(context.factory.createIdentifier('JSValue'), undefined);
}

/**
 * Traverse the nodes
 */
function traverseNode(node: ts.Node, context: ts.TransformationContext, typeAliasMap: Map<string, string>): ts.Node {
  if (ts.isTypeReferenceNode(node)) {
    const typeName = node.typeName;
    if (shouldReplaceTypeReference(typeName, typeAliasMap)) {
      return replaceTypeReference(node, context);
    }
  }
  return ts.visitEachChild(node, (child) => traverseNode(child, context, typeAliasMap), context);
}

/**
 * Replace the type reference with JSValue
 */
function replaceTypeReferenceWithObject(
  node: ts.Node,
  context: ts.TransformationContext,
  typeAliasMap: Map<string, string>
): ts.Node {
  return traverseNode(node, context, typeAliasMap);
}

/**
 * Get the first identifier in a qualified name
 */
function getFirstIdentifierInQualifiedName(qualifiedName: ts.QualifiedName): ts.Identifier | undefined {
  let current: ts.Node = qualifiedName;
  while (ts.isQualifiedName(current)) {
    current = current.left;
  }
  return ts.isIdentifier(current) ? current : undefined;
}

/**
 * Check if the module import is limited
 */
function isLimitImport(moduleSpecifier: ts.Expression, limitImport: string[]): boolean {
  for (const item of limitImport) {
    if (ts.isStringLiteral(moduleSpecifier) && moduleSpecifier.text.startsWith(item)) {
      return true;
    }
  }

  return false;
}

/**
 * Check if the node is of bigint type.
 */
function isBigintType(v: ts.Node): boolean {
  if (ts.isLiteralTypeNode(v)) {
    const literalTypeNode = v as ts.LiteralTypeNode;
    if (literalTypeNode.literal && literalTypeNode.literal.kind !== undefined) {
      if (literalTypeNode.literal.kind === ts.SyntaxKind.PrefixUnaryExpression) {
        const prefix = literalTypeNode.literal as ts.PrefixUnaryExpression;
        return prefix.pos !== -1 && prefix.operand.kind === ts.SyntaxKind.BigIntLiteral;
      }
      return literalTypeNode.literal.kind === ts.SyntaxKind.BigIntLiteral;
    }
  }
  return v.kind === ts.SyntaxKind.BigIntKeyword || v.kind === ts.SyntaxKind.BigIntLiteral;
}

/**
 * Check if the node is of boolean type.
 */
function isBooleanType(v: ts.Node): boolean {
  if (ts.isLiteralTypeNode(v)) {
    const literalTypeNode = v as ts.LiteralTypeNode;
    if (literalTypeNode.literal && literalTypeNode.literal.kind !== undefined) {
      return (
        literalTypeNode.literal.kind === ts.SyntaxKind.FalseKeyword ||
        literalTypeNode.literal.kind === ts.SyntaxKind.TrueKeyword
      );
    }
  }
  return (
    v.kind === ts.SyntaxKind.FalseKeyword ||
    v.kind === ts.SyntaxKind.TrueKeyword ||
    v.kind === ts.SyntaxKind.BooleanKeyword
  );
}

/**
 * Get the updated type.
 */
function getUpdatedType(
  v: ts.Node,
  nodeFlag: ts.NodeFlags,
  context: ts.TransformationContext
): ts.TypeNode | undefined {
  if (ts.isIdentifier(v)) {
    return undefined;
  }
  const isBigint = isBigintType(v);
  const isBoolean = isBooleanType(v);

  if (isBigint && nodeFlag === ts.NodeFlags.Const) {
    return context.factory.createKeywordTypeNode(ts.SyntaxKind.BigIntKeyword);
  } else if (isBoolean) {
    return context.factory.createKeywordTypeNode(ts.SyntaxKind.BooleanKeyword);
  }
  return undefined;
}

/**
 * Process variable declarations
 */
function processVariableDeclaration(
  child: ts.VariableDeclaration,
  nodeFlag: ts.NodeFlags,
  context: ts.TransformationContext
): ts.VariableDeclaration {
  let updatedType: ts.TypeNode | undefined;
  child.forEachChild((v) => {
    updatedType = getUpdatedType(v, nodeFlag, context);
    return !!updatedType;
  });
  if (updatedType) {
    return context.factory.createVariableDeclaration(child.name, undefined, updatedType, undefined);
  }

  return child;
}

/**
 * Change the declarations of logical operators to the boolean type,
 * and change the declarations of bigint constants to the bigint type.
 */
function transformLogicalOperators(
  node: ts.Node,
  context: ts.TransformationContext,
  nodeFlag: ts.NodeFlags
): ts.VisitResult<ts.Node> {
  const variableDeclarations: ts.VariableDeclaration[] = [];
  node.forEachChild((child) => {
    if (ts.isVariableDeclaration(child)) {
      const processedDeclaration = processVariableDeclaration(child, nodeFlag, context);
      variableDeclarations.push(processedDeclaration);
    }
  });
  return context.factory.createVariableDeclarationList(variableDeclarations, nodeFlag);
}

/**
 * Change the literal union type to a type union.
 */
function createUnionTypeNode(
  unionTypeNode: ts.UnionTypeNode,
  context: ts.TransformationContext
): ts.VisitResult<ts.Node> {
  let typeNode: ts.TypeNode[] = [];
  unionTypeNode.forEachChild((child) => {
    if (ts.isLiteralTypeNode(child)) {
      if (child.literal.kind === ts.SyntaxKind.FalseKeyword || child.literal.kind === ts.SyntaxKind.TrueKeyword) {
        typeNode.push(context.factory.createKeywordTypeNode(ts.SyntaxKind.BooleanKeyword));
      } else if (child.literal.kind === ts.SyntaxKind.NumericLiteral) {
        typeNode.push(context.factory.createKeywordTypeNode(ts.SyntaxKind.NumberKeyword));
      } else {
        typeNode.push(child);
      }
    } else {
      typeNode.push(child as ts.TypeNode);
    }
  });
  return context.factory.createUnionTypeNode(typeNode);
}

function processSourceFileStatements(
  statements: readonly ts.Statement[],
  context: ts.TransformationContext
): {
  importDeclarations: ts.Statement[];
  statements: ts.Statement[];
  typeAliasMap: Map<string, string>;
} {
  const importDeclarations: ts.Statement[] = [];
  // 把变量名改回 statements 以匹配返回类型
  const statementsResult: ts.Statement[] = [];
  const typeAliasMap = new Map<string, string>();

  for (const stmt of statements) {
    if (ts.isImportDeclaration(stmt)) {
      processImportDeclaration(stmt, importDeclarations, typeAliasMap);
    } else {
      const newStmt = replaceTypeReferenceWithObject(stmt, context, typeAliasMap);
      statementsResult.push(newStmt as ts.Statement);
    }
  }

  return { importDeclarations, statements: statementsResult, typeAliasMap };
}

function processImportDeclaration(
  stmt: ts.ImportDeclaration,
  importDeclarations: ts.Statement[],
  typeAliasMap: Map<string, string>
): void {
  const moduleSpecifier = stmt.moduleSpecifier;
  if (isLimitImport(moduleSpecifier, KitPrefix)) {
    const importClause = stmt.importClause;
    if (importClause && importClause.namedBindings && ts.isNamedImports(importClause.namedBindings)) {
      processNamedImports(importClause.namedBindings, typeAliasMap);
    } else if (importClause?.name) {
      const originalName = importClause?.name?.text;
      const alias = originalName;
      typeAliasMap.set(alias, originalName);
    }
    return;
  }
  importDeclarations.push(stmt);
}

function processNamedImports(namedImports: ts.NamedImports, typeAliasMap: Map<string, string>): void {
  namedImports.elements.forEach((element) => {
    const originalName = element.propertyName ? element.propertyName.text : element.name.text;
    const alias = element.propertyName ? element.name.text : originalName;
    typeAliasMap.set(alias, originalName);
  });
}

function extractTypeNodes(
  node: ts.TypeParameterDeclaration,
  context: ts.TransformationContext
): {
  unionTypeNode: ts.TypeNode | undefined;
  typeNode: ts.TypeNode | undefined;
} {
  let unionTypeNode: ts.TypeNode | undefined;
  let typeNode: ts.TypeNode | undefined;

  node.forEachChild((parm) => {
    if (!ts.isIdentifier(parm)) {
      if (ts.isUnionTypeNode(parm)) {
        unionTypeNode = createUnionTypeNode(parm, context) as ts.TypeNode;
      } else if (ts.isLiteralTypeNode(parm)) {
        typeNode = getLiteralType(parm, context);
      } else {
        typeNode = undefined;
      }
    }
  });

  return { unionTypeNode, typeNode };
}

function getLiteralType(parm: ts.LiteralTypeNode, context: ts.TransformationContext): ts.TypeNode | undefined {
  if (parm.literal.kind === ts.SyntaxKind.NumericLiteral) {
    return context.factory.createKeywordTypeNode(ts.SyntaxKind.NumberKeyword);
  } else if (parm.literal.kind === ts.SyntaxKind.TrueKeyword || parm.literal.kind === ts.SyntaxKind.FalseKeyword) {
    return context.factory.createKeywordTypeNode(ts.SyntaxKind.BooleanKeyword);
  }
  return undefined;
}

function createTypeParameterDeclaration(
  node: ts.TypeParameterDeclaration,
  constraint: ts.TypeNode | undefined,
  defaultType: ts.TypeNode | undefined,
  context: ts.TransformationContext
): ts.TypeParameterDeclaration {
  return context.factory.createTypeParameterDeclaration(undefined, node.name, constraint, defaultType);
}

/***
 * Handle initializes are not allowed in the environment content
 */
function inferNodeTypeFromInitializer(
  node: ts.PropertyDeclaration,
  context: ts.TransformationContext
): ts.TypeNode | undefined {
  if (node.initializer !== undefined) {
    switch (node.initializer.kind) {
      case ts.SyntaxKind.FalseKeyword:
      case ts.SyntaxKind.TrueKeyword:
        return context.factory.createKeywordTypeNode(ts.SyntaxKind.BooleanKeyword);
      case ts.SyntaxKind.NumericLiteral:
        return context.factory.createKeywordTypeNode(ts.SyntaxKind.NumberKeyword);
      case ts.SyntaxKind.BigIntLiteral:
        return context.factory.createKeywordTypeNode(ts.SyntaxKind.BigIntKeyword);
      case ts.SyntaxKind.StringLiteral:
        return context.factory.createKeywordTypeNode(ts.SyntaxKind.StringKeyword);
      default:
        return undefined;
    }
  } else if (node.type !== undefined && ts.isLiteralTypeNode(node.type)) {
    if (isBooleanType(node.type)) {
      return context.factory.createKeywordTypeNode(ts.SyntaxKind.BooleanKeyword);
    } else if (isBigintType(node.type)) {
      return context.factory.createKeywordTypeNode(ts.SyntaxKind.BigIntKeyword);
    }
  }

  return undefined;
}
