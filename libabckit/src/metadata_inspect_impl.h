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

#ifndef LIBABCKIT_STD_METADATA_INSPECT_IMPL_H
#define LIBABCKIT_STD_METADATA_INSPECT_IMPL_H

#include "libabckit/c/metadata_core.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <variant>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <thread>

struct AbckitIrInterface;
struct AbckitDynamicGraphCacheEntry;

enum class AbckitDynamicGraphSnapshotState {
    EMPTY,
    BUILDING,
    READY,
    FAILED,
    STALE,
};

enum class AbckitFileLifecycleState {
    OPEN,
    CLOSING,
    CLOSED,
};

namespace panda::panda_file {
class File;
}  // namespace panda::panda_file

namespace panda::pandasm {
struct Program;
struct Function;
struct Record;
struct LiteralArray;
}  // namespace panda::pandasm
namespace ark::pandasm {
class Function;
struct Program;
struct Record;
struct Field;
struct LiteralArray;
}  // namespace ark::pandasm

namespace libabckit {

enum class Mode {
    DYNAMIC,
    STATIC,
};

struct pandasm_Literal;
struct pandasm_Value;

}  // namespace libabckit

/* ====================
 * Internal implementations for all of the Abckit* structures
 */

/*
 * NOTE: after v1 releases, move this enum to the user headers, add static types
 * NOTE: come up with a better name
 */
enum AbckitImportExportDescriptorKind {
    UNTYPED,  // Applies to: JavaScript
};

enum AbckitDynamicExportKind {
    ABCKIT_DYNAMIC_EXPORT_KIND_LOCAL_EXPORT,
    ABCKIT_DYNAMIC_EXPORT_KIND_INDIRECT_EXPORT,
    ABCKIT_DYNAMIC_EXPORT_KIND_STAR_EXPORT,
};

// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
using AbckitValueImplT = std::unique_ptr<libabckit::pandasm_Value, void (*)(libabckit::pandasm_Value *)>;
struct AbckitValue {
    AbckitFile *file = nullptr;
    /*
     * Underlying implementation
     */
    AbckitValueImplT val;
    AbckitValue(AbckitFile *file, AbckitValueImplT val) : file(file), val(std::move(val)) {}
};

struct AbckitArktsAnnotationInterface {
    /*
     * Points to the pandasm::Record that stores annotation definition.
     */
    std::variant<panda::pandasm::Record *, ark::pandasm::Record *> impl;

    ark::pandasm::Record *GetStaticImpl()
    {
        return std::get<ark::pandasm::Record *>(impl);
    }

    panda::pandasm::Record *GetDynamicImpl()
    {
        return std::get<panda::pandasm::Record *>(impl);
    }

    AbckitCoreAnnotationInterface *core = nullptr;

    explicit AbckitArktsAnnotationInterface(ark::pandasm::Record *record)
    {
        impl = reinterpret_cast<ark::pandasm::Record *>(record);
    }

    AbckitArktsAnnotationInterface() = default;
};

struct AbckitCoreAnnotationInterface {
    /*
     * To refer to the properties of the origin module.
     */
    AbckitCoreModule *owningModule = nullptr;

    /*
     * To refer to the properties of the parent namespace.
     */
    AbckitCoreNamespace *parentNamespace = nullptr;

    /*
     * Contains annotation interface fields
     */
    std::vector<std::unique_ptr<AbckitCoreAnnotationInterfaceField>> fields;

    /*
     * To refer to the properties of the annotations.
     */
    std::vector<AbckitCoreAnnotation *> annotations;

    std::variant<std::unique_ptr<AbckitArktsAnnotationInterface>> impl;
    AbckitArktsAnnotationInterface *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsAnnotationInterface>>(impl).get();
    }

    explicit AbckitCoreAnnotationInterface(AbckitCoreModule *m, AbckitArktsAnnotationInterface annotationInterface)
    {
        owningModule = m;
        impl = std::make_unique<AbckitArktsAnnotationInterface>(annotationInterface);
        GetArkTSImpl()->core = this;
    }

    AbckitCoreAnnotationInterface() = default;
};

struct AbckitArktsAnnotationInterfaceField {
    AbckitCoreAnnotationInterfaceField *core = nullptr;
    std::variant<ark::pandasm::Field *> impl;

    ark::pandasm::Field *GetStaticImpl()
    {
        return std::get<ark::pandasm::Field *>(impl);
    }

    explicit AbckitArktsAnnotationInterfaceField(ark::pandasm::Field *field)
    {
        impl = field;
    }

    explicit AbckitArktsAnnotationInterfaceField(AbckitCoreAnnotationInterfaceField *field)
    {
        core = field;
    }

    AbckitArktsAnnotationInterfaceField() = default;
};

struct AbckitCoreAnnotationInterfaceField {
    /*
     * To refer to the properties of the origin annotation interface.
     */
    AbckitCoreAnnotationInterface *ai = nullptr;

    /*
     * Field name
     */
    AbckitString *name = nullptr;

    /*
     * Field type
     */
    AbckitType *type = nullptr;

    /*
     * Field value
     */
    AbckitValue *value = nullptr;

    std::variant<std::unique_ptr<AbckitArktsAnnotationInterfaceField>> impl;
    AbckitArktsAnnotationInterfaceField *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsAnnotationInterfaceField>>(impl).get();
    }
    AbckitCoreAnnotationInterfaceField(AbckitCoreAnnotationInterface *annotationInterface, ark::pandasm::Field *field)
    {
        ai = annotationInterface;
        impl = std::make_unique<AbckitArktsAnnotationInterfaceField>(field);
        GetArkTSImpl()->core = this;
    }

    AbckitCoreAnnotationInterfaceField() = default;

    ark::pandasm::Record *GetOwnerStaticImpl() const
    {
        return this->ai->GetArkTSImpl()->GetStaticImpl();
    }
};

struct AbckitArktsAnnotationElement {
    AbckitCoreAnnotationElement *core = nullptr;
};

struct AbckitCoreAnnotationElement {
    /*
     * To refer to the properties of the origin annotation.
     */
    AbckitCoreAnnotation *ann = nullptr;

    /*
     * Name of annotation element
     */
    AbckitString *name = nullptr;

    /*
     * Value stored in annotation
     */
    AbckitValue *value = nullptr;

    std::variant<std::unique_ptr<AbckitArktsAnnotationElement>> impl;
    AbckitArktsAnnotationElement *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsAnnotationElement>>(impl).get();
    }
};

struct AbckitArktsAnnotation {
    AbckitCoreAnnotation *core = nullptr;
};

struct AbckitCoreAnnotation {
    /*
     * To refer to the properties of the annotation interface
     */
    AbckitCoreAnnotationInterface *ai = nullptr;

    /*
     * To refer to the properties of the owner.
     */
    std::variant<AbckitCoreClass *, AbckitCoreFunction *, AbckitCoreClassField *, AbckitCoreInterface *,
                 AbckitCoreInterfaceField *>
        owner;

    /*
     * Name of the annotation
     */
    AbckitString *name = nullptr;

    /*
     * Annotation elements
     */
    std::vector<std::unique_ptr<AbckitCoreAnnotationElement>> elements;

    std::variant<std::unique_ptr<AbckitArktsAnnotation>> impl;
    AbckitArktsAnnotation *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsAnnotation>>(impl).get();
    }
};

struct AbckitArktsClassPayload {
    /*
     * In Arkts1 class is defined by it's constructor.
     * In Arkts2 class is defined by corresponding pandasm::Record.
     */
    std::variant<panda::pandasm::Function *, ark::pandasm::Record *> cl;

    ark::pandasm::Record *GetStaticClass()
    {
        return std::get<ark::pandasm::Record *>(cl);
    }
    panda::pandasm::Function *GetDynamicClass()
    {
        return std::get<panda::pandasm::Function *>(cl);
    }
};

struct AbckitArktsClass {
    AbckitArktsClassPayload impl;
    AbckitCoreClass *core = nullptr;

    explicit AbckitArktsClass(panda::pandasm::Function *function)
    {
        impl.cl = reinterpret_cast<panda::pandasm::Function *>(function);
    };

    explicit AbckitArktsClass(ark::pandasm::Record *record)
    {
        impl.cl = reinterpret_cast<ark::pandasm::Record *>(record);
    };
};

struct AbckitJsClass {
    panda::pandasm::Function *impl;
    AbckitCoreClass *core = nullptr;

    explicit AbckitJsClass(panda::pandasm::Function *function)
    {
        impl = reinterpret_cast<panda::pandasm::Function *>(function);
    };
};

