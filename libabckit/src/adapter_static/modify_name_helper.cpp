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
#include "modify_name_helper.h"

#include "libabckit/src/logger.h"
#include "libabckit/src/metadata_inspect_impl.h"
#include "libabckit/src/adapter_static/metadata_modify_static.h"
#include "static_core/assembler/assembly-program.h"
#include "static_core/assembler/mangling.h"
#include "metadata_inspect_static.h"
#include "helpers_static.h"
#include "name_util.h"
#include "string_util.h"

namespace {
constexpr std::string_view ETS_EXTENDS = "ets.extends";
constexpr std::string_view ETS_IMPLEMENTS = "ets.implements";
constexpr std::string_view ETS_ANNOTATION_CLASS = "ets.annotation.class";
constexpr std::string_view ARRAY_ENUM_SUFFIX = "[]";
constexpr std::string_view GLOBAL_CLASS = "ETSGLOBAL";
constexpr std::string_view NAME_DELIMITER = ".";
constexpr std::string_view FUNCTION_DELIMITER = ":";
constexpr std::string_view INTERFACE_FIELD_PREFIX = "<property>";
constexpr std::string_view ASYNC_PATTERN = "(%%async-)(.+):(.+)";
constexpr std::string_view ASYNC_PREFIX = "%%async-";

struct AnnotationUpdateData {
    std::string oldName;
    std::string newName;
};

std::string GetSetPattern(const std::string &name)
{
    return "<([gs]et)>(" + name + "):(.+)";
}

std::string GetSetReplace(const std::string &name)
{
    return "<$1>" + name + ":$3";
}

std::string AsyncReplace(const std::string &name)
{
    return "$1" + name + ":$3";
}

std::vector<std::string> Split(const std::string &str, const std::string &delimiter)
{
    std::vector<std::string> tokens;
    auto start = str.find_first_not_of(delimiter, 0);
    auto pos = str.find_first_of(delimiter, 0);
    while (start != std::string::npos) {
        if (pos > start) {
            tokens.push_back(str.substr(start, pos - start));
        }
        start = str.find_first_not_of(delimiter, pos);
        pos = str.find_first_of(delimiter, start + 1);
    }
    ASSERT(!tokens.empty());

    return tokens;
}

bool ProgramUpdateRecordTableKey(ark::pandasm::Program *prog, const std::string &oldKeyName,
                                 const std::string &newKeyName)
{
    LIBABCKIT_LOG_FUNC;

    if (prog->recordTable.find(oldKeyName) == prog->recordTable.end()) {
        LIBABCKIT_LOG(ERROR) << "no record name:" << oldKeyName << std::endl;
        return false;
    }

    if (prog->recordTable.find(newKeyName) != prog->recordTable.end()) {
        LIBABCKIT_LOG(ERROR) << "duplicate record name:" << newKeyName << std::endl;
        return false;
    }

    auto entry = prog->recordTable.extract(oldKeyName);

    entry.key() = newKeyName;

    prog->recordTable.insert(std::move(entry));

    return true;
}

bool ProgramUpdateFunctionTableKey(ark::pandasm::Program *prog, const std::string &oldKeyName,
                                   const std::string &newKeyName)
{
    LIBABCKIT_LOG_FUNC;

    std::map<std::string, ark::pandasm::Function> *functionTable = nullptr;
    if (prog->functionStaticTable.find(oldKeyName) != prog->functionStaticTable.end()) {
        functionTable = &prog->functionStaticTable;
    } else if (prog->functionInstanceTable.find(oldKeyName) != prog->functionInstanceTable.end()) {
        functionTable = &prog->functionInstanceTable;
    } else {
        LIBABCKIT_LOG(ERROR) << "invalid function name:" << oldKeyName << std::endl;
        return false;
    }

    auto entry = functionTable->extract(oldKeyName);
    entry.key() = newKeyName;
    functionTable->insert(std::move(entry));
    return true;
}

bool ObjectRefreshName(
    std::variant<AbckitCoreNamespace *, AbckitCoreClass *, AbckitCoreEnum *, AbckitCoreInterface *> object,
    const std::string &newName, bool isObjectLiteral = false)
{
    LIBABCKIT_LOG_FUNC;
    const auto funcName = libabckit::StringUtil::GetFuncNameWithSquareBrackets(__func__);
    return std::visit(
        [newName, isObjectLiteral, funcName](auto &coreObject) {
            auto record = libabckit::GetStaticImplRecord(coreObject);
            const auto oldFullName = record->name;
            const auto newFullName = libabckit::NameUtil::GetFullName(coreObject, newName, isObjectLiteral);
            if (oldFullName == newFullName) {
                return true;
            }

            LIBABCKIT_LOG_NO_FUNC(DEBUG) << funcName << "old full name:" << oldFullName << std::endl;
            LIBABCKIT_LOG_NO_FUNC(DEBUG) << funcName << "new full name:" << newFullName << std::endl;

            if (!ProgramUpdateRecordTableKey(coreObject->owningModule->file->GetStaticProgram(), oldFullName,
                                             newFullName)) {
                LIBABCKIT_LOG_NO_FUNC(ERROR) << funcName << "Failed to update object program record" << std::endl;
                return false;
            }
            record->name = newFullName;

            return true;
        },
        object);
}

}  // namespace

