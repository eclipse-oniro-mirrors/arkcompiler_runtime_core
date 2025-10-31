/**
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
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

#include "include/object_header.h"
#include "intrinsics.h"
#include "libarkbase/utils/logger.h"
#include "plugins/ets/runtime/ets_class_linker_context.h"
#include "plugins/ets/runtime/ets_class_linker_extension.h"
#include "plugins/ets/runtime/ets_coroutine.h"
#include "plugins/ets/runtime/ets_stubs-inl.h"
#include "plugins/ets/runtime/ets_vm.h"
#include "plugins/ets/runtime/types/ets_field.h"
#include "plugins/ets/runtime/types/ets_method.h"
#include "plugins/ets/runtime/types/ets_primitives.h"
#include "plugins/ets/runtime/types/ets_runtime_linker.h"
#include "plugins/ets/runtime/types/ets_string.h"
#include "plugins/ets/runtime/types/ets_class.h"
#include "plugins/ets/runtime/types/ets_reflect_field.h"
#include "plugins/ets/runtime/types/ets_reflect_method.h"
#include "plugins/ets/runtime/types/ets_typeapi.h"
#include "plugins/ets/runtime/ets_annotation.h"
#include "plugins/ets/runtime/ets_utils.h"
#include "runtime/handle_scope-inl.h"
#include "libarkbase/utils/utf.h"

#ifdef PANDA_ETS_INTEROP_JS
#include "plugins/ets/runtime/interop_js/interop_context.h"
#endif /* PANDA_ETS_INTEROP_JS */