struct AbckitCoreClass {
    /*
     * To refer to the properties of the origin module.
     */
    AbckitCoreModule *owningModule = nullptr;

    /*
     * To refer to the properties of the parent namepsace.
     */
    AbckitCoreNamespace *parentNamespace = nullptr;

    /*
     * To refer to the properties of the parent function.
     */
    AbckitCoreFunction *parentFunction = nullptr;

    /*
     * To refer to the properties of the super class.
     */
    AbckitCoreClass *superClass = nullptr;

    /*
     * To refer to the partial.
     */
    std::unique_ptr<AbckitCoreClass> partial;

    /*
     * To store sub classes.
     */
    std::vector<AbckitCoreClass *> subClasses;

    /*
     * To store class methods.
     */
    std::vector<std::unique_ptr<AbckitCoreFunction>> methods {};

    /*
     * To store links to the wrapped annotations.
     */
    std::vector<std::unique_ptr<AbckitCoreAnnotation>> annotations;

    /*
     * Table to store links to the wrapped annotations.
     */
    std::unordered_map<std::string, AbckitCoreAnnotation *> annotationTable;

    /*
     * To store class fields.
     */
    std::vector<std::unique_ptr<AbckitCoreClassField>> fields {};

    /*
     * To store interfaces.
     */
    std::vector<AbckitCoreInterface *> interfaces {};

    /*
     * Table to store the object literal
     */
    std::vector<std::unique_ptr<AbckitCoreClass>> objectLiterals;

    /*
     * To store type users
     */
    std::vector<AbckitType *> typeUsers;

    /*
     * To store class[]
     */
    std::unique_ptr<AbckitCoreClass> array = nullptr;

    // Concurrent Dynamic cache misses are benign: CreateStringDynamic interns names under
    // stringsCacheMutex, so racing publishers store the same AbckitString pointer.
    std::atomic<AbckitString *> cachedDisplayName {nullptr};

    /*
     * Language-dependent implementation to store class data.
     */
    std::variant<std::unique_ptr<AbckitJsClass>, std::unique_ptr<AbckitArktsClass>> impl;
    AbckitArktsClass *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsClass>>(impl).get();
    }
    AbckitJsClass *GetJsImpl()
    {
        return std::get<std::unique_ptr<AbckitJsClass>>(impl).get();
    }

    AbckitCoreClass(AbckitCoreModule *module, AbckitJsClass klass)
    {
        klass.core = this;
        impl = std::make_unique<AbckitJsClass>(klass);
        owningModule = module;
    }
    AbckitCoreClass(AbckitCoreModule *module, AbckitArktsClass klass)
    {
        klass.core = this;
        impl = std::make_unique<AbckitArktsClass>(klass);
        owningModule = module;
    }
};

struct AbckitJsFunction {
    AbckitCoreFunction *core = nullptr;
    panda::pandasm::Function *impl = nullptr;
};

struct AbckitArktsFunction {
    std::variant<panda::pandasm::Function *, ark::pandasm::Function *> impl;

    ark::pandasm::Function *GetStaticImpl()
    {
        return std::get<ark::pandasm::Function *>(impl);
    }
    panda::pandasm::Function *GetDynamicImpl()
    {
        return std::get<panda::pandasm::Function *>(impl);
    }

    AbckitCoreFunction *core = nullptr;
};

struct AbckitCoreFunction {
    /*
     * To refer to the properties of the origin module.
     */
    AbckitCoreModule *owningModule = nullptr;

    /*
     * To refer to the properties of the parent namepsace.
     */
    AbckitCoreNamespace *parentNamespace = nullptr;

    /*
     * To be able to refer to the class where method is defined.
     * For global functions the rules are as follows:
     * - Dynamic: pandasm::Function with js file name.
     * - Static: pandasm::Record with name `L/.../ETSGLOBAL`.
     */
    AbckitCoreClass *parentClass = nullptr;

    /*
     * To be able to refer to the class where method is defined.
     */
    AbckitCoreFunction *parentFunction = nullptr;

    /*
     * To be able to refer to the entity where method is defined.
     */
    std::variant<AbckitCoreModule *, AbckitCoreNamespace *, AbckitCoreClass *, AbckitCoreInterface *, AbckitCoreEnum *>
        parent;

    /*
     * To store links to the wrapped annotations.
     */
    std::vector<std::unique_ptr<AbckitCoreAnnotation>> annotations;

    /*
     * Table to store links to the wrapped annotations.
     */
    std::unordered_map<std::string, AbckitCoreAnnotation *> annotationTable;

    /*
     * To store parameters of the function.
     */
    std::vector<std::unique_ptr<AbckitCoreFunctionParam>> parameters;

    /*
     * To store function return type
     */
    AbckitType *returnType = nullptr;

    std::vector<std::unique_ptr<AbckitCoreFunction>> nestedFunction;
    std::vector<std::unique_ptr<AbckitCoreClass>> nestedClasses;

    bool isAnonymous = false;

    /*
     * To store asyncImpl of the function.
     */
    std::unique_ptr<AbckitCoreFunction> asyncImpl = nullptr;

    // Concurrent Dynamic cache misses are benign: CreateStringDynamic interns names under
    // stringsCacheMutex, so racing publishers store the same AbckitString pointer.
    std::atomic<AbckitString *> cachedDisplayName {nullptr};

    std::variant<std::unique_ptr<AbckitJsFunction>, std::unique_ptr<AbckitArktsFunction>> impl;

    AbckitArktsFunction *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsFunction>>(impl).get();
    }
    AbckitJsFunction *GetJsImpl()
    {
        return std::get<std::unique_ptr<AbckitJsFunction>>(impl).get();
    }
};

struct AbckitArktsNamespacePayload {
    /*
     * In ArkTS2 namespace is defined by corresponding pandasm::Record.
     */
    std::variant<ark::pandasm::Record *> cl;

    ark::pandasm::Record *GetStaticClass()
    {
        return std::get<ark::pandasm::Record *>(cl);
    }
};

struct AbckitArktsNamespace {
    AbckitArktsNamespacePayload impl;
    AbckitCoreNamespace *core = nullptr;
    /*
     * To store links to the wrapped methods.
     */
    std::unique_ptr<AbckitCoreFunction> f;

    explicit AbckitArktsNamespace(ark::pandasm::Record *record)
    {
        impl.cl = reinterpret_cast<ark::pandasm::Record *>(record);
    }

    AbckitArktsNamespace() = default;
};

struct AbckitCoreNamespace {
    explicit AbckitCoreNamespace(AbckitCoreModule *owningModule) : owningModule(owningModule) {}

    /*
     * To refer to the properties of the origin module.
     */
    AbckitCoreModule *owningModule = nullptr;

    /*
     * To be able to refer to the namespace where namespace is defined.
     */
    AbckitCoreNamespace *parentNamespace = nullptr;

    /*
     * To store links to the wrapped methods.
     */
    std::vector<std::unique_ptr<AbckitCoreFunction>> functions;

    /*
     * To store links to the wrapped fields.
     */
    std::vector<std::unique_ptr<AbckitCoreNamespaceField>> fields;

    /*
     * To store links to the wrapped classes.
     */
    std::vector<std::unique_ptr<AbckitCoreClass>> classes;
    /*
     * To store links to the wrapped namespaces.
     */
    std::vector<std::unique_ptr<AbckitCoreNamespace>> namespaces;

    /*
     * Tables to store and find wrapped entities by their name.
     */
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreClass>> ct;
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreNamespace>> nt;
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreInterface>> it;
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreEnum>> et;
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreAnnotationInterface>> at;

    std::variant<std::unique_ptr<AbckitArktsNamespace>> impl;

    AbckitArktsNamespace *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsNamespace>>(impl).get();
    }

    AbckitCoreNamespace(AbckitCoreModule *m, AbckitArktsNamespace ns)
    {
        ns.core = this;
        impl = std::make_unique<AbckitArktsNamespace>(std::move(ns));
        owningModule = m;
    }

    template <typename AbckitCoreType>
    void InsertInstance(const std::string &name, std::unique_ptr<AbckitCoreType> &&instance);
};

template <>
inline void AbckitCoreNamespace::InsertInstance<AbckitCoreClass>(const std::string &name,
                                                                 std::unique_ptr<AbckitCoreClass> &&instance)
{
    ct.emplace(name, std::move(instance));
}

template <>
inline void AbckitCoreNamespace::InsertInstance<AbckitCoreNamespace>(const std::string &name,
                                                                     std::unique_ptr<AbckitCoreNamespace> &&instance)
{
    nt.emplace(name, std::move(instance));
}