// --------------------------------------- public ----------------------
bool libabckit::ModifyNameHelper::ModuleRefreshName(AbckitCoreModule *m, const std::string &newName)
{
    LIBABCKIT_LOG(DEBUG) << "old module name:" << m->moduleName->impl << std::endl;
    LIBABCKIT_LOG(DEBUG) << "new module name:" << newName << std::endl;

    const auto oldRecordName = GetStaticImplRecord(m)->name;
    const auto newRecordName = newName + NAME_DELIMITER.data() + GLOBAL_CLASS.data();

    LIBABCKIT_LOG(DEBUG) << "old module record name:" << oldRecordName << std::endl;
    LIBABCKIT_LOG(DEBUG) << "new module record name:" << newRecordName << std::endl;

    // update prog record name
    if (!ProgramUpdateRecordTableKey(m->file->GetStaticProgram(), oldRecordName, newRecordName)) {
        LIBABCKIT_LOG(ERROR) << "Failed to update module program record" << std::endl;
        return false;
    }

    GetStaticImplRecord(m)->name = newRecordName;

    // update module name
    m->moduleName = CreateStringStatic(m->file, newName.data(), newName.length());

    ModuleRefreshNamespaces(m);
    ModuleRefreshEnums(m);
    ModuleRefreshInterfaces(m);
    ModuleRefreshClasses(m);
    ModuleRefreshFunctions(m);

    return true;
}

bool libabckit::ModifyNameHelper::NamespaceRefreshName(AbckitCoreNamespace *ns, const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;

    if (!ObjectRefreshName(ns, newName)) {
        return false;
    }

    NamespaceRefreshNamespaces(ns);
    NamespaceRefreshEnums(ns);
    NamespaceRefreshInterfaces(ns);
    NamespaceRefreshClasses(ns);
    NamespaceRefreshFunctions(ns);

    return true;
}

bool libabckit::ModifyNameHelper::FunctionRefreshName(AbckitCoreFunction *function, const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;

    if (function->asyncImpl) {
        if (!FunctionRefreshName(function->asyncImpl.get(), newName)) {
            return false;
        }
    }

    auto *funcImpl = function->GetArkTSImpl()->GetStaticImpl();
    auto oldFunctionKeyName = ark::pandasm::MangleFunctionName(funcImpl->name, funcImpl->params, funcImpl->returnType);
    std::string newFunctionKeyName;
    std::string newFunctionName;
    if (newName.empty()) {
        FunctionRefreshParams(function);
        FunctionRefreshReturnType(function);
        newFunctionKeyName = NameUtil::GetFullName(function);
        newFunctionName = Split(newFunctionKeyName, FUNCTION_DELIMITER.data())[0];
    } else if (oldFunctionKeyName.find(ASYNC_PREFIX) != std::string::npos) {
        newFunctionKeyName =
            std::regex_replace(oldFunctionKeyName, std::regex(ASYNC_PATTERN.data()), AsyncReplace(newName));
        newFunctionName = Split(newFunctionKeyName, FUNCTION_DELIMITER.data())[0];
    } else {
        newFunctionName = NameUtil::GetPackageName(function) + NAME_DELIMITER.data() + newName;
        newFunctionKeyName = ark::pandasm::MangleFunctionName(newFunctionName, funcImpl->params, funcImpl->returnType);
    }
    LIBABCKIT_LOG(DEBUG) << "old function key name:" << oldFunctionKeyName << std::endl;
    LIBABCKIT_LOG(DEBUG) << "new function key name:" << newFunctionKeyName << std::endl;
    LIBABCKIT_LOG(DEBUG) << "new function name:" << newFunctionName << std::endl;
    if (oldFunctionKeyName == newFunctionKeyName) {
        return true;
    }

    if (!ProgramUpdateFunctionTableKey(function->owningModule->file->GetStaticProgram(), oldFunctionKeyName,
                                       newFunctionKeyName)) {
        LIBABCKIT_LOG(ERROR) << "Failed to update function program table" << std::endl;
        return false;
    }
    funcImpl->name = newFunctionName;

    return true;
}

