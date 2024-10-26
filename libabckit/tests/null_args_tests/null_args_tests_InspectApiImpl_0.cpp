/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

// Autogenerated file by gen_null_arg_tests.rb script -- DO NOT EDIT!

#include "libabckit/include/c/abckit.h"
#include "libabckit/include/c/metadata_core.h"
#include "libabckit/include/c/extensions/arkts/metadata_arkts.h"
#include "libabckit/include/c/extensions/js/metadata_js.h"
#include "libabckit/include/c/ir_core.h"
#include "libabckit/include/c/isa/isa_dynamic.h"
#include "libabckit/src/include_v2/c/isa/isa_static.h"

#include "helpers/helpers.h"
#include "helpers/helpers_nullptr.h"

#include <gtest/gtest.h>

namespace libabckit::test {

static auto g_inspectApiImp = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);

class LibAbcKitNullptrTestsInspectApiImpl0 : public ::testing::Test {};

// Test: test-kind=api, api=InspectApiImpl::importDescriptorGetFile,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, importDescriptorGetFile)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->importDescriptorGetFile);
}

// Test: test-kind=api, api=InspectApiImpl::functionEnumerateNestedFunctions,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, functionEnumerateNestedFunctions)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->functionEnumerateNestedFunctions);
}

// Test: test-kind=api, api=InspectApiImpl::namespaceEnumerateNamespaces,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, namespaceEnumerateNamespaces)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->namespaceEnumerateNamespaces);
}

// Test: test-kind=api, api=InspectApiImpl::annotationElementGetAnnotation,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationElementGetAnnotation)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationElementGetAnnotation);
}

// Test: test-kind=api, api=InspectApiImpl::functionIsCtor,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, functionIsCtor)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->functionIsCtor);
}

// Test: test-kind=api, api=InspectApiImpl::moduleEnumerateAnnotationInterfaces,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, moduleEnumerateAnnotationInterfaces)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->moduleEnumerateAnnotationInterfaces);
}

// Test: test-kind=api, api=InspectApiImpl::exportDescriptorGetFile,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, exportDescriptorGetFile)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->exportDescriptorGetFile);
}

// Test: test-kind=api, api=InspectApiImpl::namespaceEnumerateTopLevelFunctions,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, namespaceEnumerateTopLevelFunctions)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->namespaceEnumerateTopLevelFunctions);
}

// Test: test-kind=api, api=InspectApiImpl::importDescriptorGetImportedModule,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, importDescriptorGetImportedModule)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->importDescriptorGetImportedModule);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetU64,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalGetU64)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalGetU64);
}

// Test: test-kind=api, api=InspectApiImpl::namespaceEnumerateClasses,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, namespaceEnumerateClasses)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->namespaceEnumerateClasses);
}

// Test: test-kind=api, api=InspectApiImpl::annotationInterfaceGetName,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationInterfaceGetName)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationInterfaceGetName);
}

// Test: test-kind=api, api=InspectApiImpl::annotationGetFile,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationGetFile)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationGetFile);
}

// Test: test-kind=api, api=InspectApiImpl::annotationEnumerateElements,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationEnumerateElements)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationEnumerateElements);
}

// Test: test-kind=api, api=InspectApiImpl::functionEnumerateAnnotations,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, functionEnumerateAnnotations)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->functionEnumerateAnnotations);
}

// Test: test-kind=api, api=InspectApiImpl::exportDescriptorGetExportedModule,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, exportDescriptorGetExportedModule)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->exportDescriptorGetExportedModule);
}

// Test: test-kind=api, api=InspectApiImpl::moduleEnumerateAnonymousFunctions,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, moduleEnumerateAnonymousFunctions)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->moduleEnumerateAnonymousFunctions);
}

// Test: test-kind=api, api=InspectApiImpl::moduleGetTarget,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, moduleGetTarget)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->moduleGetTarget);
}

// Test: test-kind=api, api=InspectApiImpl::valueGetDouble,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, valueGetDouble)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->valueGetDouble);
}

// Test: test-kind=api, api=InspectApiImpl::annotationInterfaceFieldGetName,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationInterfaceFieldGetName)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationInterfaceFieldGetName);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetU8,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalGetU8)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalGetU8);
}

// Test: test-kind=api, api=InspectApiImpl::arrayValueGetLiteralArray,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, arrayValueGetLiteralArray)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->arrayValueGetLiteralArray);
}