template <>
inline void AbckitCoreNamespace::InsertInstance<AbckitCoreInterface>(const std::string &name,
                                                                     std::unique_ptr<AbckitCoreInterface> &&instance)
{
    it.emplace(name, std::move(instance));
}

template <>
inline void AbckitCoreNamespace::InsertInstance<AbckitCoreEnum>(const std::string &name,
                                                                std::unique_ptr<AbckitCoreEnum> &&instance)
{
    et.emplace(name, std::move(instance));
}

template <>
inline void AbckitCoreNamespace::InsertInstance<AbckitCoreAnnotationInterface>(
    const std::string &name, std::unique_ptr<AbckitCoreAnnotationInterface> &&instance)
{
    at.emplace(name, std::move(instance));
}

struct AbckitModulePayloadDyn {
    /*
     * In JS module is defined by corresponding pandasm::Record and AbckitLiteralArray.
     */
    const panda::pandasm::Record *record = nullptr;
    AbckitLiteralArray *moduleLiteralArray = nullptr;
    AbckitLiteralArray *scopeNamesLiteralArray = nullptr;
    bool absPaths = false;
    size_t moduleRequestsOffset = 0;
    size_t regularImportsOffset = 0;
    size_t namespaceImportsOffset = 0;
    size_t localExportsOffset = 0;
    size_t indirectExportsOffset = 0;
    size_t starExportsOffset = 0;
};

struct AbckitModulePayloadStat {
    ark::pandasm::Record *record = nullptr;

    explicit AbckitModulePayloadStat(ark::pandasm::Record *record)
    {
        this->record = record;
    }
};

struct AbckitModulePayload {
    /*
     * Some data structure for STS which should store module's name and other data.
     */
    std::variant<AbckitModulePayloadDyn, AbckitModulePayloadStat> impl;

    AbckitModulePayload() = default;

    explicit AbckitModulePayload(ark::pandasm::Record *record)
    {
        this->impl = AbckitModulePayloadStat(record);
    }

    /*
     * Implementation for JS.
     */
    AbckitModulePayloadDyn &GetDynModule()
    {
        return std::get<AbckitModulePayloadDyn>(impl);
    }

    /*
     * Implementation for ArkTS1.2
     */
    AbckitModulePayloadStat &GetStatModule()
    {
        return std::get<AbckitModulePayloadStat>(impl);
    }
};

struct AbckitArktsModule {
    AbckitModulePayload impl;
    AbckitCoreModule *core = nullptr;

    AbckitArktsModule() = default;

    AbckitArktsModule(ark::pandasm::Record *record, AbckitCoreModule *core)
    {
        this->impl = AbckitModulePayload(record);
        this->core = core;
    }
};

struct AbckitJsModule {
    AbckitModulePayloadDyn impl {};
    AbckitCoreModule *core = nullptr;
};

struct AbckitCoreModule {
    /*
     * To refer to the properties of the original `.abc` file, such as is it dynamic, etc.
     */
    AbckitFile *file = nullptr;

    /*
     * Stores module's dependencies.
     * NOTE: For JS index here must match the index in `module_requests`.
     */
    std::vector<AbckitCoreModule *> md;

    /*
     * Stores module's imports.
     * NOTE: For JS will need to perform linear search to find needed import,
     * because namespace imports and regular imports are stored together here.
     */
    std::vector<std::unique_ptr<AbckitCoreImportDescriptor>> id;

    /*
     * Stores module's exports.
     * NOTE: For JS will need to perform linear search to find needed import,
     * because local exports, indirect exports and star exports are stored together here.
     */
    std::vector<std::unique_ptr<AbckitCoreExportDescriptor>> ed;

    /*
     * Tables to store and find wrapped entities by their name.
     */
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreClass>> ct;
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreNamespace>> nt;
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreInterface>> it;
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreEnum>> et;
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreAnnotationInterface>> at;
    std::vector<std::unique_ptr<AbckitCoreNamespace>> namespaces;
    std::vector<std::unique_ptr<AbckitCoreInterface>> interfaces;
    std::vector<std::unique_ptr<AbckitCoreEnum>> enums;
    std::vector<std::unique_ptr<AbckitCoreModuleField>> fields;

    /*
     * Only stores top level functions.
     */
    std::vector<std::unique_ptr<AbckitCoreFunction>> functions;

    /*
     * Current module's name.
     */
    AbckitString *moduleName = nullptr;

    /*
     * Indicator whether current module is local or external.
     */
    bool isExternal = false;

    /*
     * Target language of current module
     */
    AbckitTarget target = ABCKIT_TARGET_UNKNOWN;

    /*
     * Language-dependent implementation to store module data.
     */
    std::variant<std::unique_ptr<AbckitJsModule>, std::unique_ptr<AbckitArktsModule>> impl;
    AbckitArktsModule *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsModule>>(impl).get();
    }
    AbckitJsModule *GetJsImpl()
    {
        return std::get<std::unique_ptr<AbckitJsModule>>(impl).get();
    }

    void InsertClass(const std::string &name, std::unique_ptr<AbckitCoreClass> &&klass)
    {
        ct.emplace(name, std::move(klass));
    }

    template <typename AbckitCoreType>
    void InsertInstance(const std::string &name, std::unique_ptr<AbckitCoreType> &&instance);
};

template <>
inline void AbckitCoreModule::InsertInstance<AbckitCoreClass>(const std::string &name,
                                                              std::unique_ptr<AbckitCoreClass> &&instance)
{
    ct.emplace(name, std::move(instance));
}

template <>
inline void AbckitCoreModule::InsertInstance<AbckitCoreNamespace>(const std::string &name,
                                                                  std::unique_ptr<AbckitCoreNamespace> &&instance)
{
    nt.emplace(name, std::move(instance));
}

template <>
inline void AbckitCoreModule::InsertInstance<AbckitCoreInterface>(const std::string &name,
                                                                  std::unique_ptr<AbckitCoreInterface> &&instance)
{
    it.emplace(name, std::move(instance));
}

template <>
inline void AbckitCoreModule::InsertInstance<AbckitCoreEnum>(const std::string &name,
                                                             std::unique_ptr<AbckitCoreEnum> &&instance)
{
    et.emplace(name, std::move(instance));
}

template <>
inline void AbckitCoreModule::InsertInstance<AbckitCoreAnnotationInterface>(
    const std::string &name, std::unique_ptr<AbckitCoreAnnotationInterface> &&instance)
{
    at.emplace(name, std::move(instance));
}

struct AbckitString {
    AbckitString() = default;

    AbckitFile *file = nullptr;
    std::string_view impl;
};

using AbckitLiteralImplT = std::unique_ptr<libabckit::pandasm_Literal, void (*)(libabckit::pandasm_Literal *)>;
struct AbckitLiteral {
    AbckitLiteral(AbckitFile *file, AbckitLiteralImplT val) : file(file), val(std::move(val)) {}
    AbckitFile *file;
    AbckitLiteralImplT val;
};

struct AbckitLiteralArray {
    AbckitFile *file = nullptr;
    std::variant<ark::pandasm::LiteralArray *, panda::pandasm::LiteralArray *> impl;

    AbckitLiteralArray() = default;

    AbckitLiteralArray(AbckitFile *f, ark::pandasm::LiteralArray *laImpl)
    {
        impl = laImpl;
        file = f;
    }

    AbckitLiteralArray(AbckitFile *f, panda::pandasm::LiteralArray *laImpl)
    {
        impl = laImpl;
        file = f;
    }

    ark::pandasm::LiteralArray *GetStaticImpl() const
    {
        return std::get<ark::pandasm::LiteralArray *>(impl);
    }
    panda::pandasm::LiteralArray *GetDynamicImpl() const
    {
        return std::get<panda::pandasm::LiteralArray *>(impl);
    }
};

struct AbckitType {
    AbckitTypeId id = ABCKIT_TYPE_ID_INVALID;
    size_t rank = 0;
    AbckitString *name = nullptr;
    AbckitFile *file = nullptr;

    /*
     * If it is a union type, this field will record each type. name will store union name and other field in this case
     * is invalid, only in this field are valid.
     */
    std::vector<AbckitType *> types;

    std::variant<AbckitCoreClass *, AbckitCoreInterface *, AbckitCoreEnum *, std::nullptr_t> reference = nullptr;

    /*
     * To store all functions of this type as param or return type
     */
    std::unordered_set<AbckitCoreFunction *> functionUsers {};
    /*
     * To store all objects of this type as field type
     * The AbckitCoreInterfaceField name is obtained by parsing the function name, so it store in functionUsers
     */
    std::unordered_set<std::variant<AbckitCoreModuleField *, AbckitCoreNamespaceField *, AbckitCoreClassField *,
                                    AbckitCoreEnumField *, AbckitCoreAnnotationInterfaceField *>>
        fieldTypeUsers {};