bool libabckit::ModifyNameHelper::ClassRefreshName(AbckitCoreClass *klass, const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;

    if (!ObjectRefreshName(klass, newName)) {
        return false;
    }

    if (!ObjectRefreshTypeUsersName(klass, GetStaticImplRecord(klass)->name)) {
        return false;
    }

    ClassRefreshMethods(klass);
    ClassRefreshObjectLiteral(klass);
    ClassRefreshSubClasses(klass);

    return true;
}

bool libabckit::ModifyNameHelper::EnumRefreshName(AbckitCoreEnum *enm, const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;

    auto oldEnumName = GetStaticImplRecord(enm)->name;
    if (!ObjectRefreshName(enm, newName)) {
        return false;
    }

    if (!ObjectRefreshTypeUsersName(enm, GetStaticImplRecord(enm)->name)) {
        return false;
    }

    LIBABCKIT_LOG(DEBUG) << "refresh enum array:" << oldEnumName + "[]" << std::endl;
    const auto arrayEnumName = std::string(EnumGetNameStatic(enm)->impl) + std::string(ARRAY_ENUM_SUFFIX);
    ClassRefreshName(enm->arrayEnum.get(), arrayEnumName);
    EnumRefreshMethods(enm);
    EnumRefreshFieldsType(enm);

    return true;
}

bool libabckit::ModifyNameHelper::AnnotationRefreshName(AbckitCoreAnnotation *anno, const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;
    if (anno->ai == nullptr) {
        return true;
    }
    const std::string oldName = anno->name->impl.data();
    const std::string oldAIName = NameUtil::GetFullName(anno->ai, oldName);
    const std::string newAIName = NameUtil::GetFullName(anno->ai, newName);
    if (!newName.empty()) {
        if (!AnnotationInterfaceRefreshName(anno->ai, newName)) {
            return false;
        }
    }
    anno->name = AnnotationInterfaceGetNameStatic(anno->ai);

    return std::visit(
        [&](auto owner) {
            using Type = std::decay_t<decltype(owner)>;
            if constexpr (std::is_same_v<Type, AbckitCoreClass *>) {
                return ClassRefreshAnnotations(owner, oldAIName, newAIName);
            } else if constexpr (std::is_same_v<Type, AbckitCoreInterface *>) {
                return InterfaceRefreshAnnotations(owner, oldAIName, newAIName);
            } else if constexpr (std::is_same_v<Type, AbckitCoreFunction *>) {
                return FunctionRefreshAnnotations(owner, oldAIName, newAIName);
            } else if constexpr (std::is_same_v<Type, AbckitCoreClassField *>) {
                return ClassFieldRefreshAnnotations(owner, oldAIName, newAIName);
            } else {
                return false;
            }
        },
        anno->owner);
}

bool libabckit::ModifyNameHelper::InterfaceRefreshName(AbckitCoreInterface *iface, const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;

    if (!ObjectRefreshName(iface, newName)) {
        return false;
    }

    if (!ObjectRefreshTypeUsersName(iface, GetStaticImplRecord(iface)->name)) {
        return false;
    }

    InterfaceRefreshObjectLiteral(iface);
    InterfaceRefreshClasses(iface);
    InterfaceRefreshSubInterfaces(iface);
    InterfaceRefreshMethods(iface);

    return true;
}