// Test: test-kind=api, api=InspectApiImpl::moduleGetFile,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, moduleGetFile)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->moduleGetFile);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetFloat,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalGetFloat)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalGetFloat);
}

// Test: test-kind=api, api=InspectApiImpl::functionIsStatic,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, functionIsStatic)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->functionIsStatic);
}

// Test: test-kind=api, api=InspectApiImpl::annotationGetInterface,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationGetInterface)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationGetInterface);
}

// Test: test-kind=api, api=InspectApiImpl::annotationInterfaceFieldGetDefaultValue,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationInterfaceFieldGetDefaultValue)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationInterfaceFieldGetDefaultValue);
}

// Test: test-kind=api, api=InspectApiImpl::functionGetParentClass,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, functionGetParentClass)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->functionGetParentClass);
}

// Test: test-kind=api, api=InspectApiImpl::classEnumerateMethods,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, classEnumerateMethods)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->classEnumerateMethods);
}

// Test: test-kind=api, api=InspectApiImpl::functionGetModule,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, functionGetModule)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->functionGetModule);
}

// Test: test-kind=api, api=InspectApiImpl::annotationInterfaceEnumerateFields,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationInterfaceEnumerateFields)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationInterfaceEnumerateFields);
}

// Test: test-kind=api, api=InspectApiImpl::literalArrayEnumerateElements,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalArrayEnumerateElements)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalArrayEnumerateElements);
}

// Test: test-kind=api, api=InspectApiImpl::annotationInterfaceGetFile,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationInterfaceGetFile)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationInterfaceGetFile);
}

// Test: test-kind=api, api=InspectApiImpl::importDescriptorGetName,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, importDescriptorGetName)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->importDescriptorGetName);
}

// Test: test-kind=api, api=InspectApiImpl::fileGetVersion,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, fileGetVersion)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->fileGetVersion);
}

// Test: test-kind=api, api=InspectApiImpl::annotationElementGetName,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationElementGetName)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationElementGetName);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetDouble,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalGetDouble)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalGetDouble);
}

// Test: test-kind=api, api=InspectApiImpl::importDescriptorGetImportingModule,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, importDescriptorGetImportingModule)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->importDescriptorGetImportingModule);
}

// Test: test-kind=api, api=InspectApiImpl::namespaceGetName,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, namespaceGetName)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->namespaceGetName);
}

// Test: test-kind=api, api=InspectApiImpl::classGetName,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, classGetName)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->classGetName);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetU32,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalGetU32)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalGetU32);
}

// Test: test-kind=api, api=InspectApiImpl::moduleEnumerateNamespaces,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, moduleEnumerateNamespaces)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->moduleEnumerateNamespaces);
}

// Test: test-kind=api, api=InspectApiImpl::valueGetU1,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, valueGetU1)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->valueGetU1);
}

// Test: test-kind=api, api=InspectApiImpl::annotationElementGetValue,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationElementGetValue)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationElementGetValue);
}

// Test: test-kind=api, api=InspectApiImpl::exportDescriptorGetExportingModule,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, exportDescriptorGetExportingModule)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->exportDescriptorGetExportingModule);
}

// Test: test-kind=api, api=InspectApiImpl::annotationInterfaceGetModule,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationInterfaceGetModule)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationInterfaceGetModule);
}

// Test: test-kind=api, api=InspectApiImpl::moduleEnumerateImports,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, moduleEnumerateImports)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->moduleEnumerateImports);
}

// Test: test-kind=api, api=InspectApiImpl::functionGetName,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, functionGetName)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->functionGetName);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetLiteralArray,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalGetLiteralArray)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalGetLiteralArray);
}

// Test: test-kind=api, api=InspectApiImpl::moduleIsExternal,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, moduleIsExternal)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->moduleIsExternal);
}

// Test: test-kind=api, api=InspectApiImpl::functionIsAnonymous,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, functionIsAnonymous)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->functionIsAnonymous);
}

// Test: test-kind=api, api=InspectApiImpl::annotationInterfaceFieldGetFile,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationInterfaceFieldGetFile)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationInterfaceFieldGetFile);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetMethodAffiliate,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalGetMethodAffiliate)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalGetMethodAffiliate);
}