    AbckitCoreClass *GetClass() const
    {
        if (auto p = std::get_if<AbckitCoreClass *>(&reference)) {
            return *p;
        }

        return nullptr;
    }
};

struct AbckitFileThreadState {
    size_t GetActiveOperationCount()
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        return activeOperations;
    }

    bool TryBeginOperation()
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        if (lifecycleState != AbckitFileLifecycleState::OPEN) {
            return false;
        }
        ++activeOperations;
        return true;
    }

    void FinishOperation()
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        if (activeOperations > 0) {
            --activeOperations;
        }
    }

    bool PromoteOperationToGraph()
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        if (activeOperations == 0 || lifecycleState != AbckitFileLifecycleState::OPEN) {
            return false;
        }
        --activeOperations;
        ++activeGraphs;
        return true;
    }

    void FinishGraph()
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        if (activeGraphs > 0) {
            --activeGraphs;
        }
    }

    bool CanWriteFromRegisteredOperation()
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        return lifecycleState == AbckitFileLifecycleState::OPEN && activeOperations == 1 && activeGraphs == 0;
    }

    bool TryBeginClose()
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        if (lifecycleState != AbckitFileLifecycleState::OPEN || activeOperations != 0 || activeGraphs != 0) {
            return false;
        }
        lifecycleState = AbckitFileLifecycleState::CLOSING;
        return true;
    }

    void FinishClose()
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        lifecycleState = AbckitFileLifecycleState::CLOSED;
    }

    // Lock order: apiMutex -> typesCacheMutex -> stringsCacheMutex.
    // litarrsCacheMutex and metadataCacheMutex are independent leaf locks.
    // Guards public API access to AbckitFile-owned metadata.
    std::shared_mutex apiMutex;
    // Guards file->strings and Program::strings.
    std::shared_mutex stringsCacheMutex;
    // Guards file->types. Type updates can create strings, so this lock may be held before stringsCacheMutex.
    std::recursive_mutex typesCacheMutex;
    // Guards file->litarrs.
    std::mutex litarrsCacheMutex;
    // Guards remaining lazy metadata wrappers such as literals and values.
    std::recursive_mutex metadataCacheMutex;
    // Guards lifecycleState, activeOperations and activeGraphs.
    std::mutex lifecycleMutex;
    AbckitFileLifecycleState lifecycleState = AbckitFileLifecycleState::OPEN;
    size_t activeOperations = 0;
    size_t activeGraphs = 0;
    // apiMutex -> suspendedReadersMutex. Structural writers drop apiMutex before waiting here.
    // One entry per thread temporarily releasing an outer API read lock, not per nesting level.
    // Updates hold both locks; reads need suspendedReadersMutex or exclusive apiMutex ownership.
    std::mutex suspendedReadersMutex;
    std::condition_variable suspendedReadersCv;
    size_t suspendedReaders = 0;
};

struct AbckitFile {
    struct AbcKitLiterals {
        std::unordered_map<bool, std::unique_ptr<AbckitLiteral>> boolLits;
        std::unordered_map<uint8_t, std::unique_ptr<AbckitLiteral>> u8Lits;
        std::unordered_map<uint16_t, std::unique_ptr<AbckitLiteral>> u16Lits;
        std::unordered_map<uint16_t, std::unique_ptr<AbckitLiteral>> methodAffilateLits;
        std::unordered_map<uint32_t, std::unique_ptr<AbckitLiteral>> u32Lits;
        std::unordered_map<uint64_t, std::unique_ptr<AbckitLiteral>> u64Lits;
        std::unordered_map<float, std::unique_ptr<AbckitLiteral>> floatLits;
        std::unordered_map<double, std::unique_ptr<AbckitLiteral>> doubleLits;
        std::unordered_map<std::string, std::unique_ptr<AbckitLiteral>> litArrLits;
        std::unordered_map<std::string, std::unique_ptr<AbckitLiteral>> stringLits;
        std::unordered_map<std::string, std::unique_ptr<AbckitLiteral>> methodLits;
        std::unique_ptr<AbckitLiteral> nullValueLit;
    };
    struct AbcKitValues {
        std::unordered_map<int, std::unique_ptr<AbckitValue>> intVals;
        std::unordered_map<int64_t, std::unique_ptr<AbckitValue>> longVals;
        std::unordered_map<bool, std::unique_ptr<AbckitValue>> boolVals;
        std::unordered_map<double, std::unique_ptr<AbckitValue>> doubleVals;
        std::unordered_map<std::string, std::unique_ptr<AbckitValue>> stringVals;
        std::unordered_map<int32_t, std::unique_ptr<AbckitValue>> stringNullptrVals;
        std::unordered_map<std::string, std::unique_ptr<AbckitValue>> litarrVals;
        std::unordered_map<std::string, std::unique_ptr<AbckitValue>> recordVals;
        std::unordered_map<std::string, std::unique_ptr<AbckitValue>> methodVals;
        std::unordered_map<std::string, std::unique_ptr<AbckitValue>> enumVals;
        std::unordered_map<std::string, std::unique_ptr<AbckitValue>> annotationVals;
        std::unordered_map<std::string, std::unique_ptr<AbckitValue>> arrayVals;
    };

    std::shared_ptr<AbckitFileThreadState> threadState = std::make_shared<AbckitFileThreadState>();

    libabckit::Mode frontend = libabckit::Mode::DYNAMIC;

    /*
     * Table to store wrapped internal modules.
     */
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreModule>> localModules;

    /*
     * Table to store wrapped external modules.
     */
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreModule>> externalModules;

    /*
     * To store the original program and update it.
     */
    std::variant<panda::pandasm::Program *, ark::pandasm::Program *> program;

    ark::pandasm::Program *GetStaticProgram()
    {
        return std::get<ark::pandasm::Program *>(program);
    }
    panda::pandasm::Program *GetDynamicProgram()
    {
        return std::get<panda::pandasm::Program *>(program);
    }

    AbcKitLiterals literals;
    std::unordered_map<size_t, AbckitType *> types;
    AbcKitValues values;
    std::vector<std::unique_ptr<AbckitLiteralArray>> litarrs;

    struct DynamicGraphCache {
        class BuildStateGuard final {
        public:
            BuildStateGuard(DynamicGraphCache &cache, uint64_t buildEpoch) : cache_(cache), buildEpoch_(buildEpoch) {}

            ~BuildStateGuard()
            {
                if (!active_) {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(cache_.mutex);
                    cache_.entry.reset();
                    cache_.state = cache_.invalidationEpoch == buildEpoch_ ? AbckitDynamicGraphSnapshotState::FAILED
                                                                           : AbckitDynamicGraphSnapshotState::STALE;
                }
                cache_.cv.notify_all();
            }

            BuildStateGuard(const BuildStateGuard &) = delete;
            BuildStateGuard &operator=(const BuildStateGuard &) = delete;

            void Release()
            {
                active_ = false;
            }

        private:
            DynamicGraphCache &cache_;
            uint64_t buildEpoch_;
            bool active_ = true;
        };

        size_t GetSuccessfulBuildCount() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return successfulBuildCount;
        }

        mutable std::mutex mutex;
        std::condition_variable cv;
        AbckitDynamicGraphSnapshotState state = AbckitDynamicGraphSnapshotState::EMPTY;
        std::shared_ptr<AbckitDynamicGraphCacheEntry> entry;
        uint64_t invalidationEpoch = 0;
        size_t successfulBuildCount = 0;
    };

    DynamicGraphCache dynamicGraphCache;
    // Protected by threadState->apiMutex.
    std::atomic<uint64_t> dynamicStructuralGeneration {0};
    std::unordered_map<std::thread::id, uint64_t> dynamicStructuralGenerationsByThread;
    std::unordered_map<AbckitCoreFunction *, uint64_t> dynamicFunctionVersions;

    /*
     * To store all program strings
     */
    std::unordered_map<std::string, std::unique_ptr<AbckitString>> strings;

    std::unordered_map<std::string, AbckitCoreFunction *> nameToFunctionStatic;
    std::unordered_map<std::string, AbckitCoreFunction *> nameToFunctionInstance;
    std::unordered_map<std::string, AbckitCoreFunction *> nameToExternalFunction;
    std::vector<std::unique_ptr<AbckitCoreFunction>> externalFunctions;

    std::unordered_multimap<std::string, std::variant<AbckitCoreModuleField *, AbckitCoreNamespaceField *,
                                                      AbckitCoreClassField *, AbckitCoreInterfaceField *,
                                                      AbckitCoreEnumField *, AbckitCoreAnnotationInterfaceField *>>
        nameToField;

    std::unordered_map<std::string, std::variant<AbckitCoreClass *, AbckitCoreInterface *, AbckitCoreEnum *>>
        nameToClass;

    /*
     * To store the .abc file version
     */
    AbckitFileVersion version = nullptr;

    void *internal = nullptr;

    /**
     * To store some data for build all objects
     */
    void *data = nullptr;

    /**
     * Function point to delete data
     */
    void (*destoryData)(void *) = nullptr;

    /**
     * Whether need refresh insts
     */
    bool needRefreshInsts = false;

    /**
     * Pending (oldKey, newKey) function renames; applied in batch in WriteAbcStatic to reduce
     * per-function annotation refresh calls (async, lambda, FunctionOverload).
     */
    std::vector<std::pair<std::string, std::string>> pendingFunctionRenames;
};