bool libabckit::ModifyNameHelper::AnnotationInterfaceRefreshName(AbckitCoreAnnotationInterface *ai,
                                                                 const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;
    const auto record = GetStaticImplRecord(ai);
    const auto oldAIName = record->name;
    const auto newAIName = NameUtil::GetFullName(ai, newName);
    if (oldAIName == newAIName) {
        return true;
    }

    LIBABCKIT_LOG(DEBUG) << "old annotation interface name:" << oldAIName << std::endl;
    LIBABCKIT_LOG(DEBUG) << "new annotation interface name:" << newAIName << std::endl;

    if (!ProgramUpdateRecordTableKey(ai->owningModule->file->GetStaticProgram(), oldAIName, newAIName)) {
        LIBABCKIT_LOG(ERROR) << "Failed to update annotation interface program record" << std::endl;
        return false;
    }

    record->name = newAIName;

    AnnotationInterfaceRefreshAnnotation(ai);
    return true;
}

bool libabckit::ModifyNameHelper::FieldRefreshName(
    std::variant<AbckitCoreModuleField *, AbckitCoreNamespaceField *, AbckitCoreClassField *, AbckitCoreEnumField *>
        field,
    const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;

    if (newName.empty()) {
        return false;
    }

    const auto funcName = StringUtil::GetFuncNameWithSquareBrackets(__func__);
    return std::visit(
        [newName, funcName](auto &object) {
            auto fieldRecord = object->GetArkTSImpl()->GetStaticImpl();
            const auto oldName = fieldRecord->name;
            if (oldName == newName) {
                return true;
            }

            LIBABCKIT_LOG_NO_FUNC(DEBUG) << funcName << "fieldName refreshed: " << oldName << " --> " << newName
                                         << std::endl;
            fieldRecord->name = newName;

            return true;
        },
        field);
}

bool libabckit::ModifyNameHelper::InterfaceFieldRefreshName(AbckitCoreInterfaceField *ifaceField,
                                                            const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;
    if (newName.empty()) {
        return false;
    }

    std::string oldName = ifaceField->name->impl.data();
    std::string newFieldName = INTERFACE_FIELD_PREFIX.data() + newName;
    ifaceField->name =
        CreateStringStatic(ifaceField->owner->owningModule->file, newFieldName.data(), newFieldName.size());
    GetSetMethodRefreshName(ifaceField->owner, oldName, newName);
    InterfaceRefreshObjectLiteralField(ifaceField->owner, oldName, newName);
    return true;
}

bool libabckit::ModifyNameHelper::ModuleRefreshNamespaces(AbckitCoreModule *m)
{
    LIBABCKIT_LOG_FUNC;

    for (const auto &[_, ns] : m->nt) {
        if (!NamespaceRefreshName(ns.get())) {
            return false;
        }
    }
    return true;
}

bool libabckit::ModifyNameHelper::ModuleRefreshFunctions(AbckitCoreModule *m)
{
    LIBABCKIT_LOG_FUNC;

    for (const auto &function : m->functions) {
        if (!FunctionRefreshName(function.get())) {
            return false;
        }
    }
    return true;
}

bool libabckit::ModifyNameHelper::ModuleRefreshClasses(AbckitCoreModule *m)
{
    LIBABCKIT_LOG_FUNC;

    for (const auto &[_, klass] : m->ct) {
        if (!ClassRefreshName(klass.get())) {
            return false;
        }
    }

    return true;
}

bool libabckit::ModifyNameHelper::ModuleRefreshInterfaces(AbckitCoreModule *m)
{
    LIBABCKIT_LOG_FUNC;

    for (const auto &[_, iface] : m->it) {
        if (!InterfaceRefreshName(iface.get())) {
            return false;
        }
    }

    return true;
}

bool libabckit::ModifyNameHelper::ModuleRefreshEnums(AbckitCoreModule *m)
{
    LIBABCKIT_LOG_FUNC;

    for (const auto &[_, enm] : m->et) {
        if (!EnumRefreshName(enm.get())) {
            return false;
        }
    }
    return true;
}