namespace ark::ets::intrinsics {

template <class Callback>
static void EnumerateBaseClassesReverseOrder(EtsClass *cls, const Callback &callback)
{
    auto currentClass = cls;
    bool finished = false;
    while (currentClass && !finished) {
        if constexpr (std::is_same_v<std::invoke_result_t<Callback, decltype(cls)>, void>) {
            callback(currentClass);
            finished = false;
        } else {
            finished = callback(currentClass);
        }
        currentClass = currentClass->GetSuperClass();
    }
}

EtsString *StdCoreClassGetNameInternal(EtsClass *cls)
{
    return cls->GetName();
}

EtsRuntimeLinker *StdCoreClassGetLinkerInternal(EtsClass *cls)
{
    auto *ctx = cls->GetLoadContext();
    return ctx->IsBootContext() ? nullptr : EtsClassLinkerContext::FromCoreType(ctx)->GetRuntimeLinker();
}

EtsBoolean StdCoreClassIsNamespace(EtsClass *cls)
{
    return cls->IsModule();
}

EtsClass *StdCoreClassOf(EtsObject *obj)
{
    ASSERT(obj != nullptr);
    auto *cls = obj->GetClass();
    return cls->ResolvePublicClass();
}

EtsClass *StdCoreClassOfNull()
{
    auto *coro = EtsCoroutine::GetCurrent();
    ASSERT(coro != nullptr);
    const auto *nullObject = EtsObject::FromCoreType(coro->GetNullValue());
    return nullObject->GetClass();
}

EtsClass *StdCoreClassCurrent()
{
    return GetMethodOwnerClassInFrames(EtsCoroutine::GetCurrent(), 0);
}

EtsClass *StdCoreClassOfCaller()
{
    return GetMethodOwnerClassInFrames(EtsCoroutine::GetCurrent(), 1);
}

void StdCoreClassInitialize(EtsClass *cls)
{
    ASSERT(cls != nullptr);

    if (UNLIKELY(!cls->IsInitialized())) {
        auto coro = EtsCoroutine::GetCurrent();
        ASSERT(coro != nullptr);
        EtsClassLinker *linker = coro->GetPandaVM()->GetClassLinker();
        linker->InitializeClass(coro, cls);
    }
}

EtsString *StdCoreClassGetDescriptor(EtsClass *cls)
{
    ASSERT(cls != nullptr);
    return EtsString::CreateFromMUtf8(cls->GetDescriptor());
}

ObjectHeader *StdCoreClassGetInterfaces(EtsClass *cls)
{
    auto *coro = EtsCoroutine::GetCurrent();
    const auto interfaces = cls->GetRuntimeClass()->GetInterfaces();
    auto result = EtsObjectArray::Create(PlatformTypes(coro)->coreClass, interfaces.size());
    for (size_t i = 0; i < interfaces.size(); i++) {
        result->Set(i, reinterpret_cast<EtsObject *>(EtsClass::FromRuntimeClass(interfaces[i])));
    }
    return reinterpret_cast<ObjectHeader *>(result);
}

EtsObject *StdCoreClassCreateInstance(EtsClass *cls)
{
    ASSERT(cls != nullptr);
    return cls->CreateInstance();
}

EtsClass *EtsRuntimeLinkerFindLoadedClass(EtsRuntimeLinker *runtimeLinker, EtsString *clsName)
{
    ark::ets::ClassPublicNameParser parser(clsName->GetMutf8());
    const auto name = parser.Resolve();
    const auto *classDescriptor = utf::CStringAsMutf8(name.c_str());
    if (classDescriptor == nullptr) {
        return nullptr;
    }
    return runtimeLinker->FindLoadedClass(classDescriptor);
}

void EtsRuntimeLinkerInitializeContext(EtsRuntimeLinker *runtimeLinker)
{
    auto *coro = EtsCoroutine::GetCurrent();
    auto *ext = coro->GetPandaVM()->GetEtsClassLinkerExtension();
    if (UNLIKELY(runtimeLinker->IsInstanceOf(PlatformTypes(coro)->coreBootRuntimeLinker))) {
        // BootRuntimeLinker is a singletone, initialize it once
        runtimeLinker->SetClassLinkerContext(ext->GetBootContext());
        return;
    }

    ext->RegisterContext([ext, runtimeLinker]() {
        ASSERT(runtimeLinker->GetClassLinkerContext() == nullptr);
        auto allocator = ext->GetClassLinker()->GetAllocator();
        auto *ctx = allocator->New<EtsClassLinkerContext>(runtimeLinker);
        runtimeLinker->SetClassLinkerContext(ctx);
        return ctx;
    });
#ifdef PANDA_ETS_INTEROP_JS
    // At first call to ets from js, no available class linker context on the stack
    // Thus, register the first available application class linker context as default class linker context for interop
    // Tracked in #24199
    interop::js::InteropCtx::InitializeDefaultLinkerCtxIfNeeded(runtimeLinker);
#endif /* PANDA_ETS_INTEROP_JS */
}

EtsClass *EtsBootRuntimeLinkerFindAndLoadClass(ObjectHeader *runtimeLinker, EtsString *clsName, EtsBoolean init)
{
    ark::ets::ClassPublicNameParser parser(clsName->GetMutf8());
    const auto name = parser.Resolve();
    auto *classDescriptor = utf::CStringAsMutf8(name.c_str());
    if (classDescriptor == nullptr) {
        return nullptr;
    }

    auto *coro = EtsCoroutine::GetCurrent();
    // Use core ClassLinker in order to pass nullptr as error handler,
    // as exception is thrown in managed RuntimeLinker.loadClass
    auto *linker = Runtime::GetCurrent()->GetClassLinker();
    auto *ctx = EtsRuntimeLinker::FromCoreType(runtimeLinker)->GetClassLinkerContext();
    ASSERT(ctx->IsBootContext());
    auto *klass = linker->GetClass(classDescriptor, true, ctx, nullptr);
    if (klass == nullptr) {
        return nullptr;
    }

    if (UNLIKELY(init != 0 && !klass->IsInitialized())) {
        if (UNLIKELY(!linker->InitializeClass(coro, klass))) {
            ASSERT(coro->HasPendingException());
            return nullptr;
        }
    }
    return EtsClass::FromRuntimeClass(klass);
}

EtsRuntimeLinker *EtsGetNearestNonBootRuntimeLinker()
{
    auto *coro = EtsCoroutine::GetCurrent();
    for (auto stack = StackWalker::Create(coro); stack.HasFrame(); stack.NextFrame()) {
        auto *method = stack.GetMethod();
        if (LIKELY(method != nullptr)) {
            auto *cls = method->GetClass();
            ASSERT(cls != nullptr);
            auto *ctx = cls->GetLoadContext();
            ASSERT(ctx != nullptr);
            if (!ctx->IsBootContext()) {
                return EtsClassLinkerContext::FromCoreType(ctx)->GetRuntimeLinker();
            }
        }
    }
    return nullptr;
}

template <bool IS_STATIC = false, bool IS_CONSTRUCTOR = false>
static ObjectHeader *CreateEtsReflectMethodArray(EtsCoroutine *coro, const PandaVector<EtsMethod *> &methods)
{
    [[maybe_unused]] EtsHandleScope scope(coro);

    EtsClass *klass = IS_CONSTRUCTOR ? PlatformTypes(coro)->reflectConstructor
                                     : (IS_STATIC ? PlatformTypes(coro)->reflectStaticMethod
                                                  : PlatformTypes(coro)->reflectInstanceMethod);
    ASSERT(klass != nullptr);

    EtsHandle<EtsObjectArray> arrayH(coro, EtsObjectArray::Create(klass, methods.size()));
    if (UNLIKELY(arrayH.GetPtr() == nullptr)) {
        ASSERT(coro->HasPendingException());
        return nullptr;
    }
    ASSERT(arrayH.GetPtr() != nullptr);

    for (size_t idx = 0; idx < methods.size(); ++idx) {
        auto *reflectMethod = EtsReflectMethod::CreateFromEtsMethod(coro, methods[idx]);
        arrayH->Set(idx, reflectMethod->AsObject());
    }

    return arrayH->AsObject()->GetCoreType();
}

ObjectHeader *StdCoreClassGetInstanceMethodsInternal(EtsClass *cls, EtsBoolean publicOnly)
{
    ASSERT(cls != nullptr);

    auto *coro = EtsCoroutine::GetCurrent();
    ASSERT(coro != nullptr);

    bool collectPublic = FromEtsBoolean(publicOnly);
    PandaVector<EtsMethod *> instanceMethods;

    auto *rtCls = cls->GetRuntimeClass();
    Span<Method> vMethods(rtCls->GetVirtualMethods());
    Span<Method> cMethods(rtCls->GetCopiedMethods());

    for (auto &method : vMethods) {
        if (!method.IsConstructor() && (!collectPublic || method.IsPublic())) {
            instanceMethods.push_back(EtsMethod::FromRuntimeMethod(&method));
        }
    }
    for (auto &cmethod : cMethods) {
        if (!collectPublic || cmethod.IsPublic()) {
            instanceMethods.push_back(EtsMethod::FromRuntimeMethod(&cmethod));
        }
    }

    return CreateEtsReflectMethodArray(coro, instanceMethods);
}

EtsReflectField *StdCoreClassGetInstanceFieldByNameInternal(EtsClass *cls, EtsString *name, EtsBoolean publicOnly)
{
    ASSERT(cls != nullptr);
    ASSERT(name != nullptr);

    auto *coro = EtsCoroutine::GetCurrent();
    ASSERT(coro != nullptr);

    EtsField *field = cls->GetFieldIDByName(name->GetUtf8().c_str(), nullptr);
    if (field == nullptr) {
        return nullptr;
    }
    if (FromEtsBoolean(publicOnly) && !field->IsPublic()) {
        return nullptr;
    }
    return EtsReflectField::CreateFromEtsField(coro, field);
}

EtsReflectField *StdCoreClassGetStaticFieldByNameInternal(EtsClass *cls, EtsString *name, EtsBoolean publicOnly)
{
    ASSERT(cls != nullptr);
    ASSERT(name != nullptr);

    auto *coro = EtsCoroutine::GetCurrent();
    ASSERT(coro != nullptr);

    EtsField *field = cls->GetStaticFieldIDByName(name->GetUtf8().data(), nullptr);
    if (field == nullptr) {
        return nullptr;
    }
    if (FromEtsBoolean(publicOnly) && !field->IsPublic()) {
        return nullptr;
    }
    return EtsReflectField::CreateFromEtsField(coro, field);
}

static PandaVector<EtsMethod *> GetConstructors(EtsClass *cls, bool onlyPublic)
{
    PandaVector<EtsMethod *> constructors;

    cls->EnumerateDirectMethods([&constructors, onlyPublic](EtsMethod *method) {
        if (method->IsConstructor()) {
            if ((onlyPublic && method->IsPublic()) || !onlyPublic) {
                constructors.push_back(method);
            }
        }
        return false;
    });

    return constructors;
}

ObjectHeader *StdCoreClassGetConstructorsInternal(EtsClass *cls, EtsBoolean publicOnly)
{
    ASSERT(cls != nullptr);

    auto *coro = EtsCoroutine::GetCurrent();
    ASSERT(coro != nullptr);

    auto constructors = GetConstructors(cls, FromEtsBoolean(publicOnly));

    return CreateEtsReflectMethodArray<false, true>(coro, constructors);
}

ObjectHeader *StdCoreClassGetStaticMethodsInternal(EtsClass *cls, EtsBoolean publicOnly)
{
    ASSERT(cls != nullptr);

    auto *coro = EtsCoroutine::GetCurrent();
    ASSERT(coro != nullptr);

    bool collectPublic = FromEtsBoolean(publicOnly);
    PandaVector<EtsMethod *> staticMethods;

    Span<Method> sMethods = cls->GetRuntimeClass()->GetStaticMethods();

    for (auto &method : sMethods) {
        if (!collectPublic || method.IsPublic()) {
            staticMethods.push_back(EtsMethod::FromRuntimeMethod(&method));
        }
    }

    return CreateEtsReflectMethodArray<true, false>(coro, staticMethods);
}

static PandaVector<EtsField *> GetInstanceFields(EtsClass *cls, bool onlyPublic)
{
    PandaVector<EtsField *> instanceFields;
    PandaUnorderedSet<PandaString> uniqNames;

    EnumerateBaseClassesReverseOrder(cls, [&](EtsClass *c) {
        auto fields = c->GetRuntimeClass()->GetInstanceFields();
        auto fnum = fields.Size();
        for (uint32_t i = 0; i < fnum; i++) {
            if (onlyPublic && !fields[i].IsPublic()) {
                continue;
            }
            PandaString fieldName = utf::Mutf8AsCString(fields[i].GetName().data);
            if (!uniqNames.count(fieldName)) {
                instanceFields.push_back(EtsField::FromRuntimeField(&fields[i]));
                uniqNames.emplace(fieldName);
            }
        }
        return false;
    });
    return instanceFields;
}

static PandaVector<EtsField *> GetStaticFields(EtsClass *cls, bool onlyPublic)
{
    PandaVector<EtsField *> staticFields;
    PandaUnorderedSet<PandaString> uniqNames;

    EnumerateBaseClassesReverseOrder(cls, [&](EtsClass *c) {
        auto fields = c->GetRuntimeClass()->GetStaticFields();
        auto fnum = fields.Size();
        for (uint32_t i = 0; i < fnum; i++) {
            if (onlyPublic && !fields[i].IsPublic()) {
                continue;
            }
            PandaString fieldName = utf::Mutf8AsCString(fields[i].GetName().data);
            if (!uniqNames.count(fieldName)) {
                staticFields.push_back(EtsField::FromRuntimeField(&fields[i]));
                uniqNames.emplace(fieldName);
            }
        }
        return false;
    });
    return staticFields;
}

template <bool IS_STATIC = false>
static ObjectHeader *CreateEtsReflectFieldArray(EtsCoroutine *coro, const PandaVector<EtsField *> &fields)
{
    EtsClass *klass = IS_STATIC ? PlatformTypes(coro)->reflectStaticField : PlatformTypes(coro)->reflectInstanceField;
    ASSERT(klass != nullptr);

    [[maybe_unused]] EtsHandleScope scope(coro);

    EtsHandle<EtsObjectArray> arrayH(coro, EtsObjectArray::Create(klass, fields.size()));
    if (UNLIKELY(arrayH.GetPtr() == nullptr)) {
        ASSERT(coro->HasPendingException());
        return nullptr;
    }
    ASSERT(arrayH.GetPtr() != nullptr);

    for (size_t idx = 0; idx < fields.size(); ++idx) {
        auto *reflectField = EtsReflectField::CreateFromEtsField(coro, fields[idx]);
        arrayH->Set(idx, reflectField->AsObject());
    }

    return arrayH->AsObject()->GetCoreType();
}

ObjectHeader *StdCoreClassGetInstanceFieldsInternal(EtsClass *cls, EtsBoolean publicOnly)
{
    ASSERT(cls != nullptr);
    ASSERT(!(cls->IsInterface()));

    auto *coro = EtsCoroutine::GetCurrent();
    ASSERT(coro != nullptr);

    auto instanceFields = GetInstanceFields(cls, FromEtsBoolean(publicOnly));

    return CreateEtsReflectFieldArray(coro, instanceFields);
}

ObjectHeader *StdCoreClassGetStaticFieldsInternal(EtsClass *cls, EtsBoolean publicOnly)
{
    ASSERT(cls != nullptr);
    ASSERT(!(cls->IsInterface()));

    auto *coro = EtsCoroutine::GetCurrent();
    ASSERT(coro != nullptr);

    auto staticFields = GetStaticFields(cls, FromEtsBoolean(publicOnly));

    return CreateEtsReflectFieldArray<true>(coro, staticFields);
}

EtsBoolean StdCoreClassIsEnum(EtsClass *cls)
{
    ASSERT(cls != nullptr);
    return cls->IsEtsEnum();
}

EtsBoolean StdCoreClassIsInterface(EtsClass *cls)
{
    ASSERT(cls != nullptr);
    return cls->IsInterface();
}

EtsBoolean StdCoreClassIsFixedArray(EtsClass *cls)
{
    ASSERT(cls != nullptr);
    return cls->IsArrayClass();
}

EtsBoolean StdCoreClassIsSubtypeOf(EtsClass *cls, EtsClass *other)
{
    if (other->IsInterface()) {
        return static_cast<EtsBoolean>(cls->GetRuntimeClass()->Implements(other->GetRuntimeClass()));
    }
    return static_cast<EtsBoolean>(other->IsAssignableFrom(cls));
}

EtsClass *StdCoreClassGetFixedArrayComponentType(EtsClass *cls)
{
    if (!cls->IsArrayClass()) {
        return nullptr;
    }
    return cls->GetComponentType();
}

EtsBoolean StdCoreClassIsUnion(EtsClass *cls)
{
    ASSERT(cls != nullptr);
    return cls->IsUnionClass();
}

ObjectHeader *StdCoreClassGetUnionConstituentTypesInternal(EtsClass *cls)
{
    ASSERT(cls != nullptr);
    ASSERT(cls->IsUnionClass());

    auto *coro = EtsCoroutine::GetCurrent();
    ASSERT(coro != nullptr);

    auto constituentTypes = cls->GetRuntimeClass()->GetConstituentTypes();
    auto *typesArray = EtsObjectArray::Create(PlatformTypes(coro)->coreClass, constituentTypes.size());
    if (UNLIKELY(typesArray == nullptr)) {
        ASSERT(coro->HasPendingException());
        return nullptr;
    }
    ASSERT(typesArray != nullptr);

    for (size_t idx = 0; idx < constituentTypes.size(); ++idx) {
        typesArray->Set(idx, reinterpret_cast<EtsObject *>(EtsClass::FromRuntimeClass(constituentTypes[idx])));
    }

    return typesArray->AsObject()->GetCoreType();
}

EtsBoolean StdCoreClassIsFinal(EtsClass *cls)
{
    ASSERT(cls != nullptr);
    return cls->IsFinal() || cls->IsStringClass();
}

EtsBoolean StdCoreClassIsAbstract(EtsClass *cls)
{
    ASSERT(cls != nullptr);
    return cls->IsAbstract();
}

EtsBoolean StdCoreClassIsPrimitive(EtsClass *cls)
{
    ASSERT(cls != nullptr);
    return cls->IsPrimitive();
}

}  // namespace ark::ets::intrinsics