inline bool UsesDynamicThreadSafety(const AbckitFile *file)
{
    return file != nullptr && file->frontend == libabckit::Mode::DYNAMIC;
}

class AbckitFileApiReadLock final {
public:
    explicit AbckitFileApiReadLock(AbckitFile *file)
    {
        if (!UsesDynamicThreadSafety(file)) {
            return;
        }
        state_ = file->threadState;

        auto *state = state_.get();
        auto &readDepth = ReadDepth();
        auto found = readDepth.find(state);
        if (found != readDepth.end()) {
            ++found->second;
            return;
        }
        readDepth[state] = 1;
        if (WriteDepth().find(state) != WriteDepth().end()) {
            return;
        }
        tracksOperation_ = state_->TryBeginOperation();
        if (!tracksOperation_) {
            readDepth.erase(state);
            conflict_ = true;
            return;
        }
        state_->apiMutex.lock_shared();
        locked_ = true;
    }

    ~AbckitFileApiReadLock()
    {
        if (state_ == nullptr) {
            return;
        }

        auto *state = state_.get();
        auto &readDepth = ReadDepth();
        auto found = readDepth.find(state);
        if (found == readDepth.end()) {
            return;
        }
        if (--found->second == 0) {
            readDepth.erase(found);
        }
        if (tracksOperation_) {
            state_->FinishOperation();
        }
        if (locked_) {
            state_->apiMutex.unlock_shared();
        }
    }

    AbckitFileApiReadLock(const AbckitFileApiReadLock &) = delete;
    AbckitFileApiReadLock &operator=(const AbckitFileApiReadLock &) = delete;

    bool HasConflict() const
    {
        return conflict_;
    }

private:
    static std::unordered_map<AbckitFileThreadState *, size_t> &ReadDepth()
    {
        thread_local std::unordered_map<AbckitFileThreadState *, size_t> depth;
        return depth;
    }

    static std::unordered_map<AbckitFileThreadState *, size_t> &WriteDepth()
    {
        thread_local std::unordered_map<AbckitFileThreadState *, size_t> depth;
        return depth;
    }

    static std::unordered_map<AbckitFileThreadState *, size_t> &SuspendedReadDepth()
    {
        thread_local std::unordered_map<AbckitFileThreadState *, size_t> depth;
        return depth;
    }

    std::shared_ptr<AbckitFileThreadState> state_;
    bool locked_ = false;
    bool tracksOperation_ = false;
    bool conflict_ = false;

    friend class AbckitFileApiWriteLock;
    friend class AbckitFileApiReadLockSuspension;
};

class AbckitFileApiReadLockSuspension final {
public:
    explicit AbckitFileApiReadLockSuspension(AbckitFile *file)
    {
        if (!UsesDynamicThreadSafety(file)) {
            return;
        }
        state_ = file->threadState;

        auto *state = state_.get();
        if (AbckitFileApiReadLock::ReadDepth().find(state) == AbckitFileApiReadLock::ReadDepth().end() ||
            AbckitFileApiReadLock::WriteDepth().find(state) != AbckitFileApiReadLock::WriteDepth().end()) {
            return;
        }

        auto &suspendedReadDepth = AbckitFileApiReadLock::SuspendedReadDepth();
        auto &depth = suspendedReadDepth[state];
        if (depth++ == 0) {
            {
                std::lock_guard<std::mutex> lock(state_->suspendedReadersMutex);
                ++state_->suspendedReaders;
            }
            state_->apiMutex.unlock_shared();
        }
        participates_ = true;
    }

    ~AbckitFileApiReadLockSuspension()
    {
        if (!participates_) {
            return;
        }

        auto *state = state_.get();
        auto &suspendedReadDepth = AbckitFileApiReadLock::SuspendedReadDepth();
        auto found = suspendedReadDepth.find(state);
        if (found == suspendedReadDepth.end()) {
            return;
        }
        if (--found->second == 0) {
            suspendedReadDepth.erase(found);
            state_->apiMutex.lock_shared();
            {
                std::lock_guard<std::mutex> lock(state_->suspendedReadersMutex);
                --state_->suspendedReaders;
            }
            state_->suspendedReadersCv.notify_all();
        }
    }

    AbckitFileApiReadLockSuspension(const AbckitFileApiReadLockSuspension &) = delete;
    AbckitFileApiReadLockSuspension &operator=(const AbckitFileApiReadLockSuspension &) = delete;

private:
    std::shared_ptr<AbckitFileThreadState> state_;
    bool participates_ = false;
};

enum class AbckitFileApiWriteMode { DEFAULT, STRUCTURAL };

class AbckitFileApiWriteLock final {
public:
    explicit AbckitFileApiWriteLock(AbckitFile *file, bool trackLifecycle = true,
                                    AbckitFileApiWriteMode mode = AbckitFileApiWriteMode::DEFAULT)
    {
        if (!UsesDynamicThreadSafety(file)) {
            return;
        }
        state_ = file->threadState;

        auto *state = state_.get();
        if (AbckitFileApiReadLock::ReadDepth().find(state) != AbckitFileApiReadLock::ReadDepth().end() &&
            AbckitFileApiReadLock::WriteDepth().find(state) == AbckitFileApiReadLock::WriteDepth().end() &&
            AbckitFileApiReadLock::SuspendedReadDepth().find(state) ==
                AbckitFileApiReadLock::SuspendedReadDepth().end()) {
            conflict_ = true;
            return;
        }

        auto &writeDepth = AbckitFileApiReadLock::WriteDepth();
        auto found = writeDepth.find(state);
        if (found != writeDepth.end()) {
            if (mode == AbckitFileApiWriteMode::STRUCTURAL && !WaitForSuspendedReaders(true)) {
                conflict_ = true;
                return;
            }
            ++found->second;
            return;
        }
        if (trackLifecycle) {
            tracksOperation_ = state_->TryBeginOperation();
            if (!tracksOperation_) {
                conflict_ = true;
                return;
            }
        }
        writeDepth[state] = 1;
        state_->apiMutex.lock();
        if (mode == AbckitFileApiWriteMode::STRUCTURAL && !WaitForSuspendedReaders(false)) {
            writeDepth.erase(state);
            state_->apiMutex.unlock();
            if (tracksOperation_) {
                state_->FinishOperation();
                tracksOperation_ = false;
            }
            conflict_ = true;
            return;
        }
        locked_ = true;
    }

    ~AbckitFileApiWriteLock()
    {
        if (state_ == nullptr || conflict_) {
            return;
        }

        auto *state = state_.get();
        auto &writeDepth = AbckitFileApiReadLock::WriteDepth();
        auto found = writeDepth.find(state);
        if (found == writeDepth.end()) {
            return;
        }
        if (--found->second == 0) {
            writeDepth.erase(found);
        }
        if (tracksOperation_) {
            state_->FinishOperation();
        }
        if (locked_) {
            state_->apiMutex.unlock();
        }
    }

    bool HasConflict() const
    {
        return conflict_;
    }

    AbckitFileApiWriteLock(const AbckitFileApiWriteLock &) = delete;
    AbckitFileApiWriteLock &operator=(const AbckitFileApiWriteLock &) = delete;

private:
    // Called with apiMutex exclusively held. A waiting writer must let suspended
    // readers finish snapshot construction and reacquire their outer shared lock.
    bool WaitForSuspendedReaders(bool recursive)
    {
        if (state_->suspendedReaders == 0U) {
            return true;
        }
        const auto &suspendedDepth = AbckitFileApiReadLock::SuspendedReadDepth();
        const size_t ownReader = suspendedDepth.find(state_.get()) != suspendedDepth.end() ? 1U : 0U;
        std::unique_lock<std::mutex> lock(state_->suspendedReadersMutex);
        while (state_->suspendedReaders > ownReader) {
            // Two read callbacks cannot upgrade against each other. Nor may a
            // nested writer release an exclusive lock belonging to its caller.
            if (ownReader != 0U || recursive) {
                return false;
            }
            state_->apiMutex.unlock();
            state_->suspendedReadersCv.wait(lock, [this]() { return state_->suspendedReaders == 0U; });
            lock.unlock();
            state_->apiMutex.lock();
            lock.lock();
        }
        return true;
    }