bool libabckit::ModifyNameHelper::NamespaceRefreshNamespaces(AbckitCoreNamespace *ns)
{
    LIBABCKIT_LOG_FUNC;

    for (const auto &[_, ns] : ns->nt) {
        if (!NamespaceRefreshName(ns.get())) {
            return false;
        }
    }

    return true;
}

bool libabckit::ModifyNameHelper::NamespaceRefreshFunctions(AbckitCoreNamespace *ns)
{
    LIBABCKIT_LOG_FUNC;

    for (const auto &function : ns->functions) {
        if (!FunctionRefreshName(function.get())) {
            return false;
        }
    }

    return true;
}

bool libabckit::ModifyNameHelper::NamespaceRefreshClasses(AbckitCoreNamespace *ns)
{
    LIBABCKIT_LOG_FUNC;

    for (const auto &[_, klass] : ns->ct) {
        if (!ClassRefreshName(klass.get())) {
            return false;
        }
    }

    return true;
}

bool libabckit::ModifyNameHelper::NamespaceRefreshInterfaces(AbckitCoreNamespace *ns)
{
    LIBABCKIT_LOG_FUNC;

    for (const auto &[_, iface] : ns->it) {
        if (!InterfaceRefreshName(iface.get())) {
            return false;
        }
    }

    return true;
}

bool libabckit::ModifyNameHelper::NamespaceRefreshEnums(AbckitCoreNamespace *ns)
{
    LIBABCKIT_LOG_FUNC;

    for (const auto &[_, enm] : ns->et) {
        if (!EnumRefreshName(enm.get())) {
            return false;
        }
    }

    return true;
}

bool libabckit::ModifyNameHelper::FunctionRefreshParams(AbckitCoreFunction *function)
{
    LIBABCKIT_LOG(DEBUG) << "functionName:" << function->GetArkTSImpl()->GetStaticImpl()->name << std::endl;
    const auto funcImpl = function->GetArkTSImpl()->GetStaticImpl();
    for (size_t i = 0; i < function->parameters.size(); i++) {
        const auto type = function->parameters[i]->type;
        if (type->id != AbckitTypeId::ABCKIT_TYPE_ID_REFERENCE) {
            continue;
        }

        auto newName = StringUtil::GetTypeNameStr(function->parameters[i]->type);
        LIBABCKIT_LOG(DEBUG) << "old param[" << i << "] type name:" << funcImpl->params[i].type.GetName() << std::endl;
        LIBABCKIT_LOG(DEBUG) << "new param[" << i << "] type name:" << newName << std::endl;
        auto progType = funcImpl->params[i].type;
        funcImpl->params[i].type = ark::pandasm::Type(newName, progType.GetRank());
    }
    return true;
}

bool libabckit::ModifyNameHelper::FunctionRefreshReturnType(AbckitCoreFunction *function)
{
    LIBABCKIT_LOG(DEBUG) << "functionName:" << function->GetArkTSImpl()->GetStaticImpl()->name << std::endl;
    if (function->returnType->id != AbckitTypeId::ABCKIT_TYPE_ID_REFERENCE) {
        return true;
    }

    const auto funcImpl = function->GetArkTSImpl()->GetStaticImpl();
    auto newName = StringUtil::GetTypeNameStr(function->returnType);
    LIBABCKIT_LOG(DEBUG) << "old returnType name:" << funcImpl->returnType.GetName() << std::endl;
    LIBABCKIT_LOG(DEBUG) << "new returnType name:" << newName << std::endl;
    const auto progType = funcImpl->returnType;
    funcImpl->returnType = ark::pandasm::Type(newName, progType.GetRank());

    return true;
}

bool libabckit::ModifyNameHelper::FunctionRefreshAnnotations(AbckitCoreFunction *function, const std::string &oldName,
                                                             const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;
    const auto &record = function->GetArkTSImpl()->GetStaticImpl();
    std::vector<std::string> annotationNames;
    for (const auto &annotation : function->annotations) {
        annotationNames.emplace_back(NameUtil::GetFullName(annotation->ai));
    }
    record->metadata->SetAttributeValues(ETS_ANNOTATION_CLASS.data(), annotationNames);
    AnnotationUpdateData updateData {oldName, newName};
    record->metadata->EnumerateAnnotations([&](ark::pandasm::AnnotationData &anno) {
        if (anno.GetName() == oldName) {
            anno.SetName(newName);
        }
    });
    return true;
}