// Test: test-kind=api, api=InspectApiImpl::functionGetParentNamespace,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, functionGetParentNamespace)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->functionGetParentNamespace);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetString,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalGetString)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalGetString);
}

// Test: test-kind=api, api=InspectApiImpl::createGraphFromFunction,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, createGraphFromFunction)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->createGraphFromFunction);
}

// Test: test-kind=api, api=InspectApiImpl::classEnumerateAnnotations,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, classEnumerateAnnotations)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->classEnumerateAnnotations);
}

// Test: test-kind=api, api=InspectApiImpl::classGetModule,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, classGetModule)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->classGetModule);
}

// Test: test-kind=api, api=InspectApiImpl::annotationElementGetFile,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationElementGetFile)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationElementGetFile);
}

// Test: test-kind=api, api=InspectApiImpl::namespaceGetParentNamespace,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, namespaceGetParentNamespace)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->namespaceGetParentNamespace);
}

// Test: test-kind=api, api=InspectApiImpl::moduleEnumerateExports,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, moduleEnumerateExports)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->moduleEnumerateExports);
}

// Test: test-kind=api, api=InspectApiImpl::functionGetFile,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, functionGetFile)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->functionGetFile);
}

// Test: test-kind=api, api=InspectApiImpl::typeGetTypeId,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, typeGetTypeId)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->typeGetTypeId);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetTag,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalGetTag)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalGetTag);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetFile,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalGetFile)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalGetFile);
}

// Test: test-kind=api, api=InspectApiImpl::annotationInterfaceFieldGetInterface,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationInterfaceFieldGetInterface)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationInterfaceFieldGetInterface);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetBool,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalGetBool)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalGetBool);
}

// Test: test-kind=api, api=InspectApiImpl::moduleEnumerateClasses,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, moduleEnumerateClasses)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->moduleEnumerateClasses);
}

// Test: test-kind=api, api=InspectApiImpl::fileEnumerateModules,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, fileEnumerateModules)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->fileEnumerateModules);
}

// Test: test-kind=api, api=InspectApiImpl::valueGetFile,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, valueGetFile)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->valueGetFile);
}

// Test: test-kind=api, api=InspectApiImpl::valueGetType,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, valueGetType)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->valueGetType);
}

// Test: test-kind=api, api=InspectApiImpl::fileEnumerateExternalModules,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, fileEnumerateExternalModules)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->fileEnumerateExternalModules);
}

// Test: test-kind=api, api=InspectApiImpl::classGetParentNamespace,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, classGetParentNamespace)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->classGetParentNamespace);
}

// Test: test-kind=api, api=InspectApiImpl::abckitStringToString,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, abckitStringToString)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->abckitStringToString);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetMethod,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalGetMethod)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalGetMethod);
}

// Test: test-kind=api, api=InspectApiImpl::exportDescriptorGetAlias,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, exportDescriptorGetAlias)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->exportDescriptorGetAlias);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetU16,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, literalGetU16)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->literalGetU16);
}

// Test: test-kind=api, api=InspectApiImpl::valueGetString,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, valueGetString)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->valueGetString);
}

// Test: test-kind=api, api=InspectApiImpl::moduleGetName,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, moduleGetName)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->moduleGetName);
}

// Test: test-kind=api, api=InspectApiImpl::annotationInterfaceFieldGetType,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, annotationInterfaceFieldGetType)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->annotationInterfaceFieldGetType);
}

// Test: test-kind=api, api=InspectApiImpl::exportDescriptorGetName,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, exportDescriptorGetName)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->exportDescriptorGetName);
}

// Test: test-kind=api, api=InspectApiImpl::importDescriptorGetAlias,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, importDescriptorGetAlias)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->importDescriptorGetAlias);
}

// Test: test-kind=api, api=InspectApiImpl::classGetFile,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, classGetFile)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->classGetFile);
}

// Test: test-kind=api, api=InspectApiImpl::typeGetReferenceClass,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, typeGetReferenceClass)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->typeGetReferenceClass);
}

// Test: test-kind=api, api=InspectApiImpl::moduleEnumerateTopLevelFunctions,
// abc-kind=NoABC, category=negative-nullptr
TEST_F(LibAbcKitNullptrTestsInspectApiImpl0, moduleEnumerateTopLevelFunctions)
{
    helpers_nullptr::TestNullptr(g_inspectApiImp->moduleEnumerateTopLevelFunctions);
}

}  // namespace libabckit::test