    std::shared_ptr<AbckitFileThreadState> state_;
    bool locked_ = false;
    bool tracksOperation_ = false;
    bool conflict_ = false;
};

class AbckitFileMetadataCacheLock final {
public:
    explicit AbckitFileMetadataCacheLock(AbckitFile *file)
    {
        if (!UsesDynamicThreadSafety(file)) {
            return;
        }
        state_ = file->threadState;
        lock_ = std::unique_lock<std::recursive_mutex>(state_->metadataCacheMutex);
    }

private:
    std::shared_ptr<AbckitFileThreadState> state_;
    std::unique_lock<std::recursive_mutex> lock_;
};

class AbckitFileStringsCacheReadLock final {
public:
    explicit AbckitFileStringsCacheReadLock(AbckitFile *file)
    {
        if (!UsesDynamicThreadSafety(file)) {
            return;
        }
        state_ = file->threadState;
        lock_ = std::shared_lock<std::shared_mutex>(state_->stringsCacheMutex);
    }

private:
    std::shared_ptr<AbckitFileThreadState> state_;
    std::shared_lock<std::shared_mutex> lock_;
};

class AbckitFileStringsCacheWriteLock final {
public:
    explicit AbckitFileStringsCacheWriteLock(AbckitFile *file)
    {
        if (!UsesDynamicThreadSafety(file)) {
            return;
        }
        state_ = file->threadState;
        lock_ = std::unique_lock<std::shared_mutex>(state_->stringsCacheMutex);
    }

private:
    std::shared_ptr<AbckitFileThreadState> state_;
    std::unique_lock<std::shared_mutex> lock_;
};

class AbckitFileTypesCacheLock final {
public:
    explicit AbckitFileTypesCacheLock(AbckitFile *file)
    {
        if (!UsesDynamicThreadSafety(file)) {
            return;
        }
        state_ = file->threadState;
        lock_ = std::unique_lock<std::recursive_mutex>(state_->typesCacheMutex);
    }

private:
    std::shared_ptr<AbckitFileThreadState> state_;
    std::unique_lock<std::recursive_mutex> lock_;
};

class AbckitFileLitArrsCacheLock final {
public:
    explicit AbckitFileLitArrsCacheLock(AbckitFile *file)
    {
        if (!UsesDynamicThreadSafety(file)) {
            return;
        }
        state_ = file->threadState;
        lock_ = std::unique_lock<std::mutex>(state_->litarrsCacheMutex);
    }

private:
    std::shared_ptr<AbckitFileThreadState> state_;
    std::unique_lock<std::mutex> lock_;
};

struct AbckitDynamicImportDescriptorPayload {
    /*
     * Indicator whether import is namespace or regular.
     */
    bool isRegularImport = false;
    /*
     * Offset in the corresponding `XXX_imports` in mainline.
     */
    uint32_t moduleRecordIndexOff = 0;
};

struct AbckitStaticImportDescriptorPayload {
    std::variant<AbckitCoreAnnotationInterface *, AbckitCoreClass *, AbckitCoreFunction *, AbckitCoreModuleField *,
                 AbckitCoreClassField *, AbckitCoreInterfaceField *, AbckitCoreEnumField *>
        impl;
    /*
     * Implementation for AbckitImportExportDescriptorKind::ANNOTATION.
     */
    AbckitCoreAnnotationInterface *GetAnnotationInterfacePayload()
    {
        return std::get<AbckitCoreAnnotationInterface *>(impl);
    }
    /*
     * Implementation for AbckitImportExportDescriptorKind::CLASS.
     */
    AbckitCoreClass *GetClassPayload()
    {
        return std::get<AbckitCoreClass *>(impl);
    }
    /*
     * Implementation for AbckitImportExportDescriptorKind::FUNCTION.
     */
    AbckitCoreFunction *GetFunctionPayload()
    {
        return std::get<AbckitCoreFunction *>(impl);
    }
    /*
     * Implementation for AbckitImportExportDescriptorKind::FIELD.
     */
    AbckitCoreModuleField *GetModuleFieldPayload()
    {
        return std::get<AbckitCoreModuleField *>(impl);
    }
    /*
     * Implementation for AbckitImportExportDescriptorKind::FIELD.
     */
    AbckitCoreClassField *GetClassFieldPayload()
    {
        return std::get<AbckitCoreClassField *>(impl);
    }
    /*
     * Implementation for AbckitImportExportDescriptorKind::FIELD.
     */
    AbckitCoreInterfaceField *GetInterfaceFieldPayload()
    {
        return std::get<AbckitCoreInterfaceField *>(impl);
    }
    /*
     * Implementation for AbckitImportExportDescriptorKind::FIELD.
     */
    AbckitCoreEnumField *GetEnumFieldPayload()
    {
        return std::get<AbckitCoreEnumField *>(impl);
    }
};

struct AbckitCoreImportDescriptorPayload {
    std::variant<AbckitDynamicImportDescriptorPayload, AbckitStaticImportDescriptorPayload> impl;
    /*
     * Implementation for AbckitImportExportDescriptorKind::UNTYPED.
     */
    AbckitDynamicImportDescriptorPayload &GetDynId()
    {
        return std::get<AbckitDynamicImportDescriptorPayload>(impl);
    }
    /*
     * Implementation for static kinds.
     */
    AbckitStaticImportDescriptorPayload GetStatId()
    {
        return std::get<AbckitStaticImportDescriptorPayload>(impl);
    }
};

struct AbckitArktsImportDescriptor {
    /*
     * Kind of the imported entity.
     * Use AbckitImportExportDescriptorKind::UNTYPED for imports from dynamic modules.
     * Other kinds are used for imports from static modules.
     */
    AbckitImportExportDescriptorKind kind = UNTYPED;
    /*
     * Data needed to work with the import.
     */
    AbckitCoreImportDescriptorPayload payload;
    AbckitCoreImportDescriptor *core = nullptr;
};

struct AbckitJsImportDescriptor {
    /*
     * Kind of the imported entity.
     * Use AbckitImportExportDescriptorKind::UNTYPED for imports from dynamic modules.
     * Other kinds are used for imports from static modules.
     */
    AbckitImportExportDescriptorKind kind = UNTYPED;
    /*
     * Data needed to work with the import.
     */
    AbckitCoreImportDescriptorPayload payload;
    AbckitCoreImportDescriptor *core = nullptr;
};

struct AbckitCoreImportDescriptor {
    /*
     * Back link to the importing module.
     */
    AbckitCoreModule *importingModule = nullptr;
    /*
     * Link to the module from which entity is imported.
     */
    AbckitCoreModule *importedModule = nullptr;

    std::variant<std::unique_ptr<AbckitJsImportDescriptor>, std::unique_ptr<AbckitArktsImportDescriptor>> impl;
    AbckitArktsImportDescriptor *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsImportDescriptor>>(impl).get();
    }
    AbckitJsImportDescriptor *GetJsImpl()
    {
        return std::get<std::unique_ptr<AbckitJsImportDescriptor>>(impl).get();
    }
};

struct AbckitDynamicExportDescriptorPayload {
    /*
     * The kind of export. Used to determine where to look by the index.
     */
    AbckitDynamicExportKind kind = ABCKIT_DYNAMIC_EXPORT_KIND_LOCAL_EXPORT;
    /*
     * For special StarExport case 'export * as <StarExport> from "..."'.
     * It converts to NamespaceImport + LocalExport:
     * import * as =ens{$i} from "...";
     * export =ens{$i} as <StarExport>;
     */
    bool hasServiceImport = false;
    /*
     * For special StarExport case 'export * as <StarExport> from "..."'.
     */
    size_t serviceNamespaceImportIdx = 0;
    /*
     * Offset in the corresponding `XXX_exports` (depends on the `kind`) in mainline.
     */
    uint32_t moduleRecordIndexOff = 0;
};

struct AbckitCoreExportDescriptorPayload {
    std::variant<AbckitDynamicExportDescriptorPayload, AbckitCoreAnnotationInterface *, AbckitCoreClass *,
                 AbckitCoreFunction *, AbckitCoreModuleField *, AbckitCoreClassField *, AbckitCoreInterfaceField *,
                 AbckitCoreEnumField *>
        impl;