bool libabckit::ModifyNameHelper::ClassRefreshMethods(AbckitCoreClass *klass)
{
    LIBABCKIT_LOG_FUNC;

    for (auto &function : klass->methods) {
        if (!FunctionRefreshName(function.get())) {
            return false;
        }
    }
    return true;
}

bool libabckit::ModifyNameHelper::ClassRefreshObjectLiteral(AbckitCoreClass *klass)
{
    LIBABCKIT_LOG_FUNC;

    auto baseName = GetStaticImplRecord(klass)->name;
    for (const auto &objectLiteral : klass->objectLiterals) {
        const auto &record = GetStaticImplRecord(objectLiteral.get());
        record->metadata->SetAttributeValues(ETS_EXTENDS.data(), {baseName});
        ObjectLiteralRefreshName(objectLiteral.get(), baseName);
    }
    return true;
}

bool libabckit::ModifyNameHelper::ClassRefreshSubClasses(AbckitCoreClass *klass)
{
    LIBABCKIT_LOG_FUNC;
    const auto superClassName = GetStaticImplRecord(klass)->name;

    for (const auto &subClass : klass->subClasses) {
        const auto &record = GetStaticImplRecord(subClass);
        record->metadata->SetAttributeValues(ETS_EXTENDS.data(), {superClassName});
    }
    return true;
}

bool libabckit::ModifyNameHelper::ClassRefreshAnnotations(AbckitCoreClass *klass, const std::string &oldName,
                                                          const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;
    const auto &record = GetStaticImplRecord(klass);
    std::vector<std::string> annotationNames;
    for (const auto &annotation : klass->annotations) {
        annotationNames.emplace_back(NameUtil::GetFullName(annotation->ai));
    }
    record->metadata->SetAttributeValues(ETS_ANNOTATION_CLASS.data(), annotationNames);
    AnnotationUpdateData updateData {oldName, newName};
    record->metadata->EnumerateAnnotations([&](ark::pandasm::AnnotationData &anno) {
        if (anno.GetName() == oldName) {
            anno.SetName(newName);
        }
    });
    return true;
}

bool libabckit::ModifyNameHelper::EnumRefreshMethods(AbckitCoreEnum *enm)
{
    LIBABCKIT_LOG_FUNC;

    for (auto &method : enm->methods) {
        if (!FunctionRefreshName(method.get())) {
            return false;
        }
    }
    return true;
}

bool libabckit::ModifyNameHelper::EnumRefreshFieldsType(AbckitCoreEnum *enm)
{
    for (const auto &field : enm->fields) {
        if (!FieldRefreshTypeName(field.get())) {
            return false;
        }
    }
    return true;
}

bool libabckit::ModifyNameHelper::InterfaceRefreshMethods(AbckitCoreInterface *iface)
{
    LIBABCKIT_LOG_FUNC;

    for (auto &method : iface->methods) {
        if (!FunctionRefreshName(method.get())) {
            return false;
        }
    }
    return true;
}

bool libabckit::ModifyNameHelper::InterfaceRefreshObjectLiteral(AbckitCoreInterface *iface)
{
    LIBABCKIT_LOG_FUNC;

    auto baseName = GetStaticImplRecord(iface)->name;
    for (const auto &objectLiteral : iface->objectLiterals) {
        const auto &record = GetStaticImplRecord(objectLiteral.get());
        record->metadata->SetAttributeValues(ETS_IMPLEMENTS.data(), {baseName});
        ObjectLiteralRefreshName(objectLiteral.get(), baseName);
    }
    return true;
}

