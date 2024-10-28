/**
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

#include <cassert>
#include "libabckit/include/c/abckit.h"
#include "libabckit/include/c/metadata_core.h"

#include "libabckit/src/helpers_common.h"
#include "libabckit/src/adapter_dynamic/metadata_modify_dynamic.h"
#include "libabckit/src/logger.h"
#include "libabckit/src/macros.h"

#include "libabckit/src/metadata_inspect_impl.h"
#include "libabckit/src/metadata_arkts_inspect_impl.h"
#include "libabckit/src/metadata_js_inspect_impl.h"
#include "libabckit/src/adapter_dynamic/metadata_inspect_dynamic.h"
#include "libabckit/src/adapter_static/metadata_inspect_static.h"

namespace libabckit {

// ========================================
// File
// ========================================

extern "C" AbckitFileVersion FileGetVersion(AbckitFile *file)
{
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(file, nullptr);

    return file->version;
}

extern "C" void FileEnumerateModules(AbckitFile *file, void *data, bool (*cb)(AbckitCoreModule *module, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT_VOID(file)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)
    for (auto &[moduleName, module] : file->localModules) {
        cb(module.get(), data);
    }
    for (auto &[moduleName, module] : file->externalModules) {
        cb(module.get(), data);
    }
}

extern "C" void FileEnumerateExternalModules(AbckitFile *file, void *data,
                                             bool (*cb)(AbckitCoreModule *module, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT_VOID(file)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)
    for (auto &[moduleName, module] : file->externalModules) {
        cb(module.get(), data);
    }
}

// ========================================
// Module
// ========================================

extern "C" AbckitFile *ModuleGetFile(AbckitCoreModule *m)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(m, nullptr);
    return m->file;
}

extern "C" AbckitTarget ModuleGetTarget(AbckitCoreModule *m)
{
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(m, ABCKIT_TARGET_UNKNOWN);

    return m->target;
}

extern "C" AbckitString *ModuleGetName(AbckitCoreModule *m)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(m, nullptr);
    return m->moduleName;
}

extern "C" bool ModuleIsExternal(AbckitCoreModule *m)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(m, false);
    return m->isExternal;
}

extern "C" void ModuleEnumerateImports(AbckitCoreModule *m, void *data,
                                       bool (*cb)(AbckitCoreImportDescriptor *i, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT_VOID(m);
    LIBABCKIT_BAD_ARGUMENT_VOID(cb);
    switch (ModuleGetTarget(m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSModuleEnumerateImports(m, data, cb);
        case ABCKIT_TARGET_JS:
            return JSModuleEnumerateImports(m, data, cb);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" void ModuleEnumerateExports(AbckitCoreModule *m, void *data,
                                       bool (*cb)(AbckitCoreExportDescriptor *e, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT_VOID(m)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)

    switch (ModuleGetTarget(m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSModuleEnumerateExports(m, data, cb);
        case ABCKIT_TARGET_JS:
            return JSModuleEnumerateExports(m, data, cb);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" void ModuleEnumerateNamespaces(AbckitCoreModule *m, void *data,
                                          bool (*cb)(AbckitCoreNamespace *n, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT_VOID(m)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)

    switch (ModuleGetTarget(m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSModuleEnumerateNamespaces(m, data, cb);
        case ABCKIT_TARGET_JS:
            return;
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" void ModuleEnumerateClasses(AbckitCoreModule *m, void *data, bool (*cb)(AbckitCoreClass *klass, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT_VOID(m)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)
    switch (ModuleGetTarget(m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSModuleEnumerateClasses(m, data, cb);
        case ABCKIT_TARGET_JS:
            return JSModuleEnumerateClasses(m, data, cb);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" void ModuleEnumerateTopLevelFunctions(AbckitCoreModule *m, void *data,
                                                 bool (*cb)(AbckitCoreFunction *function, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT_VOID(m)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)
    switch (ModuleGetTarget(m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSModuleEnumerateTopLevelFunctions(m, data, cb);
        case ABCKIT_TARGET_JS:
            return JSModuleEnumerateTopLevelFunctions(m, data, cb);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" void ModuleEnumerateAnonymousFunctions(AbckitCoreModule *m, void *data,
                                                  bool (*cb)(AbckitCoreFunction *function, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT_VOID(m)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)
    switch (ModuleGetTarget(m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSModuleEnumerateAnonymousFunctions(m, data, cb);
        case ABCKIT_TARGET_JS:
            return JSModuleEnumerateAnonymousFunctions(m, data, cb);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" void ModuleEnumerateAnnotationInterfaces(AbckitCoreModule *m, void *data,
                                                    bool (*cb)(AbckitCoreAnnotationInterface *ai, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT_VOID(m)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)
    switch (ModuleGetTarget(m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSModuleEnumerateAnnotationInterfaces(m, data, cb);
        case ABCKIT_TARGET_JS:
            return;
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

// ========================================
// Namespace
// ========================================

extern "C" AbckitString *NamespaceGetName(AbckitCoreNamespace *n)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(n, nullptr);
    switch (n->m->target) {
        case ABCKIT_TARGET_ARK_TS_V1:
            return NamespaceGetNameDynamic(n);
        case ABCKIT_TARGET_JS:
        case ABCKIT_TARGET_ARK_TS_V2:
            statuses::SetLastError(ABCKIT_STATUS_WRONG_TARGET);
            return nullptr;
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" AbckitCoreNamespace *NamespaceGetParentNamespace(AbckitCoreNamespace *n)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(n, nullptr);
    return n->n;
}

extern "C" void NamespaceEnumerateNamespaces(AbckitCoreNamespace *n, void *data,
                                             bool (*cb)(AbckitCoreNamespace *klass, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT_VOID(n)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)

    switch (ModuleGetTarget(n->m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSNamespaceEnumerateNamespaces(n, data, cb);
        case ABCKIT_TARGET_JS:
            return;
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" void NamespaceEnumerateClasses(AbckitCoreNamespace *n, void *data,
                                          bool (*cb)(AbckitCoreClass *klass, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT_VOID(n)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)

    switch (ModuleGetTarget(n->m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSNamespaceEnumerateClasses(n, data, cb);
        case ABCKIT_TARGET_JS:
            return;
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" void NamespaceEnumerateTopLevelFunctions(AbckitCoreNamespace *n, void *data,
                                                    bool (*cb)(AbckitCoreFunction *func, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT_VOID(n)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)

    switch (ModuleGetTarget(n->m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSNamespaceEnumerateTopLevelFunctions(n, data, cb);
        case ABCKIT_TARGET_JS:
            return;
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

// ========================================
// ImportDescriptor
// ========================================

extern "C" AbckitFile *ImportDescriptorGetFile(AbckitCoreImportDescriptor *i)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(i, nullptr);
    return i->importingModule->file;
}

extern "C" AbckitCoreModule *ImportDescriptorGetImportedModule(AbckitCoreImportDescriptor *i)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(i, nullptr);
    return i->importedModule;
}

extern "C" AbckitCoreModule *ImportDescriptorGetImportingModule(AbckitCoreImportDescriptor *i)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(i, nullptr);
    return i->importingModule;
}

extern "C" AbckitString *ImportDescriptorGetName(AbckitCoreImportDescriptor *i)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(i, nullptr);
    if (IsDynamic(i->importingModule->target)) {
        return ImportDescriptorGetNameDynamic(i);
    }
    statuses::SetLastError(ABCKIT_STATUS_UNSUPPORTED);
    return nullptr;
}

extern "C" AbckitString *ImportDescriptorGetAlias(AbckitCoreImportDescriptor *i)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(i, nullptr);
    if (IsDynamic(i->importingModule->target)) {
        return ImportDescriptorGetAliasDynamic(i);
    }
    statuses::SetLastError(ABCKIT_STATUS_UNSUPPORTED);
    return nullptr;
}

// ========================================
// ExportDescriptor
// ========================================

extern "C" AbckitFile *ExportDescriptorGetFile(AbckitCoreExportDescriptor *i)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(i, nullptr);
    return i->exportingModule->file;
}

extern "C" AbckitCoreModule *ExportDescriptorGetExportingModule(AbckitCoreExportDescriptor *i)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(i, nullptr);
    return i->exportingModule;
}

extern "C" AbckitCoreModule *ExportDescriptorGetExportedModule(AbckitCoreExportDescriptor *i)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(i, nullptr);
    return i->exportedModule;
}

extern "C" AbckitString *ExportDescriptorGetName(AbckitCoreExportDescriptor *i)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(i, nullptr);
    if (IsDynamic(i->exportingModule->target)) {
        return ExportDescriptorGetNameDynamic(i);
    }
    statuses::SetLastError(ABCKIT_STATUS_UNSUPPORTED);
    return nullptr;
}

extern "C" AbckitString *ExportDescriptorGetAlias(AbckitCoreExportDescriptor *i)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(i, nullptr);
    if (IsDynamic(i->exportingModule->target)) {
        return ExportDescriptorGetAliasDynamic(i);
    }
    statuses::SetLastError(ABCKIT_STATUS_UNSUPPORTED);
    return nullptr;
}

// ========================================
// Class
// ========================================

extern "C" AbckitFile *ClassGetFile(AbckitCoreClass *klass)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(klass, nullptr);
    return klass->m->file;
}

extern "C" AbckitCoreModule *ClassGetModule(AbckitCoreClass *klass)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(klass, nullptr);

    return klass->m;
}

extern "C" AbckitString *ClassGetName(AbckitCoreClass *klass)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(klass, nullptr)

    if (IsDynamic(klass->m->target)) {
        return ClassGetNameDynamic(klass);
    }
    return ClassGetNameStatic(klass);
}

extern "C" AbckitCoreNamespace *ClassGetParentNamespace(AbckitCoreClass *klass)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(klass, nullptr);
    return klass->n;
}

extern "C" void ClassEnumerateMethods(AbckitCoreClass *klass, void *data,
                                      bool (*cb)(AbckitCoreFunction *function, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT_VOID(klass)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)

    switch (klass->m->target) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSClassEnumerateMethods(klass, data, cb);
        case ABCKIT_TARGET_JS:
            return JSClassEnumerateMethods(klass, data, cb);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" void ClassEnumerateAnnotations(AbckitCoreClass *klass, void *data,
                                          bool (*cb)(AbckitCoreAnnotation *anno, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT_VOID(klass)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)

    switch (ModuleGetTarget(klass->m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSClassEnumerateAnnotations(klass, data, cb);
        case ABCKIT_TARGET_JS:
            return;
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

// ========================================
// AnnotationInterface
// ========================================

extern "C" AbckitFile *AnnotationInterfaceGetFile(AbckitCoreAnnotationInterface *anno)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(anno, nullptr);
    return anno->m->file;
}

extern "C" AbckitCoreModule *AnnotationInterfaceGetModule(AbckitCoreAnnotationInterface *anno)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(anno, nullptr);
    return anno->m;
}

extern "C" AbckitString *AnnotationInterfaceGetName(AbckitCoreAnnotationInterface *ai)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(ai, nullptr);

    if (IsDynamic(ai->m->target)) {
        return AnnotationInterfaceGetNameDynamic(ai);
    }
    statuses::SetLastError(ABCKIT_STATUS_UNSUPPORTED);
    return nullptr;
}

extern "C" void AnnotationInterfaceEnumerateFields(AbckitCoreAnnotationInterface *ai, void *data,
                                                   bool (*cb)(AbckitCoreAnnotationInterfaceField *fld, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT_VOID(ai)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)

    switch (ModuleGetTarget(ai->m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSAnnotationInterfaceEnumerateFields(ai, data, cb);
        case ABCKIT_TARGET_JS:
            return;
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

// ========================================
// AnnotationInterfaceField
// ========================================

extern "C" AbckitFile *AnnotationInterfaceFieldGetFile(AbckitCoreAnnotationInterfaceField *fld)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(fld, nullptr);

    return fld->ai->m->file;
}

extern "C" AbckitCoreAnnotationInterface *AnnotationInterfaceFieldGetInterface(AbckitCoreAnnotationInterfaceField *fld)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(fld, nullptr);

    return fld->ai;
}

extern "C" AbckitString *AnnotationInterfaceFieldGetName(AbckitCoreAnnotationInterfaceField *fld)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(fld, nullptr);

    return fld->name;
}

extern "C" AbckitType *AnnotationInterfaceFieldGetType(AbckitCoreAnnotationInterfaceField *fld)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(fld, nullptr);

    return fld->type;
}

extern "C" AbckitValue *AnnotationInterfaceFieldGetDefaultValue(AbckitCoreAnnotationInterfaceField *fld)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(fld, nullptr);

    return fld->value;
}

// ========================================
// Function
// ========================================

extern "C" AbckitFile *FunctionGetFile(AbckitCoreFunction *function)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(function, nullptr);
    return function->m->file;
}

extern "C" AbckitCoreModule *FunctionGetModule(AbckitCoreFunction *function)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(function, nullptr);

    return function->m;
}

extern "C" AbckitString *FunctionGetName(AbckitCoreFunction *function)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(function, nullptr);

    if (IsDynamic(function->m->target)) {
        return FunctionGetNameDynamic(function);
    }
    return FunctionGetNameStatic(function);
}

extern "C" AbckitCoreClass *FunctionGetParentClass(AbckitCoreFunction *function)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(function, nullptr);

    return function->klass;
}

extern "C" AbckitCoreNamespace *FunctionGetParentNamespace(AbckitCoreFunction *function)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(function, nullptr);
    return function->n;
}

extern "C" void FunctionEnumerateNestedFunctions(AbckitCoreFunction *function, void *data,
                                                 bool (*cb)(AbckitCoreFunction *nestedFunc, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT_VOID(function)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)

    switch (ModuleGetTarget(function->m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSFunctionEnumerateNestedFunctions(function, data, cb);
        case ABCKIT_TARGET_JS:
            return JSFunctionEnumerateNestedFunctions(function, data, cb);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" void FunctionEnumerateAnnotations(AbckitCoreFunction *function, void *data,
                                             bool (*cb)(AbckitCoreAnnotation *anno, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT_VOID(function)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)

    switch (ModuleGetTarget(function->m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSFunctionEnumerateAnnotations(function, data, cb);
        case ABCKIT_TARGET_JS:
            return;
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" AbckitGraph *CreateGraphFromFunction(AbckitCoreFunction *function)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(function, nullptr);

    if (IsDynamic(function->m->target)) {
        return CreateGraphFromFunctionDynamic(function);
    }
    return CreateGraphFromFunctionStatic(function);
}

extern "C" bool FunctionIsStatic(AbckitCoreFunction *function)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(function, false);

    if (IsDynamic(function->m->target)) {
        return FunctionIsStaticDynamic(function);
    }
    return FunctionIsStaticStatic(function);
}

extern "C" bool FunctionIsCtor(AbckitCoreFunction *function)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(function, false);

    if (IsDynamic(function->m->target)) {
        return FunctionIsCtorDynamic(function);
    }
    return FunctionIsCtorStatic(function);
}

extern "C" bool FunctionIsAnonymous(AbckitCoreFunction *function)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(function, false);

    if (IsDynamic(function->m->target)) {
        return FunctionIsAnonymousDynamic(function);
    }
    return FunctionIsAnonymousStatic(function);
}

// ========================================
// Annotation
// ========================================

extern "C" AbckitFile *AnnotationGetFile(AbckitCoreAnnotation *anno)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(anno, nullptr);

    return anno->ai->m->file;
}

extern "C" AbckitCoreAnnotationInterface *AnnotationGetInterface(AbckitCoreAnnotation *anno)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(anno, nullptr);

    return anno->ai;
}

extern "C" void AnnotationEnumerateElements(AbckitCoreAnnotation *anno, void *data,
                                            bool (*cb)(AbckitCoreAnnotationElement *ae, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT_VOID(anno)
    LIBABCKIT_BAD_ARGUMENT_VOID(cb)

    AbckitCoreModule *m = anno->ai->m;

    switch (ModuleGetTarget(m)) {
        case ABCKIT_TARGET_ARK_TS_V1:
        case ABCKIT_TARGET_ARK_TS_V2:
            return ArkTSAnnotationEnumerateElements(anno, data, cb);
        case ABCKIT_TARGET_JS:
            return;
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

// ========================================
// AnnotationElement
// ========================================

extern "C" AbckitFile *AnnotationElementGetFile(AbckitCoreAnnotationElement *ae)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(ae, nullptr);

    return ae->ann->ai->m->file;
}

extern "C" AbckitCoreAnnotation *AnnotationElementGetAnnotation(AbckitCoreAnnotationElement *ae)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(ae, nullptr);

    return ae->ann;
}

extern "C" AbckitString *AnnotationElementGetName(AbckitCoreAnnotationElement *ae)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(ae, nullptr);

    return ae->name;
}

extern "C" AbckitValue *AnnotationElementGetValue(AbckitCoreAnnotationElement *ae)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(ae, nullptr);

    return ae->value.get();
}

// ========================================
// Type
// ========================================

extern "C" AbckitTypeId TypeGetTypeId(AbckitType *type)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(type, AbckitTypeId::ABCKIT_TYPE_ID_INVALID);

    return type->id;
}

extern "C" AbckitCoreClass *TypeGetReferenceClass(AbckitType *type)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;
    LIBABCKIT_BAD_ARGUMENT(type, nullptr);
    if (type->id != AbckitTypeId::ABCKIT_TYPE_ID_REFERENCE) {
        return nullptr;
    }

    return type->klass;
}

// ========================================
// Value
// ========================================

extern "C" AbckitFile *ValueGetFile(AbckitValue *value)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(value, nullptr);

    return value->file;
}

extern "C" AbckitType *ValueGetType(AbckitValue *value)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(value, nullptr);

    switch (value->file->frontend) {
        case Mode::DYNAMIC:
            return ValueGetTypeDynamic(value);
        case Mode::STATIC:
            return ValueGetTypeStatic(value);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" bool ValueGetU1(AbckitValue *value)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(value, false);

    switch (value->file->frontend) {
        case Mode::DYNAMIC:
            return ValueGetU1Dynamic(value);
        case Mode::STATIC:
            return ValueGetU1Static(value);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" double ValueGetDouble(AbckitValue *value)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(value, 0);

    switch (value->file->frontend) {
        case Mode::DYNAMIC:
            return ValueGetDoubleDynamic(value);
        case Mode::STATIC:
            return ValueGetDoubleStatic(value);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" AbckitString *ValueGetString(AbckitValue *value)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(value, nullptr);

    switch (value->file->frontend) {
        case Mode::DYNAMIC:
            return ValueGetStringDynamic(value);
        case Mode::STATIC:
            return ValueGetStringStatic(value);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" AbckitLiteralArray *ArrayValueGetLiteralArray(AbckitValue *value)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(value, nullptr);

    switch (value->file->frontend) {
        case Mode::DYNAMIC:
            return ArrayValueGetLiteralArrayDynamic(value);
        case Mode::STATIC:
            return ArrayValueGetLiteralArrayStatic(value);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

// ========================================
// String
// ========================================

extern "C" const char *AbckitStringToString(AbckitString *value)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(value, nullptr);

    return value->impl.data();
}

// ========================================
// LiteralArray
// ========================================

extern "C" void LiteralArrayEnumerateElements(AbckitLiteralArray *litArr, void *data,
                                              bool (*cb)(AbckitFile *file, AbckitLiteral *v, void *data))
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT_VOID(litArr);
    LIBABCKIT_BAD_ARGUMENT_VOID(cb);

    switch (litArr->file->frontend) {
        case Mode::DYNAMIC:
            return LiteralArrayEnumerateElementsDynamic(litArr, data, cb);
        case Mode::STATIC:
            statuses::SetLastError(ABCKIT_STATUS_UNSUPPORTED);
            return;
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

// ========================================
// Literal
// ========================================

extern "C" AbckitFile *LiteralGetFile(AbckitLiteral *lit)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(lit, nullptr);

    return lit->file;
}

extern "C" AbckitLiteralTag LiteralGetTag(AbckitLiteral *lit)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(lit, ABCKIT_LITERAL_TAG_INVALID);

    switch (lit->file->frontend) {
        case Mode::DYNAMIC:
            return LiteralGetTagDynamic(lit);
        case Mode::STATIC:
            return LiteralGetTagStatic(lit);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" bool LiteralGetBool(AbckitLiteral *lit)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(lit, false);

    switch (lit->file->frontend) {
        case Mode::DYNAMIC:
            return LiteralGetBoolDynamic(lit);
        case Mode::STATIC:
            return LiteralGetBoolStatic(lit);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" uint8_t LiteralGetU8(AbckitLiteral *lit)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(lit, 0);

    switch (lit->file->frontend) {
        case Mode::DYNAMIC:
            return LiteralGetU8Dynamic(lit);
        case Mode::STATIC:
            return LiteralGetU8Static(lit);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" uint16_t LiteralGetU16(AbckitLiteral *lit)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(lit, 0);

    switch (lit->file->frontend) {
        case Mode::DYNAMIC:
            return LiteralGetU16Dynamic(lit);
        case Mode::STATIC:
            return LiteralGetU16Static(lit);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" uint16_t LiteralGetMethodAffiliate(AbckitLiteral *lit)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(lit, 0);

    switch (lit->file->frontend) {
        case Mode::DYNAMIC:
            return LiteralGetMethodAffiliateDynamic(lit);
        case Mode::STATIC:
            return LiteralGetMethodAffiliateStatic(lit);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" uint32_t LiteralGetU32(AbckitLiteral *lit)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(lit, 0);

    switch (lit->file->frontend) {
        case Mode::DYNAMIC:
            return LiteralGetU32Dynamic(lit);
        case Mode::STATIC:
            return LiteralGetU32Static(lit);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" uint64_t LiteralGetU64(AbckitLiteral *lit)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(lit, 0);

    switch (lit->file->frontend) {
        case Mode::DYNAMIC:
            return LiteralGetU64Dynamic(lit);
        case Mode::STATIC:
            return LiteralGetU64Static(lit);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" float LiteralGetFloat(AbckitLiteral *lit)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(lit, 0);

    switch (lit->file->frontend) {
        case Mode::DYNAMIC:
            return LiteralGetFloatDynamic(lit);
        case Mode::STATIC:
            return LiteralGetFloatStatic(lit);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" double LiteralGetDouble(AbckitLiteral *lit)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(lit, 0);

    switch (lit->file->frontend) {
        case Mode::DYNAMIC:
            return LiteralGetDoubleDynamic(lit);
        case Mode::STATIC:
            return LiteralGetDoubleStatic(lit);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" AbckitLiteralArray *LiteralGetLiteralArray(AbckitLiteral *lit)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(lit, nullptr);

    switch (lit->file->frontend) {
        case Mode::DYNAMIC:
            return LiteralGetLiteralArrayDynamic(lit);
        case Mode::STATIC:
            statuses::SetLastError(ABCKIT_STATUS_UNSUPPORTED);
            return nullptr;
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" AbckitString *LiteralGetString(AbckitLiteral *lit)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(lit, nullptr);

    switch (lit->file->frontend) {
        case Mode::DYNAMIC:
            return LiteralGetStringDynamic(lit);
        case Mode::STATIC:
            return LiteralGetStringStatic(lit);
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

extern "C" AbckitString *LiteralGetMethod(AbckitLiteral *lit)
{
    LIBABCKIT_CLEAR_LAST_ERROR;
    LIBABCKIT_IMPLEMENTED;

    LIBABCKIT_BAD_ARGUMENT(lit, nullptr);

    switch (lit->file->frontend) {
        case Mode::DYNAMIC:
            return LiteralGetStringDynamic(lit);
        case Mode::STATIC:
            statuses::SetLastError(ABCKIT_STATUS_UNSUPPORTED);
            return nullptr;
        default:
            LIBABCKIT_UNREACHABLE;
    }
}

AbckitInspectApi g_inspectApiImpl = {
    // ========================================
    // File
    // ========================================

    FileGetVersion,
    FileEnumerateModules,
    FileEnumerateExternalModules,

    // ========================================
    // String
    // ========================================

    AbckitStringToString,

    // ========================================
    // Type
    // ========================================

    TypeGetTypeId,
    TypeGetReferenceClass,

    // ========================================
    // Value
    // ========================================

    ValueGetFile,
    ValueGetType,
    ValueGetU1,
    ValueGetDouble,
    ValueGetString,
    ArrayValueGetLiteralArray,

    // ========================================
    // Literal
    // ========================================

    LiteralGetFile,
    LiteralGetTag,
    LiteralGetBool,
    LiteralGetU8,
    LiteralGetU16,
    LiteralGetMethodAffiliate,
    LiteralGetU32,
    LiteralGetU64,
    LiteralGetFloat,
    LiteralGetDouble,
    LiteralGetLiteralArray,
    LiteralGetString,
    LiteralGetMethod,

    // ========================================
    // LiteralArray
    // ========================================

    LiteralArrayEnumerateElements,

    // ========================================
    // Module
    // ========================================

    ModuleGetFile,
    ModuleGetTarget,
    ModuleGetName,
    ModuleIsExternal,
    ModuleEnumerateImports,
    ModuleEnumerateExports,
    ModuleEnumerateNamespaces,
    ModuleEnumerateClasses,
    ModuleEnumerateTopLevelFunctions,
    ModuleEnumerateAnonymousFunctions,
    ModuleEnumerateAnnotationInterfaces,

    // ========================================
    // Namespace
    // ========================================

    NamespaceGetName,
    NamespaceGetParentNamespace,
    NamespaceEnumerateNamespaces,
    NamespaceEnumerateClasses,
    NamespaceEnumerateTopLevelFunctions,

    // ========================================
    // ImportDescriptor
    // ========================================

    ImportDescriptorGetFile,
    ImportDescriptorGetImportingModule,
    ImportDescriptorGetImportedModule,
    ImportDescriptorGetName,
    ImportDescriptorGetAlias,

    // ========================================
    // ExportDescriptor
    // ========================================

    ExportDescriptorGetFile,
    ExportDescriptorGetExportingModule,
    ExportDescriptorGetExportedModule,
    ExportDescriptorGetName,
    ExportDescriptorGetAlias,

    // ========================================
    // Class
    // ========================================

    ClassGetFile,
    ClassGetModule,
    ClassGetName,
    ClassGetParentNamespace,
    ClassEnumerateMethods,
    ClassEnumerateAnnotations,

    // ========================================
    // Function
    // ========================================

    FunctionGetFile,
    FunctionGetModule,
    FunctionGetName,
    FunctionGetParentClass,
    FunctionGetParentNamespace,
    FunctionEnumerateNestedFunctions,
    FunctionEnumerateAnnotations,
    CreateGraphFromFunction,
    FunctionIsStatic,
    FunctionIsCtor,
    FunctionIsAnonymous,

    // ========================================
    // Annotation
    // ========================================

    AnnotationGetFile,
    AnnotationGetInterface,
    AnnotationEnumerateElements,
    AnnotationElementGetFile,
    AnnotationElementGetAnnotation,
    AnnotationElementGetName,
    AnnotationElementGetValue,

    // ========================================
    // AnnotationInterface
    // ========================================

    AnnotationInterfaceGetFile,
    AnnotationInterfaceGetModule,
    AnnotationInterfaceGetName,
    AnnotationInterfaceEnumerateFields,

    // ========================================
    // AnnotationInterfaceField
    // ========================================

    AnnotationInterfaceFieldGetFile,
    AnnotationInterfaceFieldGetInterface,
    AnnotationInterfaceFieldGetName,
    AnnotationInterfaceFieldGetType,
    AnnotationInterfaceFieldGetDefaultValue,
};

}  // namespace libabckit

extern "C" AbckitInspectApi const *AbckitGetInspectApiImpl(AbckitApiVersion version)
{
    switch (version) {
        case ABCKIT_VERSION_RELEASE_1_0_0:
            return &libabckit::g_inspectApiImpl;
        default:
            libabckit::statuses::SetLastError(ABCKIT_STATUS_UNKNOWN_API_VERSION);
            return nullptr;
    }
}