    /*
     * Implementation for AbckitImportExportDescriptorKind::UNTYPED.
     */
    AbckitDynamicExportDescriptorPayload &GetDynamicPayload()
    {
        return std::get<AbckitDynamicExportDescriptorPayload>(impl);
    }
    /*
     * Payload for AbckitImportExportDescriptorKind::ANNOTATION.
     * Should point to the LOCAL entity.
     */
    AbckitCoreAnnotationInterface *GetAnnotationInterfacePayload()
    {
        return std::get<AbckitCoreAnnotationInterface *>(impl);
    }
    /*
     * Payload for AbckitImportExportDescriptorKind::CLASS.
     * Should point to the LOCAL entity.
     */
    AbckitCoreClass *GetClassPayload()
    {
        return std::get<AbckitCoreClass *>(impl);
    }
    /*
     * Payload for AbckitImportExportDescriptorKind::FUNCTION.
     * Should point to the LOCAL entity.
     */
    AbckitCoreFunction *GetFunctionPayload()
    {
        return std::get<AbckitCoreFunction *>(impl);
    }
    /*
     * Payload for AbckitImportExportDescriptorKind::FIELD.
     * Should point to the LOCAL entity.
     */
    AbckitCoreModuleField *GetModuleFieldPayload()
    {
        return std::get<AbckitCoreModuleField *>(impl);
    }
    /*
     * Payload for AbckitImportExportDescriptorKind::FIELD.
     * Should point to the LOCAL entity.
     */
    AbckitCoreClassField *GetClassFieldPayload()
    {
        return std::get<AbckitCoreClassField *>(impl);
    }
    /*
     * Payload for AbckitImportExportDescriptorKind::FIELD.
     * Should point to the LOCAL entity.
     */
    AbckitCoreInterfaceField *GetInterfaceFieldPayload()
    {
        return std::get<AbckitCoreInterfaceField *>(impl);
    }
    /*
     * Payload for AbckitImportExportDescriptorKind::FIELD.
     * Should point to the LOCAL entity.
     */
    AbckitCoreEnumField *GetEnumFieldPayload()
    {
        return std::get<AbckitCoreEnumField *>(impl);
    }
};

struct AbckitArktsExportDescriptor {
    AbckitCoreExportDescriptor *core = nullptr;
    /*
     * Kind of the exported entity.
     * Use AbckitImportExportDescriptorKind::UNTYPED for exports from dynamic modules.
     * Other kinds are used for exports from static modules.
     * NOTE: Use same enum as import API here. May need to rename this enum
     * when implementing v2 API to something neutral, like AbckitDescriptorKind.
     */
    AbckitImportExportDescriptorKind kind = UNTYPED;
    /*
     * Data needed to work with the import.
     */
    AbckitCoreExportDescriptorPayload payload;
};

struct AbckitJsExportDescriptor {
    AbckitCoreExportDescriptor *core = nullptr;
    /*
     * Kind of the exported entity.
     * Use AbckitImportExportDescriptorKind::UNTYPED for exports from dynamic modules.
     * Other kinds are used for exports from static modules.
     * NOTE: Use same enum as import API here. May need to rename this enum
     * when implementing v2 API to something neutral, like AbckitDescriptorKind.
     */
    AbckitImportExportDescriptorKind kind = UNTYPED;
    /*
     * Data needed to work with the import.
     */
    AbckitCoreExportDescriptorPayload payload;
};

struct AbckitCoreExportDescriptor {
    /*
     * Back link to the exporting module.
     */
    AbckitCoreModule *exportingModule = nullptr;
    /*
     * Link to the exported module.
     */
    AbckitCoreModule *exportedModule = nullptr;

    std::variant<std::unique_ptr<AbckitJsExportDescriptor>, std::unique_ptr<AbckitArktsExportDescriptor>> impl;
    AbckitArktsExportDescriptor *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsExportDescriptor>>(impl).get();
    }
    AbckitJsExportDescriptor *GetJsImpl()
    {
        return std::get<std::unique_ptr<AbckitJsExportDescriptor>>(impl).get();
    }
};

struct AbckitArktsModuleField {
    AbckitCoreModuleField *core = nullptr;
    std::variant<ark::pandasm::Field *> impl;

    ark::pandasm::Field *GetStaticImpl()
    {
        return std::get<ark::pandasm::Field *>(impl);
    }

    explicit AbckitArktsModuleField(ark::pandasm::Field *field)
    {
        impl = field;
    }
};

struct AbckitCoreModuleField {
    /*
     * Name of the field.
     */
    AbckitString *name = nullptr;
    /*
     * Type of the field.
     */
    AbckitType *type = nullptr;
    /*
     * Value of the field.
     */
    AbckitValue *value = nullptr;
    /*
     * Table to store the annotations of the field.
     */
    std::vector<std::unique_ptr<AbckitCoreAnnotation>> annotations;
    /*
     * Table to store links to the wrapped annotations.
     */
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreAnnotation>> annotationTable;
    /*
     * To refer to the properties of the origin module.
     */
    AbckitCoreModule *owner = nullptr;

    std::variant<std::unique_ptr<AbckitArktsModuleField>> impl;
    AbckitArktsModuleField *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsModuleField>>(impl).get();
    }

    ark::pandasm::Record *GetOwnerStaticImpl() const
    {
        return this->owner->GetArkTSImpl()->impl.GetStatModule().record;
    }

    AbckitCoreModuleField(AbckitCoreModule *module, ark::pandasm::Field *field)
    {
        owner = module;
        impl = std::make_unique<AbckitArktsModuleField>(field);
        GetArkTSImpl()->core = this;
    }
};

struct AbckitArktsNamespaceField {
    AbckitCoreNamespaceField *core = nullptr;
    std::variant<ark::pandasm::Field *> impl;

    ark::pandasm::Field *GetStaticImpl()
    {
        return std::get<ark::pandasm::Field *>(impl);
    }

    explicit AbckitArktsNamespaceField(ark::pandasm::Field *field)
    {
        impl = field;
    }
};

struct AbckitCoreNamespaceField {
    /*
     * Name of the field.
     */
    AbckitString *name = nullptr;
    /*
     * Type of the field.
     */
    AbckitType *type = nullptr;
    /*
     * Value of the field.
     */
    AbckitValue *value = nullptr;
    /*
     * Table to store the annotations of the field.
     */
    std::vector<std::unique_ptr<AbckitCoreAnnotation>> annotations;
    /*
     * Table to store links to the wrapped annotations.
     */
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreAnnotation>> annotationTable;
    /*
     * To refer to the properties of the origin namespace.
     */
    AbckitCoreNamespace *owner = nullptr;

    std::variant<std::unique_ptr<AbckitArktsNamespaceField>> impl;
    AbckitArktsNamespaceField *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsNamespaceField>>(impl).get();
    }

    AbckitCoreNamespaceField(AbckitCoreNamespace *ns, ark::pandasm::Field *field)
    {
        owner = ns;
        impl = std::make_unique<AbckitArktsNamespaceField>(field);
        GetArkTSImpl()->core = this;
    }

    ark::pandasm::Record *GetOwnerStaticImpl() const
    {
        return this->owner->GetArkTSImpl()->impl.GetStaticClass();
    }
};

struct AbckitArktsClassField {
    AbckitCoreClassField *core = nullptr;
    std::variant<ark::pandasm::Field *> impl;

    ark::pandasm::Field *GetStaticImpl()
    {
        return std::get<ark::pandasm::Field *>(impl);
    }

    explicit AbckitArktsClassField(ark::pandasm::Field *field)
    {
        impl = field;
    }
};

struct AbckitCoreClassField {
    /*
     * Name of the field.
     */
    AbckitString *name = nullptr;
    /*
     * Type of the field.
     */
    AbckitType *type = nullptr;
    /*
     * Value of the field.
     */
    AbckitValue *value = nullptr;
    /*
     * Table to store the annotations of the field.
     */
    std::vector<std::unique_ptr<AbckitCoreAnnotation>> annotations;
    /*
     * Table to store links to the wrapped annotations.
     */
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreAnnotation>> annotationTable;
    /*
     * To refer to the properties of the origin class.
     */
    AbckitCoreClass *owner = nullptr;

    std::variant<std::unique_ptr<AbckitArktsClassField>> impl;
    AbckitArktsClassField *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsClassField>>(impl).get();
    }

    AbckitCoreClassField(AbckitCoreClass *klass, ark::pandasm::Field *field)
    {
        owner = klass;
        impl = std::make_unique<AbckitArktsClassField>(field);
        GetArkTSImpl()->core = this;
    }

    ark::pandasm::Record *GetOwnerStaticImpl() const
    {
        return this->owner->GetArkTSImpl()->impl.GetStaticClass();
    }
};