bool libabckit::ModifyNameHelper::InterfaceRefreshClasses(AbckitCoreInterface *iface)
{
    LIBABCKIT_LOG_FUNC;
    auto newName = GetStaticImplRecord(iface)->name;
    for (const auto &klass : iface->classes) {
        std::vector<std::string> implements;
        for (const auto &interface : klass->interfaces) {
            implements.emplace_back(GetStaticImplRecord(interface)->name);
        }
        if (!implements.empty()) {
            GetStaticImplRecord(klass)->metadata->SetAttributeValues(ETS_IMPLEMENTS.data(), implements);
        }
    }
    return true;
}

bool libabckit::ModifyNameHelper::InterfaceRefreshAnnotations(AbckitCoreInterface *iface, const std::string &oldName,
                                                              const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;
    const auto &record = GetStaticImplRecord(iface);
    std::vector<std::string> annotationNames;
    for (const auto &[_, annotation] : iface->annotationTable) {
        annotationNames.emplace_back(NameUtil::GetFullName(annotation->ai));
    }
    record->metadata->SetAttributeValues(ETS_ANNOTATION_CLASS.data(), annotationNames);
    AnnotationUpdateData updateData {oldName, newName};
    record->metadata->EnumerateAnnotations([&](ark::pandasm::AnnotationData &anno) {
        if (anno.GetName() == oldName) {
            anno.SetName(newName);
        }
    });
    return true;
}

bool libabckit::ModifyNameHelper::InterfaceRefreshSubInterfaces(AbckitCoreInterface *iface)
{
    LIBABCKIT_LOG_FUNC;
    for (const auto &subInterface : iface->subInterfaces) {
        const auto &record = GetStaticImplRecord(subInterface);
        std::vector<std::string> superInterfaceNames;
        for (const auto &superInterface : subInterface->superInterfaces) {
            superInterfaceNames.emplace_back(NameUtil::GetFullName(superInterface));
        }
        record->metadata->SetAttributeValues(ETS_IMPLEMENTS.data(), superInterfaceNames);
    }
    return true;
}

bool libabckit::ModifyNameHelper::AnnotationInterfaceRefreshAnnotation(AbckitCoreAnnotationInterface *ai)
{
    LIBABCKIT_LOG_FUNC;
    for (const auto &anno : ai->annotations) {
        if (!AnnotationRefreshName(anno)) {
            return false;
        }
    }
    return true;
}

bool libabckit::ModifyNameHelper::ObjectLiteralRefreshName(AbckitCoreClass *objectLiteral, const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;

    if (!ObjectRefreshName(objectLiteral, newName, true)) {
        return false;
    }

    if (!ObjectRefreshTypeUsersName(objectLiteral, GetStaticImplRecord(objectLiteral)->name)) {
        return false;
    }

    ClassRefreshMethods(objectLiteral);
    return true;
}

bool libabckit::ModifyNameHelper::ClassFieldRefreshAnnotations(AbckitCoreClassField *classField,
                                                               const std::string &oldName, const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;
    const auto &record = classField->GetArkTSImpl()->GetStaticImpl();
    std::vector<std::string> annotationNames;
    for (const auto &[_, annotation] : classField->annotationTable) {
        annotationNames.emplace_back(NameUtil::GetFullName(annotation->ai));
    }
    record->metadata->SetAttributeValues(ETS_ANNOTATION_CLASS.data(), annotationNames);
    AnnotationUpdateData updateData {oldName, newName};
    record->metadata->EnumerateAnnotations([&](ark::pandasm::AnnotationData &anno) {
        if (anno.GetName() == oldName) {
            anno.SetName(newName);
        }
    });
    return true;
}