struct AbckitArktsInterfacePayload {
    /*
     * In ArkTS2 interface is defined by corresponding pandasm::Record.
     */
    std::variant<ark::pandasm::Record *> cl;

    ark::pandasm::Record *GetStaticClass()
    {
        return std::get<ark::pandasm::Record *>(cl);
    }
};

struct AbckitArktsInterface {
    AbckitArktsInterfacePayload impl;
    AbckitCoreInterface *core = nullptr;

    explicit AbckitArktsInterface(ark::pandasm::Record *record)
    {
        impl.cl = reinterpret_cast<ark::pandasm::Record *>(record);
    };
};

struct AbckitCoreInterface {
    /*
     * Back link to the module.
     */
    AbckitCoreModule *owningModule = nullptr;
    /*
     * To refer to the properties of the parent namepsace.
     */
    AbckitCoreNamespace *parentNamespace = nullptr;
    /*
     * Name of the interface.
     */
    AbckitString *name = nullptr;
    /*
     * To refer to the partial.
     */
    std::unique_ptr<AbckitCoreClass> partial;
    /*
     * Table to store the methods of the interface.
     */
    std::vector<std::unique_ptr<AbckitCoreFunction>> methods;
    /*
     * Table to store the fields of the interface.
     */
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreInterfaceField>> fields;
    /*
     * Table to store the annotations of the interface.
     */
    std::vector<std::unique_ptr<AbckitCoreAnnotation>> annotations;
    /*
     * Table to store links to the wrapped annotations.
     */
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreAnnotation>> annotationTable;
    /*
     * Table to store the classes implement the interface.
     */
    std::vector<AbckitCoreClass *> classes;
    /*
     * Table to store the super interfaces of the interface.
     */
    std::vector<AbckitCoreInterface *> superInterfaces;
    /*
     * Table to store the sub interfaces of the interface.
     */
    std::vector<AbckitCoreInterface *> subInterfaces;
    /*
     * Table to store the object literal
     */
    std::vector<std::unique_ptr<AbckitCoreClass>> objectLiterals;
    /*
     * To store type users
     */
    std::vector<AbckitType *> typeUsers;
    /*
     * To store interface[]
     */
    std::unique_ptr<AbckitCoreClass> array = nullptr;

    std::variant<std::unique_ptr<AbckitArktsInterface>> impl;
    AbckitArktsInterface *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsInterface>>(impl).get();
    }

    AbckitCoreInterface(AbckitCoreModule *module, AbckitArktsInterface iface)
    {
        iface.core = this;
        impl = std::make_unique<AbckitArktsInterface>(iface);
        owningModule = module;
    }
};

struct AbckitArktsInterfaceField {
    AbckitCoreInterfaceField *core = nullptr;
    std::variant<ark::pandasm::Field *> impl;

    ark::pandasm::Field *GetStaticImpl()
    {
        return std::get<ark::pandasm::Field *>(impl);
    }

    explicit AbckitArktsInterfaceField() = default;

    explicit AbckitArktsInterfaceField(ark::pandasm::Field *field)
    {
        impl = field;
    }
};

struct AbckitCoreInterfaceField {
    /*
     * Name of the field.
     */
    AbckitString *name = nullptr;
    /*
     * Type of the field.
     */
    AbckitType *type = nullptr;
    /*
     * Access flag of the field.
     */
    uint32_t flag = 0;
    /*
     * Table to store the annotations of the field.
     */
    std::vector<std::unique_ptr<AbckitCoreAnnotation>> annotations;
    /*
     * To refer to the properties of the origin interface.
     */
    AbckitCoreInterface *owner = nullptr;

    std::variant<std::unique_ptr<AbckitArktsInterfaceField>> impl;
    AbckitArktsInterfaceField *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsInterfaceField>>(impl).get();
    }

    ark::pandasm::Record *GetOwnerStaticImpl() const
    {
        return this->owner->GetArkTSImpl()->impl.GetStaticClass();
    }
};

struct AbckitArktsEnumPayload {
    /*
     * In ArkTS2 enum is defined by corresponding pandasm::Record.
     */
    std::variant<ark::pandasm::Record *> cl;

    ark::pandasm::Record *GetStaticClass()
    {
        return std::get<ark::pandasm::Record *>(cl);
    }
};

struct AbckitArktsEnum {
    AbckitArktsEnumPayload impl;
    AbckitCoreEnum *core = nullptr;

    explicit AbckitArktsEnum(ark::pandasm::Record *record)
    {
        impl.cl = reinterpret_cast<ark::pandasm::Record *>(record);
    };
};

struct AbckitCoreEnum {
    /*
     * Back link to the module.
     */
    AbckitCoreModule *owningModule = nullptr;
    /*
     * To refer to the properties of the parent namepsace.
     */
    AbckitCoreNamespace *parentNamespace = nullptr;
    /*
     * Name of the enum.
     */
    AbckitString *name = nullptr;
    /*
     * To refer to the partial.
     */
    std::unique_ptr<AbckitCoreClass> partial;
    /*
     * Table to store the methods of the enum.
     */
    std::vector<std::unique_ptr<AbckitCoreFunction>> methods;
    /*
     * Table to store the fields of the enum.
     */
    std::vector<std::unique_ptr<AbckitCoreEnumField>> fields;
    /*
     * Table to store the annotations of the enum.
     */
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreAnnotation>> annotations;
    /*
     * Table to store links to the wrapped annotations.
     */
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreAnnotation>> annotationTable;
    /*
     * To store type users
     */
    std::vector<AbckitType *> typeUsers;

    /*
     * To store enum[]
     */
    std::unique_ptr<AbckitCoreClass> array = nullptr;

    std::variant<std::unique_ptr<AbckitArktsEnum>> impl;
    AbckitArktsEnum *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsEnum>>(impl).get();
    }

    AbckitCoreEnum(AbckitCoreModule *module, AbckitArktsEnum enm)
    {
        enm.core = this;
        impl = std::make_unique<AbckitArktsEnum>(enm);
        owningModule = module;
    }
};

struct AbckitArktsEnumField {
    AbckitCoreEnumField *core = nullptr;
    std::variant<ark::pandasm::Field *> impl;

    ark::pandasm::Field *GetStaticImpl()
    {
        return std::get<ark::pandasm::Field *>(impl);
    }

    explicit AbckitArktsEnumField(ark::pandasm::Field *field)
    {
        impl = field;
    }
};

struct AbckitCoreEnumField {
    /*
     * Name of the field.
     */
    AbckitString *name = nullptr;
    /*
     * Type of the field.
     */
    AbckitType *type = nullptr;
    /*
     * Value of the field.
     */
    AbckitValue *value = nullptr;
    /*
     * Table to store the annotations of the field.
     */
    std::vector<std::unique_ptr<AbckitCoreAnnotation>> annotations;
    /*
     * Table to store links to the wrapped annotations.
     */
    std::unordered_map<std::string, std::unique_ptr<AbckitCoreAnnotation>> annotationTable;
    /*
     * To refer to the properties of the origin enum.
     */
    AbckitCoreEnum *owner = nullptr;

    std::variant<std::unique_ptr<AbckitArktsEnumField>> impl;
    AbckitArktsEnumField *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsEnumField>>(impl).get();
    }

    AbckitCoreEnumField(AbckitCoreEnum *enm, ark::pandasm::Field *field)
    {
        owner = enm;
        impl = std::make_unique<AbckitArktsEnumField>(field);
        GetArkTSImpl()->core = this;
    }

    ark::pandasm::Record *GetOwnerStaticImpl() const
    {
        return this->owner->GetArkTSImpl()->impl.GetStaticClass();
    }
};

struct AbckitArktsFunctionParam {
    AbckitCoreFunctionParam *core = nullptr;
};

struct AbckitCoreFunctionParam {
    /*
     * Back link to the function.
     */
    AbckitCoreFunction *function = nullptr;
    /*
     * Name of the parameter.
     */
    AbckitString *name = nullptr;
    /*
     * Type of the parameter.
     */
    AbckitType *type = nullptr;
    /*
     * Default value of the parameter.
     */
    AbckitValue *defaultValue = nullptr;

    std::variant<std::unique_ptr<AbckitArktsFunctionParam>> impl;
    AbckitArktsFunctionParam *GetArkTSImpl()
    {
        return std::get<std::unique_ptr<AbckitArktsFunctionParam>>(impl).get();
    }
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

#endif  // LIBABCKIT_STD_METADATA_INSPECT_IMPL_H