bool libabckit::ModifyNameHelper::GetSetMethodRefreshName(
    const std::variant<AbckitCoreClass *, AbckitCoreInterface *> &object, const std::string &oldName,
    const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;
    std::string fieldName = oldName;
    if (fieldName.find(INTERFACE_FIELD_PREFIX) != std::string::npos) {
        fieldName = fieldName.substr(INTERFACE_FIELD_PREFIX.size());
    }
    std::regex re(GetSetPattern(fieldName));
    std::string replacement = GetSetReplace(newName);

    const auto funcName = StringUtil::GetFuncNameWithSquareBrackets(__func__);
    return std::visit(
        [&](auto *coreObject) {
            for (const auto &function : coreObject->methods) {
                auto *funcImpl = function->GetArkTSImpl()->GetStaticImpl();
                auto oldFunctionKeyName =
                    ark::pandasm::MangleFunctionName(funcImpl->name, funcImpl->params, funcImpl->returnType);
                if (!std::regex_search(oldFunctionKeyName, re)) {
                    continue;
                }

                std::string newFunctionKeyName = std::regex_replace(oldFunctionKeyName, re, replacement);
                std::string newFunctionName = Split(newFunctionKeyName, FUNCTION_DELIMITER.data())[0];
                LIBABCKIT_LOG_NO_FUNC(DEBUG) << funcName << "old function key name:" << oldFunctionKeyName << std::endl;
                LIBABCKIT_LOG_NO_FUNC(DEBUG) << funcName << "new function key name:" << newFunctionKeyName << std::endl;
                LIBABCKIT_LOG_NO_FUNC(DEBUG) << funcName << "new function name:" << newFunctionName << std::endl;

                if (!ProgramUpdateFunctionTableKey(function->owningModule->file->GetStaticProgram(), oldFunctionKeyName,
                                                   newFunctionKeyName)) {
                    LIBABCKIT_LOG_NO_FUNC(ERROR) << funcName << "Failed to update function program table" << std::endl;
                    return false;
                }
                funcImpl->name = newFunctionName;
            }
            return true;
        },
        object);
}

bool libabckit::ModifyNameHelper::InterfaceRefreshObjectLiteralField(AbckitCoreInterface *iface,
                                                                     const std::string &oldName,
                                                                     const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;
    for (const auto &objectLiteral : iface->objectLiterals) {
        for (const auto &field : objectLiteral->fields) {
            if (ClassFieldGetNameStatic(field.get())->impl.data() == oldName) {
                FieldRefreshName(field.get(), INTERFACE_FIELD_PREFIX.data() + newName);
                GetSetMethodRefreshName(objectLiteral.get(), oldName, newName);
            }
        }
    }
    return true;
}

bool libabckit::ModifyNameHelper::FieldRefreshTypeName(
    std::variant<AbckitCoreModuleField *, AbckitCoreNamespaceField *, AbckitCoreClassField *, AbckitCoreEnumField *,
                 AbckitCoreAnnotationInterfaceField *>
        field)
{
    LIBABCKIT_LOG_FUNC;
    const auto funcName = StringUtil::GetFuncNameWithSquareBrackets(__func__);
    return std::visit(
        [funcName](auto &object) {
            if (object->type->id != AbckitTypeId::ABCKIT_TYPE_ID_REFERENCE) {
                return true;
            }

            auto &fieldType = object->GetArkTSImpl()->GetStaticImpl()->type;
            const auto oldName = fieldType.GetName();
            const auto newName = StringUtil::GetTypeNameStr(object->type);
            LIBABCKIT_LOG_NO_FUNC(DEBUG) << funcName << "FieldRefreshTypeName : " << oldName << " --> " << newName
                                         << std::endl;
            fieldType = ark::pandasm::Type(newName, fieldType.GetRank());

            return true;
        },
        field);
}

bool libabckit::ModifyNameHelper::ObjectRefreshTypeUsersName(
    std::variant<AbckitCoreClass *, AbckitCoreEnum *, AbckitCoreInterface *> object, const std::string &newName)
{
    LIBABCKIT_LOG_FUNC;

    if (newName.empty()) {
        return false;
    }

    const auto funcName = StringUtil::GetFuncNameWithSquareBrackets(__func__);
    return std::visit(
        [newName, funcName](auto &coreObject) {
            for (const auto typeUser : coreObject->typeUsers) {
                LIBABCKIT_LOG_NO_FUNC(DEBUG) << funcName << "type name:" << typeUser->name->impl.data() << std::endl;
                typeUser->name =
                    libabckit::CreateStringStatic(coreObject->owningModule->file, newName.data(), newName.length());

                for (auto function : typeUser->functionUsers) {
                    FunctionRefreshName(function);
                }

                for (const auto fieldTypeUser : typeUser->fieldTypeUsers) {
                    FieldRefreshTypeName(fieldTypeUser);
                }
            }
            return true;
        },
        object);
}
